# ToBRFix
[![Patreon-Button](.forgejo/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.forgejo/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**ToBRFix** is an ASI plugin for **Tales of Berseria Remastered** that can add ultrawide/narrower support, skip intro logos and more.

## Features
### General
- Skip intro logos and autosave warning.
- Adjust resolution scale.
- Adjust draw distance for objects/grass/NPCs.
- Adjust gameplay FOV.

### Ultrawide/Narrower
- Support for ultrawide aspect ratios.
- Support for narrower (<16:9) aspect ratios.
- Scaled 16:9 HUD.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip into the game folder. (e.g. "**steamapps\common\Tales of Berseria Remastered**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="version=n,b" %command%` to the launch options.

## Configuration
- Open **`ToBRFix.ini`** to adjust settings.

## Screenshots
| ![ToBR_32by9_gameplay](.forgejo/images/ToBR_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
Special thanks to **XenoVars** for making this fix possible!  
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading.  
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading.  
[spdlog](https://github.com/gabime/spdlog) for logging.  
[safetyhook](https://github.com/cursey/safetyhook) for hooking.

