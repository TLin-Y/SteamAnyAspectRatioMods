# WARRIORS-OROCHI-3-Ultimate-Any-Aspect-Ratio-Fov-With-Slow-Motion-Fix

# Set the in-game resolution to 1280x720 and display scaling to "Stretch" so the picture fills the screen (larger text on handhelds)

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
