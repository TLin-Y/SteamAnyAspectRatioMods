# RESIDENT-EVIL-VILLAGE-Any-Aspect-Ratio-and-FOV

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

Set the in-game resolution to your screen resolution (Steam Deck: 1280x800). The game now fills the whole screen natively with no black bars; no SteamOS display scaling needed.

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

Customize the aspect ratio (AspectWidth / AspectHeight) and FOV (FOVScale) in RE8_AR_FOV_Fix.ini. The mod defaults to 16:10. Set the ratio of your screen. On screens wider than 16:9 the HUD and menus keep their 16:9 shape; on screens wider than 21:9 the vignette is turned off.

4. Full Screen

The mod removes the game's black bars so the picture fills any screen at full resolution. To go back to the game's own black bars, set Enabled = false under [FitScreen] in RE8_AR_FOV_Fix.ini. The old way (in-game 1280x720 with SteamOS display scaling set to "Stretch") still works as before.

The bundled dinput8.dll is from Ultimate ASI Loader. Its MIT license is included in Resident-Evil-Village-v1.5.0.zip and in the repository root.
