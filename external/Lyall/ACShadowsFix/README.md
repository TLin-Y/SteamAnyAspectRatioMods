# ACShadowsFix
[![Patreon-Button](.forgejo/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.forgejo/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**ACShadowsFix** is an ASI plugin for Assassin's Creed Shadows that can:
- Skip intro videos.
- Extend in-game FOV slider.
- Center the gameplay camera.
- Disable cutscene pillarboxing.
- Adjust cloth physics framerate.
- Allow cutscene frame generation.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip into the game folder.  

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open the game properties in Steam and add `WINEDLLOVERRIDES="winmm=n,b" %command%` to the launch options.  

## Configuration
- Open **`ACShadowsFix.ini`** to adjust settings.

## SpecialK
- [SpecialK](https://www.special-k.info/) - The latest builds of SpecialK include some adapted features from ACShadowsFix. If you run into any issues with this fix, then it may be worth trying SpecialK instead.

## Screenshots
| ![cutscene-comparison](.forgejo/images/acshadows_comparison.png) |
|:--:|
| Cutscene |

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading.  
[mINI](https://github.com/metayeti/mINI) for ini reading.  
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
