const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const ts = require("typescript");

function renderSection({ folders = [], pick, add, remove }) {
  const calls = [];
  const errors = [];
  let changed = 0;
  const element = (type, props) => ({ type, props: props || {} });
  const dependencies = {
    "react/jsx-runtime": { jsx: element, jsxs: element, Fragment: "Fragment" },
    "@decky/ui": { ButtonItem: "ButtonItem", DialogButton: "DialogButton", PanelSection: "PanelSection",
      PanelSectionRow: "PanelSectionRow" },
    "@decky/api": { openFilePicker: async (...args) => { calls.push(["pick", ...args]); return pick(); } },
    "../../services/callables": {
      addCustomGameDir: async (folder) => { calls.push(["add", folder]); return add ? add(folder) : {}; },
      removeCustomGameDir: async (folder) => { calls.push(["remove", folder]); return remove ? remove(folder) : {}; },
    },
  };
  const source = fs.readFileSync(path.join(__dirname, "../src/components/views/GameFoldersSection.tsx"), "utf8");
  const compiled = ts.transpileModule(source, {
    compilerOptions: { module: ts.ModuleKind.CommonJS, jsx: ts.JsxEmit.ReactJSX },
  }).outputText;
  const module = { exports: {} };
  new Function("require", "module", "exports", compiled)((name) => dependencies[name], module, module.exports);
  const tree = module.exports.GameFoldersSection({
    folders, disabled: false,
    onChanged: async () => { changed += 1; },
    onError: (title, error) => errors.push([title, error.message]),
  });
  const find = (node, predicate) => {
    if (Array.isArray(node)) return node.flatMap((child) => find(child, predicate));
    if (!node || typeof node !== "object") return [];
    return [...(predicate(node) ? [node] : []), ...find(node.props?.children, predicate)];
  };
  return {
    tree, calls, errors,
    get changed() { return changed; },
    button: (text) => find(tree, (node) => (node.type === "ButtonItem" || node.type === "DialogButton")
      && [].concat(node.props.children).join("") === text),
  };
}

const tick = () => new Promise((resolve) => setImmediate(resolve));

test("a picked folder is added by its real path and the settings reload", async () => {
  const section = renderSection({ pick: async () => ({ path: "/link/Games", realpath: "/real/Games" }) });
  section.button("Add game folder...")[0].props.onClick();
  await tick();
  assert.deepEqual(section.calls.map((call) => call[0]), ["pick", "add"]);
  assert.equal(section.calls[0][1], 1);
  assert.deepEqual(section.calls[1], ["add", "/real/Games"]);
  assert.equal(section.changed, 1);
});

test("dismissing the picker adds nothing; a rejected folder is reported", async () => {
  const dismissed = renderSection({ pick: async () => { throw new Error("closed"); } });
  dismissed.button("Add game folder...")[0].props.onClick();
  await tick();
  assert.deepEqual(dismissed.calls.map((call) => call[0]), ["pick"]);
  assert.deepEqual(dismissed.errors, []);

  const rejected = renderSection({
    pick: async () => ({ path: "/games" }),
    add: async () => { throw new Error("Game folder does not exist"); },
  });
  rejected.button("Add game folder...")[0].props.onClick();
  await tick();
  assert.deepEqual(rejected.errors, [["Could not add the game folder", "Game folder does not exist"]]);
  assert.equal(rejected.changed, 0);
});

test("each listed folder can be removed", async () => {
  const section = renderSection({ folders: ["/games/a", "/games/b"], pick: async () => null });
  const removeButtons = section.button("Remove");
  assert.equal(removeButtons.length, 2);
  removeButtons[1].props.onClick();
  await tick();
  assert.deepEqual(section.calls, [["remove", "/games/b"]]);
  assert.equal(section.changed, 1);
});
