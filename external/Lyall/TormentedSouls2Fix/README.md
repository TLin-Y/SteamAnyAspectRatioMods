# TormentedSouls2Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**TormentedSouls2Fix** is an ASI plugin for **Tormented Souls 2** that can skip intro logos, add ultrawide/narrower support and more.

## Features

### General
- Skip intro logos.
- Enable developer console.

### Ultrawide/Narrower
- Support for ultrawide/narrower aspect ratios.
- Fix several HUD issues.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the game folder. (e.g. "**steamapps\common\Tormented Souls 2**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="dsound=n,b" %command%` to the launch options.

## Configuration
- Open **`TormentedSouls2Fix.ini`** to adjust settings.

## Screenshots
| ![TormentedSouls2-32by9](.github/images/TormentedSouls2_32by9.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking. <br />
[Dumper-7](https://github.com/Encryqed/Dumper-7) for dumping the SDK.
