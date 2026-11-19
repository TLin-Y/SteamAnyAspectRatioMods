const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const test = require("node:test");
const ts = require("typescript");

function loadFilter() {
  const file = path.join(__dirname, "..", "src", "services", "gameFilter.ts");
  const module = { exports: {} };
  const code = ts.transpileModule(fs.readFileSync(file, "utf8"), {
    compilerOptions: { module: ts.ModuleKind.CommonJS, target: ts.ScriptTarget.ES2020 },
  }).outputText;
  vm.runInNewContext(code, { module, exports: module.exports });
  return module.exports.gameMatchesFilter;
}

const matches = loadFilter();
const game = { app_name: "NINJA GAIDEN Σ2", mod_name: "Ninja Gaiden Sigma 2 Fix" };

test("An empty or blank filter shows every game", () => {
  assert.equal(matches(game, ""), true);
  assert.equal(matches(game, "   "), true);
});

test("Every word must appear in the game or fix name, ignoring case", () => {
  assert.equal(matches(game, "ninja"), true);
  assert.equal(matches(game, "  SIGMA   2 "), true);
  assert.equal(matches(game, "gaiden fix"), true);
  assert.equal(matches(game, "ninja 3"), false);
  assert.equal(matches(game, "persona"), false);
});
