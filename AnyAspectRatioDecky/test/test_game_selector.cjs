const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");
const ts = require("typescript");

const source = fs.readFileSync(path.join(__dirname, "..", "src", "index.tsx"), "utf8");
const compiled = ts.transpileModule(`${source}\nexport { GamesTab };`, {
  compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020, jsx: ts.JsxEmit.ReactJSX },
}).outputText;

function renderGames(report, options = {}) {
  const gameStates = [report];
  const modalStates = [];
  let states = gameStates;
  let hook = 0;
  let modal;
  const calls = [];
  let choice;
  const element = (type, props) => ({ type, props: props || {} });
  const react = {
    Fragment: "Fragment",
    useState(initial) {
      const index = hook++;
      const currentStates = states;
      if (!(index in currentStates)) currentStates[index] = initial;
      return [currentStates[index], (value) => {
        currentStates[index] = typeof value === "function" ? value(currentStates[index]) : value;
      }];
    },
    useRef(initial) {
      const index = hook++;
      if (!(index in states)) states[index] = { current: initial };
      return states[index];
    },
    useEffect() { hook++; },
  };
  const ui = new Proxy({}, {
    get: (_, name) => name === "showModal"
      ? (node, _parent, options) => {
          modal = { node, options };
          modalStates.length = 0;
          return { Close: () => options?.fnOnClose?.() };
        }
      : name,
  });
  const api = {
    callable: (name) => async (...args) => {
      calls.push({ name, args });
      if (name === "set_mod_applied") {
        const handler = args[2] ? options.apply : options.remove;
        const result = handler
          ? await handler(...args)
          : { conflicting_files: [], copied_file_count: 3 };
        if (args[2] === false && !result.error && result.conflicting_files.length === 0) choice = undefined;
        return result;
      }
      if (name === "select_matched_game") {
        choice = args;
        return { selected: true };
      }
      if (name === "clear_matched_game") {
        choice = undefined;
        return { cleared: true };
      }
      if (name === "get_mod_log") {
        if (options.getModLog) return options.getModLog(...args);
        return { name: args[2], content: "sample log", truncated: true };
      }
      if (name === "get_mod_readme") return { title: "ReadMe", content: "Manual instructions" };
      if (name === "scan_supported_games") return {
        ...report,
        supported_games: choice ? report.supported_games.filter((game) =>
          game.ambiguity_group !== choice[0] || (game.mod_id === choice[1] && game.game_path === choice[2]),
        ).map((game) => game.ambiguity_group === choice[0] ? { ...game, match_locked: true } : game)
          : report.supported_games,
      };
      return {};
    },
    definePlugin: () => {}, openFilePicker: () => {},
  };
  const module = { exports: {} };
  const context = {
    module, exports: module.exports,
    require(name) {
      if (name === "react") return react;
      if (name === "react/jsx-runtime") return { jsx: element, jsxs: element, Fragment: "Fragment" };
      if (name === "@decky/ui") return ui;
      if (name === "@decky/api") return api;
      if (name === "react-markdown") return { __esModule: true, default: "ReactMarkdown" };
      if (name === "./launch_options/steamLaunchOptions") return {
        prepareLaunchOptions: async () => null,
        removeLaunchOptions: options.removeLaunchOptions,
        readCurrentLaunchOptions: async () => "",
      };
      throw new Error(`Unexpected dependency: ${name}`);
    },
  };
  vm.runInNewContext(compiled, context);
  const render = () => {
    states = gameStates;
    hook = 0;
    return module.exports.GamesTab({ dataRevision: 0 });
  };
  render.modal = () => {
    states = modalStates;
    hook = 0;
    return typeof modal.node.type === "function" ? modal.node.type(modal.node.props) : modal.node;
  };
  render.closeModal = () => modal.options?.fnOnClose?.();
  render.calls = calls;
  return render;
}

function findElements(node, type) {
  if (!node || typeof node !== "object") return [];
  if (Array.isArray(node)) return node.flatMap((child) => findElements(child, type));
  return [
    ...(node.type === type ? [node] : []),
    ...findElements(node.props?.children, type),
  ];
}

