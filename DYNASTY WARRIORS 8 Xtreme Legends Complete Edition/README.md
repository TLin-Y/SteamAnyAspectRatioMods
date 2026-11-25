# DYNASTY-WARRIORS-8-Xtreme-Legends-Complete-Edition-Any-Aspect-Ratio

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

![DYNASTY WARRIORS 8 Xtreme Legends Complete Edition at 4:3, 16:10 and 48:9](https://raw.githubusercontent.com/TLin-Y/SteamAnyAspectRatioMods/main/DYNASTY%20WARRIORS%208%20Xtreme%20Legends%20Complete%20Edition/demo.jpg)

Fills the screen at any aspect ratio. The HUD, menus and movies keep their 16:9 shape, and screens narrower than 16:9 get extra view above and below.

# Install
Extract the mod files directly into your Steam game folder (where Launch.exe is located).

Fullscreen and windowed both work; fullscreen uses a borderless window. The in-game resolution setting is ignored; the mod sets the resolution.

# For SteamOS / Winlator (Wine Users)
For Steam Deck / SteamOS, open the game's Properties, then General > Launch Options, and use:
`WINEDLLOVERRIDES="dinput8=n,b" %command%`

For Winlator / Wine, add this environment variable to the game or container settings:
Name: WINEDLLOVERRIDES
Value: dinput8=n,b

# macOS (CrossOver)
Use Fullscreen, and set Width and Height in the ini to your screen size (for example 1728 and 1117 on a MacBook Pro 16").

# Custom Aspect Ratio
Open DW8XL_ARFix.ini with any text editor. Width and Height set the resolution (e.g., 4:3, 16:10, 21:9); 0 uses your screen size.

Multiplier under [FOV] changes the field of view: 1.0 is the normal view, 1.1 shows 10% more (0.5 to 2.0).
