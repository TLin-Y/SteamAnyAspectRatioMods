# Guacamelee-Super-Turbo-Championship-Edition-Any-Aspect-Ratio

## Install

Extract the files into the game folder containing `GuacSTCE.exe` or `Game.exe`.

## SteamOS / Winlator

Set the launch option or Wine environment variable to:

`WINEDLLOVERRIDES="dinput8=n,b" %command%`

## Custom Aspect Ratio

Edit `Guac1_ARFix.ini` and set `Width` and `Height` to the desired resolution. The mod creates a borderless window at that size and adjusts the game's aspect ratio to match. The default resolution is 1280x800 (16:10).

The updated fix retains the game's field of view when adapting to the selected aspect ratio.
