# Dragon Age 1/2 Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is an ASI plugin for Dragon Age: Origins and Dragon Age II that fixes various ultrawide display issues, adds borderless windowed mode and more.

## Features
### General
- [**DA:O/DA2**]: Borderless windowed mode.
- [**DA:O**]: Adjustable HUD scale with automatic setting.

### Ultrawide
- [**DA:O/DA2**]: Disable pillarboxing during dialog/cutscenes.
- [**DA:O**]: Fix foliage culling and shadow issues at ultrawide aspect ratios.

### Graphics
- [**DA:O**]: Adjust various draw distances (foliage, NPCs and objects).
- [**DA:O**]: Adjust shadow resolution.

## Installation
- Download the latest [release](../../../releases).
- Extract the contents of the release zip in to the the game folder. <br />
e.g. ("**steamapps\common\Dragon Age Ultimate Edition**" or "**steamapps\common\Dragon Age II**" for Steam).

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the launch options.

## Configuration
- See **DAFix.ini** to adjust settings.

## Screenshots

| ![dao_dialog_comparison](.github/images/dao_dialog_comparison.png) |
|:--------------------------:|
| [**DA:O**]: Dialog |

| ![da2_cutscene_comparison](.github/images/da2_cutscene_comparison.png) |
|:--------------------------:|
| [**DA2**]: Cutscene |

## Known Issues
Please report any issues you see.

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
