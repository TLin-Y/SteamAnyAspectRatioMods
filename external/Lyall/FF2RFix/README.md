# FF2RFix
[![Patreon-Button](.forgejo/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.forgejo/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**FF2RFix** is an ASI plugin for **FATAL FRAME II: Crimson Butterfly REMAKE** that can add custom resolutions, expand ultrawide support and more.

## Features
### General
- Skip intro logos.
- Custom resolutions.
- Adjustable framerate target.

### Ultrawide/Narrower
- Expanded support for ultrawide resolutions.
- Support for <16:9 displays.
- Centered 16:9 HUD.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip into the game folder. (e.g. "**steamapps\common\FatalFrameII**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the launch options.

## Configuration
- Open **`FF2RFix.ini`** to adjust settings.

## Screenshots
| ![FF2R_32by9_gameplay](.forgejo/images/FF2R_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
Thanks to [wagrenier](https://github.com/wagrenier) for making this fix happen and testing it out!  
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading.  
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading.  
[spdlog](https://github.com/gabime/spdlog) for logging.  
[safetyhook](https://github.com/cursey/safetyhook) for hooking.