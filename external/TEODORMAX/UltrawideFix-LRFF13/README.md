# UltrawideFix - Lightning Returns: Final Fantasy XIII

Author: TEODORMAX - https://www.nexusmods.com/lightningreturnsfinalfantasy13/mods/35

Version 1.0.0, last updated 28 July 2026.

Ultrawide Fix for Lightning Returns: Final Fantasy XIII. Fixes the stretched image at wide aspect ratios and adds live FOV control, plus depth of field and volume settings (with numpad hotkeys). Works at 21:9, 32:9, and anything wider than 16:9.

## Installation

Extract the zip into your game folder (where `LRFF13.exe` is located):

`steamapps\common\LIGHTNING RETURNS FINAL FANTASY XIII\`

so that `version.dll` and `UltrawideFix.ini` are in that folder. Do not install it together with the `UltrawideFix.asi` variant.

Uninstall: delete the files. Nothing else is modified.

### SteamOS / Winlator (Wine users)

For Steam Deck / SteamOS, open the game's Properties, then General > Launch Options, and use:
`WINEDLLOVERRIDES="version=n,b" %command%`

For Winlator / Wine, add the following environment variable to the game or container settings:
Name: WINEDLLOVERRIDES
Value: version=n,b

### Custom Aspect Ratio

Open `UltrawideFix.ini` with any text editor and set `Width` and `Height` under `[Resolution]` to the ratio you want (e.g., 4:3, 16:10, 21:9). Set both to `0` to use the Windows resolution.

### Full-Screen Stretching

Set your SteamOS display scaling to "Stretch" and set the in-game resolution to 1280x720. The stretch mods render at the ratio in the ini, then scale that picture to fill the screen; if the in-game resolution is not 1280x720, it will not stretch. This pack defaults to 16:10; set the width and height in the ini to the ratio you want.

## Main features

- World & cutscenes un-stretched: correct proportions in gameplay and realtime cutscenes.
- FMV fix: pre-rendered videos display correctly.
- GUI fix: menus, HUD and dialog rendered at proper proportions and centered.
- FOV control: set a default in the INI, adjust live in-game: F5 = narrower, F6 = wider, F7 = reset. Your last value persists across launches.
- Visual effect settings: disable depth of field from the INI or with the F8 toggle key in game.
- Volume control: the setting the game never had. Set it in the INI or adjust live: Numpad + / - = volume, * = mute, / = full. Your last value persists across launches.
- ASI loader: other `*.asi` plugins placed next to it are loaded, so other mods can coexist.

## Requirements

Steam version of Lightning Returns: Final Fantasy XIII. No other dependencies.
