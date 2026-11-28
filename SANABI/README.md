# SANABI-Any-Aspect-Ratio

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

This fix removes SANABI's camera aspect-ratio limits and adapts the view to the game's active render dimensions. It does not change the game resolution; choose the desired resolution in the game or launcher.

## Demo

![SANABI running at different aspect ratios](https://raw.githubusercontent.com/TLin-Y/SteamAnyAspectRatioMods/main/SANABI/demo.jpg)

## Install

1. Extract the archive into the game folder containing `SNB.exe`.
2. On SteamOS / Proton, set the Steam launch option to:
   `WINEDLLOVERRIDES="winmm=n,b" %command%`
3. On Winlator, add the environment variable `WINEDLLOVERRIDES` with value `winmm=n,b`.

## Custom Aspect Ratio

Choose a game resolution with the aspect ratio you want. The fix follows the active render dimensions automatically; it does not use fixed width/height values.

In `SNB_ARFix.ini`, `[AspectRatio] Enabled` turns the fix on or off. `MaintainFOV = true` preserves vertical framing when adapting the camera.

```ini
[AspectRatio]
Enabled = true
MaintainFOV = true
```

## Text Size

To make the text bigger (menus and speech bubbles), set `TextScale` in `SNB_ARFix.ini`, for example `1.5` or `2.0`. `1.0` keeps the original size. Restart the game after changing it.

```ini
[UI]
TextScale = 1.0
```
