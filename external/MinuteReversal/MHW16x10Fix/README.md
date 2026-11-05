# MHW16x10Fix - Monster Hunter: World

Author: MinuteReversal - https://github.com/MinuteReversal/MHW16x10Fix

Version 0.11.13 (released 27 Oct 2026), MIT licensed (see [LICENSE](./LICENSE)).

An aspect ratio fix and black bar remover for Monster Hunter: World / Iceborne. It removes the forced 16:9 letterbox (top and bottom black bars) and enables native 16:10 rendering, including the Steam Deck's 1280x800 display. Both DX11 and DX12 are supported.

Verified on game version `15.23.00` (Steam), DX11 and DX12, at 1280x800 on Windows and Steam Deck. Other 16:10 and ultrawide resolutions may work but have not received the same validation. Only that executable build is supported; an unknown build fails closed.

## Installation

Extract the zip into your Monster Hunter World folder (where `MonsterHunterWorld.exe` is located):

`steamapps\common\Monster Hunter World\`

so that `dinput8.dll` and `mhw_16x10.ini` are in that folder.

### SteamOS / Winlator (Wine users)

For Steam Deck / SteamOS, open the game's Properties, then General > Launch Options, and use:
`WINEDLLOVERRIDES="dinput8=n,b" %command%`

For Winlator / Wine, add the following environment variable to the game or container settings:
Name: WINEDLLOVERRIDES
Value: dinput8=n,b

### Other proxy mods

The game folder can contain only one `dinput8.dll`. To use another proxy mod (ICE, Stracker's Loader) at the same time, rename the other mod's proxy DLL to `dinput8_chain.dll` and keep both beside `MonsterHunterWorld.exe`; it is loaded automatically. See the author's repository for SharpPluginLoader instructions.

## Configuration

Open `mhw_16x10.ini` with any text editor. With `AutoDetectAspect=true` the screen's real aspect ratio is used and `Width` / `Height` are only fallbacks. Logs are written to `MHW16x10Fix.log`.

## Known limitations

- A small number of UI dialogs may retain 16:9-oriented positioning.
