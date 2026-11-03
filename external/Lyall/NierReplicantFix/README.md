# NierReplicantFix
[![Patreon-Button](.forgejo/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.forgejo/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**NierReplicantFix** is an ASI plugin for **NieR Replicant ver.1.22474487139** that can add ultrawide/narrower support, unlock the framerate, skip intro logos and more. 

## Features
### General
- Skip intro logos and press any key screen.
- Unlock the framerate cap and fix many related bugs (like boar drifting, ladder sliding etc).
- Disable letterboxing in cutscenes.

### Fixes
- Auto-hide the mouse cursor when using a gamepad.
- Fix stuttering after hotplugging a controller.
- Fix SSAO artifacting at higher resolutions.
- Make camera movement with the mouse framerate-independant.

### Ultrawide/Narrower
- Support for any aspect ratio with corrected FOV.
- NPC culling fixed at wider aspect ratios.
- 16:9 scaled HUD with spanned elements where possible.

### Graphics
- Adjust shadow resolution.
- Disable culling of distant NPCs.

## Installation  
1. Download the latest [release](../../../releases). 
2. Extract the zip into the game's root folder, e.g.: "**steamapps\common\NieR Replicant ver.1.22474487139**" 

### Steam Deck/Linux Additional Instructions
🚩**Windows users can skip this step!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="winmm=n,b" %command%` to the launch options.

## Configuration
- Open **`NierReplicantFix.ini`** to adjust settings.

## Screenshots
| ![NierReplicant_32by9_gameplay](.forgejo/images/NierReplicant_32by9_cutscene.jpg) |
|:--------------------------:|
| 32:9 Cutscene |

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading.  
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading.  
[spdlog](https://github.com/gabime/spdlog) for logging.  
[safetyhook](https://github.com/cursey/safetyhook) for hooking.