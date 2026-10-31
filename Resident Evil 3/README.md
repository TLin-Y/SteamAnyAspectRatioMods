# RESIDENT-EVIL-3-Any-Aspect-Ratio

Why?
- This mod adjusts Resident Evil 3 Remake's aspect ratio for handheld screens. Unlike some other mods, it does not include an FOV adjustment.

1. Install

Extract the mod files directly into your game root folder (where re3.exe is located).

2. For SteamOS / Winlator (Wine Users)

If you are playing on Linux (Proton/SteamOS) or Android (Winlator/Wine), add the following environment variable to your game or container settings to load the mod:
Name: WINEDLLOVERRIDES
Value: dinput8=n,b

3. Custom Aspect Ratio

Open RE3_ARFix.ini with any text editor. Change AspectWidth and AspectHeight to match the aspect ratio you want (e.g., 4:3, 16:10, 21:9). Set EnableARFix to false to disable the fix. This mod does not include an FOV setting.

4. Full-Screen Stretching

Set your display scaling to "Stretch" in your game settings. For a handheld, setting your container resolution to 1280x720 and stretching to full screen can fill the display.

The bundled dinput8.dll is from Ultimate ASI Loader. Its MIT license is included in Release-v1.0.0.zip and in the repository root.
