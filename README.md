# SteamAnyAspectRatioMods

Play your favorite games at the aspect ratio you want, on the platform you choose. This collection provides easy to use, cross platform aspect ratio fixes for games in Proton/Wine/Linux/MacOS/SteamOS/Windows.

## About this collection

Many ready-made game mods are already available in [Lyall's repository](https://codeberg.org/Lyall); check there to see whether a mod for your game already exists. This collection focuses mainly on filling handheld-specific gaps and adapting mods for devices such as Steam Deck, AYN, and Retroid Pocket.

## In action

Resident Evil 3 running on a handheld:

![Resident Evil 3 running on a handheld](resources/demo.jpg)

## Building another ASI fix

The reusable Windows/x64 ASI compiler is in [compiler](./compiler/README.md). Its build files and dependencies are isolated from the game mods in this repository. The sample implementation is based on the clean source layout of Lyall's [Sky1stChapterFix](https://codeberg.org/Lyall/Sky1stChapterFix); replace `compiler/dllmain.cpp` and set `compiler/build_config.json` for each game. Lyall's license and attribution remain with the derived source, while the generated plugin's version metadata identifies AnyAspectRatio as its publisher.

## License

The [MIT License](./LICENSE) applies only to original code and documentation authored by TLin-Y, unless a file or directory states otherwise. It does not grant rights to third-party code, game files, or other assets. The compiler's Lyall-derived files and the bundled Ultimate ASI Loader each retain their separate license and attribution.
