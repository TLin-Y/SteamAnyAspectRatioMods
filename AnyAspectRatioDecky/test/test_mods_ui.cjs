const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const ts = require("typescript");

function createHarness(offline = false) {
  const states = [];
  let stateIndex = 0;
  let effect;
  let updateCalls = 0;
  let creditCalls = 0;
  let changed = 0;
  let resolveUpdate;
  const errors = [];
  const pending = new Promise((resolve) => { resolveUpdate = resolve; });
  const credits = { mod_count: 1, offline_mode: offline, authors: [], pack: { file_count: 0, size_bytes: 0 } };
  const hooks = {
    useState(initial) {
      const index = stateIndex++;
      if (!(index in states)) states[index] = initial;
      return [states[index], (value) => { states[index] = value; }];
    },
    useEffect(callback) { effect = callback; },
    useSyncExternalStore(subscribe, getSnapshot) { return getSnapshot(); },
  };
  const element = (type, props) => ({ type, props: props || {} });
  const compile = (file, dependencies) => {
    const source = fs.readFileSync(path.join(__dirname, file), "utf8");
    const output = ts.transpileModule(source, {
      compilerOptions: { module: ts.ModuleKind.CommonJS, jsx: ts.JsxEmit.ReactJSX },
    }).outputText;
    const module = { exports: {} };
    new Function("require", "module", "exports", output)(
      (name) => {
        if (!(name in dependencies)) throw new Error(`Unexpected dependency: ${name}`);
        return dependencies[name];
      },
      module,
      module.exports,
    );
    return module.exports;
  };
  const formatters = compile("../src/services/formatters.ts", {});
  const api = {
    getCredits: async () => { creditCalls += 1; return credits; },
    updateDependencies: () => { updateCalls += 1; return pending; },
  };
  const components = compile("../src/components/views/ModsTab.tsx", {
    react: hooks,
    "react/jsx-runtime": { jsx: element, jsxs: element },
    "@decky/ui": Object.fromEntries(
      ["ButtonItem", "ConfirmModal", "DialogButton", "Navigation", "PanelSection", "PanelSectionRow", "showModal"]
        .map((name) => [name, name]),
    ),
    "@decky/api": {},
    "../../services/callables": api,
    "../../services/modals": { showErrorModal: (...args) => errors.push(args) },
    "../../services/formatters": formatters,
  });
  const render = () => {
    stateIndex = 0;
    return components.ModsTab({ onDataChanged: () => { changed += 1; } });
  };
  const find = (node, predicate) => {
    if (Array.isArray(node)) {
      for (const child of node) {
        const found = find(child, predicate);
        if (found) return found;
      }
    } else if (node && typeof node === "object") {
      if (predicate(node)) return node;
      return find(node.props?.children, predicate);
    }
    return undefined;
  };
  return {
    render, find, errors, resolveUpdate,
    get updateCalls() { return updateCalls; },
    get creditCalls() { return creditCalls; },
    get changed() { return changed; },
    async load() {
      render();
      effect();
      await new Promise((resolve) => setImmediate(resolve));
    },
  };
}

test("catalog update is first, reports progress and refreshes the UI", async () => {
  const h = createHarness();
  let tree = h.render();
  let button = tree.props.children[0].props.children;
  assert.equal(button.props.children, "Update catalog & mods");
  assert.equal(button.props.disabled, true);
  await h.load();
  tree = h.render();
  button = tree.props.children[0].props.children;
  assert.equal(button.props.disabled, false);
  const operation = button.props.onClick();
  tree = h.render();
  assert.equal(tree.props.children[0].props.children.props.disabled, true);
  assert.equal(h.find(tree, (n) => n.props?.role === "status").props.children, "Checking the repository for updates...");
  h.resolveUpdate({ catalog_changed: true, mod_count: 100, local_archive_count: 3, warnings: ["Archive warning"] });
  await operation;
  tree = h.render();
  assert.match(h.find(tree, (n) => n.props?.role === "status").props.children, /Catalog updated: 100 mod\(s\), 3 local archive\(s\)/);
  assert.match(h.find(tree, (n) => n.props?.role === "status").props.children, /Archive warning/);
  assert.equal(h.updateCalls, 1);
  assert.equal(h.changed, 1);
  await h.load();
  assert.equal(h.creditCalls, 2);
});

test("offline mode disables catalog update", async () => {
  const h = createHarness(true);
  await h.load();
  assert.equal(h.render().props.children[0].props.children.props.disabled, true);
  assert.equal(h.updateCalls, 0);
});

test("catalog update errors are surfaced without reporting success", async () => {
  const h = createHarness();
  await h.load();
  const operation = h.render().props.children[0].props.children.props.onClick();
  h.resolveUpdate({ error: "Network unavailable" });
  await operation;
  assert.deepEqual(h.errors, [["Update failed", "Network unavailable"]]);
  assert.equal(h.changed, 0);
  assert.equal(h.render().props.children[0].props.children.props.disabled, false);
});

test("backup-target replacement confirmation explains the original-file backup", async () => {
  let modal;
  const source = fs.readFileSync(path.join(__dirname, "../src/services/modals.tsx"), "utf8");
  const output = ts.transpileModule(source, {
    compilerOptions: { module: ts.ModuleKind.CommonJS, jsx: ts.JsxEmit.ReactJSX },
  }).outputText;
  const module = { exports: {} };
  new Function("require", "module", "exports", output)(
    (name) => {
      if (name === "react/jsx-runtime") return { jsx: (type, props) => ({ type, props }) };
      if (name === "@decky/ui") return {
        ConfirmModal: "ConfirmModal",
        showModal: (node) => {
          modal = node;
          return { Close() {} };
        },
      };
      if (name === "../components/modals/LaunchOptionsInfoModal") return { LaunchOptionsInfoModal: "LaunchOptionsInfoModal" };
      if (name === "../components/modals/GameChoiceModal") return { GameChoiceModal: "GameChoiceModal" };
      throw new Error(`Unexpected dependency: ${name}`);
    },
    module,
    module.exports,
  );
  const confirmation = module.exports.confirmFileReplacement(
    "AI: The Somnium Files",
    ["AI_TheSomniumFiles_Data/Managed/Assembly-CSharp.dll"],
    ["AI_TheSomniumFiles_Data/Managed/Assembly-CSharp.dll"],
  );
  assert.equal(modal.props.strTitle, "Replace original game file?");
  assert.match(modal.props.strDescription, /requires replacing the original/);
  assert.match(modal.props.strDescription, /backed up so it can be restored/);
  assert.equal(modal.props.strOKButtonText, "Replace original file");
  modal.props.onOK();
  assert.equal(await confirmation, true);
});
