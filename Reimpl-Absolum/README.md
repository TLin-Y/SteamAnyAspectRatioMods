# Absolum-Any-Aspect-Ratio

The fix supports custom window resolutions and aspect ratios.

## Install

1. If switching from Lyall's AbsolumFix, first remove `AbsolumFix.asi` and `AbsolumFix.ini` from the executable folder. Back up the INI if you want to keep its settings. The existing `winhttp.dll` can be replaced by this mod; remove the old `winhttp=n,b` launch override only if it is no longer needed. The Decky plugin will prompt before replacing a different existing loader file.
2. Extract the archive into the folder containing `Absolum.exe` (on Steam, `Absolum\x64.steam`).
3. On SteamOS / Proton, set the Steam launch option to:
   `WINEDLLOVERRIDES="winhttp=n,b" %command%`
4. On Winlator, add the environment variable `WINEDLLOVERRIDES` with value `winhttp=n,b`.

## Custom Aspect Ratio

Edit `Absolum_ARFix.ini` to set the `[Window]` `Width` and `Height`. The supplied default is 1280x800 (16:10). Set both to `0` to use the current monitor dimensions. `Borderless` controls the window style; set `[Window] Enabled` to `false` to leave window sizing to the game.

`[Fix Resolution]` enables the aspect-ratio correction, `[Fix FOV]` preserves horizontal field of view on narrower screens, and the Lyall-derived `[Fix HUD]` corrections are enabled by default. On narrower screens, the HUD is top-aligned.

## Credits and License

HUD corrections are derived from [Lyall's AbsolumFix](https://codeberg.org/Lyall/AbsolumFix), licensed under the MIT License:

MIT License

Copyright (c) 2025 Lyall

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
