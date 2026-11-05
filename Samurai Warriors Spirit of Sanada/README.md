# SAMURAI-WARRIORS-Spirit-of-Sanada-Any-Aspect-Ratio

# Set the in-game resolution to 1280x720 and the SteamOS display scaling to "Stretch" so the picture fills the screen (larger text on handhelds)

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
