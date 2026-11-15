const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const ts = require("typescript");

function createSettingsHarness(updateResult = { updated: ["Game"], unchanged: [], skipped: [], failures: [] }, sourceChanged = false) {
  const states = [];
  let stateIndex = 0;
  let initialEffect;
  let effectsRegistered = false;
  const calls = [];
  const errors = [];
  const modals = [];
  let updateCalls = 0;
  let catalogUpdateCalls = 0;
  const settings = {
    catalog_repository_url: "https://github.com/example/repo",
    catalog_ref: "main",
    catalog_path: "catalog.json",
    local_archive_dir: "",
    offline_mode: false,
    ini_resolution_width: 1280,
    ini_resolution_height: 800,
    ini_resolution_update_pending: false,
  };
  const defaults = { ...settings };
  const getSettings = async () => ({
    settings: { ...settings },
    defaults,
    catalog: { origin: "bundled", mod_count: 1, fetched_at: null, warning: null },
    local_archives: { count: 0, warnings: [] },
    paths: { data_dir: "", log_file: "" },
  });
  const api = {
    getPluginSettings: getSettings,
    savePluginSettings: async (...args) => {
      calls.push(args);
      if (!/^[0-9]+$/.test(args[4]) || !/^[0-9]+$/.test(args[5])) {
        throw new Error("INI resolution dimensions must be whole numbers");
      }
      if (settings.ini_resolution_width !== Number(args[4]) || settings.ini_resolution_height !== Number(args[5])) {
        settings.ini_resolution_update_pending = true;
      }
      Object.assign(settings, {
        catalog_repository_url: args[0],
        catalog_ref: args[1],
        catalog_path: args[2],
        local_archive_dir: args[3],
        ini_resolution_width: Number(args[4]),
        ini_resolution_height: Number(args[5]),
      });
      return { source_changed: sourceChanged };
    },
    updateDependencies: async () => {
      catalogUpdateCalls += 1;
      return { catalog_changed: true, mod_count: 1, local_archive_count: 0, warnings: [] };
    },
    PLUGIN_VERSION: "test",
    updateCurrentResolution: async () => {
      updateCalls += 1;
      if (!updateResult.failures.length) settings.ini_resolution_update_pending = false;
      return { ...updateResult, resolution_update_pending: settings.ini_resolution_update_pending };
    },
  };
  const hooks = {
    useState(initial) {
      const index = stateIndex++;
      if (!(index in states)) states[index] = initial;
      return [states[index], (next) => {
        states[index] = typeof next === "function" ? next(states[index]) : next;
      }];
    },
    useRef(initial) {
      const index = stateIndex++;
      if (!(index in states)) states[index] = { current: initial };
      return states[index];
    },
    useEffect(effect) {
      if (!effectsRegistered) initialEffect = effect;
    },
  };
  const element = (type, props) => ({ type, props: props || {} });
  const dependencies = {
    react: hooks,
    "react/jsx-runtime": { jsx: element, jsxs: element, Fragment: "Fragment" },
    "@decky/ui": {
      ...Object.fromEntries(
        ["ButtonItem", "ConfirmModal", "PanelSection", "PanelSectionRow", "ToggleField"]
          .map((name) => [name, name]),
      ),
      showModal: (modal) => modals.push(modal),
    },
    "@decky/api": {},
    "../../services/callables": api,
    "../../services/modals": { showErrorModal: (...args) => errors.push(args) },
    "../../services/formatters": {
      formatCatalogUpdate: () => "Catalog updated.",
      errorText: (error) => (error instanceof Error ? error.message : String(error)),
    },
    "./PluginUpdateSection": { PluginUpdateSection: "PluginUpdateSection" },
    "./EditableValue": { EditableValue: "EditableValue" },
    "../../services/remembered": { useRemembered: (_key, initial) => hooks.useState(initial) },
  };
  const source = fs.readFileSync(path.join(__dirname, "../src/components/views/SettingsTab.tsx"), "utf8");
  const compiled = ts.transpileModule(source, {
    compilerOptions: { module: ts.ModuleKind.CommonJS, jsx: ts.JsxEmit.ReactJSX },
  }).outputText;
  const module = { exports: {} };
  new Function("require", "module", "exports", compiled)(
    (name) => {
      if (!(name in dependencies)) throw new Error(`Unexpected dependency: ${name}`);
      return dependencies[name];
    },
    module,
    module.exports,
  );
  const render = () => {
    stateIndex = 0;
    return module.exports.SettingsTab({ onDataChanged() {} });
  };
  const find = (root, predicate) => {
    if (Array.isArray(root)) {
      for (const child of root) {
        const found = find(child, predicate);
        if (found) return found;
      }
      return undefined;
    }
    if (!root || typeof root !== "object") return undefined;
    if (predicate(root)) return root;
    return find(root.props?.children, predicate);
  };
  const section = (tree, title) => find(tree, (item) => item.type === "PanelSection" && item.props.title === title);
  const field = (tree, title, label) => find(section(tree, title), (item) =>
    item.type === "EditableValue" && item.props.label === label,
  );
  const tick = () => new Promise((resolve) => setImmediate(resolve));
  // Commits a value the way the edit dialog's OK does, then waits for the save.
  const commit = async (title, label, value) => {
    field(render(), title, label).props.onCommit(value);
    await tick();
  };
  const text = (node) => {
    if (typeof node === "string") return node;
    if (Array.isArray(node)) return node.map(text).join("");
    return node && typeof node === "object" ? text(node.props?.children) : "";
  };
  const button = (tree, title, label) => find(section(tree, title), (item) =>
    item.type === "ButtonItem" && text(item.props.children).replace(/ \*$/, "") === label,
  );
  const moreToggle = () => find(render(), (item) =>
    item.type === "ButtonItem" && /more settings/i.test(text(item.props.children)),
  );
  return {
    calls, errors, settings, render, field, commit, button, modals, find, text, moreToggle,
    showMore() {
      moreToggle().props.onClick();
    },
    get updateCalls() { return updateCalls; },
    get catalogUpdateCalls() { return catalogUpdateCalls; },
    async initialize() {
      render();
      effectsRegistered = true;
      initialEffect();
      await new Promise((resolve) => setImmediate(resolve));
    },
  };
}

