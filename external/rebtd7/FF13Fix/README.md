# Final Fantasy XIII: Combined Fixes

The AnyAspectRatio package combines TLin-Y's aspect-ratio fix and [rebtd7's FF13Fix 1.6.4](https://github.com/rebtd7/FF13Fix), based on OneTweakNG by Nucleoprotein. FF13Fix provides performance and controller fixes.

## Install

Apply with the AnyAspectRatio plugin, or extract `FF13-ARFix-with-FF13Fix-v1.0.0.zip` beside `ffxiiiimg.exe` in `FINAL FANTASY XIII\white_data\prog\win\bin`.

On SteamOS / Winlator, use `WINEDLLOVERRIDES="d3d9,dinput8=n,b" %command%`. For a separate DXVK installation, see [FF13Fix's README](./FF13Fix_README.txt).

## Configuration

In `FF13_ARFix.ini`, set `ReplaceWidth` and `ReplaceHeight` to the launcher's resolution, and `TargetWidth` and `TargetHeight` to the desired display resolution. The plugin's global resolution setting updates only the `TargetWidth` and `TargetHeight` pair.

Edit `FF13Fix.ini` separately for FF13Fix options. The plugin's INI editor opens `FF13_ARFix.ini`; back up `FF13Fix.ini` before removing the combined mod if you want to keep custom FF13Fix settings.

## Attribution and source

The unchanged upstream release is `FF13Fix_164_.zip`. The combined package contains FF13Fix's GPLv3 license and MinHook notices. Corresponding source is available from the upstream projects:

- [FF13Fix 1.6.4 source](https://github.com/rebtd7/FF13Fix/archive/refs/tags/1.6.4.zip)
- [MinHook source at submodule revision 423d1e45](https://github.com/TsudaKageyu/minhook/archive/423d1e45af2ed2719a5c31e990e935ef301ed9c3.zip)

The standalone aspect-ratio fix and its source are in `Final Fantasy XIII` at the repository root. Ultimate ASI Loader's license is included in the combined package.
