# Ys-VIII-Lacrimosa-of-DANA-Any-Aspect-Ratio

## Install

Extract the files into the game folder containing `ys8.exe`.

## SteamOS / Winlator

Set the launch option or Wine environment variable to:

`WINEDLLOVERRIDES="dinput8=n,b" %command%`

## Custom Aspect Ratio

Edit `Ys8_AspectPatch.ini` and set `Width` and `Height` to your desired resolution. Select the same resolution in the game; the mod does not change the game's resolution. By default, the mod adjusts the camera aspect ratio and removes letterboxing. The default resolution is 1280x800 (16:10).

`CustomAR` is an optional manual aspect-ratio override. Leave `SharedProjection = false`; that experimental option may distort the UI.

The gameplay patch validates the supported game executable before making changes. Check `Ys8_AspectPatch.log` if a patch is not applied.

## Screenshots

![Ys VIII gameplay](https://raw.githubusercontent.com/TLin-Y/SteamAnyAspectRatioMods/main/Ys%20VIII%20Lacrimosa%20of%20DANA/demo.jpg)

![Ys VIII ultrawide gameplay](https://raw.githubusercontent.com/TLin-Y/SteamAnyAspectRatioMods/main/Ys%20VIII%20Lacrimosa%20of%20DANA/demo2.jpg)