const RES = "Default mod INI resolution";
const WIDTH = "Width (pixels)";
const pending = (h, tree) => h.find(tree, (item) =>
  item.props?.role === "status" && h.text(item).startsWith("Saved resolution has not"),
);

test("resolution values wait for backend settings before showing defaults", async () => {
  const h = createSettingsHarness();
  const before = h.field(h.render(), RES, WIDTH);
  assert.equal(before.props.value, "");
  assert.equal(before.props.disabled, true);
  assert.equal(before.props.numeric, true);
  await h.initialize();
  assert.equal(h.field(h.render(), RES, WIDTH).props.value, "1280");
});

test("a committed dimension is saved at once and highlights update current", async () => {
  const h = createSettingsHarness();
  await h.initialize();
  await h.commit(RES, WIDTH, "1920");
  assert.deepEqual(h.calls[0].slice(4), ["1920", "800"]);
  let tree = h.render();
  assert.equal(h.field(tree, RES, WIDTH).props.value, "1920");
  assert.ok(h.find(tree, (item) => item.props?.role === "status" && h.text(item) === "Resolution saved."));
  assert.equal(h.button(tree, RES, "Update current").props.children.props.style.color, "#ff9830");
  assert.ok(pending(h, tree));
  h.button(tree, RES, "Update current").props.onClick();
  assert.match(h.modals[0].props.strDescription, /1920 x 800/);
  await h.modals[0].props.onOK();
  tree = h.render();
  assert.equal(h.button(tree, RES, "Update current").props.children.props.style, undefined);
  assert.equal(pending(h, tree), undefined);
  assert.deepEqual(h.errors, []);
});

