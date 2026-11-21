# AnyAspectRatio Desktop

Play your games in full screen at any aspect ratio (32:9 / 21:9 / 16:10 / 4:3 / 3:2 etc.) on Windows, macOS and Linux.

This app finds your supported games and applies their aspect-ratio fixes. It is the [AnyAspectRatio Decky plugin](../AnyAspectRatioDecky/README.md) as a desktop app.

| Apply a fix per game                              | Edit its settings                                          |
| ------------------------------------------------- | ---------------------------------------------------------- |
| ![Games tab](../resources/desktop-games.jpg)      | ![Editing a fix's INI](../resources/desktop-edit-ini.jpg) |

## Install

Download the file for your system from the [latest release](https://github.com/TLin-Y/SteamAnyAspectRatioMods/releases/latest):

| System | File | How to start |
|---|---|---|
| Windows 10/11 | `...-Windows.exe` | Run it. If SmartScreen warns, choose **More info > Run anyway**. |
| macOS (Apple silicon) | `...-macOS.zip` | Unzip and move **AnyAspectRatio** to Applications. The first time, open **System Settings > Privacy & Security** and click **Open Anyway**. |
| Linux | `...-Linux.tar.xz` | Extract it and run `AnyAspectRatio/AnyAspectRatio`. |

Then press **Scan for supported games** and turn on **Apply** for a game.

- **macOS:** works with Windows Steam inside a CrossOver or Whisky bottle. The app sets up the bottle for you.
- **Linux:** the app shows a launch option to paste into the game's Steam properties.
- **Games outside Steam:** add their folder under **Settings → Game folders**.

## How the mods work

Each mod is a small DLL that patches the game engine. Depending on the game, it adds a custom resolution, fixes the aspect ratio and adjusts the FOV, so you see the same view as at 16:9 with more of the scene around it.

Adjust your system resolution and in-game resolution so the mod works properly:

![System resolution, in-game resolution and mod resolution nested inside each other](../resources/how-mods-work.svg?v=18210063)

## Good to know

- Fixes are made for the Steam version of each game. A game update may need a new fix.
- Fixes by **Lyall** are shipped as-is and not all tested by me. If one misbehaves, press **Remove** and tell me which game.
- Anti-cheat or online games: use fixes at your own risk.
- This app has zero tracking or upload code, whether offline mode is on or off.
- **Settings → Offline mode** blocks all internet use. Import the offline mods pack from the release page first.
- Deleting the app keeps applied fixes. Press **Remove** on each game first if you want them gone.

## License

MIT, see [LICENSE](../LICENSE). Third-party notices: [THIRD_PARTY_NOTICES.md](../AnyAspectRatioDecky/THIRD_PARTY_NOTICES.md); each download includes the licenses of the open-source parts it uses. To build the app yourself, see [build.py](./build.py).
