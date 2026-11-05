# RESIDENT-EVIL-VILLAGE-Any-Aspect-Ratio-and-FOV

## v1.0.1 update

- Corrected the FOV BasePlayer pointer offset from `0x128` to `0x130`.

Why?
- REFramework is incompatible with ARM64EC containers and can crash when its x86 instrumentation is dispatched. This mod applies aspect-ratio and FOV fixes without relying on REFramework.

1. Install

Extract the mod files directly into your game root folder (where re8.exe is located).

2. For SteamOS / Winlator (Wine Users)

For Steam Deck / SteamOS, open the game's Properties, then General > Launch Options, and use:
`WINEDLLOVERRIDES="dinput8=n,b" %command%`

For Winlator / Wine, add the following environment variable to the game or container settings:
Name: WINEDLLOVERRIDES
Value: dinput8=n,b

3. Custom Aspect Ratio & FOV

Open RE8_AR_FOV_Fix.ini with any text editor. Change AspectWidth and AspectHeight to match the aspect ratio you want (e.g., 4:3, 16:10, 21:9). Set EnableARFix to false to disable the aspect-ratio fix. Change IdleFOV and AimFOV to adjust the FOV, or set EnableFOVFix to false to disable the FOV fix.

4. Full-Screen Stretching

Set your SteamOS display scaling to "Stretch" and set the in-game resolution to 1280x720. The stretch mods render at the ratio in the ini, then scale that picture to fill the screen; if the in-game resolution is not 1280x720, it will not stretch. The mod defaults to 16:10; set the target width and height in the ini to the ratio you want.

The bundled dinput8.dll is from Ultimate ASI Loader. Its MIT license is included in Resident-Evil-Village-v1.1.0.zip and in the repository root.
