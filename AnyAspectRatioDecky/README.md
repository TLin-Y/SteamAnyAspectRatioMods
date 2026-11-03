# AnyAspectRatio for Decky

Decky Loader plugin that installs the aspect-ratio fixes from this repository into your Steam games.

## Install

Requires a Steam Deck (or SteamOS) with [Decky Loader](https://decky.xyz).

1. Download `AnyAspectRatio-1.1.0.zip` from the [latest release](https://github.com/TLin-Y/SteamAnyAspectRatioMods/releases/latest).
2. Put the zip on the Deck (for example in `Downloads`).
3. In Game Mode, open Decky (`...` button), then **Settings → General** and turn on **Developer mode**.
4. Open **Settings → Developer → Install Plugin from ZIP**, pick the zip, and confirm.
5. Open **AnyAspectRatio** in the Decky menu and use **Scan Steam libraries**.

To update, install the newer zip the same way. To remove, uninstall from the Decky plugin list.

## Tabs

**Games**: scans your Steam libraries and lists games that have a matching mod. Apply or remove a mod per game; the launch option is set for you when needed. Files are copied independently of launch options.

**Mods**: credits for every mod, grouped by author and sorted by mod count (tap an author to open their page). Also manages the offline pack:
- **Download all for offline**: fetches every mod and the catalog into local storage, so nothing is downloaded again.
- **Export pack...**: saves the pack as one zip you can share.
- **Import pack...**: loads an offline pack zip, fully offline. The picker opens in the Deck's `Downloads` folder. To use this repository's mods offline, download the whole repository as a zip (GitHub **Code → Download ZIP**, or the `main.zip` archive link) into the Deck's `Downloads` folder, then pick it here. Only the catalog, mods and READMEs are kept; source code is ignored.

**Settings**: choose where the catalog and mods come from (default: this repository), an optional local folder of `*.zip` mods, and **Update** to re-fetch the catalog and mods after the repository changes. Cache, config and logs live in the plugin's local data folder.

## License

MIT, see [LICENSE](../LICENSE). Third-party notices: [THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md).
