# Nioh3Fix
[![Patreon-Button](.forgejo/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.forgejo/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**Nioh3Fix** is an ASI plugin for **Nioh 3** that can add custom resolutions, expand ultrawide support and more.

## Features
### General
- Custom resolutions.
- Adjust LOD distance.
- (Experimental) Adjust framerate cap.

### Ultrawide/Narrower
- Support for <16:9 resolutions with corrected HUD issues.
- Expanded support for ultrawide resolutions.
- Fix cropped pre-rendered movies (ultrawide).
- Fix vert- FOV in cutscenes (ultrawide).

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip into the game folder. (e.g. "**steamapps\common\Nioh3**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the launch options.

## Configuration
- Open **`Nioh3Fix.ini`** to adjust settings.

## Screenshots
| ![nioh3_48by9_gameplay](.forgejo/images/nioh3_48by9_gameplay.jpg) |
|:--------------------------:|
| 48:9 Gameplay |

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading.  
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading.  
[spdlog](https://github.com/gabime/spdlog) for logging.  
[safetyhook](https://github.com/cursey/safetyhook) for hooking.

