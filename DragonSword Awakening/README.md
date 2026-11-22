# DragonSword: Awakening-Any-Aspect-Ratio

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

Removes the black bars from cutscenes and runs the game at any aspect ratio.

## Install

Extract `DSClient_ARFix.asi`, `DSClient_ARFix.ini`, and `winmm.dll` into `DS\Binaries\Win64`
(beside `DSClient-Win64-Shipping.exe`). Use the game's windowed mode.

## SteamOS / Winlator

Add the launch option:

```text
WINEDLLOVERRIDES="winmm=n,b" %command%
```

## Custom Aspect Ratio

Edit `DSClient_ARFix.ini` and restart the game:

```ini
[Resolution]
Width = 1280
Height = 800

[FOV]
KeepHorizontalFOV = 1
```

Default is 1280x800 (16:10). On screens narrower than 16:9, `KeepHorizontalFOV = 1` shows more above and below; `0` crops the sides instead.

## Demo

![DragonSword: Awakening demo](https://raw.githubusercontent.com/TLin-Y/SteamAnyAspectRatioMods/main/DragonSword%20Awakening/demo.jpg)