test("Apply confirms exactly one matching game before installing it", async () => {
  const games = ["doa5lrfix", "sotdfix", "p5strikersfix"].map((id) => ({
    mod_id: id, mod_name: id, app_name: "DOA5", steam_app_id: "3000000001",
    game_path: "/games/DOA5", ambiguity_group: "shortcut", manual: false,
    source_kind: "archive", wine_dll_overrides: [], mod_files: [], file_status: "none",
    files_present: [], files_missing: [], matched_by_executable: true,
  }));
  const report = {
    steam_scan: { apps_scanned: 0, libraries_scanned: 0 },
    catalogued_mods: 3, supported_games: games, warnings: [],
  };
  for (const index of [0, 1, 2]) {
    const render = renderGames(report);
    let tree = render();
    assert.ok(JSON.stringify(findElements(tree, "strong")).includes("(.exe)"));
    assert.ok(!JSON.stringify(tree).includes("By Exe"));
    assert.ok(!JSON.stringify(tree).includes("Choose game to manage"));
    assert.ok(findElements(tree, "div").some((node) =>
      typeof node.props.children === "string" && node.props.children.includes("Found 1 supported game row(s)"),
    ));
    findElements(tree, "ToggleField")[0].props.onChange(true);
    let modal = render.modal();
    assert.equal(modal.props.bOKDisabled, true);
    assert.equal(findElements(modal, "DialogCheckbox").length, 3);
    assert.equal(render.calls.filter((call) => call.name === "set_mod_applied").length, 0);
    modal.props.onOK();
    assert.equal(render.calls.filter((call) => call.name === "set_mod_applied").length, 0);
    findElements(modal, "DialogCheckbox")[(index + 1) % 3].props.onChange(true);
    modal = render.modal();
    findElements(modal, "DialogCheckbox")[index].props.onChange(true);
    modal = render.modal();
    assert.equal(modal.props.bOKDisabled, false);
    assert.deepEqual(
      Array.from(findElements(modal, "DialogCheckbox"), (checkbox) => checkbox.props.checked),
      games.map((_, i) => i === index),
    );
    modal.props.onOK();
    await new Promise((resolve) => setImmediate(resolve));
    const apply = render.calls.find((call) => call.name === "set_mod_applied");
    assert.deepEqual(Array.from(apply.args), [games[index].mod_id, "/games/DOA5", true, null, null]);
    tree = render();
    assert.ok(JSON.stringify(findElements(tree, "strong")).includes("(.exe)"));
    assert.equal(findElements(tree, "DialogButton").length, 2);
    const status = findElements(tree, "div").find((node) =>
      Array.isArray(node.props.children) && node.props.children[0] === "Selected fix: ",
    );
    assert.equal(status.props.children[1], games[index].mod_name);
    findElements(tree, "ToggleField")[0].props.onChange(false);
    await new Promise((resolve) => setImmediate(resolve));
    tree = render();
    assert.equal(findElements(tree, "ToggleField")[0].props.checked, false);
    assert.ok(!JSON.stringify(tree).includes("Choose game to manage"));
    assert.equal(findElements(tree, "div").some((node) =>
      Array.isArray(node.props.children) && node.props.children[0] === "Selected fix: ",
    ), false);
    findElements(tree, "ToggleField")[0].props.onChange(true);
    assert.equal(findElements(render.modal(), "DialogCheckbox").length, 3);
  }
});

test("modified INI confirmation retains selection until removal completes", async () => {
  for (const keep of [true, false]) {
    let finishRemoval;
    const game = {
      mod_id: "doa5", mod_name: "DOA5", app_name: "DOA5", steam_app_id: "1",
      game_path: "/game", ambiguity_group: "shortcut", manual: false,
      source_kind: "archive", wine_dll_overrides: [], mod_files: [], file_status: "complete",
      files_present: ["fix.asi"], files_missing: [],
    };
    const render = renderGames({
      steam_scan: { apps_scanned: 0, libraries_scanned: 0 },
      catalogued_mods: 1, supported_games: [game], warnings: [],
    }, {
      remove: async (_id, _path, _applied, _replace, preserve) =>
        preserve === null ? { conflicting_files: ["fix.ini"] }
          : new Promise((resolve) => { finishRemoval = () => resolve({ conflicting_files: [] }); }),
    });
    findElements(render(), "ToggleField")[0].props.onChange(true);
    findElements(render.modal(), "DialogCheckbox")[0].props.onChange(true);
    render.modal().props.onOK();
    await new Promise((resolve) => setImmediate(resolve));
    findElements(render(), "ToggleField")[0].props.onChange(false);
    await new Promise((resolve) => setImmediate(resolve));
    assert.ok(JSON.stringify(render()).includes("Selected fix: "));
    const modal = render.modal();
    (keep ? modal.props.onOK : modal.props.onCancel)();
    await new Promise((resolve) => setImmediate(resolve));
    assert.ok(JSON.stringify(render()).includes("Selected fix: "));
    finishRemoval();
    await new Promise((resolve) => setImmediate(resolve));
    assert.ok(!JSON.stringify(render()).includes("Selected fix: "));
    assert.ok(!JSON.stringify(render()).includes("Choose game to manage"));
    const removals = render.calls.filter((call) => call.name === "set_mod_applied" && !call.args[2]);
    assert.equal(removals[1].args[4], keep);
  }
});

