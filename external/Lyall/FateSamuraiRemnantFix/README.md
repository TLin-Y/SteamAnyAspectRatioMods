# Fate/Samurai Remnant Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is an ASI plugin for Fate/Samurai Remnant that adds support for custom resolutions and ultrawide displays.

## Features

- Custom resolutions.
- Ultrawide support with 16:9 HUD.

## Installation
- Download the latest [release](../../../releases).
- Extract the contents of the release zip in to the the game folder.
e.g. ("**steamapps\common\FateSamuraiRemnant**" for Steam).

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the launch options.

## Configuration
- See **FateSamuraiRemnantFix.ini** to adjust settings.

## Screenshots

| ![gameplay-comparison](.github/images/411059714-a2d5d107-a170-45c7-9408-26f8f3bf70aa.png) |
|:--------------------------:|
| Gameplay |

| ![cutscene-comparison](.github/images/411061088-689d369d-4a74-4cd7-b003-22284dd0d418.png) |
|:--------------------------:|
| Cutscene |

## Known Issues
Please report any issues you see.

## Credits
Special thanks to Hotiraripha & Tater-Tatsumi for providing a copy of the game and commissioning the fix! <br/>
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
