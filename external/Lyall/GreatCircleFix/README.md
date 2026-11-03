# Indiana Jones and the Great Circle Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is an ASI plugin for Indiana Jones and the Great Circle that fixes cutscene FOV issues when using an ultrawide display and more.

## Features
### General
- Unrestrict available console commands.
- Allow changing read-only console commands.
- Skip intro videos. 

### Ultrawide
- Fix vert- FOV in cutscenes when using the "Fullscreen" picture framing option.
- Fix culling issues at wider aspect ratios.

### Framerate
- Allow frame generation in cutscenes.
- Unlock cutscene framerate.

## Installation
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the the game folder.
e.g. ("**XboxGames\Indiana Jones and the Great Circle\Content**" for Xbox or "**steamapps\common\The Great Circle**" for Steam).

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the launch options.

## Configuration
- See **GreatCircleFix.ini** to adjust settings.

## Screenshots

| ![animated-comparison](.github/images/animated_comparison.png) |
|:--------------------------:|
| Main Menu |

## Known Issues
Please report any issues you see.

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
