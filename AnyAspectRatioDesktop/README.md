# AnyAspectRatio Desktop (Windows, in progress)

Desktop version of the AnyAspectRatio Decky plugin. It runs the plugin's Python backend from `..\AnyAspectRatioDecky` unchanged; only the host differs.

- Data, cache and log: `%LOCALAPPDATA%\AnyAspectRatio`
- Steam is found through the Windows registry (`SteamPath`) or `Program Files (x86)\Steam`.
- No launch options are needed on Windows: games load the ASI loader DLL from their own folder.

## Window app

The window shows the plugin's own React views (`..\AnyAspectRatioDecky\src`); `web\src\decky-ui.tsx` and `web\src\decky-api.ts` stand in for Decky's `@decky/ui` and `@decky/api`.

```powershell
pip install pywebview==6.2.1
cd web; npm install; node build.mjs; npx tsc -p .; cd ..
python app.py            # --debug opens the browser dev tools
```

## Command line

```powershell
python cli.py scan
python cli.py update-catalog
python cli.py apply <mod_id>            # stops and lists files it would replace
python cli.py apply <mod_id> --force    # replaces them
python cli.py remove <mod_id>
```

## Tests

```powershell
python -m unittest discover -s test
```
