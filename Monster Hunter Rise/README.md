Why?
- REFramework currently blows up on ARM64EC containers. It crashes straight away once the x86 instrumentation gets dispatched.

1. Install
   
Extract the mod files directly into your Monster Hunter Rise root folder (where MonsterHunterRise.exe is located).

2. For SteamOS / Winlator (Wine Users)
   
If you are playing on Linux (Proton/SteamOS) or Android (Winlator/Wine), you must add the following environment variable to your game or container settings to load the mod:
Name: WINEDLLOVERRIDES
Value: dinput8=n,b

3. Custom Aspect Ratio
   
Open MHRiseFix.ini with any text editor. You can easily change TargetWidth and TargetHeight to match any screen ratio you want (e.g., 4:3, 16:10, 21:9).

4. Full-Screen Stretching
   
Set your display scaling to "Stretch" in your GameNative settings. For RP Nova, If your container resolution is set to 1280x720, this will perfectly stretch the 4:3 rendered image to fill your entire screen.
