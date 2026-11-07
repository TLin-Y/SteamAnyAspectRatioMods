# Elden Ring Ultrawide Fix (Offline)

Author: [TechieW](https://www.nexusmods.com/eldenring/mods/7870).
The author's release ZIP is kept unchanged.

## AnyAspectRatio plugin

1. Close the game. Apply copies the archive into `ELDEN RING/Game`.
2. Confirm replacement. The plugin renames `start_protected_game.exe` to
   `start_protected_game-old.exe` in the same folder, then copies
   `start_game_in_offline_mode.exe` to `start_protected_game.exe`.
3. SteamOS/Winlator launch option: `WINEDLLOVERRIDES="dinput8=n,b" %command%`.
4. Play offline only. Never rename `eldenring.exe` itself, delete the backup,
   or run the bundled anti-cheat toggler while the plugin manages the launcher.
5. Remove deletes the installed mod files and restores the original protected
   launcher. Remove the mod before returning to online play.

Edit `Game/mods/AdjustTheFov/config.ini` to change FOV; restart the game.
The optional Xarrant text-size mod described below is not included in this ZIP.
Non-Steam shortcuts targeting `Game/modengine2_launcher.exe`,
`Game/start_protected_game.exe`, or `Game/start_game_in_offline_mode.exe` are
recognized when `eldenring.exe` exists beside them. The backup executable is
not a detection alternative. Existing Mod Engine 2 files are left untouched;
review replacement conflicts if other mods already use the bundled loader files.

---

## Original supplied author notes

https://www.nexusmods.com/eldenring/mods/7870?tab=description

### Original upload

02 May 2025, 8:50PM

### Created by

TechieW

Steam Deck
Go to the game's install folder, go into the Game subfolder where eldenring.exe lives
There's a file name start_protected_game.exe - rename this to something else like start_protected_game-old.exe
Make a copy of either eldenring.exe or the file named start_game_in_offline_mode.exe, then rename that copy of the file to start_protected_game.exe.
Note: DO NOT rename eldenring.exe itself to start_protected_game.exe, the game will simply not start.  Rename a copied file instead.
If you want to play online again, rename start_protected_game-old.exe back to start_protected_game.exe.  If you're using Steam, just ask it to verify files and it'll do the same thing.
After doing this, the game will always start in offline mode where you can use mods safely.  I'm using ERSS-FG along with Decky Framegen and Decky VK-LSFG on the Steam Deck.

**The Ultimate Elden Ring Ultrawide Fix!**

This mod unlocks full ultrawide support for *Elden Ring and Shadow of the Erdtree*, providing everything you need for a smooth and immersive ultrawide experience. It removes black bars and addresses key graphical issues that normally affect ultrawide gameplay, and it’s fully compatible with the **Shadow of the Erdtree expansion!**

The mod includes:

* **Black bar removal:** Eliminates letterboxing in cutscenes and gameplay for true ultrawide aspect ratios (21:9, 32:9, and beyond).
* **FoV adjustment:** Expands the field of view to better suit ultrawide screens, making exploration and combat feel natural.
* **Remove Vignette: **Removes the vignette from the game, giving a clear ultrawide view.
* **Camera fix:** Adjusts the in-game camera to behave properly with wider displays.
* **Chromatic aberration removal:** Cleans up the image by removing the game's default chromatic aberration effect for sharper visuals.
* **Increased animation distance:** Ensures animations (e.g., distant enemies or NPCs) are loaded correctly outside the normal 16:9 view)
* **Elden Ring Mod Loader:** An easy-to-use mod loader to manage additional mods.
* **Easy Anti-Cheat (EAC) disabler/enabler:** Quickly toggle between offline (modded) and online (vanilla) play without manual file edits.

**Installation:**

1. Place the contents of "Elden Ring Ultrawide Fix.zip" inside your game folder. For example: C:\\SteamLibrary\\steamapps\\common\\ELDEN RING\\Game\\
2. Run "start\_game\_in\_offline\_mode.exe" in the game folder.

**Important Notes:**

* Fully compatible with the **Shadow of the Erdtree expansion**.
* Mods must be used offline when EAC is disabled. Do not attempt online play with mods active to avoid bans.
* Always back up your game files before installing.
* Compatible with the latest version of *Elden Ring* (check the changelog for updates).

**Q&A

How do I change the FOV?**
Open the config.ini file in ELDEN RING\\Game\\mods\\AdjustTheFov, change the number and save the file. Make sure to restart the game to see the effect.
