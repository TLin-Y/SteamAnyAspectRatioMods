# CODE VEIN-Any-Aspect-Ratio

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

Runs the game at any aspect ratio without black bars, with an optional FOV multiplier and borderless window.

## Install

Extract `CodeVein_ARFix.asi`, `CodeVein_ARFix.ini`, and `winhttp.dll` into `CodeVein\Binaries\Win64`
(beside `CodeVein-Win64-Shipping.exe`). Pick your screen resolution in the game's Graphics Settings.

## SteamOS / Winlator

Add the launch option:

```text
WINEDLLOVERRIDES="winhttp=n,b" %command%
```

## Settings

Edit `CodeVein_ARFix.ini` and restart the game:

```ini
[FOV]
Multiplier = 1.0

[Display]
Borderless = false
```

`Multiplier` scales the camera field of view (0.5 to 2.0), cutscenes included.
`Borderless = true` removes the title bar in windowed mode and centres the window.

## Demo

![CODE VEIN demo](https://raw.githubusercontent.com/TLin-Y/SteamAnyAspectRatioMods/main/CODE%20VEIN/demo.jpg)
