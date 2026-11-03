# WoLongFix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**WoLongFix** is an ASI plugin for **Wo Long: Fallen Dynasty** that adds ultrawide/narrower support, custom resolutions and more.

## Features

### General
- Custom resolutions.
- Adjustable gameplay FOV.
- Improve level of detail distance.

### Ultrawide/Narrower
- Adjustable HUD size (ultrawide-only).
- Expanded ultrawide support.
- Support for narrower than 16:9 resolutions.
- Disable pillarboxing.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the the game folder. (e.g. **steamapps\common\WoLongFallenDynasty** for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the launch options.

## Configuration
- Open **`WoLongFix.ini`** to adjust settings.

## Screenshots
| ![wolong-32by9](.github/images/wolong_32by9.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
Thanks to **Hotiraipha** for commissioning this fix! <br />
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
