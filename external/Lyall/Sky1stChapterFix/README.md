# Sky1stChapterFix

[![Codeberg](https://img.shields.io/badge/Codeberg-Lyall-2185D0?style=for-the-badge&logo=codeberg&logoColor=white)](https://codeberg.org/Lyall) [![Patreon](https://img.shields.io/badge/Patreon-Support%20Lyall-F96854?style=for-the-badge&logo=patreon&logoColor=white)](https://www.patreon.com/Wintermance) [![Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20Lyall-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/W7W01UAI9)

**Sky1stChapterFix** is an ASI plugin for **Trails in the Sky: 1st Chapter** that expands ultrawide/narrower support, disables letterboxing and more.

## Features
### General
- Disable letterboxing in cutscenes/events.
- Disable character dithering when the camera gets close.

### Ultrawide/Narrower
- Support for any resolution/aspect ratio.
- Corrected FOV at <16:9.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the game folder. (e.g. "**steamapps\common\Trails in the Sky 1st Chapter**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="winmm=n,b" %command%` to the launch options.

## Configuration
- Open **`Sky1stChapterFix.ini`** to adjust settings.

## Screenshots
| ![sky1st_32by9_gameplay](.github/images/sky1st_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
Special thanks to **XenoVars** for making this fix happen! <br />
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
