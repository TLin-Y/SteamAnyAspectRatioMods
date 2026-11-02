# AnyAspectRatio ASI Compiler

This reusable Windows/x64 ASI build template is based on the clean source layout of [Lyall's Sky1stChapterFix](https://codeberg.org/Lyall/Sky1stChapterFix), using upstream commit `5892a9f0d70ff2ac6fcc0058d3859277bdb3de1c`.

The included `dllmain.cpp` and `ExampleFix.ini` are the original game-specific Sky1stChapterFix sample. Replace the sample `dllmain.cpp` with the target game's implementation before building. No per-game local worktree modifications are included.

## One-time setup

Install Git, CMake, xmake, and the Visual Studio C++ build tools. From this directory, fetch the pinned build dependencies once:

```powershell
.\setup-dependencies.ps1
```

Dependency checkouts and build products are local-only and ignored by Git.

## Build another game's ASI

1. Replace `dllmain.cpp` with the target game's implementation. Keep the shared headers and version resource unless the implementation requires additional dependencies.
2. Set `project_name` and the version fields in `build_config.json`. The project name controls the `.asi` output name and the `sFixName` supplied by `version.h`; name the game's runtime INI `<project_name>.ini` as well.
3. Run xmake:

   ```powershell
   xmake f -p windows -a x64 -m release
   xmake
   ```

The output is written to `build\windows\x64\release\<project_name>.asi`. The included sample uses `ExampleFix.ini`; when changing to another game's `dllmain.cpp`, also provide that implementation's matching INI file when it expects one.

## Source and license

The shared helper, logging/configuration pattern, and sample source are derived from Lyall's [Sky1stChapterFix](https://codeberg.org/Lyall/Sky1stChapterFix), licensed under the MIT License. See [LICENSE](./LICENSE) for the original copyright and license notice. Generated plugin version metadata identifies AnyAspectRatio as the publisher; the project name and semantic version come from `build_config.json`. These metadata identify this compiler/project and do not replace the required attribution for the derived source.
