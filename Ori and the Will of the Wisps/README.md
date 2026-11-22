# Ori-and-the-Will-of-the-Wisps-Any-Aspect-Ratio v1.0.0

[![GitHub](https://img.shields.io/badge/GitHub-TLin--Y-181717?style=for-the-badge&logo=github&logoColor=white)](https://github.com/TLin-Y/SteamAnyAspectRatioMods) [![Support on Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/lin66961)


### Installation
1. Place `version.dll`, `OriARFix.asi`, and `OriARFix.ini` into the game's root folder (where `oriandthewillowofthewisps.exe` is located).
2. **Linux / Android Winlator / Proton Users:**
   * **Winlator / Armada:** Container Settings -> Environment Variables -> Name slot `WINEDLLOVERRIDES`, Value slot `version=n,b`
   * **Steam (Proton):** Add `WINEDLLOVERRIDES="version=n,b" %command%` to your game's Launch Options.

### Configuration (`OriARFix.ini`)
```ini
[AspectRatio]
TargetWidth = 4
TargetHeight = 3

[FOV]
; true = maintain original width
; false = maintain original height
MaintainFOV = true
; Scaling fov, 1.15x == zoom in for 4:3
FOVScale = 1.0
; 2D UI zoom, 0 == auto calc
UIScale = 0
