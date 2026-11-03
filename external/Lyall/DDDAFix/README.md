# Dragon's Dogma: Dark Arisen Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is a fix for Dragon's Dogma: Dark Arisen that attempts to solve various issues related to non-16:9 display support and more.<br />

## Ultrawide/Narrower Features
- Fixed broken depth of field.
- 16:9 centred HUD.
- Fixed many broken/misaligned HUD elements.
- Corrected mouse input for interacting with UI.
- Fixed cropped cutscene FOV.
- Spanned backgrounds for various HUD elements like fades etc.
- Correctly scaled FMVs.

## Other Features
- Option to uncap 150fps "variable" framerate cap.
- Borderless windowed mode.
- Option to disable pausing when game is alt+tabbed.

## Installation
- Grab the latest release of DDDAFix from [here.](../../../releases)
- Extract the contents of the release zip in to the the game folder. (e.g. "**steamapps\common\DDDA**" for Steam).

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="winmm=n,b" %command%` to the launch options.

## Configuration
- See **DDDAFix.ini** to adjust settings for the fix.

## Known Issues
Please report any issues you see.
This list will contain bugs which may or may not be fixed.
- Loaded map area is limited to 16:9.

## Credits
Thanks to [@p1xel8ted](https://github.com/p1xel8ted) for testing! <br />
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
