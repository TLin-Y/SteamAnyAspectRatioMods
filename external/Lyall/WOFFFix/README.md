# World of Final Fantasy Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)


This is a fix for World of Final Fantasy that adds support for custom resolutions, ultrawide displays and more.

## Features
- Custom resolution support.
- Ultrawide/narrow aspect ratio support.
- Correct FOV at any aspect ratio.
- Centred 16:9 HUD.
- Borderless fullscreen.
- Hide mouse cursor.
- (EXPERIMENTAL) Remove 30fps cap and allow dynamic game speed.

## Installation
- Grab the latest release of WOFFFix from [here.](../../../releases)
- Extract the contents of the release zip in to the the game folder.<br />(e.g. "**steamapps\common\World of Final Fantasy**" for Steam).

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- If using the Steam Deck, I recommend disabling the "Fix HUD" option in **WOFFFix.ini** as the HUD will only be slightly stretched vertically.
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the launch options.

## Configuration
- See **WOFFFix.ini** to adjust settings for the fix.

## Known Issues
Please report any issues you see.
This list will contain bugs which may or may not be fixed.

#### Framerate Cap Issues
- Menu interactions/inputs are sped up.
- Cutscenes are still animated at 30fps (not sure if this can be fixed).

#### HUD Issues
- When using the HUD fix, you may see uncovered UI at the top/bottom/sides. This is most easily seen in certain menus and with the minimap.

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
