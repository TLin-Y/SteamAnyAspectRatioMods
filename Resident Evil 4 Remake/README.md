# RESIDENT-EVIL-4-Any-Aspect-Ratio-and-FOV

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

Set the in-game resolution to your screen resolution (Steam Deck: 1280x800). The game now fills the whole screen natively with no black bars; no SteamOS display scaling needed.

The C++ source is included as `re4.cpp`.

Why?
- This mod adjusts Resident Evil 4's aspect ratio for handheld screens. Unlike some other mods, it does not require x86 REFramework.

1. Install

Extract the mod files directly into your game root folder (where re4.exe is located).

2. For SteamOS / Winlator (Wine Users)

For Steam Deck / SteamOS, open the game's Properties, then General > Launch Options, and use:
`WINEDLLOVERRIDES="dinput8=n,b" %command%`

For Winlator / Wine, add the following environment variable to the game or container settings:
Name: WINEDLLOVERRIDES
Value: dinput8=n,b

3. Custom Aspect Ratio & FOV

Customize the aspect ratio (AspectWidth / AspectHeight) and FOV (FOVScale) in RE4_ARFix.ini. The mod defaults to 16:10. Set the ratio of your screen.

4. Full Screen

The mod removes the game's black bars so the picture fills any screen at full resolution. To go back to the game's own black bars, set Enabled = false under [FitScreen] in RE4_ARFix.ini. The old way (in-game 1280x720 with SteamOS display scaling set to "Stretch") still works as before.

On macOS (CrossOver), use Fullscreen or Borderless Window at your screen resolution, and set the ratio of your screen in RE4_ARFix.ini (for example 1728:1117 on a MacBook Pro 16").

The bundled dinput8.dll is from Ultimate ASI Loader. Its MIT license is included in Resident-Evil-4-Remake-v1.4.1.zip and in the repository root.
