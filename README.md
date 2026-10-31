# SteamAnyAspectRatioMods

Play your favorite games at the aspect ratio you want, on the platform you choose. This collection provides easy to use, cross platform aspect ratio fixes for games in Proton/Wine/Linux/MacOS/SteamOS/Windows.

## About this collection

Many ready-made game mods are already available in [Lyall's repository](https://codeberg.org/Lyall); check there to see whether a mod for your game already exists. This collection focuses mainly on filling handheld-specific gaps and adapting mods for devices such as Steam Deck, AYN, and Retroid Pocket.

## In action

Resident Evil 3 running on a handheld:

![Resident Evil 3 running on a handheld](resources/demo.jpg)

## Notes

- Each project folder keeps its `README.md`. ZIP filenames identify the game and version (for example, `Resident-Evil-4-v1.0.0.zip`). Release archives are based on the upstream `v1.0.0` release; the nine applicable ZIPs also contain `LICENSE-Ultimate-ASI-Loader.txt` for the bundled ASI Loader proxy DLL.
- The ASI Loader proxy DLLs in those release archives are from [ThirteenAG/Ultimate-ASI-Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases). The upstream project is MIT-licensed; its copyright and license notice is included in this repository and in each applicable release ZIP.
- The Resident Evil 3 Remake mod adjusts aspect ratio only; it has no FOV adjustment.
- The additional `d3d11.dll` and `mods/fix.dll` in the Dark Souls III and Warriors Orochi 3 Ultimate packages are separate mod files, not identified here as ASI Loader proxy DLLs.
- The Ori and the Blind Forest Definitive Edition folder includes the `Assembly-CSharp.dll` aspect-ratio mod, along with its C++ source and installation instructions.
- The Ninja Gaiden Sigma 2 upstream repository has no C++ source. Its two mod folders are included; the upstream 123 MB release archive is not.
