# Eiyuden Chronicle: Hundred Heroes Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is a BepInEx plugin for Eiyuden Chronicle: Hundred Heroes that adds custom resolutions, ultrawide/narrower support and more.<br />

## Features
- Custom resolution support.
- Ultrawide and narrower aspect ratio support.
- Intro skip.
- Hide mouse cursor.
- Graphical tweaks like the ability to change render scale, shadow resolution/distance and vignette/CA toggles.
- Dialog tweaks (removing delay on voice lines, text speed etc).
- Battle tweaks (auto-battle and manual battle speed options).
- Force specific controller button icons (DS4/DS5/Xbox).

## Installation
- Grab the latest release of HundredHeroesFix from [here.](../../../releases)
- Extract the contents of the release zip in to the the game folder. (e.g. "**steamapps\common\Eiyuden Chronicle**" for Steam).
- 🚩First boot of the game may take a few minutes as BepInEx generates an assembly cache!

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="winhttp=n,b" %command%` to the launch options.

## Configuration
- See **`GameFolder`\BepInEx\config\HundredHeroesFix.cfg** after launch to adjust settings for the fix.

## Known Issues
Please report any issues you see.
This list will contain bugs which may or may not be fixed.

- When using an ultrawide display, the HUD is a mixture of spanned and 16:9.
- Some screens may be mis-aligned or zoomed in when using an ultrawide/narrower display.

## Credits
[BepinEx](https://github.com/BepInEx/BepInEx) for plugin loading.
