# Octopath0Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**Octopath0Fix** is an ASI plugin for **Octopath Traveler 0** that can add ultrawide/narrower support, skip the intro logos and more.

## Features
### General
- Skip intro logos.
- Enable developer console.

### Ultrawide/Narrower
- Support for any resolution/aspect ratio.
- Scaled 16:9 HUD.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the game folder. (e.g. "**steamapps\common\Octopath_Traveler0**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="dsound=n,b" %command%` to the launch options.

## Configuration
- Open **`Octopath0Fix.ini`** to adjust settings.

## Known Issues
- On ultrawide/narrower displays, the dither mask that appears on the player when behind buildings/objects is displayed at the wrong aspect ratio.

## Screenshots
| ![octopath0_32by9_gameplay](.github/images/octopath0_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
Special thanks to **Revy** for making this fix happen! <br/>
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking. <br />
[Dumper-7](https://github.com/Encryqed/Dumper-7) for dumping the SDK.
