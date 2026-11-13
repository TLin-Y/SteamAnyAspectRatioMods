# Daemon X Machina: Titanic Scion Widescreen Fix

Author: [Yuki](https://yuki.pet/daemon-x-machina-titanic-scion-widescreen-fix/).
`patch.zip` holds the author's `patch.7z` contents unchanged, repacked as ZIP for the plugin.

Made for game version 1.2.2. A later game update may break it until the author updates the patch.

## Install

1. Extract `patch.zip` (`SUWSF.asi`, `SUWSF.ini`, `dsound.dll`) into
   `steamapps/common/Daemon X Machina Titanic Scion/Game/Binaries/Win64/`.
2. SteamOS/Winlator launch option: `WINEDLLOVERRIDES="dsound=n,b" %command%`.
3. Launch the game.

## Resolution

`SUWSF.ini` uses `Resolution="auto"` (your screen resolution) by default. To force one, set e.g. `Resolution="1280x800"`.
