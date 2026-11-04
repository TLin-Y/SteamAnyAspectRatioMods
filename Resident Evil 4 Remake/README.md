# RESIDENT-EVIL-4-Any-Aspect-Ratio

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

3. Custom Aspect Ratio

Open RE4_ARFix.ini with any text editor. Change AspectWidth and AspectHeight to match the aspect ratio you want (e.g., 4:3, 16:10, 21:9). Set EnableARFix to false to disable the fix.

4. Full-Screen Stretching

Set your display scaling to "Stretch" (GameNative, or your game settings) and set the in-game resolution to 1280x720. The stretch mods render at the ratio in the ini, then scale that picture to fill the screen; if the in-game resolution is not 1280x720, it will not stretch. The mod defaults to 16:10; set the target width and height in the ini to the ratio you want.

The bundled dinput8.dll is from Ultimate ASI Loader. Its MIT license is included in Resident-Evil-4-Remake-v1.1.0.zip and in the repository root.
