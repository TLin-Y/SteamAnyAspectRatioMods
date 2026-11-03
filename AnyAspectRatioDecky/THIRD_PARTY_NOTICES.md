# Third-Party Notices

## Decky plugin template

The project follows the structure of the [official Decky plugin template](https://github.com/SteamDeckHomebrew/decky-plugin-template). `rollup.config.js` and `tsconfig.json` are adapted from the template. The plugin UI and backend implementation were written for this project.

Template-derived configuration is covered by the BSD 3-Clause License. See [LICENSE-Decky-Plugin-Template.txt](./LICENSE-Decky-Plugin-Template.txt) for the retained upstream notice and license terms.

## Decky API

The built frontend includes code from [`@decky/api` 1.1.3](https://github.com/SteamDeckHomebrew/loader-api), licensed under GNU LGPL version 2.1. The license text is included in [LICENSE-LGPL-2.1.txt](./LICENSE-LGPL-2.1.txt).

## Decky UI and build tooling

The plugin uses `@decky/ui` as a Decky-provided runtime external and does not bundle its implementation in `dist/index.js`. `@decky/rollup` is build-time tooling. These and all transitive npm dependencies remain under their respective upstream licenses.
