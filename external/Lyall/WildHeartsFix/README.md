# WildHeartsFix
[![Patreon-Button](.forgejo/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.forgejo/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**WildHeartsFix** is an ASI plugin for **WILD HEARTS** that can add custom resolutions, expand ultrawide support and more.

## Features
### General
- Skip intro logos.
- Custom resolutions.
- 60FPS cutscenes.
- Disable frameskipping.

### Ultrawide/Narrower
- Expanded support for ultrawide resolutions.
- Adjustable HUD size.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip into the game folder. (e.g. "**steamapps\common\WILD HEARTS**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the launch options.

## Configuration
- Open **`WildHeartsFix.ini`** to adjust settings.

## Screenshots
| ![WildHearts_32by9_gameplay](.forgejo/images/WildHearts_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading.  
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading.  
[spdlog](https://github.com/gabime/spdlog) for logging.  
[safetyhook](https://github.com/cursey/safetyhook) for hooking.