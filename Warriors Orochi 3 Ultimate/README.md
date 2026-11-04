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
Open .ini with any text editor. You can easily change TargetWidth and TargetHeight to match any screen ratio you want (e.g., 4:3, 16:10, 21:9).
