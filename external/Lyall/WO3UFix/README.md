# WO3UFix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**WO3UFix** is an ASI plugin for **WARRIORS OROCHI 3 Ultimate Definitive Edition** that can add custom resolutions, ultrawide/narrower support and more.

## Features

### General
- Custom resolution support.
- Ultrawide/narrower support.

### Ultrawide/Narrower
- Correctly scaled 16:9 HUD.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the game folder. (e.g. "**steamapps\common\WARRIORS OROCHI 3 Ultimate**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the launch options.

## Configuration
- Open **`WO3UFix.ini`** to adjust settings.

## Screenshots
| ![wo3u_21by9_gameplay](.github/images/wo3u_21by9_gameplay.jpg) |
|:--------------------------:|
| 21:9 Gameplay |

## Credits
Special thanks to **hotiraripha** for making this fix happen! <br />
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
