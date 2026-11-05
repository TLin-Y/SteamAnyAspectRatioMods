# FFXIII2UW - Final Fantasy XIII-2 Ultrawide Fix

Author: TEODORMAX - https://www.nexusmods.com/finalfantasyxiii2/mods/42

Version 1.4.0, last updated 17 July 2026.

Ultrawide Fix for Final Fantasy XIII-2. Fixes the stretched image at wide aspect ratios and adds live FOV control, plus optional bloom, depth of field, and volume settings (with numpad hotkeys). Works at 21:9, 32:9, and anything wider than 16:9 - everything derives from your actual resolution, the mod disables itself at 16:9, and all optional settings default to vanilla.

## Installation

This pack uses the ASI version (Ultimate ASI Loader as `dinput8.dll`). Extract the zip into your game folder:

`steamapps\common\FINAL FANTASY XIII-2\alba_data\prog\win\bin\`

so that `dinput8.dll`, `FFXIII2UW.asi` and `FFXIII2FOV.ini` are in that folder. Never install it together with the standalone `VERSION.dll` variant.

Uninstall: delete the files. Nothing else is modified.

### SteamOS / Winlator (Wine users)

For Steam Deck / SteamOS, open the game's Properties, then General > Launch Options, and use:
`WINEDLLOVERRIDES="dinput8=n,b" %command%`

For Winlator / Wine, add the following environment variable to the game or container settings:
Name: WINEDLLOVERRIDES
Value: dinput8=n,b

## Main features

- World & cutscenes un-stretched: correct proportions in gameplay and realtime cutscenes.
- FMV fix: pre-rendered videos display correctly, fullscreen and inset.
- GUI fix: menus, HUD and dialog rendered at proper proportions and centered.
- Historia Crux and Beginner's Primer fixes.
- FOV control: set a default in the INI, adjust live in-game: F5 = narrower, F6 = wider, F7 = reset. Your last value persists across launches.
- Visual effect settings: adjust or disable bloom and depth of field from the INI.
- Volume control: the setting the game never had. Set it in the INI or adjust live: Numpad + / - = volume, * = mute, / = full. Your last value persists across launches.
- ASI loader: other `*.asi` plugins placed next to it are loaded, so other mods can coexist.

## Compatibility

- Nova Chrysalia: fully compatible - install this mod manually as above and launch through Nova as usual.
- Texture/data mods (Nova modpacks, HD packs): no conflict - different layer entirely.

## Known limitations

- The corner minimap renders as the game draws it natively at wide aspect (slightly stretched, fully functional).
- Battle cameras use the game's default FOV; the FOV control affects field/exploration cameras.
- Tested at 32:9 (5120x1440) and confirmed at 21:9 (5120x2160) by user feedback. Works at any aspect ratio.

Note: This mod has not yet been tested through a full playthrough - it was developed and verified across many areas, battles, cutscenes, FMVs and menus at 32:9, but not every corner of the game. Most likely everything just works.

## Requirements

Steam version of Final Fantasy XIII-2. No other dependencies.
