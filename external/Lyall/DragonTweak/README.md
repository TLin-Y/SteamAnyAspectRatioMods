# DragonTweak
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**DragonTweak** is an ASI plugin for the Yakuza/Like a Dragon game series that can:
- Disable pillarboxing/letterboxing.
- Adjust shadow quality.
- Adjust level of detail (LOD).
- Skip intro logos.

### Current Features 
| Game                                              | Disable Pillarboxing/Letterboxing | Adjust Shadow Quality    | Adjust LOD | Skip Intro Logos |
|---------------------------------------------------|-----------------------------------|--------------------------|------------|------------------|
| Yakuza Kiwami 3 & Dark Ties (Demo)                | ✔️                               | ✔️                       | ✔️         | ✔️              |
| Like a Dragon: Pirate Yakuza in Hawaii            | ✔️                               | ✔️                       | ✔️         | ✔️              |
| Like a Dragon: Infinite Wealth                    | ✔️                               | ✔️                       | ✔️         | ✔️              |
| Like a Dragon Gaiden: The Man Who Erased His Name | ✔️                               | ✔️                       | ✔️         | ✔️              |
| Lost Judgment                                     | ✔️                               | ✔️                       | ✔️         | ✔️              |
| Yakuza: Like A Dragon                             | ✔️                               | ✔️                       | ❌         | ✔️              |
| Judgment                                          | ✔️                               | ✔️                       | ❌         | ✔️              |
| Yakuza Kiwami 2                                   | ✔️                               | ✔️                       | ❌         | ✔️              |
| Yakuza Kiwami 2 (Legacy)                          | ✔️                               | ✔️                       | ❌         | ✔️              |
| Yakuza 6: The Song of Life                        | ✔️                               | ✔️                       | ❌         | ✔️              |
| Yakuza 0: Director's Cut                          | ✔️                               | ❌                       | ❌         | ✔️              |
| Yakuza 0                                          | ✔️                               | ❌                       | ❌         | ✔️              |
| Yakuza Kiwami                                     | ✔️                               | ❌                       | ❌         | ✔️              |
| Yakuza Kiwami (Legacy)                            | ✔️                               | ❌                       | ❌         | ✔️              |
| Yakuza 5 Remastered                               | ✔️                               | ❌                       | ❌         | ✔️              |
| Yakuza 4 Remastered                               | ✔️                               | ❌                       | ❌         | ✔️              |
| Yakuza 3 Remastered                               | ✔️                               | ❌                       | ❌         | ✔️              |

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the folder that contains the game's executable. e.g. (Steam - Like a Dragon: Infinite Wealth): `steamapps\common\LikeADragon8\runtime\media`

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open the game properties in Steam and add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the launch options.  

## Configuration
- Open **`DragonTweak.ini`** to adjust settings.

## Troubleshooting
- If the ASI loader name **`dinput8.dll`** is incompatible with your configuration, please refer to this [list of possible DLL proxies](https://github.com/ThirteenAG/Ultimate-ASI-Loader#description).

## Known Issues
- Skipping the intro logos in Yakuza 0 & Yakuza 0: Director's Cut causes a visual glitch in the main menu.

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.