test("failed removal retains selection; launch-option failure does not", async () => {
  for (const failRemoval of [true, false]) {
    const game = {
      mod_id: "doa5", mod_name: "DOA5", app_name: "DOA5", steam_app_id: "1",
      game_path: "/game", ambiguity_group: "shortcut", manual: false,
      source_kind: "archive", wine_dll_overrides: ["winmm=n,b"], mod_files: [], file_status: "complete",
      files_present: ["fix.asi"], files_missing: [],
    };
    const render = renderGames({
      steam_scan: { apps_scanned: 0, libraries_scanned: 0 },
      catalogued_mods: 1, supported_games: [game], warnings: [],
    }, {
      remove: async () => failRemoval ? { error: "remove failed" } : { conflicting_files: [] },
      removeLaunchOptions: async () => { throw new Error("launch options failed"); },
    });
    findElements(render(), "ToggleField")[0].props.onChange(true);
    findElements(render.modal(), "DialogCheckbox")[0].props.onChange(true);
    render.modal().props.onOK();
    await new Promise((resolve) => setImmediate(resolve));
    findElements(render(), "ToggleField")[0].props.onChange(false);
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(JSON.stringify(render()).includes("Selected fix: "), failRemoval);
    assert.ok(!JSON.stringify(render()).includes("Choose game to manage"));
  }
});

test("Cancel or dismissing the choice never applies files", async () => {
  for (const dismiss of ["cancel", "close"]) {
    const game = {
      mod_id: "doa5", mod_name: "DOA5", app_name: "DOA5", steam_app_id: "1",
      game_path: "/game", ambiguity_group: "shortcut", manual: false,
      source_kind: "archive", wine_dll_overrides: [], mod_files: [], file_status: "none",
      files_present: [], files_missing: [],
    };
    const render = renderGames({
      steam_scan: { apps_scanned: 0, libraries_scanned: 0 },
      catalogued_mods: 1, supported_games: [game], warnings: [],
    });
    findElements(render(), "ToggleField")[0].props.onChange(true);
    const modal = render.modal();
    findElements(modal, "DialogCheckbox")[0].props.onChange(true);
    if (dismiss === "cancel") render.modal().props.onCancel();
    else render.closeModal();
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(render.calls.length, 0);
    assert.equal(findElements(render(), "ToggleField")[0].props.checked, false);
  }
});

test("manual and failed choices can be cleared without removing files", async () => {
  for (const manual of [true, false]) {
    const game = {
      mod_id: "fix", mod_name: "Fix", app_name: "Game", steam_app_id: "1",
      game_path: "/game", ambiguity_group: "shortcut", manual,
      source_kind: "archive", wine_dll_overrides: [], mod_files: [], file_status: "none",
      files_present: [], files_missing: [],
    };
    const render = renderGames({
      steam_scan: { apps_scanned: 0, libraries_scanned: 0 },
      catalogued_mods: 1, supported_games: [game], warnings: [],
    }, { apply: async () => ({ error: "download failed" }) });
    findElements(render(), "ToggleField")[0].props.onChange(true);
    findElements(render.modal(), "DialogCheckbox")[0].props.onChange(true);
    render.modal().props.onOK();
    await new Promise((resolve) => setImmediate(resolve));
    if (manual) {
      findElements(render(), "ButtonItem").find((button) => button.props.children === "Back to games").props.onClick();
    }
    const reset = findElements(render(), "ButtonItem").find((button) => button.props.children === "Clear game choice");
    assert.ok(reset);
    reset.props.onClick();
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(render.calls.filter((call) => call.name === "set_mod_applied" && !call.args[2]).length, 0);
    assert.ok(!JSON.stringify(render()).includes("Selected fix: "));
    findElements(render(), "ToggleField")[0].props.onChange(true);
    assert.equal(findElements(render.modal(), "DialogCheckbox").length, 1);
  }
});

