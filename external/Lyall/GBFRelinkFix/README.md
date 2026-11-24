# Granblue Fantasy: Relink Fix

[![Codeberg](https://img.shields.io/badge/Codeberg-Lyall-2185D0?style=for-the-badge&logo=codeberg&logoColor=white)](https://codeberg.org/Lyall) [![Patreon](https://img.shields.io/badge/Patreon-Support%20Lyall-F96854?style=for-the-badge&logo=patreon&logoColor=white)](https://www.patreon.com/Wintermance) [![Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20Lyall-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/W7W01UAI9)

This is an ASI plugin that adds custom resolutions, ultrawide/narrower support and more to Granblue Fantasy: Relink.

## Features
- Custom resolution/ultrawide support.
- Correct FOV and aspect ratio at any resolution.
- Gameplay FOV/camera distance modification.
- Scaled HUD to 16:9.
- Adjustable spanned gameplay HUD.
- Spanned HUD backgrounds to fill screen (loading fades etc).
- Graphical tweaks (Shadow Quality/LOD Distance/Disable TAA).
- Experimental option to remove the framerate limit. (Check ini to enable)

## Installation
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip into the game folder. (e.g. "**steamapps\common\Granblue Fantasy Relink**" for Steam).

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="winmm=n,b" %command%` to the launch options.

## Configuration
- See **scripts\GBFRelinkFix.ini** to adjust settings for the fix.

## Screenshots
| ![GBFRelink_32by9_gameplay](.forgejo/images/GBFRelink_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
Many thanks to **VagrantZero** on the WSGF Discord for providing a copy of the game!  
Also thanks to **KingKrouch** and **nikos** on the WSGF Discord for doing valuable testing.

[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading.  
[mINI](https://github.com/metayeti/mINI) for ini reading.  
[safetyhook](https://github.com/cursey/safetyhook) for hooking.  
