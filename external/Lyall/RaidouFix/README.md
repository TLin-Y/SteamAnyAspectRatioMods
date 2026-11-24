# RAIDOU Remastered Fix

[![Codeberg](https://img.shields.io/badge/Codeberg-Lyall-2185D0?style=for-the-badge&logo=codeberg&logoColor=white)](https://codeberg.org/Lyall) [![Patreon](https://img.shields.io/badge/Patreon-Support%20Lyall-F96854?style=for-the-badge&logo=patreon&logoColor=white)](https://www.patreon.com/Wintermance) [![Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20Lyall-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/W7W01UAI9)

This is a BepInEx plugin for **RAIDOU Remastered: The Mystery of the Soulless Army** that adds ultrawide/narrower support.<br />

## Features
- Ultrawide/narrower aspect ratio support.
- Adjustable maximum render scale.

## Installation
- Grab the latest release of RaidouFix from [here.](../../../releases)
- Extract the contents of the release zip in to the the game folder. (e.g. "**steamapps\common\Raidou TMOTSA**" for Steam).
- 🚩First boot of the game may take a few minutes as BepInEx generates an assembly cache!

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="winhttp=n,b" %command%` to the launch options.

## Configuration
- See **`GameFolder`\BepInEx\config\RaidouFix.cfg** after first launch to adjust settings for the fix.

## Known Issues
### Ultrawide/narrower
- Certain HUD elements remain visible outside of a 16:9 boundary.

## Screenshots
| ![raidou-animated-comparison](.github/images/raidou_animated_comparison.png) |
|:--------------------------:|
| Gameplay |

## Credits
[BepinEx](https://github.com/BepInEx/BepInEx) for plugin loading.
