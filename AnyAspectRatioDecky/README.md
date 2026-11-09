# AnyAspectRatio for Decky

Play your games at fullscreen 16:10 on Steam Deck and SteamOS.

This Decky Loader plugin finds supported games, applies aspect-ratio fixes, and helps set Steam launch options.

| Apply a fix per game                     | Edit its settings                                 |
| ---------------------------------------- | ------------------------------------------------- |
| ![Games tab](../resources/games-tab.jpg?v=4556e8ed) | ![Editing a fix's INI](../resources/edit-ini.jpg) |

## Install

Requires a Steam Deck (or SteamOS) with [Decky Loader](https://decky.xyz).

1. Download `AnyAspectRatio-1.7.0.zip` from the [latest release](https://github.com/TLin-Y/SteamAnyAspectRatioMods/releases/latest).
2. Put the zip on the Deck (for example in `Downloads`).
3. In Game Mode, open Decky (`...` button), then **Settings → General** and turn on **Developer mode**.
4. Open **Settings → Developer → Install Plugin from ZIP**, pick the zip, and confirm.
5. Open **AnyAspectRatio** in the Decky menu and use **Scan Steam libraries**.

To update, install the newer zip the same way. To remove, uninstall from the Decky plugin list.

## Tabs

- **Games**: scan for supported games, apply or remove fixes, edit settings, and read instructions.
- **Mods**: update the catalog, see mod credits, download or import an offline pack, or delete local files and caches. Deleting local files keeps your settings and applied mods.
- **Settings**: set the resolution written into supported mod INIs when applying (enter both dimensions and choose **Save resolution**). After changing it, **Update current** is highlighted as a reminder to scan installed mods and update supported INIs using the saved resolution; confirm before overwriting their existing resolution values. Review the results for skipped or failed mods. Catalogued fields only are changed, and zero-valued defaults are left unchanged. You can also change mod sources, enable offline mode, or check for updates.

The Games scan summary counts supported games found, not all scanned Steam apps.
Elden Ring non-Steam shortcuts can target `Game/eldenring.exe`,
`Game/modengine2_launcher.exe`, `Game/start_protected_game.exe`, or
`Game/start_game_in_offline_mode.exe`. Launcher alternatives require the real
`Game/eldenring.exe`; the backup launcher is not a detection alternative.
Its fix is offline-only. Apply preserves the protected launcher as
`start_protected_game-old.exe`; Remove restores it. Existing Mod Engine 2 files
are not removed. Review file conflicts before replacing files used by other mods.

Catalog entries opt in to resolution edits with `ini_resolution` pairs specifying
`section`, `width`, `height`, and `kind` (`pixels` or `ratio`). Only declared
keys are edited; keys, comments, and formatting are preserved. Do not list
internal or replacement resolution fields.

### Offline mode and privacy

Turn on **Settings → Offline mode** to prevent the plugin from connecting to the internet. Before going offline, choose **Mods → Download offline pack**, or download `AnyAspectRatio-Offline-Mods-1.7.0.zip` from the release page and use **Mods → Import offline mods pack**. You can also use your own local mod archives. Offline mode applies to this plugin only, not Decky Loader or Steam.

## License

MIT, see [LICENSE](../LICENSE). Third-party notices: [THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md).
