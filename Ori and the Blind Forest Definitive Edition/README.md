# Ori-and-the-Blind-Forest-Definitive-Edition-Force-Any-Aspect-Ratio

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

Unlocks all aspect ratios and keeps the FOV. Tested from 32:9 to 1:1.
The game has no ultrawide resolutions in its menu, so the mod also forces the window size from the INI.

## Install

1. Close the game. Back up `oriDE_Data\Managed\Assembly-CSharp.dll`
   as `Assembly-CSharp.dll.oride-original.bak`.
2. Extract the ZIP beside `oriDE.exe`, merging folders.
3. Windows with display scaling above 100%: right-click `oriDE.exe` -> Properties -> Compatibility ->
   Change high DPI settings -> tick "Override high DPI scaling behavior" (Application).

## SteamOS / Winlator

Steam launch option:

```
WINEDLLOVERRIDES="winmm=n,b" %command%
```

Winlator: Container Settings -> Environment Variables -> add `WINEDLLOVERRIDES` = `winmm=n,b`.

## Custom Aspect Ratio

Edit `Ori1_ARFix.ini`, then restart:

```ini
[Resolution]
Width = 2560
Height = 800
```
