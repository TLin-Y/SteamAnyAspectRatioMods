# DEVIL-MAY-CRY-5-Any-Aspect-Ratio

Set the in-game resolution to 1280x720 and SteamOS display scaling to "Stretch" for full-screen stretching.

Why?
- REFramework is incompatible with ARM64EC containers and can crash when its x86 instrumentation is dispatched. This mod applies the aspect-ratio fix without relying on REFramework.
- It replaces the old "Devil May Cry 5 Vergil Ultrawide Fix.exe" patcher, which only works on a 2020 build of the game and edits the exe. This mod leaves the game files unchanged.

1. Install

Extract the mod files directly into your game root folder (where DevilMayCry5.exe is located).

2. For SteamOS / Winlator (Wine Users)

For Steam Deck / SteamOS, open the game's Properties, then General > Launch Options, and use:
`WINEDLLOVERRIDES="dinput8=n,b" %command%`

For Winlator / Wine, add the following environment variable to the game or container settings:
Name: WINEDLLOVERRIDES
Value: dinput8=n,b

3. Custom Aspect Ratio

Open DevilMayCry5_ARFix.ini with any text editor. Change AspectWidth and AspectHeight to match the aspect ratio you want (e.g., 4:3, 16:10, 21:9). These are ratio values, not pixel dimensions. Set EnableARFix to false to disable the fix. This mod does not include an FOV setting.

4. Full-Screen Stretching

Set your SteamOS display scaling to "Stretch" and set the in-game resolution to 1280x720. The stretch mods render at the ratio in the ini, then scale that picture to fill the screen; if the in-game resolution is not 1280x720, it will not stretch. The mod defaults to 16:10; set the target width and height in the ini to the ratio you want.

The bundled dinput8.dll is from Ultimate ASI Loader. Its MIT license is included in Devil-May-Cry-5-v1.0.0.zip and in the repository root.
