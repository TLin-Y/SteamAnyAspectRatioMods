# RESIDENT-EVIL-3-DX11-Any-Aspect-Ratio

**Compatibility: DirectX 11 (DX11) version only. The DirectX 12 version is not supported.**

Why?
- REFramework currently blows up on ARM64EC containers. It crashes straight away once the x86 instrumentation gets dispatched.

1. Install

Extract the mod files directly into your game root folder (where re3.exe is located).

2. For SteamOS / Winlator (Wine Users)

For Steam Deck / SteamOS, open the game's Properties, then General > Launch Options, and use:
`WINEDLLOVERRIDES="dinput8=n,b" %command%`

For Winlator / Wine, add the following environment variable to the game or container settings:
Name: WINEDLLOVERRIDES
Value: dinput8=n,b

3. Custom Aspect Ratio

Open RE3_ARFix.ini with any text editor. Change AspectWidth and AspectHeight to match the aspect ratio you want (e.g., 4:3, 16:10, 21:9). Set EnableARFix to false to disable the fix. This mod does not include an FOV setting.

4. Full-Screen Stretching

Set your display scaling to "Stretch" in your game settings. For a handheld, setting your container resolution to 1280x720 and stretching to full screen can fill the display.

The bundled dinput8.dll is from Ultimate ASI Loader. Its MIT license is included in Resident-Evil-3-Remake-DX11-v1.1.0.zip and in the repository root.
