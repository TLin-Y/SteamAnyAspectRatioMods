# SteamAnyAspectRatioMods

Play your favorite games at the aspect ratio you want, on the platform you choose. This collection provides easy to use, cross platform aspect ratio fixes for games in Proton/Wine/Linux/MacOS/SteamOS/Windows.

On Steam Deck, the [AnyAspectRatio Decky plugin](./AnyAspectRatioDecky/README.md) applies these fixes for you in one tap:

![AnyAspectRatio Decky plugin on Steam Deck](resources/games-tab.jpg)

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

| Game | Author | Steam ID | Version | Released | Details |
|---|---|---|---|---|---|
| Dark Souls III | TLin-Y (fullscreen fix by ellekz) | [374320](https://store.steampowered.com/app/374320) | [v1.1.0](./Dark%20Souls%20III/Dark-Souls-III-v1.1.0.zip) | 2026-11-03 | [README](./Dark%20Souls%20III/README.md) |
| Dynasty Warriors 3: Complete Edition Remastered | TLin-Y | [3841510](https://store.steampowered.com/app/3841510) | [v1.1.0](./Dynasty%20Warriors%203%20Remastered/DW3RE-v1.1.0.zip) | 2026-11-03 | [README](./Dynasty%20Warriors%203%20Remastered/README.md) |
| Dynasty Warriors: Origins | TLin-Y | [2384580](https://store.steampowered.com/app/2384580) | [v1.0.0](./Dynasty%20Warriors%20Origins/Dynasty-Warriors-Origins-v1.0.0.zip) | 2026-11-02 | [README](./Dynasty%20Warriors%20Origins/README.md) |
| Final Fantasy XIII | TLin-Y | [292120](https://store.steampowered.com/app/292120) | [v1.0.0](./Final%20Fantasy%20XIII/Final-Fantasy-XIII-v1.0.0.zip) | 2026-11-02 | [README](./Final%20Fantasy%20XIII/README.md) |
| Monster Hunter Rise | TLin-Y | [1446780](https://store.steampowered.com/app/1446780) | [v1.1.0](./Monster%20Hunter%20Rise/Monster-Hunter-Rise-v1.1.0.zip) | 2026-11-03 | [README](./Monster%20Hunter%20Rise/README.md) |
| Ninja Gaiden Sigma 2 | Nozomi Miyamori, Fiend Busa, Lyall | [1580790](https://store.steampowered.com/app/1580790) | [Black V18](./Ninja%20Gaiden%20Sigma%202) | 2026-11-02 | [README](./Ninja%20Gaiden%20Sigma%202/README.md) |
| Ori and the Blind Forest: Definitive Edition | TLin-Y | [387290](https://store.steampowered.com/app/387290) | [V2 (v1.0.0)](./Ori%20and%20the%20Blind%20Forest%20Definitive%20Edition) | 2026-11-02 | [README](./Ori%20and%20the%20Blind%20Forest%20Definitive%20Edition/README.md) |
| Ori and the Will of the Wisps | TLin-Y | [1057090](https://store.steampowered.com/app/1057090) | [v1.1.0](./Ori%20and%20the%20Will%20of%20the%20Wisps/Ori-and-the-Will-of-the-Wisps-v1.1.0.zip) | 2026-11-03 | [README](./Ori%20and%20the%20Will%20of%20the%20Wisps/README.md) |
| Resident Evil 2 Remake DX11 | TLin-Y | [883710](https://store.steampowered.com/app/883710) | [v1.1.0](./Resident%20Evil%202%20Remake%20DX11/Resident-Evil-2-Remake-DX11-v1.1.0.zip) | 2026-11-03 | [README](./Resident%20Evil%202%20Remake%20DX11/README.md) |
| Resident Evil 3 Remake DX11 | TLin-Y | [952060](https://store.steampowered.com/app/952060) | [v1.1.0](./Resident%20Evil%203%20Remake%20DX11/Resident-Evil-3-Remake-DX11-v1.1.0.zip) | 2026-11-03 | [README](./Resident%20Evil%203%20Remake%20DX11/README.md) |
| Resident Evil 4 Remake | TLin-Y | [2050650](https://store.steampowered.com/app/2050650) | [v1.1.0](./Resident%20Evil%204%20Remake/Resident-Evil-4-Remake-v1.1.0.zip) | 2026-11-03 | [README](./Resident%20Evil%204%20Remake/README.md) |
| Resident Evil 7 | TLin-Y | [418370](https://store.steampowered.com/app/418370) | [v1.1.0](./Resident%20Evil%207/Resident-Evil-7-v1.1.0.zip) | 2026-11-03 | [README](./Resident%20Evil%207/README.md) |
| Resident Evil Village | TLin-Y | [1196590](https://store.steampowered.com/app/1196590) | [v1.1.0](./Resident%20Evil%20Village/Resident-Evil-Village-v1.1.0.zip) | 2026-11-03 | [README](./Resident%20Evil%20Village/README.md) |
| Tales of Arise | TLin-Y | [740130](https://store.steampowered.com/app/740130) | [v1.1.0](./Tales%20of%20Arise/Tales-of-Arise-v1.1.0.zip) | 2026-11-03 | [README](./Tales%20of%20Arise/README.md) |
| Trials of Mana (WIP) | TLin-Y | [924980](https://store.steampowered.com/app/924980) | [WIP](./Trials%20of%20Mana%20%28WIP%29) | 2026-11-02 | [README](./Trials%20of%20Mana%20%28WIP%29/README.md) |
| Visions of Mana | TLin-Y | [2490990](https://store.steampowered.com/app/2490990) | [v1.0.0](./Visions%20of%20Mana/Visions-of-Mana-v1.0.0.zip) | 2026-11-02 | [README](./Visions%20of%20Mana/README.md) |
| Warriors Orochi 3 Ultimate | TLin-Y | [1879330](https://store.steampowered.com/app/1879330) | [v1.1.0](./Warriors%20Orochi%203%20Ultimate/Warriors-Orochi-3-Ultimate-v1.1.0.zip) | 2026-11-03 | [README](./Warriors%20Orochi%203%20Ultimate/README.md) |
| Warriors Orochi 4 Ultimate | TLin-Y | [831560](https://store.steampowered.com/app/831560) | [v1.1.0](./Warriors%20Orochi%204%20Ultimate/Warriors-Orochi-4-Ultimate-v1.1.0.zip) | 2026-11-03 | [README](./Warriors%20Orochi%204%20Ultimate/README.md) |

Version links open the release archive (or the mod folder when it ships loose files). Fixes by Lyall are listed with the same details in [external/Lyall](./external/Lyall/README.md).
