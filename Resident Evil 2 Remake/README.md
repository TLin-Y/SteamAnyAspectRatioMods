# RESIDENT-EVIL-2-Any-Aspect-Ratio

Set the in-game resolution to 1280x720 and SteamOS display scaling to "Stretch" for full-screen stretching.

Supports both the DX11 and DX12 versions of the game.

Why?

- REFramework currently blows up on ARM64EC containers. It crashes straight away once the x86 instrumentation gets dispatched.

1. Install

Extract the mod files directly into your game root folder (where re2.exe is located).

2. For SteamOS / Winlator (Wine Users)

For Steam Deck / SteamOS, open the game's Properties, then General > Launch Options, and use:
`WINEDLLOVERRIDES="dinput8=n,b" %command%`

For Winlator / Wine, add the following environment variable to the game or container settings:
Name: WINEDLLOVERRIDES
Value: dinput8=n,b

3. Custom Aspect Ratio & FOV

Open RE2_ARFix.ini with any text editor. Set AspectWidth and AspectHeight to the desired aspect ratio (for example, 16 and 10 for 16:10, or 21 and 9 for 21:9). These are ratio values, not pixel dimensions.

4. Full-Screen Stretching

Set your SteamOS display scaling to "Stretch" and set the in-game resolution to 1280x720. The stretch mods render at the ratio in the ini, then scale that picture to fill the screen; if the in-game resolution is not 1280x720, it will not stretch. The mod defaults to 16:10; set the target width and height in the ini to the ratio you want.

The bundled dinput8.dll is from Ultimate ASI Loader. Its MIT license is included in Resident-Evil-2-Remake-v1.2.0.zip and in the repository root.
