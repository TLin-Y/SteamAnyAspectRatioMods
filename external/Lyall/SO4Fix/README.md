# Star Ocean - The Last Hope Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is a fix that adds custom resolutions, ultrawide support and more to STAR OCEAN - THE LAST HOPE - 4K & Full HD Remaster.

## Features
- Custom resolution support.
- Borderless fullscreen.
- Ultrawide/narrow aspect ratio support.
- Correct FOV at any aspect ratio.
- Fixes many HUD issues at non-16:9 aspect ratios.
- Skip intro logos.
- Fixes bug with 4x shadow buffer option.
- Increase shadow draw distance.

## Installation
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the the game folder. e.g. ("**steamapps\common\STAR OCEAN - THE LAST HOPE - 4K & Full HD Remaster**" for Steam).

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="winmm=n,b" %command%` to the launch options.

## Configuration
- See **SO4Fix.ini** to adjust settings for the fix.

## Known Issues
Please report any issues you see.
This list will contain bugs which may or may not be fixed.

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
