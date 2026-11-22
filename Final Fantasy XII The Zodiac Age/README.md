# FINAL-FANTASY-XII-THE-ZODIAC-AGE-Any-Aspect-Ratio

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

![FINAL FANTASY XII The Zodiac Age Mod](https://raw.githubusercontent.com/TLin-Y/SteamAnyAspectRatioMods/main/Final%20Fantasy%20XII%20The%20Zodiac%20Age/ff12.jpg)

![FINAL FANTASY XII The Zodiac Age at 4:3](https://raw.githubusercontent.com/TLin-Y/SteamAnyAspectRatioMods/main/Final%20Fantasy%20XII%20The%20Zodiac%20Age/ff12demo.jpg)

![FINAL FANTASY XII The Zodiac Age ultrawide demo](https://raw.githubusercontent.com/TLin-Y/SteamAnyAspectRatioMods/main/Final%20Fantasy%20XII%20The%20Zodiac%20Age/ff12demo2.jpg)

## Install

Extract the ZIP into the game's `x64` folder, beside `FFXII_TZA.exe`. Set the
game's screen mode to **Windowed**, then launch the game.

## SteamOS / Winlator

Launch option: `WINEDLLOVERRIDES="dinput8=n,b" %command%`

## Custom Aspect Ratio

Edit `Width` and `Height` in `[Window]` in `FFXII_TZA_ARFix.ini`. Resolution
and FOV fixes are enabled by default.

Narrower screens preserve horizontal coverage; ultrawide screens preserve
vertical coverage. Legacy effect layers and menu backgrounds use matching
projection framing, fixing ghosting and compressed pause-menu backgrounds.
Set `Borderless = false` to keep window borders.

The fix uses code signatures instead of an executable version check. Other
versions may work, but have not been tested; missing hooks are logged.
