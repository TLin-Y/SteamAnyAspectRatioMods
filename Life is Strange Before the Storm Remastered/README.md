# Life is Strange Before the Storm Remastered-Any-Aspect-Ratio

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

![Life is Strange Before the Storm Remastered at 4:3, 16:10 and 32:9](https://raw.githubusercontent.com/TLin-Y/SteamAnyAspectRatioMods/main/Life%20is%20Strange%20Before%20the%20Storm%20Remastered/demo.jpg)

Runs the game at any aspect ratio without black bars, in gameplay and cutscenes, with an optional FOV multiplier.

## Install

Extract `LiSBtS_ARFix.asi`, `LiSBtS_ARFix.ini`, and `winmm.dll` into `LIS - Before the Storm Remastered`
(beside `Life is Strange - Before the Storm.exe`).

## SteamOS / Winlator

Add the launch option:

```text
WINEDLLOVERRIDES="winmm=n,b" %command%
```

## Settings

Edit `LiSBtS_ARFix.ini` and restart the game:

```ini
[Resolution]
Width = 0
Height = 0

[FOV]
Multiplier = 1.0
```

`Width` and `Height` set the resolution. `0` uses your screen resolution (borderless fullscreen). Any other size, for example `1280` and `800`, runs the game in a borderless window of that size.
The in-game Screen Resolution and Display Mode settings are overridden by the mod.
`Multiplier` scales the camera field of view (0.5 to 2.0), cutscenes included.

## Known issues

- Episode artwork and the journal stay 16:9.
