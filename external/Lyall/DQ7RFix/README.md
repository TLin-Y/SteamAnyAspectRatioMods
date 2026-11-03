# DQ7RFix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**DQ7RFix** is an ASI plugin for **Dragon Quest VII Reimagined** that can add ultrawide/narrower support, enable the console and more.

## Features
### General
- Enable developer console.
- Load loose files for modding.
- Quick start (load autosave on startup).
- Adjust text speed.
- Custom resolution support.
- Apply cvars after game overrides.

### Ultrawide/Narrower
- Support for any aspect ratio.
- Scaled 16:9 HUD.
- Span field/battle HUD.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip into the shipping executable folder. (e.g. "**steamapps\common\DQ7R\DQ7R\Binaries\Win64**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="dsound=n,b" %command%` to the launch options.

## Configuration
- Open **`DQ7RFix.ini`** to adjust settings.

## Screenshots
| ![DQ7R_32by9_gameplay](.github/images/DQ7R_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking. <br />
[Dumper-7](https://github.com/Encryqed/Dumper-7) for dumping the SDK.
