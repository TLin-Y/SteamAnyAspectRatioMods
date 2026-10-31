# SteamAnyAspectRatioMods

Play your favorite games at the aspect ratio you want, on the platform you choose. This collection provides easy to use, cross platform aspect ratio fixes for games in Proton/Wine/Linux/MacOS/SteamOS/Windows.

## About this collection

Many ready-made game mods are already available in [Lyall's repository](https://codeberg.org/Lyall); check there to see whether a mod for your game already exists. This collection focuses mainly on filling handheld-specific gaps and adapting mods for devices such as Steam Deck, AYN, and Retroid Pocket.

## Projects


| Game                                                                                                       | Source                                   | Release package                                                |
| ---------------------------------------------------------------------------------------------------------- | ---------------------------------------- | -------------------------------------------------------------- |
| [Resident Evil 2](./Resident%20Evil%202/)                                                                  | `re2.cpp`                                | `Release-v1.0.0.zip` (+ ASI Loader license)                    |
| [Resident Evil 3 Remake](./Resident%20Evil%203/)                                                           | `re3.cpp`                                | `Release-v1.0.0.zip` (+ ASI Loader license; no FOV adjustment) |
| [Warriors Orochi 3 Ultimate](./Warriors%20Orochi%203%20Ultimate/)                                          | `wo3u.cpp`                               | `Release-v1.0.0.zip` (+ ASI Loader license)                    |
| [Monster Hunter Rise](./Monster%20Hunter%20Rise/)                                                          | `rise.cpp`                               | `Release-v1.0.0.zip` (+ ASI Loader license)                    |
| [Tales of Arise](./Tales%20of%20Arise/)                                                                    | `arise.cpp`                              | `Release-v1.0.0.zip` (+ ASI Loader license)                    |
| [Ori and the Will of the Wisps](./Ori%20and%20the%20Will%20of%20the%20Wisps/)                              | `ori2arfix.cpp`                          | `Release-v1.0.0.zip` (+ ASI Loader license)                    |
| [Ori and the Blind Forest: Definitive Edition](./Ori%20and%20the%20Blind%20Forest%20Definitive%20Edition/) | `ori1dll.cpp`                            | Not included (see note below)                                  |
| [Warriors Orochi 4 Ultimate](./Warriors%20Orochi%204%20Ultimate/)                                          | `wo4u.cpp`                               | `Release-v1.0.0.zip` (+ ASI Loader license)                    |
| [Dark Souls III](./Dark%20Souls%20III/)                                                                    | `ds3.cpp`                                | `Release-v1.0.0.zip` (+ ASI Loader license)                    |
| [Ninja Gaiden Sigma 2](./Ninja%20Gaiden%20Sigma%202/)                                                      | No C++ source in the original repository | Not included (see note below)                                  |

## Notes

- Each project folder keeps its `README.md`. Release archives are based on the upstream `v1.0.0` release; the eight applicable ZIPs also contain `LICENSE-Ultimate-ASI-Loader.txt` for the bundled ASI Loader proxy DLL.
- The ASI Loader proxy DLLs in those release archives are from [ThirteenAG/Ultimate-ASI-Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases). The upstream project is MIT-licensed; its copyright and license notice is included in this repository and in each applicable release ZIP.
- The Resident Evil 3 Remake mod adjusts aspect ratio only; it has no FOV adjustment.
- The additional `d3d11.dll` and `mods/fix.dll` in the Dark Souls III and Warriors Orochi 3 Ultimate packages are separate mod files, not identified here as ASI Loader proxy DLLs.
- The Ori and the Blind Forest release archive contains an `Assembly-CSharp.dll` game assembly, so it is not redistributed here. Its source and README are included.
- The Ninja Gaiden Sigma 2 upstream repository has no C++ source. Its two mod folders are included; the upstream 123 MB release archive is not.

## Upstream repositories

- [Resident Evil 2](https://github.com/TLin-Y/RESIDENT-EVIL-2-Stretch-to-Any-Aspect-Ratio)
- [Warriors Orochi 3 Ultimate](https://github.com/TLin-Y/WARRIORS-OROCHI-3-Ultimate-Any-Aspect-Ratio-Fov-With-Slow-Motion-Fix)
- [Monster Hunter Rise](https://github.com/TLin-Y/MonsterHunterRiseAnyAspectRatio)
- [Tales of Arise](https://github.com/TLin-Y/Tales-of-Arise-Any-Aspect-Ratio)
- [Ori and the Will of the Wisps](https://github.com/TLin-Y/Ori-and-the-Will-of-the-Wisps-Any-Aspect-Ratio)
- [Ori and the Blind Forest: Definitive Edition](https://github.com/TLin-Y/Ori-and-the-Blind-Forest-Definitive-Edition-Force-Any-Aspect-Ratio)
- [Warriors Orochi 4 Ultimate](https://github.com/TLin-Y/WARRIORS-OROCHI-4-Ultimate-Any-Aspect-Ratio-Force-stretch-UI-mode-for-handheld-)
- [Dark Souls III](https://github.com/TLin-Y/Dark-Souls-III-Force-Any-Aspect-Ratio-FOV)
- [Ninja Gaiden Sigma 2](https://github.com/TLin-Y/Ninja-Gaiden-2-Sigma-Mods-for-Winlator)