test("failed batch update keeps the saved resolution reminder", async () => {
  const h = createSettingsHarness({ updated: [], unchanged: [], skipped: [], failures: ["Game: denied"] });
  await h.initialize();
  await h.commit(RES, WIDTH, "1920");
  h.button(h.render(), RES, "Update current").props.onClick();
  await h.modals[0].props.onOK();
  assert.equal(h.button(h.render(), RES, "Update current").props.children.props.style.color, "#ff9830");
});

test("saved reminder appears after reopening settings", async () => {
  const h = createSettingsHarness();
  h.settings.ini_resolution_update_pending = true;
  await h.initialize();
  assert.equal(h.button(h.render(), RES, "Update current").props.children.props.style.color, "#ff9830");
});

test("a rejected dimension shows the error and keeps the saved value", async () => {
  const h = createSettingsHarness();
  await h.initialize();
  await h.commit(RES, WIDTH, "");
  assert.equal(h.errors.length, 1);
  assert.equal(h.settings.ini_resolution_width, 1280);
  const tree = h.render();
  assert.equal(h.field(tree, RES, WIDTH).props.value, "1280");
  assert.ok(h.find(tree, (item) => item.props?.role === "status" && h.text(item) === "Could not save the resolution."));
});

test("other settings save with the saved resolution", async () => {
  const h = createSettingsHarness();
  await h.initialize();
  h.showMore();
  await h.commit("Local mod archives", "Folder containing *.zip mods", "/mods");
  assert.deepEqual(h.calls[0].slice(3), ["/mods", "1280", "800"]);
  assert.equal(h.find(h.render(), (item) => item.props?.role === "status" && h.text(item) === "Resolution saved."), undefined);
});

test("changing the catalog source still refreshes it automatically", async () => {
  const h = createSettingsHarness(undefined, true);
  await h.initialize();
  h.showMore();
  await h.commit("Mod source (GitHub)", "Branch, tag, or commit", "new-branch");
  assert.equal(h.calls[0][1], "new-branch");
  assert.equal(h.catalogUpdateCalls, 1);
  assert.deepEqual(h.errors, []);
});

test("resetting defaults restores the default resolution", async () => {
  const h = createSettingsHarness();
  await h.initialize();
  await h.commit(RES, WIDTH, "1920");
  h.showMore();
  await h.button(h.render(), "Settings", "Reset to defaults").props.onClick();
  assert.deepEqual(h.calls[1].slice(4), ["1280", "800"]);
  assert.equal(h.field(h.render(), RES, WIDTH).props.value, "1280");
});

test("update results show counts and only the mods that need attention", async () => {
  const h = createSettingsHarness({
    updated: ["Game A", "Game B", "Game C"],
    unchanged: ["Game D"],
    skipped: ["Game E: select the correct mod first."],
    failures: ["Game F: permission denied"],
  });
  await h.initialize();
  h.button(h.render(), "Default mod INI resolution", "Update current").props.onClick();
  await h.modals[0].props.onOK();
  assert.equal(
    h.modals[1].props.strDescription,
    "Updated 3 mods. 1 mod already up to date.\nFailed: Game F: permission denied\nSkipped: Game E: select the correct mod first.",
  );
  assert.ok(h.find(h.render(), (item) => h.text(item) === "Updated 3 mods. 1 mod already up to date. 2 need attention."));
});

test("source, archive folder and reset stay hidden until More settings is opened", async () => {
  const h = createSettingsHarness();
  await h.initialize();
  const sections = (tree) => ["Mod source (GitHub)", "Local mod archives", "Settings"]
    .map((title) => !!h.find(tree, (item) => item.type === "PanelSection" && item.props.title === title));
  assert.deepEqual(sections(h.render()), [false, false, false]);
  assert.equal(h.text(h.moreToggle().props.children), "More settings");
  h.showMore();
  assert.deepEqual(sections(h.render()), [true, true, true]);
  assert.equal(h.text(h.moreToggle().props.children), "Hide more settings");
});
