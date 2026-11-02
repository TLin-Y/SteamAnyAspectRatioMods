# FINAL-FANTASY-XIII-Any-Aspect-Ratio

Why?
# FINAL-FANTASY-XIII-Any-Aspect-Ratio

Why?
- The [existing Final Fantasy XIII Ultrawide Fix](https://community.pcgamingwiki.com/files/file/2017-final-fantasy-xiii-ultrawide-fix/page/2/) crops the image to fill handheld screens. I made this patch to preserve the full image at narrower custom resolutions below 21:9, such as the Steam Deck's 16:10 display.

1. Install

Extract the contents of `Final-Fantasy-XIII-v1.0.0.zip` into the game's `bin` folder:

`steamapps\common\FINAL FANTASY XIII\white_data\prog\win\bin`

The folder should contain `FF13_ARFix.asi`, `FF13_ARFix.ini`, and `dinput8.dll`. Keep them beside the game's `ffxiii2img.exe`. The README and license file are documentation; they do not need to stay in the game folder.

2. For SteamOS / Winlator (Wine Users)

For SteamOS / Proton, open the game's Properties, then General > Launch Options, and use:

`WINEDLLOVERRIDES="d3d9,dinput8=n,b" %command%`

This enables both the game's `d3d9` proxy and the mod's `dinput8` ASI Loader. It can be used alongside other mods such as FF13Fix; keep both DLL names in the same `WINEDLLOVERRIDES` value rather than adding a second variable. On Windows, no launch option is needed.

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
In launcher, select **Window** mode with 1920x1080
```ini
[Resolution]
ReplaceWidth=1920
ReplaceHeight=1080
TargetWidth=1280
TargetHeight=960
```
To use another display, edit only `TargetWidth` and `TargetHeight` to that display's resolution. For example, use `1280` and `800` for 16:10, or `1920` and `1080` for 16:9. Keep `ReplaceWidth=1280` and `ReplaceHeight=720` match your launcher setting: these are the original values the patch searches for in the game, not your display resolution. (Some SteamOS, like DroidDeck, only work at 1080p.)

4. Known Issues

- The GUI may be misaligned at custom aspect ratios.
- At very narrow aspect ratios such as 4:3, subtitles may be obscured by the video.

The bundled `dinput8.dll` is the 32-bit Ultimate ASI Loader. Its MIT license is included in `Final-Fantasy-XIII-v1.0.0.zip` and in the repository root.

1. Install

Extract the contents of `Final-Fantasy-XIII-v1.0.0.zip` into the game's `bin` folder:

`steamapps\common\FINAL FANTASY XIII\white_data\prog\win\bin`

The folder should contain `FF13_ARFix.asi`, `FF13_ARFix.ini`, and `dinput8.dll`. Keep them beside the game's `ffxiii2img.exe`. The README and license file are documentation; they do not need to stay in the game folder.

2. For SteamOS / Winlator (Wine Users)

For SteamOS / Proton, open the game's Properties, then General > Launch Options, and use:

`WINEDLLOVERRIDES="d3d9,dinput8=n,b" %command%`

This enables both the game's `d3d9` proxy and the mod's `dinput8` ASI Loader. It can be used alongside other mods such as FF13Fix; keep both DLL names in the same `WINEDLLOVERRIDES` value rather than adding a second variable. On Windows, no launch option is needed.

3. Configure the Aspect Ratio

The included `FF13_ARFix.ini` is preconfigured for the Steam Deck's 1280x800 display:

```ini
[Resolution]
ReplaceWidth=1280
ReplaceHeight=720
TargetWidth=1280
TargetHeight=800
```

To use another display, edit only `TargetWidth` and `TargetHeight` to that display's resolution. For example, use `1280` and `800` for 16:10, or `1920` and `1080` for 16:9. Keep `ReplaceWidth=1280` and `ReplaceHeight=720` unchanged: these are the original values the patch searches for in the game, not your display resolution. Use a target ratio narrower than 21:9.

4. Known Issues

- The GUI may be misaligned at custom aspect ratios.
- At very narrow aspect ratios such as 4:3, subtitles may be obscured by the video.

The bundled `dinput8.dll` is the 32-bit Ultimate ASI Loader. Its MIT license is included in `Final-Fantasy-XIII-v1.0.0.zip` and in the repository root.
