# Atelier Yumia Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**AtelierYumiaFix** is an ASI plugin for Atelier Yumia: The Alchemist of Memories & the Envisioned Land that can:
- Skip intro logos.
- Add support for custom resolutions.
- Adjust gameplay/battle FOV.
- Fix HUD issues at ultrawide/narrower resolutions.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the the game folder.  

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open the game properties in Steam and add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the launch options.  

## Configuration
- Open **`AtelierYumiaFix.ini`** to adjust settings.

## Known Issues
**Ultrawide/Narrower**
- At wider than 21:9 aspect ratios, the crouch vignette does not span the screen fully.
- At wider than 32:9 aspect ratios, the minimap image is duplicated at the edge of the screen.
- Interacting with map markers can cause the cursor to snap position.

## Screenshots
| ![animated-comparison](.github/images/yumia_comparison.png) |
|:--:|
| Gameplay |

## Credits
Special thanks to RenaSera for commissioning this fix! <br/>
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
