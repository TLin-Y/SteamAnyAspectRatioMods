# AbsolumFix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**AbsolumFix** is an ASI plugin for **Absolum** that adds ultrawide/narrower support.

## Features

### Ultrawide/Narrower
- Support for any resolution/aspect ratio.
- 16:9 scaled HUD.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the game folder. (e.g. "**steamapps\common\Absolum\x64.steam**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="winhttp=n,b" %command%` to the launch options.

## Configuration
- Open **`AbsolumFix.ini`** to adjust settings.

## Screenshots
| ![absolum_32by9_gameplay](.github/images/absolum_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
Special thanks to **Volf** for testing! <br/>
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
