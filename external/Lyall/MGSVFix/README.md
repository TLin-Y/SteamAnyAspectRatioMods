# MGSVFix
[![Patreon-Button](.forgejo/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.forgejo/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**MGSVFix** is an ASI plugin for *Metal Gear Solid V: The Phantom Pain* (and *Ground Zeroes*!) that can:
- Skip intro logos and autosave dialog.
- Unlock framerate.
- Unlock resolution options/support.
- Fix HUD issues at ultrawide resolutions.
- Fix graphical effects at ultrawide resolutions.
- Tweak LOD distances.

For more details on exactly what is fixed, click [here](./fixes.md).

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip into the game folder.  

### Steam Deck/Linux Additional Instructions
🚩**Windows users can skip this step!**  
- Open the game properties in Steam and add `WINEDLLOVERRIDES="winmm=n,b" %command%` to the launch options.  

## Configuration
- Open **`MGSVFix.ini`** to adjust settings.

## Known Issues
#### Ultrawide
- Certain lens effects like dirt are displayed at 16:9.

## Screenshots
| ![animated-comparison](.forgejo/images/mgsv_comparison.png) |
|:--:|
| Gameplay |

## Credits
Thanks to Hotiraripha for commissioning this fix!  
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading.  
[mINI](https://github.com/metayeti/mINI) for ini reading.  
[safetyhook](https://github.com/cursey/safetyhook) for hooking.  
