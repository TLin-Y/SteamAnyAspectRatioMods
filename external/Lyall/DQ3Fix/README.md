# Dragon Quest III HD-2D Remake Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is an ASI plugin for Dragon Quest III HD-2D Remake that adds ultrawide/narrower support, uncaps framerate and much more.

## Features
### General
- Skip intro logos.
- Disable 60FPS cap.
- Enable developer console.
- Apply a list of console commands on level load.
- Load loose files for modding.

### Ultrawide/narrower
- Support for any aspect ratio.
- Centred 16:9 HUD with option to span some elements.
- Correct FOV at any aspect ratio.

## Installation
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the the game folder.
e.g. ("**steamapps\common\DRAGON QUEST III HD-2D Remake**" for Steam).

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="dsound=n,b" %command%` to the launch options.

## Configuration
- See **DQ3Fix.ini** to adjust settings.

## Screenshots

| ![animated-comparison](.github/images/dq3hd_comparison.png) |
|:--------------------------:|
| Gameplay |

## Known Issues
Please report any issues you see.

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.<br />
[Dumper-7](https://github.com/Encryqed/Dumper-7) for dumping the SDK.
