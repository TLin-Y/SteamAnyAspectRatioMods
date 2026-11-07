# Guacamelee-2-Any-Aspect-Ratio

## Install

Extract the files into the game folder containing the game's 64-bit executable.

## SteamOS / Winlator

Set the launch option or Wine environment variable to:

`WINEDLLOVERRIDES="dinput8=n,b" %command%`

## Custom Aspect Ratio

Edit `Guac2_ARFix.ini` and set `Width` and `Height` to the desired resolution. The mod creates a borderless window at that size and adjusts the game's aspect ratio to match. The default resolution is 1280x800 (16:10).
