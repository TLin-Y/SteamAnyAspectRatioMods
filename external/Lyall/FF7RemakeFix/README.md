# Final Fantasy VII Remake Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is an ASI plugin for Final Fantasy VII Remake that adds ultrawide/narrower support and more.

## Features
### General
- Support for custom resolutions.
- Adjut subtitle size.
- Disable/adjust vignette.
- Increase camera distance limits.

### Ultrawide/Narrower
- Support any aspect ratio with correct FOV.
- Centered 16:9 HUD.

## Installation
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the the game folder.
(e.g. "**steamapps\common\FINAL FANTASY VII Remake**" for Steam).

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="dsound=n,b" %command%` to the launch options.

## Configuration
- See **FF7RemakeFix.ini** to adjust settings.

## Known Issues
Please report any issues you see. This list will contain bugs which may or may not be fixed.

- Combat simulator transition animation is displayed at the wrong aspect ratio.
- Certain in-game displays are shown at the wrong aspect ratio.

## Recommended Mods
- [FFVIIHook](https://www.nexusmods.com/finalfantasy7remake/mods/74) by [emoose](https://github.com/emoose) - Unlocks the console, allows overriding certain cvars and more.

## Screenshots
| ![animated-comparison](.github/images/416031941-11eb03e6-34e1-47d3-87c7-c555f10e929f.png) |
|:--------------------------:|
| Gameplay |

## Credits
[Flawless Widescreen](http://www.flawlesswidescreen.org/) for the green glow bug fix. You can support the creator of that software [here](https://ko-fi.com/flawlesswidescreen).<br />
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.<br />
[Dumper-7](https://github.com/Encryqed/Dumper-7) for dumping the SDK.
