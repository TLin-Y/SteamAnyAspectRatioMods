# BinaryDomainFix
[![Patreon-Button](.forgejo/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.forgejo/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**BinaryDomainFix** is an ASI plugin for **Binary Domain** that can add ultrawide/narrower support, skip the intro logos and more.

## Features
### General
- Skip intro logos.
- Borderless windowed mode.

## Graphics
- Increase level of detail.
- Allow SSAO to be disabled fully.

### Ultrawide/Narrower
- Support for any resolution/aspect ratio.
- Scaled 16:9 HUD.
- Remove letterboxing/pillarboxing in cutscenes.
- Correct FOV at <16:9.

## Framerate
- 60 fps cutscenes.
- Unlock framerate (experimental!).

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip into the game folder. (e.g. "**steamapps\common\Binary Domain**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the launch options.

## Configuration
- Open **`BinaryDomainFix.ini`** to adjust settings.

## Screenshots
| ![BinaryDomain_32by9_gameplay](.forgejo/images/BinaryDomain_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading.  
[mINI](https://github.com/metayeti/mINI) for ini reading.  
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
