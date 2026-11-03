# Nioh2Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**Nioh2Fix** is an ASI plugin for **Nioh 2** that can add custom resolutions, expand ultrawide support and more.

## Features
### General
- Skip intro logos.
- Custom resolution support.
- Adjust LOD distance.
- Unlock cutscene framerate.

### Ultrawide
- Fix various HUD issues.
- Center HUD to 16:9.
- Disable pillarboxing/letterboxing in cutscenes.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip into the game folder. (e.g. "**steamapps\common\Nioh2**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="version=n,b" %command%` to the launch options.

## Configuration
- Open **`Nioh2Fix.ini`** to adjust settings.

## Screenshots
| ![Nioh2_32by9_gameplay](.github/images/Nioh2_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Known Issues
- At aspect ratios wider than 21:9, the yellow vignette when in the dark realm does not span the screen.

## Credits
Thanks to **Java-Love-Bird** for making this fix happen! <br />
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.

