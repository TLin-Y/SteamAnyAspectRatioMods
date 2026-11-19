# RESIDENT-EVIL-7-Any-Aspect-Ratio-and-FOV

Set the in-game resolution to your screen resolution (Steam Deck: 1280x800). The mod stretches the picture to fill the screen; no SteamOS display scaling needed.

Why?
- REFramework is incompatible with ARM64EC containers and can crash when its x86 instrumentation is dispatched. This mod applies aspect-ratio and FOV fixes without relying on REFramework.

1. Install

Extract the mod files directly into your game root folder (where re7.exe is located).

2. For SteamOS / Winlator (Wine Users)

For Steam Deck / SteamOS, open the game's Properties, then General > Launch Options, and use:
`WINEDLLOVERRIDES="dinput8=n,b" %command%`

For Winlator / Wine, add the following environment variable to the game or container settings:
Name: WINEDLLOVERRIDES
Value: dinput8=n,b

3. Custom Aspect Ratio & FOV

Customize the aspect ratio (AspectWidth / AspectHeight) and FOV (FOVScale) in RE7_ARFix.ini. The mod defaults to 16:10. Set the ratio of your screen; RE7_ARFix.log shows it, for example "Adjust aspect ratio to 8:5 for 1280x800." (8:5 is the same as 16:10).

4. Full-Screen Stretching

The game draws a 16:9 picture with black bars on other screens. The mod stretches that picture to fill the whole screen. To turn this off, set Enabled = false under [Stretch] in RE7_ARFix.ini. The old way (in-game 1280x720 with SteamOS display scaling set to "Stretch") still works as before.

The bundled dinput8.dll is from Ultimate ASI Loader. Its MIT license is included in Resident-Evil-7-v1.3.0.zip and in the repository root.
