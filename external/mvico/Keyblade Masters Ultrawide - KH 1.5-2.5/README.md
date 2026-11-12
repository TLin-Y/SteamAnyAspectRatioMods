# Keyblade Masters Ultrawide - Kingdom Hearts I.5+II.5 ReMIX

Version 1.0.1 (released 10 Sep 2026) by m.vico. Licensed under CC BY-NC-ND 4.0; see [LICENSE](./LICENSE).

Adds ultrawide support to all four games in the Remix, removes pillarboxing, and expands the level of detail for wider aspect ratios. The author reports testing at 16:10, 21:9, 32:9, and 48:9.

## Installation

Extract the release archive into the main Kingdom Hearts I.5+II.5 ReMIX folder, alongside the game executables.

For SteamOS / Wine, set this Steam launch option:

`WINEDLLOVERRIDES="d3d11=n,b" %command%`

If you use OpenKH, do not install this ASI Loader version alongside OpenKH's `DBGHELP.dll`. Use the OpenKH Mod Manager instructions on the [original mod page](https://www.nexusmods.com/kingdomheartsfinalmix/mods/230) instead.

## Configuration

Edit `Keyblade-Masters-UW-KH-1.5+2.5-config.ini`. The width and height default to `auto`; set both values to choose a specific resolution. To enable 60 FPS cutscenes, set `cutscene60fps=true` under `[FPS]` and set the in-game frame rate to 60 or 120. The author notes this option may cause glitches.

## Source

[Nexus Mods - Keyblade Masters Ultrawide - KH 1.5-2.5](https://www.nexusmods.com/kingdomheartsfinalmix/mods/230)
