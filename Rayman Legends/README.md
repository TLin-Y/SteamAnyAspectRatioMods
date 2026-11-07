# Rayman-Legends-Any-Aspect-Ratio

Set the in-game resolution to 1280x720 and SteamOS display scaling to "Stretch". This mod does not remove the game's letterbox bars.

## 1. Install

Extract the files into the game folder containing `Rayman Legends.exe`.

## 2. SteamOS / Winlator

For SteamOS, set the game's display scaling to "Stretch", and select 1280x720 as its in-game resolution. Set the Steam launch option to:

`WINEDLLOVERRIDES="dinput8=n,b" %command%`

For Winlator / Wine, set `WINEDLLOVERRIDES` to `dinput8=n,b`.

## 3. Custom Aspect Ratio

The ASI creates `RaymanLegends_ARFix.ini` beside itself:

```ini
[Resolution]
Width = 1280
Height = 800

[Fixes]
AspectRatio = true
HUD = false
```

Change `Width` and `Height` to the desired aspect ratio, then restart the game. The default is 1280x800 (16:10). The mod adjusts the aspect ratio and borderless window but does not remove the game's letterbox bars or change its internal render-target resolution. For SteamOS stretching, set the in-game resolution to 1280x720 and display scaling to "Stretch".

The gameplay hooks use fixed offsets for Steam v1.3.140380. Other game versions are not validated and may not work correctly. `HUD` is disabled by default.

## Credits

Hook locations, instruction identifiers and gameplay behavior were identified from Helifax's `Plugin.Rayman Legends.dll` version 2.6.4.0. [Original WSGF discussion](http://www.wsgf.org/forums/viewtopic.php?p=148423#p148423).
