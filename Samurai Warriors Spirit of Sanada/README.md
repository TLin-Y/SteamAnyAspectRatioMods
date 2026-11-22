# SAMURAI-WARRIORS-Spirit-of-Sanada-Any-Aspect-Ratio

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

Set the in-game resolution to your screen resolution (Steam Deck: 1280x800 or Fullscreen). The game draws a 16:9 picture with black bars on other screens; the mod stretches that picture to fill the whole screen, so no SteamOS display scaling is needed. The old way (in-game 1280x720 with SteamOS display scaling set to "Stretch") still works. To turn the mod's stretching off, set Enabled = false under [Stretch].

# Install
Extract the mod files directly into your Steam game folder (where SWSanada.exe is located).

# For SteamOS / Winlator (Wine Users)
For Steam Deck / SteamOS, open the game's Properties, then General > Launch Options, and use:
`WINEDLLOVERRIDES="dinput8=n,b" %command%`

For Winlator / Wine, add this environment variable to the game or container settings:
Name: WINEDLLOVERRIDES
Value: dinput8=n,b

# Custom Aspect Ratio
Open SWSanada_ARFix.ini with any text editor. You can easily change Width and Height to match any screen ratio you want (e.g., 4:3, 16:10, 21:9). The mod defaults to 16:10.
