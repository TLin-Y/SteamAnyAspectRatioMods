# Ori-and-the-Blind-Forest-Definitive-Edition-Force-Any-Aspect-Ratio
# V2 maintain FOV (released as v1.0.0)
I've recompiled Assembly-CSharp.dll to unlock all built-in aspect ratio limits. 
It now automatically detects and matches your current display resolution—no extra setup required. 
Tested and confirmed working on everything from 32:9 to 1:1 ratios.
## Installation
PC / Steam Deck: Copy and replace the SteamLibrary\steamapps\common\Ori DE\oriDE_Data\Managed\Assembly-CSharp.dll. The V2 managed-assembly replacement does not require a Steam launch-option DLL override.

Wine: In GameNative container: select your A: disk, replace the oriDE_Data\Managed\Assembly-CSharp.dll


# Optional: Force Any Resolution

## Installation
1. Unzip [ForceWindowSize-v1.0.0.zip](./ForceWindowSize-v1.0.0.zip) directly into your game's root folder (where `oriDE.exe` is located).
2. Linux / Android Winlator / Proton Users
*   **Winlator or Armada:** Go to Container Settings (or Armada Settings) -> Environment Variables -> Add: `WINEDLLOVERRIDES="winmm=n,b"`
*   **Steam (Proton):** Add `WINEDLLOVERRIDES="winmm=n,b" %command%` to your game's Launch Options.

You can customize the target resolution by editing the `Ori1_ARFix.ini` file located in your game folder:

```ini
[Resolution]
Width = 2560
Height = 800
```
