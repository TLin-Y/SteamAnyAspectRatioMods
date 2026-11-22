# DYNASTY-WARRIORS-8-Xtreme-Legends-Complete-Edition-Any-Aspect-Ratio

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

Set the in-game resolution to your screen resolution (Steam Deck: 1280x800 or Fullscreen). The game draws a 16:9 picture with black bars on other screens; the mod stretches that picture to fill the whole screen, so no SteamOS display scaling is needed. The old way (in-game 1280x720 with SteamOS display scaling set to "Stretch") still works. To turn the mod's stretching off, set Enabled = false under [Stretch].

## Demo

![DYNASTY WARRIORS 8 Xtreme Legends Complete Edition aspect-ratio demo](https://raw.githubusercontent.com/TLin-Y/SteamAnyAspectRatioMods/main/DYNASTY%20WARRIORS%208%20Xtreme%20Legends%20Complete%20Edition/demo.jpg)

## Install
Extract the mod files directly into your Steam game folder (where Launch.exe is located).

## For SteamOS / Winlator (Wine Users)
For Steam Deck / SteamOS, open the game's Properties, then General > Launch Options, and use:
`WINEDLLOVERRIDES="dinput8=n,b" %command%`

For Winlator / Wine, add this environment variable to the game or container settings:
Name: WINEDLLOVERRIDES
Value: dinput8=n,b

## Custom Aspect Ratio
Open DW8XL_ARFix.ini with any text editor. Change Width and Height to match your screen ratio. The mod defaults to 1280x800 (16:10).

## Camera / FOV
In DW8XL_ARFix.ini, change FovMultiplier under [Camera] to adjust the standard and blocking camera distances. The packaged default is 1.10 (110%); use 1.0 for the game's original distances. Suggested values are 1.10 for 16:10, 1.16 for 3:2, and 1.29 for 4:3.

## macOS (CrossOver)
Use Fullscreen; the mod turns it into a borderless window that covers the whole screen. Set Width and Height in the ini to your screen size (for example 1728 and 1117 on a MacBook Pro 16").
