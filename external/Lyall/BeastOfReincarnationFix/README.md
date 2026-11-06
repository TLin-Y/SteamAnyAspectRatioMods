# BeastOfReincarnationFix
[![Patreon-Button](.forgejo/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.forgejo/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**BeastOfReincarnationFix** is an ASI plugin for **Beast of Reincarnation** that can adjust gameplay FOV, enable the console and more.

## Features
### General
- Enable the developer console.
- Adjust gameplay FOV.

### Ultrawide/Narrower
- Corrects vert- FOV in cutscenes for ultrawide displays.
- Fix various HUD issues like invisible health bars.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip into the game's binaries folder. (e.g. "**<GameFolder>\Binaries\Win64**" for Steam or **<GameFolder>\Content\BeastOfReincarnation\Binaries\WinGDK** for MS Store)

For Steam, the full folder is `steamapps\common\BeastOfReincarnation\BeastOfReincarnation\Binaries\Win64`.

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="dsound=n,b" %command%` to the launch options.

## Configuration
- Open **`BeastOfReincarnationFix.ini`** to adjust settings.

## Screenshots
| ![BeastOfReincarnation_32by9_cutscene](.forgejo/images/BeastOfReincarnation_32by9_cutscene.jpg) |
|:--------------------------:|
| 32:9 Cutscene |

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading.  
[mINI](https://github.com/metayeti/mINI) for ini reading.  
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
