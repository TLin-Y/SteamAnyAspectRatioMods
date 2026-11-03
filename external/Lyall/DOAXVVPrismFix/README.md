# DOAXVVPrismFix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**DOAXVVPrismFix** is an ASI plugin for **Venus Vacation PRISM - DEAD OR ALIVE Xtreme** that can add custom resolutions, ultrawide/narrower support and more.

## Features
### General
- Custom resolution support.
- Support for ultrawide/narrower aspect ratios.
- Raise 60fps cap to 120fps.

### HUD
- Scaled 16:9 HUD.
- Disable dialog box background.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the game folder.

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Add the launch argument `WINEDLLOVERRIDES="winmm=n,b" %command%`.

## Configuration
- Open **`DOAXVVPrismFix.ini`** to adjust settings.

## Screenshots
| ![DOAXVVPrism_32by9_gameplay](.github/images/DOAXVVPrism_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
Thanks to **XenoVars** for making this fix happen! <br />
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
