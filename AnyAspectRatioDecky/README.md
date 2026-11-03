# AnyAspectRatio Decky

A Decky plugin for discovering and managing supported AnyAspectRatio game fixes on SteamOS.

> **Development status:** the plugin provides a read-only Steam library scan and matches installed games against the audited entries in `catalog.json`. It does not yet download or install mods, edit launch options, or change game files.

## Requirements

- Node.js 20 or newer
- pnpm 9.15.9
- Python 3.11 or newer for backend syntax checks
- A Steam Deck or SteamOS environment with Decky Loader for runtime testing

The frontend is based on the official [Decky plugin template](https://github.com/SteamDeckHomebrew/decky-plugin-template) and uses [Decky UI](https://github.com/SteamDeckHomebrew/decky-frontend-lib). The backend runs through Decky's Python plugin interface.

## Reproducible frontend build

Run these commands from this directory. The checked-in `pnpm-lock.yaml` pins the resolved dependency graph; `--frozen-lockfile` fails rather than silently changing it.

```sh
corepack enable
corepack prepare pnpm@9.15.9 --activate
pnpm install --frozen-lockfile
pnpm typecheck
pnpm build
```

The compiled frontend is written to `dist/index.js`. Do not edit generated files in `dist/`; they are ignored by Git.

On Windows PowerShell, run the same commands from `AnyAspectRatioDecky`. If Corepack is unavailable, install the pinned pnpm version with `npm install --global pnpm@9.15.9`.

## Backend check

The plugin backend entry point is `main.py`. `logger/plugin_logger.py` wraps Decky's logger and uses its persistent per-plugin log location (on the tested Deck: `/home/deck/homebrew/logs/AnyAspectRatio/`); the plugin does not create a second log file or manage rotation. Log events use a small structured format with operation/game/mod IDs; do not pass launch-option strings, credentials, or arbitrary user data as context.

Decky supplies the `decky` module when it loads the plugin, so `main.py` is not a standalone Python application. The logger and Steam VDF parser have no Decky dependency and can be tested locally:

```sh
python -m py_compile main.py
python -m unittest discover -s test -v
```

In Decky, open the plugin menu and select **Scan for supported games**. The scan reads configured Steam libraries and their app manifests only; it does not search entire drives or modify files. Only catalog entries with a matching Steam App ID and install directory are listed. For each match, the UI shows whether the expected executable and catalogued mod files are present.

`catalog.json` is the maintained metadata source for supported fixes. Entries must be audited for Steam App ID, install directory, executable, package contents, release URL, SHA-256, size, INI file, and Wine DLL overrides before inclusion. The backend validates the catalog at scan time.

## Local smoke-test package

The local PowerShell packager creates a timestamped ZIP under `out/` containing only the runtime plugin files. It does not install the plugin or connect to a Steam Deck:

```powershell
.\scripts\package-smoke.ps1
```

This is not a release package. Use the official [Decky CLI](https://github.com/SteamDeckHomebrew/cli) and current plugin-store requirements for distribution; its build process uses a container engine.

For a real Decky integration test, install the reviewed ZIP through Decky's supported developer workflow or an explicitly approved SSH session. The plugin directory is root-owned on the tested Deck, so deployment may require an interactive local sudo prompt. Do not put a Deck password in scripts, command arguments, or repository files. A loader restart affects all installed plugins.

After installation, select **Scan Steam libraries** in the plugin UI, then inspect Decky Loader logs if it fails. Remove only the test plugin directory when rolling back; do not remove or synchronize the entire plugins directory.

## Project layout

```text
AnyAspectRatioDecky/
  main.py             Decky Python backend and API entry point
  helper/             Shared Python constants
  logger/             Structured wrapper around Decky's persistent logger
  scanner/            Read-only Steam scan, catalog validation, and game matching
  catalog.json        Audited supported-fix metadata
  utils/              Reusable parsing and API helpers
  plugin.json         Decky plugin metadata
  LICENSE-*           Upstream template and bundled Decky API license texts
  THIRD_PARTY_NOTICES.md
  package.json        Pinned frontend toolchain and scripts
  pnpm-lock.yaml      Resolved dependency lockfile
  rollup.config.js    Decky Rollup build configuration
  scripts/            Local smoke-test packaging helper
  src/index.tsx       Supported-game scan and catalog match UI
  test/               Local backend and Steam discovery tests
  tsconfig.json       TypeScript build configuration
```

## Current validation boundary

`pnpm typecheck` and `pnpm build` validate the frontend bundle. Python syntax checks and backend unit tests cover logger formatting and Steam VDF/library discovery with temporary fixtures. A successful local build does not replace installation and runtime testing on the target SteamOS/Decky version.

## Credits and licensing

The plugin UI in `src/index.tsx` and backend in `main.py` were written for this project. The frontend follows the official [Decky plugin template](https://github.com/SteamDeckHomebrew/decky-plugin-template); `rollup.config.js` and `tsconfig.json` are adapted from its build configuration. The template's BSD 3-Clause notice is preserved in [LICENSE-Decky-Plugin-Template.txt](./LICENSE-Decky-Plugin-Template.txt).

The project-authored code is covered by the repository's [MIT license](../LICENSE). The built frontend also contains code from `@decky/api` 1.1.3, which is licensed under LGPL-2.1; the license text is included as [LICENSE-LGPL-2.1.txt](./LICENSE-LGPL-2.1.txt). [THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md) records the relevant upstream components and license scope. Other dependencies retain their respective licenses.
