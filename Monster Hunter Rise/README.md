Why?
- REFramework currently blows up on ARM64EC containers. It crashes straight away once the x86 instrumentation gets dispatched.

1. Install
   
Extract the mod files directly into your Monster Hunter Rise root folder (where MonsterHunterRise.exe is located).

2. For SteamOS / Winlator (Wine Users)
   
For Steam Deck / SteamOS, open the game's Properties, then General > Launch Options, and use:
`WINEDLLOVERRIDES="dinput8=n,b" %command%`

For Winlator / Wine, add the following environment variable to the game or container settings:
Name: WINEDLLOVERRIDES
Value: dinput8=n,b

3. Custom Aspect Ratio
   
Open MHRiseFix.ini with any text editor. You can easily change TargetWidth and TargetHeight to match any screen ratio you want (e.g., 4:3, 16:10, 21:9).

4. Full-Screen Stretching
   
Set your display scaling to "Stretch" in your GameNative settings. The mod defaults to 16:10 (1280x800, Steam Deck). If your container resolution is set to 1280x800, no stretching is needed; for other ratios, stretch the rendered image to fill your screen.
