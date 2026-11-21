# AnyAspectRatio for Decky

Play your games in full screen at any aspect ratio (48:9 / 32:9 / 21:9 / 16:10 / 4:3 / 3:2 etc.) on Steam Deck and SteamOS.

This Decky plugin finds your supported games (in every Steam library, including the microSD card) and applies their aspect-ratio fixes in one tap.

| Apply a fix per game                         | Edit its settings                              |
| -------------------------------------------- | ---------------------------------------------- |
| ![Games tab](screenshots/games-tab.jpg)      | ![Editing a fix's INI](screenshots/edit-ini.jpg) |

This is the Decky Plugin Store build. The mods and their source live in [SteamAnyAspectRatioMods](https://github.com/TLin-Y/SteamAnyAspectRatioMods). The same plugin is also a [desktop app for Windows, macOS and Linux](https://github.com/TLin-Y/SteamAnyAspectRatioMods/blob/main/AnyAspectRatioDesktop/README.md).

## Install

Install **AnyAspectRatio** from the Decky Plugin Store, open it in the Decky menu and press **Scan for supported games**.

## How the mods work

Each mod is a small DLL that patches the game engine. Depending on the game, it adds a custom resolution, fixes the aspect ratio and adjusts the FOV, so you see the same view as at 16:9 with more of the scene around it.

Adjust your system resolution and in-game resolution so the mod works properly:

![System resolution, in-game resolution and mod resolution nested inside each other](screenshots/how-mods-work.svg?v=18210063)

Each fix is made and tested by hand for one game, mostly for handhelds such as Steam Deck, AYN and Retroid Pocket.

## Good to know

- Fixes are made for the Steam version of each game. A game update may need a new fix.
- Fixes by **Lyall** are shipped as-is and not all tested by me. If one misbehaves, press **Remove** and tell me which game.
- Anti-cheat or online games: use fixes at your own risk.
- Uninstalling the plugin keeps applied fixes. Press **Remove** on each game first if you want them gone.

## Privacy and network access

This plugin has zero tracking or upload code. It only downloads the mod list and mod files from `raw.githubusercontent.com` (the SteamAnyAspectRatioMods repository) and checks every file's SHA-256 before using it. The mods are Windows DLL/ASI files that run inside the game under Proton; the plugin only copies them into the game folder and can remove them again.

**Settings → Offline mode** blocks all internet use. Before going offline, import the offline mods pack from the [releases page](https://github.com/TLin-Y/SteamAnyAspectRatioMods/releases/latest).

## License

MIT, see [LICENSE](LICENSE). It also includes the BSD 3-Clause license of the Decky plugin template. Third-party notices: [THIRD_PARTY_NOTICES.md](defaults/THIRD_PARTY_NOTICES.md); bundled npm package licenses: [THIRD_PARTY_LICENSES.txt](defaults/THIRD_PARTY_LICENSES.txt).
