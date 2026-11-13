# FINAL FANTASY X/X-2 HD Remaster - SUWSF Widescreen Patches

SUWSF by [PhantomGamers](https://community.pcgamingwiki.com/files/file/2432-final-fantasy-xx-2-hd-remaster-anima-edition/) (MIT, see `LICENSE`), with a patch set for FFX and FFX-2.

The files are the author's release, repacked at the zip root. The only change: `SUWSF.ini` defaults to `Resolution="1280x800"` (was `"auto"`) with a comment explaining the setting.

## Install

1. Extract the zip (`d3d11.dll`, `SUWSF.asi`, `SUWSF.ini`, `LICENSE`) into the game folder, beside `FFX.exe` and `FFX-2.exe`.
2. SteamOS/Winlator launch option: `WINEDLLOVERRIDES="d3d11=n,b" %command%`.
3. Launch FFX or FFX-2 from the launcher.

## Resolution

Edit `Resolution` under `[UserSettings]` in `SUWSF.ini`: `"WIDTHxHEIGHT"` (e.g. `"1920x1080"`) or `"auto"` for the desktop resolution. The AnyAspectRatio plugin's global resolution setting updates this value; `"auto"` is left unchanged.
