# NoSleepForKanameDateFix
[![Patreon-Button](.forgejo/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.forgejo/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is a BepInEx plugin for **No Sleep For Kaname Date - From AI: THE SOMNIUM FILES** that can add ultrawide/narrower support, disable cutscene letterboxing and more.

## Features

### General
- Custom resolution support.
- Adjust fixed delta time to improve camera stutter.
- Disable cutscene letterboxing.

### Ultrawide/Narrower
- Support for any aspect ratio.
- Fix FOV cropping in cutscenes.
- Fix many HUD issues at ultrawide/narrower aspect ratios.

## Installation
- Grab the latest release of NoSleepForKanameDateFix from [here.](../../../releases)
- Extract the contents of the release zip into the game folder. (e.g. "**steamapps\common\NoSleepForKanameDate**" for Steam).
- 🚩First boot of the game may take a few minutes as BepInEx generates an assembly cache!

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="winhttp=n,b" %command%` to the launch options.

## Configuration
- See **`GameFolder`\BepInEx\config\NoSleepForKanameDateFix.cfg** after first launch to adjust settings for the fix.

## Screenshots
| ![NoSleepForKanameDate_32by9_gameplay](.forgejo/images/NoSleepForKanameDate_32by9_cutscene.jpg) |
|:--------------------------:|
| 32:9 Cutscene |

## Credits
Special thanks to **XenoVars** for making this fix possible!  
[BepinEx](https://github.com/BepInEx/BepInEx) for plugin loading.
