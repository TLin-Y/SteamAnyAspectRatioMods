# RESIDENT-EVIL-2-Any-Aspect-Ratio

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

Set the in-game resolution to your screen resolution (Steam Deck: 1280x800). The game now fills the whole screen natively with no black bars; no SteamOS display scaling needed.

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

Open RE2_ARFix.ini with any text editor. Set AspectWidth and AspectHeight to the ratio of your screen (for example, 16 and 10 for 16:10, or 21 and 9 for 21:9). These are ratio values, not pixel dimensions. The mod defaults to 16:10; set the ratio of your screen. On screens wider than 16:9 the view is widened to the sides; on screens wider than 21:9 the vignette is turned off and cutscenes stay 16:9.

4. Full Screen

The mod removes the game's black bars so the picture fills any screen at full resolution. To go back to the game's own black bars, set Enabled = false under [FitScreen] in RE2_ARFix.ini. The old way (in-game 1280x720 with SteamOS display scaling set to "Stretch") still works as before.

On macOS (CrossOver), use DirectX 12 and Fullscreen or Borderless Window at your screen resolution, and set the ratio of your screen in RE2_ARFix.ini (for example 1728:1117 on a MacBook Pro 16").

The bundled dinput8.dll is from Ultimate ASI Loader. Its MIT license is included in Resident-Evil-2-Remake-v1.5.0.zip and in the repository root.
