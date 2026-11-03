# Prince of Persia: The Lost Crown Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is a BepInEx plugin for Prince of Persia: The Lost Crown that adds custom resolutions, ultrawide/narrower support and more.

## Features
- Custom resolution support.
- Ultrawide and narrower aspect ratio support.
- Intro skip.

## Installation
- Grab the latest release of PoPTLCFix from [here.](../../../releases)
- Extract the contents of the release zip in to the the game folder. (e.g. "**steamapps\common\Prince of Persia The Lost Crown**" for Steam).
- 🚩First boot of the game may take a few minutes as BepInEx generates an assembly cache!

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="winhttp=n,b" %command%` to the launch options.

## Configuration
- See **`GameFolder`\BepInEx\config\PoPTLCFix.cfg** after launching to adjust settings for the fix.

## Known Issues
Please report any issues you see.
This list will contain bugs which may or may not be fixed.

- Video cinematics cause the game to change resolution.
- Map markers appear outside the 16:9 boundary.

## Screenshots

| ![animated-comparison](.github/images/poptlc_comparison.png) |
|:--:|
| Gameplay |

## Credits
[p1xel8ted](https://github.com/p1xel8ted) for previous work on an ultrawide solution for this game. <br />
[BepinEx](https://github.com/BepInEx/BepInEx) for plugin loading.
