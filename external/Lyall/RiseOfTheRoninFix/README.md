# RiseOfTheRoninFix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**RiseOfTheRoninFix** is an ASI plugin for Rise of the Ronin that can:
- Add support for custom resolutions.
- Adjust gameplay FOV.
- Fix cropped FMV sequences.
- Fix various issues related to running at unsupported aspect ratios.

## Installation  
- Download the latest [release](../../../releases).
- Extract the contents of the release zip in to the the game folder.  

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open the game properties in Steam and add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the launch options.  

## Configuration
- Open **`RiseOfTheRoninFix.ini`** to adjust settings.

## Screenshots
| ![fmv-comparison](.github/images/rotr_fmvcomparison.png) |
|:--:|
| Fixed FMV Cropping |

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
