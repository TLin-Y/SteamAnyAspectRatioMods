# Final Fantasy XVI Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is a fix for Final Fantasy XVI that adds ultrawide/narrower support, uncaps framerate in cutscenes, lets you adjust gameplay FOV and much more.

## Features
### General
- Adjust gameplay FOV, camera distance and camera horizontal position.
- JXL screenshot quality option and fixes hitching while taking screenshots.
- Allow the use of motion blur + frame generation.
- Disable depth of field.
- Enable background audio.
- Lock cursor to game window.
- Resize game window.
- Disable display sleep/screensaver while the game is running.

### Performance
- Disable 30FPS cap in cutscenes/photo mode or set your own framerate limit.
- Allow frame generation in cutscenes.
- Disable graphics debugger checks.
- Adjust upper and lower bounds of dynamic resolution.
- Adjust level of detail distance.
 
### Ultrawide/Narrower
- Remove pillarboxing/letterboxing.
- Fixed HUD scaling with configurable HUD size.
- Fixed FOV scaling at <16:9.

## Installation
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the the game folder. e.g. ("**steamapps\common\FINAL FANTASY XVI**" for Steam).

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the launch options.

## Configuration
- See **FFXVIFix.ini** to adjust settings for the fix.

## Known Issues
Please report any issues you see.
This list will contain bugs which may or may not be fixed.

## Screenshots
| ![animated-comparison](.github/images/ffxvi_comparison.png) |
|:--:|
| Gameplay |

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
