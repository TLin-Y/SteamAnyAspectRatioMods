# AI: THE SOMNIUM FILES - nirvanA Initiative Fix

[![Codeberg](https://img.shields.io/badge/Codeberg-Lyall-2185D0?style=for-the-badge&logo=codeberg&logoColor=white)](https://codeberg.org/Lyall) [![Patreon](https://img.shields.io/badge/Patreon-Support%20Lyall-F96854?style=for-the-badge&logo=patreon&logoColor=white)](https://www.patreon.com/Wintermance) [![Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20Lyall-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/W7W01UAI9)

This BepInEx plugin for the game **AI: THE SOMNIUM FILES - nirvanA Initiative** adds support for:
- Playing the game in any ultrawide aspect ratio such as 21:9, 32:9 or even higher.
- Fixing various UI issues caused by running at unsupported resolutions.
- Setting an arbitrary resolution.

## Installation
- Grab the [latest release of AISomniumFiles2Fix.](../../../releases)
- Extract the contents of the **AISomniumFiles2Fix** zip to the **game directory**.
- Run the game. It may take a little while on the first boot as BepInEx does it's magic.

### Steam Deck/Linux Additional Instructions
**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="winhttp=n,b" %command%` to the launch options.

## Configuration
- See the generated config file **`GameFolder`\BepInEx\config\AISomniumFiles2Fix.cfg** to adjust various aspects of the fix.

## Screenshots
![ai2_animated_comparison](.github/images/ai2_animated_comparison.gif)

## Credits
[BepinEx](https://github.com/BepInEx/BepInEx) for plugin loading.