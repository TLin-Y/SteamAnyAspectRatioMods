# Hatsune Miku: Project DIVA Mega Mix+ Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is an ASI plugin for Hatsune Miku: Project DIVA Mega Mix+ that adds ultrawide/narrower support.

## Features

- Support for ultrawide/narrower resolutions.
- Centered 16:9 HUD with the option to stretch.

## Installation
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the the game folder.
e.g. ("**steamapps\common\Hatsune Miku Project DIVA Mega Mix Plus**" for Steam).

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="winmm=n,b" %command%` to the launch options.

## Configuration
- See **MegaMixPlusFix.ini** to adjust settings.

## Screenshots

| ![animated-comparison](.github/images/405759564-427e3467-a305-412a-a34f-34dc5d9ea8db.png) |
|:--------------------------:|
| Gameplay |

## Known Issues
Please report any issues you see.

- Some backgrounds are offset to the left.

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
