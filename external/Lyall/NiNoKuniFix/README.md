# NiNoKuniFix
[![Patreon-Button](.forgejo/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.forgejo/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**NiNoKuniFix** is an ASI plugin for **Ni no Kuni Wrath of the White Witch™ Remastered** that can add ultrawide/narrower support and skip the intro logos. 

## Features
### General
- Skip intro logos.

### Ultrawide/Narrower
- Support for any aspect ratio.
- 16:9 scaled HUD.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip into the game folder. (e.g. "**steamapps\common\Ni no Kuni Wrath of the White Witch™ Remastered**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="winmm=n,b" %command%` to the launch options.

## Configuration
- Open **`NiNoKuniFix.ini`** to adjust settings.

## Screenshots
| ![NiNoKuni_32by9_gameplay](.forgejo/images/NiNoKuni_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading.  
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading.  
[spdlog](https://github.com/gabime/spdlog) for logging.  
[safetyhook](https://github.com/cursey/safetyhook) for hooking.