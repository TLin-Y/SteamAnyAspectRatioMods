# RESIDENT-EVIL-2-DX11-Stretch-to-Any-Aspect-Ratio

**Compatibility: DirectX 11 (DX11) version only. The DirectX 12 version is not supported.**

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
   
Open .ini with any text editor. You can easily change TargetWidth and TargetHeight to match any screen ratio you want (e.g., 4:3, 16:10, 21:9).

4. Full-Screen Stretching
   
Set your SteamOS display scaling to "Stretch" and set the in-game resolution to 1280x720. The stretch mods render at the ratio in the ini, then scale that picture to fill the screen; if the in-game resolution is not 1280x720, it will not stretch. The mod defaults to 16:10; set the target width and height in the ini to the ratio you want.
