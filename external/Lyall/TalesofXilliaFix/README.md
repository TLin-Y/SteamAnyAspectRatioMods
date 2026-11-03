# Tales of Xillia Remastered Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is a BepInEx plugin for **Tales of Xillia Remastered** that can add ultrawide/narrower support, skip intro logos and more.

## Features

### General
- Skip intro logos/autosave warning.
- Adjust gameplay FOV.
- Adjust draw distance.

### Ultrawide/narrower
- Ultrawide/narrower aspect ratio support.
- Scaled 16:9 HUD.

## Installation
- Grab the latest release of TalesofXilliaFix from [here.](../../../releases)
- Extract the contents of the release zip in to the the game folder. (e.g. "**steamapps\common\Tales of Xillia Remastered**" for Steam).
- 🚩First boot of the game may take a few minutes as BepInEx generates an assembly cache!

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="winhttp=n,b" %command%` to the launch options.

## Configuration
- See **`GameFolder`\BepInEx\config\TalesofXilliaFix.cfg** after first launch to adjust settings for the fix.

## Screenshots
| ![tofx_32by9_gameplay](.github/images/tofx_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
Special thanks to **Persecom** and **XenoVars** for making this fix happen!<br />
[BepinEx](https://github.com/BepInEx/BepInEx) for plugin loading.
