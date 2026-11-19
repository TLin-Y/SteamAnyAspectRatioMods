# RESIDENT-EVIL-3-Any-Aspect-Ratio-and-FOV

Set the in-game resolution to your screen resolution (Steam Deck: 1280x800). The mod stretches the picture to fill the screen; no SteamOS display scaling needed.

Supports both the DX11 and DX12 versions of the game.

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

3. Custom Aspect Ratio & FOV

Customize the aspect ratio (AspectWidth / AspectHeight) and FOV (FOVScale) in RE3_ARFix.ini. The mod defaults to 16:10. Set the ratio of your screen; RE3_ARFix.log shows it, for example "Adjust aspect ratio to 16:10 for 1280x800."

4. Full-Screen Stretching

The game draws a 16:9 picture with black bars on other screens. The mod stretches that picture to fill the whole screen. To turn this off, set Enabled = false under [Stretch] in RE3_ARFix.ini.

On macOS (CrossOver), use Borderless Window mode and set the ratio shown in RE3_ARFix.log (for example 1728:1117 on a MacBook Pro 16").

The bundled dinput8.dll is from Ultimate ASI Loader. Its MIT license is included in Resident-Evil-3-Remake-v1.4.0.zip and in the repository root.
