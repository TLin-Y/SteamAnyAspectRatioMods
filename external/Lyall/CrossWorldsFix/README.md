# CrossWorldsFix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**CrossWorldsFix** is an ASI plugin for **Sonic Racing: CrossWorlds** that adds ultrawide/narrower support.

## Features

### Ultrawide/Narrower
- Support for ultrawide/narrower aspect ratios.
- Fix HUD issues (like offset markers).
- Span racing HUD.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the game's binaries folder. (e.g. "**steamapps\common\SonicRacingCrossWorlds\UNION\Binaries\Win64**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="winmm=n,b" %command%` to the launch options.

## Configuration
- Open **`CrossWorldsFix.ini`** to adjust settings.

## Screenshots
| ![crossworlds-21by9](.github/images/crossworlds_21by9.jpg) |
|:--------------------------:|
| 21:9 Gameplay |

## Credits
Special thanks to **Volf** for making this fix happen! <br />
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking. <br />
[Dumper-7](https://github.com/Encryqed/Dumper-7) for dumping the SDK.
