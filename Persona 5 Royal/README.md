Why?
- Persona 5 Royal has no native support for custom aspect ratios. This mod patches the game to render at any ratio you choose.

1. Install
   
Extract the mod files directly into your Persona 5 Royal root folder (where P5R.exe is located).

2. For SteamOS / Winlator (Wine Users)
   
For Steam Deck / SteamOS, open the game's Properties, then General > Launch Options, and use:
`WINEDLLOVERRIDES="dinput8=n,b" %command%`

For Winlator / Wine, add the following environment variable to the game or container settings:
Name: WINEDLLOVERRIDES
Value: dinput8=n,b

3. Custom Aspect Ratio
   
Open P5R_ARFix.ini with any text editor. You can easily change Width and Height to match any screen ratio you want (e.g., 4:3, 16:10, 21:9).

4. Full-Screen Stretching
   
Set your SteamOS display scaling to "Stretch" and set the in-game resolution to 1280x720. The stretch mods render at the ratio in the ini, then scale that picture to fill the screen; if the in-game resolution is not 1280x720, it will not stretch. The mod defaults to 16:10; set the width and height in the ini to the ratio you want.
