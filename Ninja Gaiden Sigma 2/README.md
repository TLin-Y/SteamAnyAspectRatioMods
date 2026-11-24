# Ninja-Gaiden-Sigma-2-Any-Aspect-Ratio

Aspect ratio fix only, based on [NinjaGaidenMCFix](https://codeberg.org/Lyall/NinjaGaidenMCFix) by Lyall. Default 16:10 (1280x800).

## Install

1. Copy the files from `NG2 Aspect Ratio` (`dinput8.dll`, `NinjaGaidenMCFix.asi`, `NinjaGaidenMCFix.ini`) into the game folder, beside `NINJA GAIDEN SIGMA2.exe`.
2. SteamOS launch option: `WINEDLLOVERRIDES="dinput8=n,b" %command%`. For Winlator, set the environment variable `WINEDLLOVERRIDES=dinput8=n,b`.

## Custom Aspect Ratio

Edit `Width` and `Height` under `[Custom Resolution]` in `NinjaGaidenMCFix.ini`.

If the game crashes or shows a black screen during in-game movies (opening a chest, the logo), try renaming the `databin/movie` folder.

## Optional mods (install manually)

These are not included. Download them from their authors and follow their instructions. Use only one of the first two; Black Mod already includes the Gore Effects.

- Black Mod by Fiend Busa: https://www.nexusmods.com/ninjagaidenmastercollection/mods/37
- Gore Effects: https://www.nexusmods.com/ninjagaidenmastercollection/mods/236
- NGS2 NM Plugin by Nozomi Miyamori: https://github.com/nm004/ngs2-nm-plugin
