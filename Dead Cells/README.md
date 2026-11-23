# DEAD-CELLS-Any-Aspect-Ratio

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

![Dead Cells at 4:3](https://raw.githubusercontent.com/TLin-Y/SteamAnyAspectRatioMods/main/Dead%20Cells/43.jpg)

![Dead Cells at 16:9](https://raw.githubusercontent.com/TLin-Y/SteamAnyAspectRatioMods/main/Dead%20Cells/169.jpg)

![Dead Cells at 32:9](https://raw.githubusercontent.com/TLin-Y/SteamAnyAspectRatioMods/main/Dead%20Cells/329.jpg)

## 1. Install

Extract the ZIP into the game folder, beside `deadcells.exe`.

## 2. Optional: SteamOS / Proton / Winlator / macOS

Launch option: `WINEDLLOVERRIDES="dinput8=n,b" %command%`

## 3. Custom Aspect Ratio

Edit `Width` and `Height` in `[Window]` in `deadcells_ARFix.ini`. The default is
1280x800 (16:10). The game runs in a borderless window of that size.
`Width = 0` and `Height = 0` use the full monitor resolution.

The game fills any window shape with no black bars: wider screens show more to
the sides, taller ones (16:10, 4:3) show more above and below. The HUD stays in
the screen corners.

While `[Window] Enabled = true`, the mod controls the window and the in-game
Window Mode option has no effect. Set it to `false` to use the game's own
windowed/fullscreen modes; the picture still fills the screen.

## 4. Custom FOV

Edit `FOV` in `[View]` in `deadcells_ARFix.ini`. `1.0` is the game's normal
view, `1.1` shows 110% of it, `2.0` shows 200%. The HUD scales with the view.

Works with the default DirectX version (`deadcells.exe`), not the OpenGL legacy option.
