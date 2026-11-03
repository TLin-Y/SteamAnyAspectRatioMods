# BDFFHDFix
[![Patreon-Button](.forgejo/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.forgejo/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is a BepInEx plugin for **BRAVELY DEFAULT FLYING FAIRY HD Remaster** that adds ultrawide/narrower support.

## Features

### Ultrawide/Narrower
- Unlocked resolution list.
- Support for any aspect ratio.
- Fixed HUD issues at unsupported aspect ratios.

## Installation
- Grab the latest release of BDFFHDFix from [here.](../../../releases)
- Extract the contents of the release zip into the game folder. (e.g. "**steamapps\common\BDFFHD**" for Steam).
- 🚩First boot of the game may take a few minutes as BepInEx generates an assembly cache!

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="version=n,b" %command%` to the launch options.

## Configuration
- See **`GameFolder`\BepInEx\config\BDFFHDFix.cfg** after first launch to adjust settings for the fix.

## Screenshots
| ![BDFFHD_32by9_gameplay](.forgejo/images/BDFFHD_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
[BepinEx](https://github.com/BepInEx/BepInEx) for plugin loading.
