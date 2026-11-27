# Sonic Racing CrossWorlds-Any-Aspect-Ratio

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

![Sonic Racing CrossWorlds at 4:3, 16:10 and 32:9](https://raw.githubusercontent.com/TLin-Y/SteamAnyAspectRatioMods/main/Sonic%20Racing%20CrossWorlds/demo.jpg)

Runs the game at any aspect ratio without black bars, in races, menus and cutscenes. Narrower screens keep the 16:9 side view and show more above and below; wider screens show more at the sides. Works with the full game and the demo.

## Install

Extract `CrossWorlds_ARFix.asi`, `CrossWorlds_ARFix.ini`, and `winmm.dll` into `UNION\Binaries\Win64` (beside `SonicRacingCrossWorldsSteam.exe`).

## SteamOS / Winlator

Add the launch option:

```text
WINEDLLOVERRIDES="winmm=n,b" %command%
```

## Settings

Pick your screen resolution in the game, for example 1280 x 800 on the Steam Deck.

Edit `CrossWorlds_ARFix.ini` and restart the game:

```ini
[FOV]
Multiplier = 1.0
```

`Multiplier` scales the camera field of view (0.5 to 2.0).
