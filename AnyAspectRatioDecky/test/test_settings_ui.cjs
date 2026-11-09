const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const ts = require("typescript");

function createSettingsHarness(updateResult = { updated: ["Game"], unchanged: [], skipped: [], failures: [], warnings: [] }, sourceChanged = false) {
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
        ["ButtonItem", "ConfirmModal", "PanelSection", "PanelSectionRow", "TextField", "ToggleField"]
          .map((name) => [name, name]),
      ),
      showModal: (modal) => modals.push(modal),
    },
    "@decky/api": {},
    "../../services/callables": api,
    "../../services/modals": { showErrorModal: (...args) => errors.push(args) },
    "../../services/formatters": { formatCatalogUpdate: () => "Catalog updated." },
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
  const field = (tree, title, label) => {
    const row = find(section(tree, title), (item) =>
      item.type === "div" && item.props.children?.[0]?.props?.children === label,
    );
    return find(row, (item) => item.type === "TextField");
  };
  const text = (node) => {
    if (typeof node === "string") return node;
    if (Array.isArray(node)) return node.map(text).join("");
    return node && typeof node === "object" ? text(node.props?.children) : "";
  };
  const button = (tree, title, label) => find(section(tree, title), (item) =>
    item.type === "ButtonItem" && text(item.props.children).replace(/ \*$/, "") === label,
  );
  return {
    calls, errors, settings, render, field, button, modals, find, text,
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

test("resolution fields wait for backend settings before showing defaults", async () => {
  const h = createSettingsHarness();
  const before = h.render();
  const field = h.field(before, "Mod INI resolution", "Width (pixels, when supported)");
  assert.equal(field.props.value, "");
  assert.equal(field.props.disabled, true);
  await h.initialize();
  assert.equal(
    h.field(h.render(), "Mod INI resolution", "Width (pixels, when supported)").props.value,
    String(h.settings.ini_resolution_width),
  );
});

test("resolution editing stays local until explicitly saved", async () => {
  const h = createSettingsHarness();
  await h.initialize();
  let tree = h.render();
  let width = h.field(tree, "Mod INI resolution", "Width (pixels, when supported)");
  assert.equal(width.props.onBlur, undefined);
  for (const text of ["", "1", "19", "192", "1920"]) {
    width.props.onChange({ currentTarget: { value: text } });
    tree = h.render();
    width = h.field(tree, "Mod INI resolution", "Width (pixels, when supported)");
    assert.equal(width.props.value, text);
    assert.equal(h.calls.length, 0);
  }
  h.field(tree, "Mod INI resolution", "Height (pixels, when supported)")
    .props.onChange({ currentTarget: { value: "1080" } });
  tree = h.render();
  await h.button(tree, "Mod INI resolution", "Save resolution").props.onClick();
  assert.deepEqual(h.calls[0].slice(4), ["1920", "1080"]);
  assert.equal(h.settings.ini_resolution_width, 1920);
  assert.equal(h.settings.ini_resolution_height, 1080);
  assert.deepEqual(h.errors, []);
});

test("saved resolution highlights update current until a successful update", async () => {
  const h = createSettingsHarness();
  await h.initialize();
  h.field(h.render(), "Mod INI resolution", "Width (pixels, when supported)")
    .props.onChange({ currentTarget: { value: "1920" } });
  await h.button(h.render(), "Mod INI resolution", "Save resolution").props.onClick();
  let tree = h.render();
  assert.equal(h.button(tree, "Mod INI resolution", "Update current").props.children.props.style.color, "#ff9830");
  assert.ok(h.find(tree, (item) => item.props?.role === "status" && h.text(item).startsWith("Saved resolution has not")));
  h.button(tree, "Mod INI resolution", "Update current").props.onClick();
  await h.modals[0].props.onOK();
  tree = h.render();
  assert.equal(h.button(tree, "Mod INI resolution", "Update current").props.children.props.style, undefined);
  assert.equal(h.find(tree, (item) => item.props?.role === "status" && h.text(item).startsWith("Saved resolution has not")), undefined);
});

test("failed batch update keeps the saved resolution reminder", async () => {
  const h = createSettingsHarness({ updated: [], unchanged: [], skipped: [], failures: ["Game: denied"], warnings: [] });
  await h.initialize();
  h.field(h.render(), "Mod INI resolution", "Width (pixels, when supported)")
    .props.onChange({ currentTarget: { value: "1920" } });
  await h.button(h.render(), "Mod INI resolution", "Save resolution").props.onClick();
  h.button(h.render(), "Mod INI resolution", "Update current").props.onClick();
  await h.modals[0].props.onOK();
  assert.equal(h.button(h.render(), "Mod INI resolution", "Update current").props.children.props.style.color, "#ff9830");
});

test("saved reminder appears after reopening settings", async () => {
  const h = createSettingsHarness();
  h.settings.ini_resolution_update_pending = true;
  await h.initialize();
  assert.equal(h.button(h.render(), "Mod INI resolution", "Update current").props.children.props.style.color, "#ff9830");
});

test("unsaved resolution is highlighted until saved or reverted", async () => {
  const h = createSettingsHarness();
  await h.initialize();
  let tree = h.render();
  const warning = (root) => h.find(root, (item) =>
    item.props?.role === "status" && h.text(item).startsWith("Unsaved resolution."),
  );
  assert.equal(warning(tree), undefined);
  h.field(tree, "Mod INI resolution", "Width (pixels, when supported)")
    .props.onChange({ currentTarget: { value: "1920" } });
  tree = h.render();
  assert.equal(warning(tree).props.style.color, "#ff9830");
  assert.equal(h.button(tree, "Mod INI resolution", "Save resolution").props.children.props.style.color, "#ff9830");
  h.field(tree, "Mod INI resolution", "Width (pixels, when supported)")
    .props.onChange({ currentTarget: { value: "1280" } });
  tree = h.render();
  assert.equal(warning(tree), undefined);
  h.field(tree, "Mod INI resolution", "Width (pixels, when supported)")
    .props.onChange({ currentTarget: { value: "" } });
  tree = h.render();
  await h.button(tree, "Mod INI resolution", "Save resolution").props.onClick();
  assert.ok(warning(h.render()));
  h.field(h.render(), "Mod INI resolution", "Width (pixels, when supported)")
    .props.onChange({ currentTarget: { value: "1920" } });
  await h.button(h.render(), "Mod INI resolution", "Save resolution").props.onClick();
  tree = h.render();
  assert.equal(warning(tree), undefined);
  assert.equal(h.button(tree, "Mod INI resolution", "Save resolution").props.children.props.style, undefined);
});

test("other settings save committed dimensions and preserve resolution drafts", async () => {
  const h = createSettingsHarness();
  await h.initialize();
  let tree = h.render();
  h.field(tree, "Mod INI resolution", "Width (pixels, when supported)")
    .props.onChange({ currentTarget: { value: "" } });
  tree = h.render();
  h.field(tree, "Local mod archives", "Folder containing *.zip mods")
    .props.onChange({ currentTarget: { value: "/mods" } });
  tree = h.render();
  h.field(tree, "Local mod archives", "Folder containing *.zip mods").props.onBlur();
  await new Promise((resolve) => setImmediate(resolve));
  assert.deepEqual(h.calls[0].slice(4), ["1280", "800"]);
  tree = h.render();
  assert.equal(h.field(tree, "Mod INI resolution", "Width (pixels, when supported)").props.value, "");
  await h.button(tree, "Mod INI resolution", "Save resolution").props.onClick();
  assert.deepEqual(h.calls[1].slice(4), ["", "800"]);
  assert.equal(h.errors.length, 1);
  assert.equal(h.settings.ini_resolution_width, 1280);
  assert.equal(h.field(h.render(), "Mod INI resolution", "Width (pixels, when supported)").props.value, "");
});

test("changing the catalog source still refreshes it automatically", async () => {
  const h = createSettingsHarness(undefined, true);
  await h.initialize();
  let tree = h.render();
  h.field(tree, "Mod INI resolution", "Width (pixels, when supported)")
    .props.onChange({ currentTarget: { value: "1920" } });
  tree = h.render();
  h.field(tree, "Mod source (GitHub)", "Branch, tag, or commit")
    .props.onChange({ currentTarget: { value: "new-branch" } });
  h.field(h.render(), "Mod source (GitHub)", "Branch, tag, or commit").props.onBlur();
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(h.catalogUpdateCalls, 1);
  assert.equal(h.field(h.render(), "Mod INI resolution", "Width (pixels, when supported)").props.value, "1920");
  assert.deepEqual(h.errors, []);
});

test("resetting defaults replaces a pending resolution draft", async () => {
  const h = createSettingsHarness();
  await h.initialize();
  let tree = h.render();
  h.field(tree, "Mod INI resolution", "Width (pixels, when supported)")
    .props.onChange({ currentTarget: { value: "1920" } });
  tree = h.render();
  await h.button(tree, "Settings", "Reset to defaults").props.onClick();
  assert.deepEqual(h.calls[0].slice(4), ["1280", "800"]);
  assert.equal(h.field(h.render(), "Mod INI resolution", "Width (pixels, when supported)").props.value, "1280");
});

test("update current confirms the saved dimensions and requires confirmation", async () => {
  const h = createSettingsHarness();
  await h.initialize();
  let tree = h.render();
  h.field(tree, "Mod INI resolution", "Width (pixels, when supported)")
    .props.onChange({ currentTarget: { value: "1920" } });
  tree = h.render();
  h.button(tree, "Mod INI resolution", "Update current").props.onClick();
  assert.equal(h.updateCalls, 0);
  assert.match(h.modals[0].props.strDescription, /1280 x 800/);
  assert.match(h.modals[0].props.strDescription, /not saved and will not be used/);
  await h.modals[0].props.onOK();
  assert.equal(h.updateCalls, 1);
  assert.equal(h.modals[1].props.strTitle, "Update current mods");
  assert.equal(h.field(h.render(), "Mod INI resolution", "Width (pixels, when supported)").props.value, "1920");
});

test("update results abbreviate long game names without hiding errors", async () => {
  const h = createSettingsHarness({
    updated: ["1234567890", "12345678901", "🎮1234567890"],
    unchanged: [],
    skipped: [],
    failures: ["Long game name: permission denied"],
    warnings: ["Other warning"],
  });
  await h.initialize();
  h.button(h.render(), "Mod INI resolution", "Update current").props.onClick();
  await h.modals[0].props.onOK();
  const message = h.modals[1].props.strDescription;
  assert.match(message, /Updated: 1234567890, 1234567890\.\.\., 🎮123456789\.\.\./u);
  assert.match(message, /Failed: Long game name: permission denied/);
  assert.match(message, /Warning: Other warning/);
});
