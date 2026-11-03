# NiNoKuni2Fix
[![Patreon-Button](.forgejo/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.forgejo/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**NiNoKuni2Fix** is an ASI plugin for **Ni no Kuni II: Revenant Kingdom** that adds support for any aspect ratio, adjustable HUD size, an intro skip and more. 

## Features
### General
- Skip intro logos and the autosave warning.
- Optionally resume automatically from the latest autosave on launch.
- Fix framerate-dependant mouse input.
- Fix black screen at launch when using HDR on an Nvidia GPU.

### Ultrawide/Narrower
- Support for any aspect ratio.
- Adjustable HUD size, with Spanned, 16:9, 16:10, and 21:9 presets.
- Disable pillarboxing/letterboxing in cutscenes.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip into the game folder. (e.g. "**steamapps\common\Ni no Kuni II Revenant Kingdom**" for Steam)

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="winmm=n,b" %command%` to the launch options.

## Configuration
- Open **`NiNoKuni2Fix.ini`** to adjust settings.

## Screenshots
| ![NiNoKuni2_32by9_gameplay](.forgejo/images/ninokuni2_21by9_cutscene.jpg) |
|:--------------------------:|
| 21:9 Cutscene |

## Credits
Many thanks to everyone below for supporting the development of these open-source fixes via [**Patreon**](https://www.patreon.com/Wintermance).

Libretro · Marcin Korzycki · zaccachino · Dave Brown · Inge Dalby · Jack · Philip Garcia · Sente · GuyOnAChair ​​ · Average Dude Tech · Archer · Kaldaien · Khoa Nguyen · ThingontheFloor · langiam · Dominik · Fallenchaos922 · Klaw117 · NGI · Alexa · Tommy · Neofaucheur · HoneyOTU · Aelzaire · Dylan Perez · breebo · Christopher · Anthony Zhang · Chris B · Laur · Flaminghotchin · タターガタ · Koji Ebersole · Brandan Sankar · Adam Babloyan · Will Tranter · Abram Vall22 · Benjamin Arnott · Luxi · Corky316rulz · Stephan · Houman Jafarnia · Thrashinuva · AlexDGr8r · Dain Bramage · Volf · Anubis · Willy Sarahan · VAXIStaa · Paneachy · Knut Haug · Anthony · Ace Light · roemer phaez · Bulldog5769 · Richard Geggie · Michael Buza · Quentin Bricquir · John

## Third-Party
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading.  
[mINI](https://github.com/metayeti/mINI) for ini reading.  
[safetyhook](https://github.com/cursey/safetyhook) for hooking.