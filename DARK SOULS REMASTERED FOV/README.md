# DARK SOULS: REMASTERED-Any-Aspect-Ratio

This fix compensates for the game's fixed 16:9 vertical FOV, which crops the
image on 16:10, 3:2, and 4:3 displays. It adjusts FOV; it does not change the
game's render resolution.

## 1. Install

Extract the mod files into the folder containing `DarkSoulsRemastered.exe`.

## 2. SteamOS / Winlator

For Steam Deck / SteamOS, open the game's **Properties → General → Launch
Options** and use:

```text
WINEDLLOVERRIDES="dinput8=n,b" %command%
```

For Winlator / Wine, set the `WINEDLLOVERRIDES` environment variable to
`dinput8=n,b`.

## 3. FOV crop fix

`DSR_FovFix.ini` sets the vertical FOV compensation multiplier. The default
`1.10` is for 16:10 displays such as Steam Deck (1280x800). Other suggested
values:

| Aspect ratio | Multiplier |
| --- | ---: |
| 16:9 | 1.00 |
| 16:10 | 1.10 |
| 3:2 | 1.16 |
| 4:3 | 1.29 |

Edit `FovMultiplier` under `[Camera]` to choose a value. The mod clamps values
to the range `0.1`–`3.0`.
