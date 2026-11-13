# Daemon X Machina: Titanic Scion Widescreen Fix

Author: [Yuki](https://yuki.pet/daemon-x-machina-titanic-scion-widescreen-fix/).
`patch.zip` holds the author's `patch.7z` contents repacked as ZIP for the plugin. The only change: `SUWSF.ini` defaults to `Resolution="1280x800"` (was `"auto"`) with a comment explaining the setting.

Made for game version 1.2.2. A later game update may break it until the author updates the patch.

## Install

1. Extract `patch.zip` (`SUWSF.asi`, `SUWSF.ini`, `dsound.dll`) into
   `steamapps/common/Daemon X Machina Titanic Scion/Game/Binaries/Win64/`.
2. SteamOS/Winlator launch option: `WINEDLLOVERRIDES="dsound=n,b" %command%`.
3. Launch the game.

## Resolution

Edit `Resolution` under `[UserSettings]` in `SUWSF.ini`: `"WIDTHxHEIGHT"` (e.g. `"1920x1080"`) or `"auto"` for the desktop resolution. The AnyAspectRatio plugin's global resolution setting updates this value; `"auto"` is left unchanged.
