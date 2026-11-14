const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");
const ts = require("typescript");

const SOURCE = process.env.LAUNCH_OPTIONS_SOURCE
  || path.join(__dirname, "..", "src", "launch_options", "steamLaunchOptions.ts");

/** Load the module against a fake Steam client whose launch options live in `options`. */
function loadLaunchOptions(options = {}) {
  const storage = new Map();
  const code = ts.transpileModule(fs.readFileSync(SOURCE, "utf8"), {
    compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020 },
  }).outputText;
  const module = { exports: {} };
  vm.runInNewContext(code, {
    module, exports: module.exports, Error, JSON, Number, Date, Promise, Object, Array, Set, Map,
    SteamClient: {
      // Like Steam's client API, the methods need their object as `this`.
      Apps: {
        options,
        RegisterForAppDetails(appId, callback) {
          const current = this.options[appId] ?? "";
          setImmediate(() => callback({ strLaunchOptions: current }));
          return { unregister() {} };
        },
        SetAppLaunchOptions(appId, value) { this.options[appId] = value; },
      },
    },
    window: {
      setTimeout, clearTimeout,
      localStorage: {
        getItem: (key) => (storage.has(key) ? storage.get(key) : null),
        setItem: (key, value) => storage.set(key, String(value)),
      },
    },
  });
  return { api: module.exports, options };
}

async function applyMod(api, appId, modId, groups) {
  const plan = await api.prepareLaunchOptions(appId, modId, groups);
  return api.applyLaunchOptions(plan, groups);
}

const scenarios = [
  ["empty options", "", ["dinput8=n,b"]],
  ["other arguments, no %command%", "-novid -fullscreen", ["dinput8=n,b"]],
  ["existing override is extended", 'WINEDLLOVERRIDES="xinput1_3=n,b" %command% -x', ["dinput8=n,b"]],
  ["rule already present", 'WINEDLLOVERRIDES="dinput8=n,b" %command%', ["dinput8=n,b"]],
  ["one of two DLLs present", 'PROTON_LOG=1 WINEDLLOVERRIDES="dxgi=n,b" %command%', ["dxgi,winmm=n,b"]],
  ["quoted argument kept", '%command% -name "My Save"', ["version=n,b"]],
];

for (const [name, initial, groups] of scenarios) {
  test(`apply then remove restores the user's options: ${name}`, async () => {
    const { api, options } = loadLaunchOptions({ 1: initial });
    const applied = await applyMod(api, 1, "mod", groups);
    assert.equal(options[1], applied.updatedOptions);
    for (const group of groups) {
      const [dlls, rule] = group.split("=");
      for (const dll of dlls.split(",")) assert.match(options[1], new RegExp(`${dll}[^;"]*=${rule}`));
    }
    assert.match(options[1], /%command%/);
    await api.removeLaunchOptions(1, "mod");
    assert.equal(options[1], initial);
  });
}

test("expected merged options", async () => {
  const expected = {
    "": 'WINEDLLOVERRIDES="dinput8=n,b" %command%',
    "-novid -fullscreen": 'WINEDLLOVERRIDES="dinput8=n,b" %command% -novid -fullscreen',
    'WINEDLLOVERRIDES="xinput1_3=n,b" %command% -x': 'WINEDLLOVERRIDES="xinput1_3=n,b;dinput8=n,b" %command% -x',
  };
  for (const [initial, merged] of Object.entries(expected)) {
    const { api, options } = loadLaunchOptions({ 1: initial });
    await applyMod(api, 1, "mod", ["dinput8=n,b"]);
    assert.equal(options[1], merged);
  }
});

test("removal keeps groups the user added after applying", async () => {
  const { api, options } = loadLaunchOptions({ 1: "" });
  await applyMod(api, 1, "mod", ["dinput8=n,b"]);
  options[1] = 'WINEDLLOVERRIDES="dinput8=n,b;d3d11=n" %command% -dx11';
  const removal = await api.removeLaunchOptions(1, "mod");
  assert.equal(removal.changed, true);
  assert.equal(options[1], 'WINEDLLOVERRIDES="d3d11=n" %command% -dx11');
});

test("a mod that added nothing has no record to remove", async () => {
  const { api, options } = loadLaunchOptions({ 1: 'WINEDLLOVERRIDES="dinput8=n,b" %command%' });
  await applyMod(api, 1, "mod", ["dinput8=n,b"]);
  const removal = await api.removeLaunchOptions(1, "mod");
  assert.equal(removal.hadRecord, false);
  assert.equal(options[1], 'WINEDLLOVERRIDES="dinput8=n,b" %command%');
});

test("incomplete quoting is refused", async () => {
  const { api } = loadLaunchOptions({ 1: '%command% -name "unterminated' });
  await assert.rejects(api.prepareLaunchOptions(1, "mod", ["dinput8=n,b"]), /incomplete quoting/);
});
