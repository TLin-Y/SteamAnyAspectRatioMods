# Persona 3 Reload Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is a fix that adds custom resolutions, ultrawide support and more to Persona 3 Reload.<br />

## Features
- Custom resolution support.
- Ultrawide/narrow aspect ratio support.
- Correct FOV at any aspect ratio.
- Configurable intro skip.
- Enable developer console.
- Render scale override.
- Control over pausing when alt+tabbed.
- The ability to increase/decrease resolution of render texture targets (i.e menus, persona model previews).

## Installation
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the the game's binary folder.<br />(e.g. "**XboxGames\Persona 3 Reload\Content**" for Xbox/MS Store or "**steamapps\common\P3R**" for Steam).

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="dsound=n,b" %command%` to the launch options.

<details>
<summary>Installing P3RFix as a Reloaded II Mod</summary>
  
*This applies to both Windows and Steam Deck/Linux*

Before starting, make sure to **delete any P3RFix files** inside of the game's files **if you have already have used this fix** previously (*P3RFix.ini*, *P3RFix.asi*, *dsound.ini* and *dsound.dll*)

To make sure P3RFix loads alongside any Reloaded II mods you are using, follow these steps:

- Install the P3RFix Reloaded-II package.

- Drag and drop `P3RFix_Reloaded-II.zip` onto the Reloaded-II window. (Alternatively: [Manual Install](https://reloaded-project.github.io/Reloaded-II/QuickStart/))

- Enable it in your `Reloaded-II` mod list.
- You should now be able to start the game and see both P3RFix and Reloaded II mods working.

</details>

## Configuration
- See **P3RFix.ini** to adjust settings for the fix.

## Screenshots
| ![p3r_comparison](.github/images/p3r_comparison.gif) |
|:--------------------------:|
| Gameplay |

## Known Issues
Please report any issues you see.
This list will contain bugs which may or may not be fixed.

- Some screen fades/transitions may not span the screen at non-16:9 resolutions.
- If you skip intro to the load save menu, then back out, you will softlock the game.
- Disabling pause on focus loss while using m/kbd can cause issues with mouse capture. Enter a menu first to release the cursor.

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
