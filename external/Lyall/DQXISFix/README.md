# Dragon Quest XI S: Echoes of an Elusive Age Fix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

This is a fix that adds ultrawide/narrower support and more to Dragon Quest XI S: Echoes of an Elusive Age.<br />

If you are interested in a technical breakdown of how I made this fix, [check it out on my Patreon.](https://www.patreon.com/posts/dragon-quest-xi-102796530?utm_medium=clipboard_copy&utm_source=copyLink&utm_campaign=postshare_creator&utm_content=join_link)

## Features
- Ultrawide/narrow aspect ratio support.
- Correct FOV at any aspect ratio.
- Adjustable gameplay FOV.
- Centered 16:9 HUD.

## Installation
- Grab the latest release of DQXISFix from [here.](../../../releases)
- Extract the contents of the release zip in to the shipping executable folder. (e.g. "**steamapps\common\DRAGON QUEST XI S\Game\Binaries\Win64**" for Steam).

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**
- Open up the game properties in Steam and add `WINEDLLOVERRIDES="dsound=n,b" %command%` to the launch options.

## Configuration
- See **DQXISFix.ini** to adjust settings for the fix.

## Known Issues
Please report any issues you see. This list will contain bugs which may or may not be fixed.
### Ultrawide/Narrower
- The HUD is offset in 2D mode.

## Recommended Mods
- [DQXIS-SDK](https://github.com/emoose/DQXIS-SDK) by [emoose](https://github.com/emoose) - Unlocks the console, fixes low-res character previews and much more.

## Screenshots
| ![dqxis_32by9_gameplay](.github/images/dqxis_32by9_gameplay.jpg) |
|:--------------------------:|
| 32:9 Gameplay |

## Credits
Thanks to Audioboxer on the WSGF Discord for testing! <br />
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.<br />
[Dumper-7](https://github.com/Encryqed/Dumper-7) for dumping the SDK.
