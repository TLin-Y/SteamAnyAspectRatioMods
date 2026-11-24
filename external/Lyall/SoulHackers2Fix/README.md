# Soul Hackers 2 Fix

[![Codeberg](https://img.shields.io/badge/Codeberg-Lyall-2185D0?style=for-the-badge&logo=codeberg&logoColor=white)](https://codeberg.org/Lyall) [![Patreon](https://img.shields.io/badge/Patreon-Support%20Lyall-F96854?style=for-the-badge&logo=patreon&logoColor=white)](https://www.patreon.com/Wintermance) [![Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20Lyall-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/W7W01UAI9)

This BepInEx plugin for the game Soul Hackers 2 features:
- Proper ultrawide and non-16:9 aspect ratio support with pillarbox removal.
- Custom resolution support.
- Graphical tweaks to increase fidelity.
  - Disable Chromatic Aberration
  - Anisotropic Filtering
  - LOD Bias
  - Render Scale (Resolution Scale)
- Fix for slidey character movement above 60fps.

## Installation
- Grab the latest release of SoulHackers2Fix from [here.](../../../releases)
- Extract the contents of the release zip in to the game directory.<br />(e.g. "**steamapps\common\SOUL HACKERS2**" for Steam).
- Run the game once to generate a config file located at **<GameDirectory>\BepInEx\config\SoulHackers2Fix.cfg**
- The first launch may take a little while as BepInEx does its magic.

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="winhttp=n,b" %command%` to the launch options.

## Configuration
- See **`GameFolder`\BepInEx\config\SoulHackers2Fix.cfg** after first launch to adjust settings for the fix.

## Known Issues
Please report any issues you see.
- Fullscreen exclusive for custom resolutions does not seem to work properly at the moment.

## Screenshots
| ![sh2_animated_comparison](.github/images/sh2_animatedcomparison.gif) |
|:--:|
| Ultrawide pillarbox removal. | 

![CA On](.github/images/sh2_ca_on.jpg)|  ![CA Off](.github/images/sh2_ca_off.jpg)  
:-------------------------:|:-------------------------:
Chromatic Aberration On |  Chromatic Aberration Off

## Credits
- [BepinEx](https://github.com/BepInEx/BepInEx) is licensed under the GNU Lesser General Public License v2.1.
