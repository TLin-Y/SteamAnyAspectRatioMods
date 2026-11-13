# RoFRemasterFix
[![Patreon-Button](.forgejo/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.forgejo/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**RoFRemasterFix** is an ASI plugin for **Resonance of Fate ENHANCED REMASTER** that adds support for custom resolutions, ultrawide/narrower support and more.

## Features
### General
- Skip intro logos and opening movie.
- Apply a custom resolution.
- Borderless fullscreen.
- Uncap 120fps framerate limit.
- Fix framerate-related bugs like walking in place during dialogue.

### Ultrawide/Narrower
- Fix cropped FOV at ultrawide resolutions.
- Fix stretched HUD.
- Fix stretched movies and crop letterboxing.

## Installation  
- [Download the latest release](../../../releases). 
- Extract the contents of the release zip into the game folder. (e.g. "**steamapps\common\Resonance of Fate**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="winmm=n,b" %command%` to the launch options.

## Configuration
- Open **`RoFRemasterFix.ini`** to adjust settings.

## Screenshots
| ![RoFRemaster_21by9_gameplay](.forgejo/images/RoFRemaster_21by9_gameplay.jpg) |
|:--------------------------:|
| 21:9 Gameplay |

## Credits
Many thanks to everyone below for supporting the development of these open-source fixes via [**Patreon**](https://www.patreon.com/Wintermance).

Libretro · Marcin Korzycki · zaccachino · Dave Brown · Inge Dalby · Jack · Sente · Average Dude Tech · Kaldaien · Khoa Nguyen · ThingontheFloor · langiam · Dominik · Brian · Klaw117 · NGI · Tommy · Neofaucheur · HoneyOTU · Aelzaire · Dylan Perez · danielsan1911 · breebo · Anthony Zhang · Chris B · Laur · Chin · Roman-Niclas Bartels · タターガタ · Rylic · Koji Ebersole · herpaderp · Brandan Sankar · Adam Babloyan · Will Tranter · Abram Vall22 · Benjamin Arnott · Corky316rulz · Houman Jafarnia · ChristopherPaulBacon · Thrashinuva · AlexDGr8r · Volf · Anubis · Willy Sarahan · VAXIStaa · Paneachy · Knut Haug · Abbas · Anthony · Ace Light · roemer phaez · Spiegel · Bulldog5769 · Juan Fariña Palleja · Richard Geggie · nta~／norma · Michael Buza · Quentin Bricquir · John

## Third-Party
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading.  
[mINI](https://github.com/metayeti/mINI) for ini reading.  
[safetyhook](https://github.com/cursey/safetyhook) for hooking.