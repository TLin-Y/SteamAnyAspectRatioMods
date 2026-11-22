# DEVIL-MAY-CRY-4-SPECIAL-EDITION-Any-Aspect-Ratio

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

![Devil May Cry 4 Special Edition at 32:9 and 4:3](https://raw.githubusercontent.com/TLin-Y/SteamAnyAspectRatioMods/main/Devil%20May%20Cry%204%20Special%20Edition/demo.jpg)

## 1. Install

Extract the ZIP into the game folder, beside `DevilMayCry4SpecialEdition.exe`.

## 2. Optional: SteamOS / Proton / Winlator / macOS

Launch option: `WINEDLLOVERRIDES="dinput8=n,b" %command%`

## 3. Custom Aspect Ratio

Edit `Width` and `Height` in `[Resolution]` in `DevilMayCry4SpecialEdition_ARFix.ini`.
The default is 1280x800 (16:10, Steam Deck). Use your screen resolution.
`Width = 0` and `Height = 0` use the resolution selected in the game.

The game fills the whole screen with no black bars. The HUD and menus stay 16:9
in the middle of the screen. Movies play in 16:9.

`Borderless = true` removes the window frame in windowed mode.
`FOV` in `[Camera]` changes the field of view (default 1.0).
`EventBars = true` keeps the black bars in cutscenes.