test("one Log button shares the Apply row and displays all logs", async () => {
  for (const logFiles of [[], ["Fix.log"], ["Fix.log", "Other.log"]]) {
    const game = {
      mod_id: "fix", mod_name: "Fix", app_name: "Game", steam_app_id: "1",
      game_path: "/game", manual: false, source_kind: "archive", wine_dll_overrides: [],
      mod_files: ["Fix.asi"], file_status: "complete", files_present: ["Fix.asi"],
      files_missing: [], log_files: logFiles,
    };
    const render = renderGames({
      steam_scan: { apps_scanned: 0, libraries_scanned: 0 },
      catalogued_mods: 1, supported_games: [game], warnings: [],
    });
    const tree = render();
    const button = findElements(tree, "DialogButton").find((item) => item.props.children === "Log");
    assert.equal(!!button, logFiles.length > 0);
    if (button) {
      assert.equal(button.props.style.flex, "0 0 22%");
      assert.equal(button.props.style.maxWidth, "25%");
      assert.equal(button.props.style.minWidth, 0);
      const row = findElements(tree, "PanelSectionRow").find((item) =>
        findElements(item, "DialogButton").includes(button),
      );
      assert.equal(findElements(row, "ToggleField").length, 1);
      assert.equal(findElements(tree, "DialogButton").filter((item) => item.props.children === "Log").length, 1);
      button.props.onClick();
      await new Promise((resolve) => setImmediate(resolve));
      const logView = render();
      assert.equal(findElements(logView, "pre").length, logFiles.length);
      assert.ok(findElements(logView, "pre").every((node) => node.props.children === "sample log"));
      assert.ok(JSON.stringify(logView).includes("last 256 KiB"));
      assert.deepEqual(render.calls.filter((call) => call.name === "get_mod_log").map((call) => call.args[2]), logFiles);
    }
  }
});

test("partial status updates the Apply label after scanning", async () => {
  for (const logFiles of [[], ["Fix.log"]]) {
    const game = {
      mod_id: "fix", mod_name: "Fix", app_name: "Game", steam_app_id: "1",
      game_path: "/game", manual: false, source_kind: "archive", wine_dll_overrides: [],
      mod_files: ["Fix.asi", "Fix.ini"], file_status: "complete", files_present: ["Fix.asi", "Fix.ini"],
      files_missing: [], log_files: logFiles,
    };
    const report = {
      steam_scan: { apps_scanned: 0, libraries_scanned: 0 },
      catalogued_mods: 1, supported_games: [game], warnings: [],
    };
    const render = renderGames(report);
    assert.equal(findElements(render(), "ToggleField")[0].props.label, "Apply");
    report.supported_games = [{ ...game, file_status: "partial", files_present: ["Fix.ini"], files_missing: ["Fix.asi"] }];
    findElements(render(), "ButtonItem").find((button) => button.props.children === "Scan for supported games").props.onClick();
    await new Promise((resolve) => setImmediate(resolve));
    const tree = render();
    assert.equal(findElements(tree, "ToggleField")[0].props.label, "Apply *");
    assert.equal(findElements(tree, "ToggleField")[0].props.checked, false);
    assert.ok(JSON.stringify(tree).includes("* Incomplete"));
    report.supported_games = [{ ...game, file_status: "none", files_present: [], files_missing: game.mod_files }];
    findElements(render(), "ButtonItem").find((button) => button.props.children === "Scan for supported games").props.onClick();
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(findElements(render(), "ToggleField")[0].props.label, "Apply");
  }
});

test("a missing log does not hide other logs or its error", async () => {
  const game = {
    mod_id: "fix", mod_name: "Fix", app_name: "Game", steam_app_id: "1",
    game_path: "/game", manual: false, source_kind: "archive", wine_dll_overrides: [],
    mod_files: ["Fix.asi", "Other.asi"], file_status: "complete", files_present: [], files_missing: [],
    log_files: ["Fix.log", "Other.log"],
  };
  const render = renderGames({
    steam_scan: { apps_scanned: 0, libraries_scanned: 0 },
    catalogued_mods: 1, supported_games: [game], warnings: [],
  }, {
    getModLog: async (_id, _path, name) => {
      if (name === "Fix.log") throw new Error("file disappeared");
      return { name, content: "other log", truncated: false };
    },
  });
  findElements(render(), "DialogButton").find((button) => button.props.children === "Log").props.onClick();
  await new Promise((resolve) => setImmediate(resolve));
  const tree = render();
  assert.ok(JSON.stringify(tree).includes("file disappeared"));
  assert.equal(findElements(tree, "pre")[0].props.children, "other log");
});

test("manual fix logs share the manual install row", async () => {
  const game = {
    mod_id: "fix", mod_name: "Fix", app_name: "Game", steam_app_id: "1",
    game_path: "/game", manual: true, source_kind: "archive", wine_dll_overrides: [],
    mod_files: ["Fix.asi"], file_status: "none", files_present: [], files_missing: [],
    log_files: ["Fix.log"],
  };
  const render = renderGames({
    steam_scan: { apps_scanned: 0, libraries_scanned: 0 },
    catalogued_mods: 1, supported_games: [game], warnings: [],
  });
  const tree = render();
  const button = findElements(tree, "DialogButton").find((item) => item.props.children === "Log");
  assert.ok(button);
  assert.equal(button.props.style.maxWidth, "25%");
  const row = findElements(tree, "PanelSectionRow").find((item) => findElements(item, "DialogButton").includes(button));
  assert.ok(JSON.stringify(row).includes("Manual install"));
  assert.equal(findElements(tree, "ToggleField").length, 0);
});
