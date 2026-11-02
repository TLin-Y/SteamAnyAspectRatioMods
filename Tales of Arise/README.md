# Tales-of-Arise-Any-Aspect-Ratio

# Install
Extract the mod files directly into your Steam game.exe folder.

# For SteamOS / Winlator (Wine Users)
For Steam Deck / SteamOS, open the game's Properties, then General > Launch Options, and use:
`WINEDLLOVERRIDES="winmm=n,b" %command%`

For Winlator / Wine, add this environment variable to the game or container settings:
Name: WINEDLLOVERRIDES
Value: winmm=n,b

# Custom Aspect Ratio
Open .ini with any text editor. You can easily change TargetWidth and TargetHeight to match any screen ratio you want (e.g., 4:3, 16:10, 21:9).
