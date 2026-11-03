# DBXV2Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**DBXV2Fix** is an ASI plugin for **DRAGON BALL XENOVERSE 2** that adds ultrawide/narrower support and more.

## Features

## General
- Support for any resolution/aspect ratio.
- Adjustable gameplay FOV.

### Ultrawide/Narrower
- Scaled 16:9 HUD.
- Corrected FOV at <16:9.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the game folder. (e.g. "**steamapps\common\DB Xenoverse 2**" for Steam)

## Anti-Cheat
- You'll need to launch the game without EAC to allow the fix to load. See the following instructions to launch the game without EAC.

### Windows Instructions
- Open the game properties in Steam and add `"FULL_PATH_TO_GAME_FOLDER\bin\DBXV2.exe" %command%` to the launch options. 
- Replace `FULL_PATH_TO_GAME_FOLDER` with the absolute path to the game folder. (i.e. `D:\Steam\steamapps\common\DB Xenoverse 2`)

### Linux/Steam Deck Instructions
- Open the game properties in Steam and add `WINEDLLOVERRIDES="winmm=n,b" bash -c 'exec "${@/START.exe/bin\/DBXV2.exe}"' -- %command%` to the launch options.

## Configuration
- Open **`DBXV2Fix.ini`** to adjust settings.

## Known Issues
- Quick chat text and the character portrait are displayed at the wrong aspect ratio.
- Some tutorial icons are anchored to the edge of the screen.
- FOV multiplier also applies to the scanner FOV.

## Screenshots
| ![dbxv2_32by9_gameplay](.github/images/dbxv2_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
Special thanks to **hotiraripha** for making this fix happen! <br />
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
