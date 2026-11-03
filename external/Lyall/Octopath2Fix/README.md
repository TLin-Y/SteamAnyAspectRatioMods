# Octopath Traveler 2 Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is a fix for ultrawide/narrower displays in Octopath Traveler 2.

## Features
- Remove pillarboxing/letterboxing.
- Correct FOV at any resolution.
- 16:9 centered HUD.
- Disable 120FPS framerate cap.
- Intro logo skip.

## Installation
- Grab the latest release of Octopath2Fix from [here.](../../../releases)
- Extract the contents of the release zip in to the the correct binaries folder. e.g. ("**steamapps\common\Octopath_Traveler2\Octopath_Traveler2\Binaries\Win64**" for Steam).

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="dsound=n,b" %command%` to the launch options.

## Configuration
- See **Octopath2Fix.ini** to adjust settings for the fix.

## Known Issues
Please report any issues you see.
This list will contain bugs which may or may not be fixed.

- "Porthole" view through buildings is displayed at at the wrong aspect ratio.

## Screenshots
| ![animated-comparison](.github/images/octopath2_comparison.gif) |
|:--:|
| Disabled pillarboxing. |

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
