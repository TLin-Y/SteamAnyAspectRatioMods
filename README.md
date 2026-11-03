# SteamAnyAspectRatioMods

Play your favorite games at the aspect ratio you want, on the platform you choose. This collection provides easy to use, cross platform aspect ratio fixes for games in Proton/Wine/Linux/MacOS/SteamOS/Windows.

## About this collection

Many ready-made game mods are already available in [Lyall's repository](https://codeberg.org/Lyall); check there to see whether a mod for your game already exists. This collection focuses mainly on filling handheld-specific gaps and adapting mods for devices such as Steam Deck, AYN, and Retroid Pocket.

## In action

Resident Evil 3 running on a handheld:

![Resident Evil 3 running on a handheld](resources/demo.jpg)

For custom narrow resolutions in [Dynasty Warriors 3: Remastered](./Dynasty%20Warriors%203%20Remastered/README.md), see the game-specific guide.

## Building another ASI fix

The reusable Windows/x64 ASI compiler is in [compiler](./compiler/README.md). Its build files and dependencies are isolated from the game mods in this repository. The sample implementation is based on the clean source layout of Lyall's [Sky1stChapterFix](https://codeberg.org/Lyall/Sky1stChapterFix); replace `compiler/dllmain.cpp` and set `compiler/build_config.json` for each game. Lyall's license and attribution remain with the derived source, while the generated plugin's version metadata identifies AnyAspectRatio as its publisher.

## License

The [MIT License](./LICENSE) applies only to original code and documentation authored by TLin-Y, unless a file or directory states otherwise. It does not grant rights to third-party code, game files, or other assets. The compiler's Lyall-derived files and the bundled Ultimate ASI Loader each retain their separate license and attribution.

## Game Mods

- [Dark Souls III](./Dark%20Souls%20III/)
- [Dynasty Warriors 3: Remastered](./Dynasty%20Warriors%203%20Remastered/)
- [Dynasty Warriors: Origins](./Dynasty%20Warriors%20Origins/)
- [Final Fantasy XIII](./Final%20Fantasy%20XIII/)
- [Monster Hunter Rise](./Monster%20Hunter%20Rise/)
- [Ninja Gaiden Sigma 2](./Ninja%20Gaiden%20Sigma%202/)
- [Ori and the Blind Forest: Definitive Edition](./Ori%20and%20the%20Blind%20Forest%20Definitive%20Edition/)
- [Ori and the Will of the Wisps](./Ori%20and%20the%20Will%20of%20the%20Wisps/)
- [Resident Evil 2 Remake DX11](./Resident%20Evil%202%20Remake%20DX11/)
- [Resident Evil 3 Remake DX11](./Resident%20Evil%203%20Remake%20DX11/)
- [Resident Evil 4 Remake](./Resident%20Evil%204%20Remake/)
- [Resident Evil 7](./Resident%20Evil%207/)
- [Resident Evil Village](./Resident%20Evil%20Village/)
- [Tales of Arise](./Tales%20of%20Arise/)
- [Trials of Mana (WIP)](./Trials%20of%20Mana%20%28WIP%29/)
- [Visions of Mana](./Visions%20of%20Mana/)
- [Warriors Orochi 3 Ultimate](./Warriors%20Orochi%203%20Ultimate/)
- [Warriors Orochi 4 Ultimate](./Warriors%20Orochi%204%20Ultimate/)
