# AnyAspectRatio for Decky

Play your games at fullsceen 16:10 on Steam Deck and SteamOS.

This Decky Loader plugin finds your supported games and applies aspect-ratio fixes in one tap, with the Steam launch options set for you


| Apply a fix per game                     | Edit its settings                                 |
| ---------------------------------------- | ------------------------------------------------- |
| ![Games tab](../resources/games-tab.jpg) | ![Editing a fix's INI](../resources/edit-ini.jpg) |

## Install

Requires a Steam Deck (or SteamOS) with [Decky Loader](https://decky.xyz).

1. Download `AnyAspectRatio-1.2.0.zip` from the [latest release](https://github.com/TLin-Y/SteamAnyAspectRatioMods/releases/latest).
2. Put the zip on the Deck (for example in `Downloads`).
3. In Game Mode, open Decky (`...` button), then **Settings → General** and turn on **Developer mode**.
4. Open **Settings → Developer → Install Plugin from ZIP**, pick the zip, and confirm.
5. Open **AnyAspectRatio** in the Decky menu and use **Scan Steam libraries**.

To update, install the newer zip the same way. To remove, uninstall from the Decky plugin list.

## Tabs

- **Games**: lists your games that have a mod, including non-Steam shortcuts. Apply or remove a mod per game, edit its settings, or read its ReadMe. Fixes that need manual steps are grouped under **Manual fixes** with a ReadMe button only.
- **Mods**: shows who made the mods. Two buttons:
  - **Import repository zip...**: use all of this repository's mods offline. On a PC or the Deck's browser, open this repository on GitHub, choose **Code → Download ZIP**, and save it in the Deck's `Downloads` folder. Then pick it here.
  - **Delete all local files and caches**: frees the space used by imported or downloaded files. Your settings and already-applied mods are kept.
- **Settings**: where mods come from, an optional folder of your own mod zips, and **Update** to fetch the newest mod list.

## License

MIT, see [LICENSE](../LICENSE). Third-party notices: [THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md).
