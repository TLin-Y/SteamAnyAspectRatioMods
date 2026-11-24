# SHfFix

[![Codeberg](https://img.shields.io/badge/Codeberg-Lyall-2185D0?style=for-the-badge&logo=codeberg&logoColor=white)](https://codeberg.org/Lyall) [![Patreon](https://img.shields.io/badge/Patreon-Support%20Lyall-F96854?style=for-the-badge&logo=patreon&logoColor=white)](https://www.patreon.com/Wintermance) [![Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20Lyall-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/W7W01UAI9)

**SHfFix** is an ASI plugin for **Silent Hill f** that can remove pillarboxing/letterboxing in cutscenes, uncap cutscene framerate and more.

## Features
### General
- Enable the console.
- Skip intro logos and warnings.
- Remove 30fps cap in cutscenes.

### Ultrawide/Narrower
- Disable letterboxing/pillarboxing in cutscenes.
- Add support for narrower than 16:10 resolutions.
- Fix cropped FOV.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the game folder. (e.g. "**steamapps\common\SILENT HILL f\SHf\Binaries\Win64**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="dsound=n,b" %command%` to the launch options.

## Configuration
- Open **`SHfFix.ini`** to adjust settings.

<details>
<summary><strong>Can't open the console?</strong></summary>

If you **cannot open the console** (after enabling it) with the default <kbd>~</kbd> (tilde) key, you can **rebind it** by adding the following to **`%LOCALAPPDATA%\SHf\Saved\Config\Windows\Input.ini`**:

```ini
[/Script/Engine.InputSettings]
ConsoleKeys = F1
```
</details>

## Screenshots
| ![SHf_32by9_cutscene](.github/images/SHf_32by9_cutscene.jpg) |
|:--------------------------:|
| 32:9 Cutscene |

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
