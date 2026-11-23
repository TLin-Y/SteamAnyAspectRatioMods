# DYNASTY-WARRIORS-6-Any-Aspect-Ratio

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

Set the in-game resolution to 1280x720 and Full-Screen to ON, and set the **SteamOS display scaling to "Stretch"**. The game picture is stretched to fill your screen; the mod corrects the 3D view and FOV for your screen ratio, so characters keep their shape. The HUD is stretched. To turn the mod's own stretching off, set Enabled = false under [Stretch].

# Install
Extract the mod files directly into your game folder (where DW6_WIN.exe is located).

# For SteamOS / Winlator (Wine Users)
For Steam Deck / SteamOS, open the game's Properties, then General > Launch Options, and use:
`WINEDLLOVERRIDES="dinput8=n,b" %command%`

For Winlator / Wine, add this environment variable to the game or container settings:
Name: WINEDLLOVERRIDES
Value: dinput8=n,b

# Custom Aspect Ratio
Open DW6_ARFix.ini with any text editor. You can easily change Width and Height to match any screen ratio you want (e.g., 4:3, 16:10, 21:9). The mod defaults to 1280x800 (16:10). Multiplier under [FOV] widens or narrows the view (1.0 = default).

# macOS (CrossOver)
Use Full-Screen ON; the mod turns it into a borderless window that covers the whole screen. Set Width and Height in the ini to your screen size (for example 1728 and 1117 on a MacBook Pro 16").
