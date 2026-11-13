# FINAL-FANTASY-XIII-Any-Aspect-Ratio

Why?
- The [existing Final Fantasy XIII Ultrawide Fix](https://community.pcgamingwiki.com/files/file/2017-final-fantasy-xiii-ultrawide-fix/page/2/) crops the image to fill handheld screens. I made this patch to preserve the full image at narrower custom resolutions below 21:9, such as the Steam Deck's 16:10 display.
  
1. Install
For the combined aspect-ratio and [FF13Fix](https://github.com/rebtd7/FF13Fix) package, use the AnyAspectRatio plugin or extract `FF13-ARFix-with-FF13Fix-v1.0.0.zip` from `external/rebtd7/FF13Fix` into the game's `bin` folder:

`steamapps\common\FINAL FANTASY XIII\white_data\prog\win\bin`

The folder should contain `FF13_ARFix.asi`, `FF13_ARFix.ini`, `dinput8.dll`, `d3d9.dll`, and `FF13Fix.ini`, beside the game's `ffxiiiimg.exe`. The license files do not need to stay in the game folder.

The standalone `Final-Fantasy-XIII-v1.0.0.zip` contains only the aspect-ratio fix and ASI loader. Install FF13Fix separately if using this archive.

2. For SteamOS / Winlator (Wine Users)

For SteamOS / Proton, open the game's Properties, then General > Launch Options, and use:

`WINEDLLOVERRIDES="d3d9,dinput8=n,b" %command%`

This enables FF13Fix's `d3d9` proxy and the mod's `dinput8` ASI Loader. On Windows, no launch option is needed. If using a separate DXVK installation, see the [FF13Fix README](../external/rebtd7/FF13Fix/FF13Fix_README.txt) for its `dxvk.dll` instructions.

3. Configure the Aspect Ratio

The included `FF13_ARFix.ini` is preconfigured for the Steam Deck's 1280x800 display:

```ini
[Resolution]
ReplaceWidth=1280
ReplaceHeight=720
TargetWidth=1280
TargetHeight=800
```
Nova 4:3:
In launcher, select **Window** mode with 1920x1080, set **Stretch** mode
```ini
[Resolution]
ReplaceWidth=1920
ReplaceHeight=1080
TargetWidth=1280
TargetHeight=960
```
Set `ReplaceWidth` and `ReplaceHeight` to match the launcher resolution: these are the original values the patch searches for in the game. Set `TargetWidth` and `TargetHeight` to the display resolution. The plugin's global resolution setting updates only the `TargetWidth` and `TargetHeight` pair. (Some SteamOS, like DroidDeck, only work at 1080p.)

Edit `FF13Fix.ini` separately for FF13Fix options. The plugin's INI editor opens `FF13_ARFix.ini`; back up `FF13Fix.ini` before removing the combined mod if you want to keep custom FF13Fix settings.

4. Known Issues

- The GUI may be misaligned at custom aspect ratios.
- At very narrow aspect ratios such as 4:3, subtitles may be obscured by the video.

The bundled `dinput8.dll` is the 32-bit Ultimate ASI Loader. Its MIT license is included in `Final-Fantasy-XIII-v1.0.0.zip` and in the repository root.

The combined package includes FF13Fix 1.6.4 by [rebtd7](https://github.com/rebtd7/FF13Fix), based on OneTweakNG by Nucleoprotein. FF13Fix's GPLv3 license and third-party notices are included. Corresponding source is available for [FF13Fix 1.6.4](https://github.com/rebtd7/FF13Fix/archive/refs/tags/1.6.4.zip) and its [MinHook submodule](https://github.com/TsudaKageyu/minhook/archive/423d1e45af2ed2719a5c31e990e935ef301ed9c3.zip).
