# Final Fantasy Resonance - Ultrawide Mod

Author: [Refilo](https://www.nexusmods.com/finalfantasyresonance/mods/29). The author's release zip (v2.6.3) is kept unchanged; its `README.txt` has the full option list.

Hor+ ultrawide (32:9, 24:10, 21:9) and Vert+ for screens taller than 16:9 such as 16:10 (Steam Deck 1280x800): more of the world above and below instead of zooming in; menus, HUD and cutscenes keep their 16:9 layout.

## Install

1. Copy `winmm.dll` and the `refilomod` folder (keep its structure) into
   `steamapps/common/FINAL FANTASY RESONANCE/FFRS/Binaries/Win64/` (the folder with `FFRS-Win64-Shipping.exe`).
2. SteamOS/Winlator launch option: `WINEDLLOVERRIDES="winmm=n,b" %command%`. Without it Proton uses its built-in `winmm.dll` and the mod does not load.
3. Start the game and pick the resolution in Settings > Graphics.

## Settings

`refilomod/ultrawide/FFRS_UW.ini`, or the in-game overlay (F2). F1 toggles the mod, F4 the vignette.

When updating from 2.3.x or older, delete the old `dxgi.dll` and old `refilomod/core` files first.
