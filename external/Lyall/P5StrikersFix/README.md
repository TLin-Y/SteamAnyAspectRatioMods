# Persona 5 Strikers Fix
[![Patreon-Button](.forgejo/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.forgejo/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is a fix that adds custom resolutions, ultrawide support and more to Persona 5 Strikers.

## Features

### General
- Custom resolution support.
- Skip intro logos.
- Gameplay FOV adjustment.
- Option to disable cutscene letterboxing.
- Shadow resolution setting.
- Ability to scale up resolution of UI render targets.
- Fix 8-way analog movement.

### Ultrawide/narrower
- Ultrawide/narrow aspect ratio support.
- Correct FOV at any aspect ratio.
- HUD is scaled to 16:9.

## Installation
- Grab the latest release of P5StrikersFix from [here.](../../../releases)
- Extract the contents of the release zip in to the the game folder. e.g. "**steamapps\common\P5S**" for Steam).

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the launch options.

## Configuration
- See **P5StrikersFix.ini** to adjust settings for the fix.

## Known Issues
Please report any issues you see.
This list will contain bugs which may or may not be fixed.

- Cutscene letterboxing looks bad at <16:9. I'd recommend disabling the cutscene letterboxing in **P5StrikersFix.ini**.

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
