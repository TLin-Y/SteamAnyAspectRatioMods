# WARRIORS-OROCHI-4-Ultimate-Any-Aspect-Ratio

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

Works in any mode: Fullscreen, Windowed with the last in-game resolution (it shows your screen size, e.g. 1280x800 on Steam Deck), or the old way (1280x720 with SteamOS display scaling set to "Stretch"). The 3D always fills the screen; the HUD keeps its 16:9 layout stretched to the screen.

# Install
Extract the mod files directly into your Steam game.exe folder.

# For SteamOS / Winlator (Wine Users)
For Steam Deck / SteamOS, open the game's Properties, then General > Launch Options, and use:
`WINEDLLOVERRIDES="dinput8=n,b" %command%`

For Winlator / Wine, add this environment variable to the game or container settings:
Name: WINEDLLOVERRIDES
Value: dinput8=n,b

# Custom Aspect Ratio
Open WO4U_ARFix.ini with any text editor. Set TargetWidth and TargetHeight to your screen ratio (e.g., 4:3, 16:10, 21:9). WO4U_ARFix.log shows it, for example "Adjust aspect ratio to 8:5 for 1280x800."

Under [Custom Resolution], Width and Height 0 use your screen size. [Stretch] fills the screen in Fullscreen mode, where the game would otherwise add black bars; set Enabled = false to turn it off.
