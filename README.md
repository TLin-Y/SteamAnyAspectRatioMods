# SteamAnyAspectRatioMods

Play your favorite games at the aspect ratio you want, on the platform you choose. This collection provides easy to use, cross platform aspect ratio fixes for games in Proton/Wine/Linux/MacOS/SteamOS/Windows.

## Support

If these fixes make your games better, you can support the author:

[![Support me on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)

Fixes by [Lyall](./external/Lyall/README.md) are mirrored here unchanged for the Decky plugin; please support him too: [Patreon](https://www.patreon.com/Wintermance) | [Ko-fi](https://ko-fi.com/W7W01UAI9)

## About this collection

Before looking for a fix here, check [Lyall's repository](https://codeberg.org/Lyall): it contains many ready-made fixes for a wide range of games. This collection focuses mainly on filling handheld-specific gaps and adapting mods for devices such as Steam Deck, AYN, and Retroid Pocket.

## Credits

This repository includes original fixes as well as work based on and distributed alongside projects by other authors. Individual game READMEs document game-specific sources, contributors, and notices.

- **Lyall** — source code, patterns, and fixes reused or adapted in this repository, including [Sky1stChapterFix](https://codeberg.org/Lyall/Sky1stChapterFix), [NinjaGaidenMCFix](https://codeberg.org/Lyall/NinjaGaidenMCFix), and other [Lyall fixes](https://codeberg.org/Lyall). See the [compiler notes](./compiler/README.md) for the derived-source details and license.
- **ThirteenAG** — [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader), included by some game mods to load ASI plugins.
- **Compiler dependencies** — [inipp](https://github.com/mcmtroffaes/inipp), [SafetyHook](https://github.com/cursey/safetyhook), [spdlog](https://github.com/gabime/spdlog), [Zydis](https://github.com/zyantific/zydis), and [Zycore](https://github.com/zyantific/zycore) (used by Zydis). The compiler setup pins specific upstream revisions.

## In action

Resident Evil 3 running on a handheld:

![Resident Evil 3 running on a handheld](resources/demo.jpg)

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
