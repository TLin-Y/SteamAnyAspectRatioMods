# FutureSoldierFix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**FutureSoldierFix** is an ASI plugin for **Tom Clancy's Ghost Recon Future Soldier** that adds ultrawide support, unlocks the framerate and more.

## Features
### General
- Borderless windowed mode.
- Unlock framerate.
- Disable ADS vignette.

### Ultrawide
- Proper ultrawide support.
- Scaled 16:9 HUD.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the game folder. (e.g. "**steamapps\common\Tom Clancy's Ghost Recon Future Soldier**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="winmm=n,b" %command%` to the launch options.

## Configuration
- Open **`FutureSoldierFix.ini`** to adjust settings.

## Screenshots
| ![futuresoldier_32by9_gameplay](.github/images/futuresoldier_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.

