# Hades - Hephaistos Aspect Ratio Fix

Hephaistos patches Hades for a selected resolution, including ultrawide and
Steam Deck 16:10. The Linux release is included for SteamOS.

## Install

1. Apply this mod in AnyAspectRatio Decky. It automatically copies the
   `hephaistos` helper into the Hades install root, one level above
   `x64/Hades.exe` (alongside `Content`, `x64`, `x64Vk`, and `x86`), and
   installs these instructions as `Hephaistos-README.md` in the same folder.
   The Linux release archive in this repository includes these instructions.
2. Switch to Desktop Mode. Open `Hephaistos-README.md` in the Hades install
   root and open a terminal in that folder.
3. Make the helper executable and start it:

   ```sh
   chmod +x ./hephaistos
   ./hephaistos
   ```

4. Choose **Patch Hades**, then enter the desired display resolution when
   prompted. Start Hades from Steam after patching.

Hephaistos patches game files in place and stores originals and patch data in
`hephaistos-data` inside the Hades folder. The game may need to be patched
again after a game update.

Decky's Apply/Remove only copies or deletes the helper; it does not run
Hephaistos' patch/restore operations.

## Restore and remove

Run `./hephaistos` again and choose **Restore Hades** before removing this mod
in Decky. This restores the backed-up game files; Decky's Remove then deletes
the helper.

## Notes

Hephaistos expands the gameplay viewport, but static artwork and many menus
remain centered at 16:9. See the [upstream README](https://github.com/nbusseneau/hephaistos)
for all options and troubleshooting.

By Nicolas Busseneau: https://github.com/nbusseneau/hephaistos
