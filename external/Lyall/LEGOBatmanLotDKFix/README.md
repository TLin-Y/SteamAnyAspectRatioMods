# LEGOBatmanLotDKFix

[![Codeberg](https://img.shields.io/badge/Codeberg-Lyall-2185D0?style=for-the-badge&logo=codeberg&logoColor=white)](https://codeberg.org/Lyall) [![Patreon](https://img.shields.io/badge/Patreon-Support%20Lyall-F96854?style=for-the-badge&logo=patreon&logoColor=white)](https://www.patreon.com/Wintermance) [![Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20Lyall-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/W7W01UAI9)

**LEGOBatmanLotDKFix** is an ASI plugin for **LEGO Batman: Legacy of the Dark Knight** that can disable pillarboxing/letterboxing and enable the console. 

## Features
### General
- Enable developer console.

### Ultrawide/Narrower
- Unlock aspect ratio in gameplay.
- Unlock aspect ratio in cutscenes.
- Correct FOV at <16:9.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip into the game folder. (e.g. "**steamapps\common\LEGO Batman - Legacy of the Dark Knight\LEGOBatmanLotDK\Binaries\Win64**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**Windows users can skip this step!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="dsound=n,b" %command%` to the launch options.

## Configuration
- Open **`LEGOBatmanLotDKFix.ini`** to adjust settings.

## Screenshots
| ![LEGOBatmanLotDK_32by9_cutscene](.forgejo/images/LEGOBatmanLotDK_32by9_cutscene.jpg) |
|:--------------------------:|
| 32:9 Cutscene |

## Credits
Special thanks to **Volf** for making this fix happen!  
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading.  
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading.  
[spdlog](https://github.com/gabime/spdlog) for logging.  
[safetyhook](https://github.com/cursey/safetyhook) for hooking.