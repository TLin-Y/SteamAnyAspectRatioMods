# MGSDeltaFix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**MGSDeltaFix** is an ASI plugin for **Metal Gear Solid Δ: Snake Eater** that adds ultrawide/narrower support, skips intro logos and more.

## Features

### General
- Skip intro logos.
- Enable developer console.
- Adjustable framerate cap with softlock fixes beyond 60fps (experimental).
- Fix upscaling quality levels.
- Automatic Nvidia shader cache rebuilding on game updates

### Ultrawide/Narrower
- Expand support for ultrawide/narrower resolutions.
- Expand limits on adjusting in-game HUD size.
- Disable cutscene letterboxing/pillarboxing.

### Logging / Warnings for Common Configuration Issues
- Warnings for outdated versions of Windows & GPU drivers, which can cause black screen on startup issues.
- Warnings for old versions of framegen .dll's, which can cause purple & black graphical corruption.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the game folder. (e.g. "**steamapps\common\MGSDelta\MGSDelta\Binaries\Win64**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="dsound=n,b" %command%` to the launch options.

## Configuration
- Open **`MGSDeltaFix.ini`** to adjust settings.

<details>
<summary><strong>Can't open the console?</strong></summary>

If you **cannot open the console** (after enabling it) with the default <kbd>~</kbd> (tilde) key, you can **rebind it** by adding the following to **`%LOCALAPPDATA%\MGSDelta\Saved\Config\Windows\Input.ini`**:

```ini
[/Script/Engine.InputSettings]
ConsoleKeys = F1
```
</details>

## Screenshots
| ![MGSDelta-32by9_gameplay](.github/images/MGSDelta_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Known Issues
- Setting the framerate cap beyond 60fps can cause issues.

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
