# Guacamelee-2-Any-Aspect-Ratio

## Install

Extract the files into the game folder containing `Guac2_x64.exe` (Steam) or `Guac2_64.exe` (GOG). The Steam install folder is `steamapps\common\Guacamelee2`.

Version 2.0.1 includes separate Steam and GOG executable scan profiles. The Steam profile waits up to 30 seconds for executable code to initialize before installing hooks.

## SteamOS / Winlator

Set the launch option or Wine environment variable to:

`WINEDLLOVERRIDES="dinput8=n,b" %command%`

## Custom Aspect Ratio

Edit `Guac2_ARFix.ini` and set `Width` and `Height` to the desired resolution. The mod creates a borderless window at that size and adjusts the game's aspect ratio to match. The default resolution is 1280x800 (16:10).

On narrower aspect ratios, the updated fix preserves horizontal field of view by expanding the camera's vertical view.
