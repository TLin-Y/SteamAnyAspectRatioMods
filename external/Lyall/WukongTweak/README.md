# WukongTweak
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is an ASI plugin for Black Myth: Wukong that features expanded ultrawide support and graphical tweaks.

## Features
### General
- Adjust gameplay FOV.
- Enable developer console.

### Ultrawide
- Remove 21:9 aspect ratio limit.
- Fix cropped FOV at <16:9.

### Graphical Tweaks
- Adjust forced sharpening.
- Disable chromatic aberration.
- Disable vignette (darkening at the edges of the screen).
- Enable virtual shadow maps.

## Installation
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the the game folder. e.g. ("**steamapps\common\BlackMythWukong**" for Steam).

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="dsound=n,b" %command%` to the launch options.

## Console
To display the console, you will need to have a console key bound.<br/>
You can do this by editing `%LOCALAPPDATA%\b1\Saved\Config\Windows\Input.ini` and adding these lines:
```ini
[/Script/Engine.InputSettings]
ConsoleKeys=Tilde
```

## Configuration
- See **WukongTweak.ini** to adjust settings.

## Known Issues
Please report any issues you see.
This list will contain bugs which may or may not be fixed.

## Screenshots
| ![animated-comparison](.github/images/wukong_comparison.png) |
|:--:|
| Expanded Ultrawide Support |

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking. <br />
[UnrealContainers](https://github.com/Fischsalat/UnrealContainers) for working with UE4/5 container types.
