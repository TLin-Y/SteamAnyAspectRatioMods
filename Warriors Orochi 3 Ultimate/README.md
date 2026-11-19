# WARRIORS-OROCHI-3-Ultimate-Any-Aspect-Ratio-Fov-With-Slow-Motion-Fix

Works in any mode: pick the last in-game resolution (it shows your screen size, e.g. 1280x800 on Steam Deck) for a native picture with a fitted HUD, or keep the old way (1280x720 with SteamOS display scaling set to "Stretch").

# Install
Extract the mod files directly into your Steam game.exe folder.

# For SteamOS / Winlator (Wine Users)
For Steam Deck / SteamOS, open the game's Properties, then General > Launch Options, and use:
`WINEDLLOVERRIDES="dinput8=n,b" %command%`

For Winlator / Wine, add this environment variable to the game or container settings:
Name: WINEDLLOVERRIDES
Value: dinput8=n,b

# Custom Aspect Ratio
Open WO3U_ARFix.ini with any text editor. Set AspectWidth and AspectHeight to the desired aspect ratio (for example, 16 and 10 for 16:10, or 21 and 9 for 21:9). These are ratio values, not pixel dimensions.

Under [Custom Resolution], Width and Height 0 use your screen size; set Enabled = false to keep the game's own resolution list.

# Credits
Custom resolution and HUD fixes adapted from [WO3UFix](https://codeberg.org/Lyall/WO3UFix) by Lyall (MIT License, included as LICENSE-WO3UFix.txt).
