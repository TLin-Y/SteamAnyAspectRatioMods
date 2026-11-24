# DOA6LRFix

[![Codeberg](https://img.shields.io/badge/Codeberg-Lyall-2185D0?style=for-the-badge&logo=codeberg&logoColor=white)](https://codeberg.org/Lyall) [![Patreon](https://img.shields.io/badge/Patreon-Support%20Lyall-F96854?style=for-the-badge&logo=patreon&logoColor=white)](https://www.patreon.com/Wintermance) [![Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20Lyall-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/W7W01UAI9)

**DOA6LRFix** is an ASI plugin for **Dead or Alive 6 Last Round** that adds ultrawide/narrower support.

## Features
### General
- Apply custom resolutions.
- Enable DLSS frame generation.

### Ultrawide/Narrower
- Support for any aspect ratio.
- Scaled 16:9 HUD.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip into the game folder. (e.g. "**steamapps\common\Dead or Alive 6 Last Round**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="winmm=n,b" %command%` to the launch options.

## Configuration
- Open **`DOA6LRFix.ini`** to adjust settings.

## Screenshots
| ![DOA6LR_32by9_gameplay](.forgejo/images/DOA6LR_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
Special thanks to **Volf** for testing!  
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading.  
[mINI](https://github.com/metayeti/mINI) for ini reading.  
[safetyhook](https://github.com/cursey/safetyhook) for hooking.