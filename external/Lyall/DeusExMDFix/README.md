# DeusExMDFix
[![Patreon-Button](.github/images/Patreon-Button.png)](https://www.patreon.com/Wintermance) [![ko-fi](.github/images/Kofi-Button.svg)](https://ko-fi.com/W7W01UAI9)

**DeusExMDFix** is an ASI plugin for **Deus Ex: Mankind Divided** that can skip intro logos, expand the FOV slider and more.

## Features

### General
- Skip intro logos.
- Expanded FOV slider.
- Adjustable viewmodel FOV.
- Disable pausing on focus loss.

### Ultrawide/Narrower
- Support for any aspect ratio.
- Adjustable HUD size.
- Fix various HUD issues.

### Graphics
- Disable film grain.
- Increase shadow quality.

## Installation  
- Download the latest [release](../../../releases). 
- Extract the contents of the release zip in to the the game folder.  

### Steam Deck/Linux Additional Instructions
🚩**You do not need to do this if you are using Windows!**  
- Open up the game properties in your launcher and add `WINEDLLOVERRIDES="winmm=n,b" %command%` to the launch options.

## Configuration
- Open **`DeusExMDFix.ini`** to adjust settings.

## Screenshots
![comparison](.github/images/443235433-41d5c28e-1f26-4e8c-93ba-d2bd22f3cc89.png)

## Credits
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) for ASI loading. <br />
[inipp](https://github.com/mcmtroffaes/inipp) for ini reading. <br />
[spdlog](https://github.com/gabime/spdlog) for logging. <br />
[safetyhook](https://github.com/cursey/safetyhook) for hooking.
