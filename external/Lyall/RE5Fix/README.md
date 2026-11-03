# RE5Fix
[![Patreon-Button](.forgejo/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.forgejo/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**RE5Fix** is an ASI plugin for **Resident Evil 5** that can remove resolution limits, adjust gameplay FOV and much more. 

## Features
### General
- Remove 4096x4096 resolution limit.
- Enable borderless windowed mode.
- Adjust "unlocked" framerate cap.
- Adjust gameplay FOV.
- Remove cutscene letterboxing/pillarboxing.

### Graphics
- Increase level of detail.
- Adjust shadowmap resolution.
- Disable colour filter.

## Installation  
- [Download the latest release](../../../releases).
- Extract the contents of the release zip into the game folder. (e.g. "**steamapps\common\Resident Evil 5**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to the launch options.

## Configuration
- Open **`RE5Fix.ini`** to adjust settings.

## Screenshots
| ![RE5_21by9_gameplay](.forgejo/images/RE5_21by9_gameplay.jpg) |
|:--------------------------:|
| 21:9 with colour filter removed. |

## Credits
Many thanks to everyone below for supporting the development of these open-source fixes via [**Patreon**](https://www.patreon.com/Wintermance).

Libretro · Marcin Korzycki · zaccachino · Dave Brown · Inge Dalby · Jack · Philip Garcia · Sente · GuyOnAChair ​​ · Average Dude Tech · Archer · Kaldaien · Khoa Nguyen · ThingontheFloor · langiam · Dominik · Fallenchaos922 · Klaw117 · NGI · Alexa · Tommy · Neofaucheur · HoneyOTU · Aelzaire · Dylan Perez · breebo · Christopher · Anthony Zhang · Chris B · Laur · Flaminghotchin · タターガタ · Koji Ebersole · Brandan Sankar · Adam Babloyan · Will Tranter · Abram Vall22 · Benjamin Arnott · Luxi · Corky316rulz · Stephan · Houman Jafarnia · Thrashinuva · AlexDGr8r · Dain Bramage · Volf · Anubis · Willy Sarahan · VAXIStaa · Paneachy · Knut Haug · Anthony · Ace Light · roemer phaez · Bulldog5769 · Richard Geggie · Michael Buza · Quentin Bricquir · John

## Third-Party
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading.  
[mINI](https://github.com/metayeti/mINI) for ini reading.  
[safetyhook](https://github.com/cursey/safetyhook) for hooking.