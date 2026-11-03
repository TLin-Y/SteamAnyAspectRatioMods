# DeathStranding2Fix
[![Patreon-Button](.forgejo/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.forgejo/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**DeathStranding2Fix** is an ASI plugin for **DEATH STRANDING 2: ON THE BEACH** that expands aspect ratio support, removes cutscene pillarboxing and more.

## Features
### General
- Adjust gameplay FOV.
- Disable depth of field/film grain/vignetting/chromatic aberration.

### Ultrawide/Narrower
- Support for any aspect ratio.
- Disable cutscene pillarboxing.
- Constrain gameplay HUD to 16:9.
- Fix cropped pre-rendered cinematics.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip into the game folder. (e.g. "**steamapps\common\DEATH STRANDING 2 - ON THE BEACH**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="version=n,b" %command%` to the launch options.

## Configuration
- Open **`DeathStranding2Fix.ini`** to adjust settings.

## Screenshots
| ![DeathStranding2_32by9_gameplay](.forgejo/images/DeathStranding2_32by9_cutscene.jpg) |
|:--------------------------:|
| 32:9 Cutscene (Pillarboxing Disabled) |

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading.  
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading.  
[spdlog](https://github.com/gabime/spdlog) for logging.  
[safetyhook](https://github.com/cursey/safetyhook) for hooking.

