# Trials of Mana-Any-Aspect-Ratio

Runs the game at any aspect ratio without black bars in cutscenes, with an optional FOV multiplier and borderless window.

This mod started from the [Steam community ultrawide workaround](https://steamcommunity.com/app/924980/discussions/0/2268068817146932077/) and UUU (https://framedsc.com/GeneralGuides/universal_ue4_consoleunlocker.htm). That guide changes a game executable byte for a particular 5120x1440 setup. This mod needs no exe edit or UUU and supports any screen, including narrower displays such as 16:10 handheld screens.

## Install

Extract `TrialsOfMana_ARFix.asi`, `TrialsOfMana_ARFix.ini`, and `dxgi.dll` into `Trials of Mana\Trials of Mana\Binaries\Win64`
(beside `Trials of Mana-Win64-Shipping.exe`). Pick your screen resolution in the game's Options.

The game only lists resolutions with the same shape as your desktop (1280x800 on Steam Deck).

## SteamOS / Winlator

Add the launch option:

```text
WINEDLLOVERRIDES="dxgi=n,b" %command%
```

## Settings

Edit `TrialsOfMana_ARFix.ini` and restart the game:

```ini
[FOV]
Multiplier = 1.0

[Display]
Borderless = false
```

`Multiplier` scales the camera field of view (0.5 to 2.0), cutscenes included.
`Borderless = true` removes the title bar in windowed mode and centres the window.

## Known issues

- Some GUI elements, such as the loading bar, may be cropped.

## Demo

![Trials of Mana demo](https://raw.githubusercontent.com/TLin-Y/SteamAnyAspectRatioMods/main/Trials%20of%20Mana/demo.jpg)
