# Hollow Knight - Ultra-Wide

Author: [p1xel8ted](https://github.com/p1xel8ted/UltrawideFixes/releases/tag/HollowKnight). Release v0.1.2 (Windows/Proton build). The author ships a `.7z`; it is repacked here unchanged as a `.zip` (same files and paths) because the plugin only reads zip archives. It bundles BepInEx 5, ConfigurationManager and the mod.

Removes the game's resolution and aspect-ratio limits and adds ultra-wide support, an FOV option, menu backgrounds that fill the screen, a hero-light toggle and a HUD offset.

## Install

1. Extract the zip into `steamapps/common/Hollow Knight/` (the folder with `hollow_knight.exe`).
2. SteamOS/Winlator launch option: `WINEDLLOVERRIDES="winhttp=n,b" %command%`. Without it Proton uses its built-in `winhttp.dll` and the mod does not load. Not needed on Windows.
3. Start the game and pick the resolution in the game's video settings.

## Settings

Press F1 in-game to open the configuration menu. Settings are saved in `BepInEx/config/` after the first launch.
