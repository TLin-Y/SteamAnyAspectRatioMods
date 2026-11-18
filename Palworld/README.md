# PALWORLD-Any-Aspect-Ratio

## Install
1. Extract the mod files into `Pal\Binaries\Win64` (where `Palworld-Win64-Shipping.exe` is located).

## SteamOS / Winlator
Set the launch option to:
`WINEDLLOVERRIDES="winmm=n,b" %command%`

For Winlator, set `WINEDLLOVERRIDES` to `winmm=n,b`.

## Custom Aspect Ratio
Edit `Palworld_ARFix.ini` and set `TargetWidth` and `TargetHeight`. The default is 1280x800.

## FOV
The view keeps the 16:9 horizontal FOV on any screen, and wider screens show more at the sides.
To change it, edit `Multiplier` in `Palworld_ARFix.ini` (`1.2` = 120%, `0.9` = 90%, range 0.5 to 2.0).

## UI Size
To make text and menus larger or smaller, edit `Scale` under `[UI]` in `Palworld_ARFix.ini` (`1.2` = 120%, range 0.5 to 2.0).
