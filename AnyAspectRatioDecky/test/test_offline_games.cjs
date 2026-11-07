const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const test = require("node:test");
const ts = require("typescript");

function harness(report) {
  const states = [report];
  let hook = 0;
  let modal;
  const calls = [];
  const cache = new Map();
  const element = (type, props) => ({ type, props: props || {} });
  const react = {
    useState(initial) {
      const index = hook++;
      if (!(index in states)) states[index] = initial;
      return [states[index], (value) => {
        states[index] = typeof value === "function" ? value(states[index]) : value;
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
    get: (_, name) => name === "showModal" ? (node, _parent, options) => {
      modal = { node, options };
      return { Close() { options?.fnOnClose?.(); } };
    } : name,
  });
  function load(file) {
    if (cache.has(file)) return cache.get(file);
    const module = { exports: {} };
    cache.set(file, module.exports);
    const code = ts.transpileModule(fs.readFileSync(file, "utf8"), {
      compilerOptions: {
        module: ts.ModuleKind.CommonJS,
        target: ts.ScriptTarget.ES2020,
        jsx: ts.JsxEmit.ReactJSX,
      },
    }).outputText;
    vm.runInNewContext(code, {
      module, exports: module.exports,
      require(name) {
        if (name === "react") return react;
        if (name === "react/jsx-runtime") return { jsx: element, jsxs: element, Fragment: "Fragment" };
        if (name === "@decky/ui") return ui;
        if (name === "react-markdown") return { __esModule: true, default: "ReactMarkdown" };
        if (name.endsWith("/callables")) return {
          setModApplied: async (...args) => {
            calls.push(args);
            return { conflicting_files: [], copied_file_count: 1 };
          },
          scanSupportedGames: async () => report,
        };
        if (name.endsWith("/steamLaunchOptions")) return {};
        if (name.startsWith(".")) {
          const base = path.resolve(path.dirname(file), name);
          return load([".tsx", ".ts"].map((ext) => base + ext).find(fs.existsSync));
        }
        throw new Error(`Unexpected import: ${name}`);
      },
    });
    cache.set(file, module.exports);
    return module.exports;
  }
  const src = path.join(__dirname, "..", "src");
  return {
    load: (relative) => load(path.join(src, relative)),
    render() {
      hook = 0;
      return this.load("components/views/GamesTab.tsx").default({ dataRevision: 0 });
    },
    get modal() { return modal; },
    calls,
  };
}

function nodes(node, type) {
  if (!node || typeof node !== "object") return [];
  if (Array.isArray(node)) return node.flatMap((child) => nodes(child, type));
  if (typeof node.type === "function") return nodes(node.type(node.props), type);
  return [
    ...(node.type === type ? [node] : []),
    ...nodes(node.props?.children, type),
  ];
}

function game(offline) {
  return {
    mod_id: "elden-ring", app_name: "Elden Ring", steam_app_id: "1245620",
    game_path: "game", file_status: "none", files_present: [], files_missing: [],
    mod_files: [], wine_dll_overrides: [], source_kind: "archive", offline_only: offline,
  };
}

test("Shared game title adds Offline without losing New", () => {
  const h = harness();
  const { GameTitle } = h.load("components/views/GameDetails.tsx");
  const title = GameTitle({ game: { ...game(true), update_available: true } });
  assert.ok(JSON.stringify(title).includes("(Offline)"));
  assert.ok(JSON.stringify(title).includes("(New)"));
  assert.ok(!JSON.stringify(GameTitle({ game: game(false) })).includes("(Offline)"));
  const unresolved = GameTitle({ game: game(true), unresolved: true });
  assert.equal(unresolved.props.style.color, "#9e9e9e");
  assert.ok(JSON.stringify(unresolved).includes("(.exe)"));
});

test("Scan summary counts found supported games instead of scanned apps", () => {
  for (const count of [0, 1, 2]) {
    const h = harness({
      supported_games: Array.from({ length: count }, (_, index) => ({
        ...game(false), mod_id: `mod-${index}`, game_path: `game-${index}`,
      })),
      warnings: [], steam_scan: { apps_scanned: 40, libraries_scanned: 3 },
      catalogued_mods: 116,
    });
    const statuses = nodes(h.render(), "div").filter((node) => node.props.role === "status");
    assert.ok(statuses.some((node) => node.props.children === `Found ${count} supported games.`));
    assert.ok(!statuses.some((node) => String(node.props.children).includes("Scanned 40")));
  }
});

test("Offline Apply requires confirmation; cancellation performs no file operations", async () => {
  for (const confirm of [false, true]) {
    const h = harness({
      supported_games: [game(true)], warnings: [],
      steam_scan: { apps_scanned: 1, libraries_scanned: 1 }, catalogued_mods: 1,
    });

    const toggle = nodes(h.render(), "ToggleField")[0];
    toggle.props.onChange(true);
    assert.equal(h.calls.length, 0);
    assert.equal(h.modal.node.props.strTitle, "Offline play only");
    h.modal.node.props[confirm ? "onOK" : "onCancel"]();
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(h.calls.length, confirm ? 1 : 0);
  }
});

test("Other games apply without an Offline prompt; Offline games remove without that prompt", async () => {
  for (const [offline, applied] of [[false, true], [true, false]]) {
    const h = harness({
      supported_games: [game(offline)], warnings: [],
      steam_scan: { apps_scanned: 1, libraries_scanned: 1 }, catalogued_mods: 1,
    });
    nodes(h.render(), "ToggleField")[0].props.onChange(applied);
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(h.modal, undefined);
    assert.equal(h.calls.length, 1);
    assert.equal(h.calls[0][2], applied);
  }
});
