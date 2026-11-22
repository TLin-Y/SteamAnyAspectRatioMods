# DEVIL-MAY-CRY-5-Any-Aspect-Ratio

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

Set the in-game resolution to your screen resolution (Steam Deck: 1280x800). The game now fills the whole screen with no black bars; no SteamOS display scaling needed.

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

Open DevilMayCry5_ARFix.ini with any text editor. The mod defaults to 16:10 (Steam Deck). On other screens, set AspectWidth and AspectHeight to your screen's ratio (e.g., 4:3, 21:9, 32:9; ratio values, not pixel dimensions), or set EnableARFix to false: the full-screen mode already matches the picture to your screen. On screens wider than 16:9 the view is widened to the sides. This mod does not include an FOV setting.

4. Full-Screen Stretching (old way)

The old way (in-game 1280x720 with SteamOS display scaling set to "Stretch") still works; keep EnableARFix = true with the ratio of your screen. To go back to the game's own black bars, set Enabled = false under [FitScreen].

On macOS (CrossOver), use Fullscreen or Borderless at your screen resolution.

The bundled dinput8.dll is from Ultimate ASI Loader. Its MIT license is included in Devil-May-Cry-5-v1.2.0.zip and in the repository root.
