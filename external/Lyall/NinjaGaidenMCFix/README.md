# NinjaGaidenMCFix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**NinjaGaidenMCFix** is an ASI plugin for **NINJA GAIDEN: Master Collection** that adds ultrawide/narrower support, disables anti-tamper protection and more.

## Features

### General
- Custom resolution support.
- Proper borderless fullscreen mode.
- Disables anti-tamper protection.

### Ultrawide/Narrower
- Ultrawide/narrower support.
- Centered 16:9 HUD.
- Fixed culling issues at wider aspect ratios.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the the game folder.

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="winmm=n,b" %command%` to the launch options.

## Configuration
- Open **`NinjaGaidenMCFix.ini`** to adjust settings.

## Screenshots
| ![ng1-animated-comparison](.github/images/ng1_animated_comparison.png) |
|:--------------------------:|
| [NG Sigma] Gameplay |

| ![ng2-animated-comparison](.github/images/ng2_animated_comparison.png) |
|:--------------------------:|
| [NG Sigma 2] Gameplay |

| ![ng3-animated-comparison](.github/images/ng3_animated_comparison.png) |
|:--------------------------:|
| [NG 3: Razor's Edge] Gameplay |

## Credits
Thanks to **Volf** for commissioning this fix! <br />
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
