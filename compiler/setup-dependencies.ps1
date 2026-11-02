$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dependencies = @(
    @{
        Name = "inipp"
        Url = "https://github.com/mcmtroffaes/inipp.git"
        Commit = "3f224f1eed7a67d5d7e5fc8cab72de02a056b966"
    },
    @{
        Name = "safetyhook"
        Url = "https://github.com/cursey/safetyhook.git"
        Commit = "983ba5c4b72866c8ed5020b6d57b7108fd1622c2"
    },
    @{
        Name = "spdlog"
        Url = "https://github.com/gabime/spdlog.git"
        Commit = "486b55554f11c9cccc913e11a87085b2a91f706f"
    },
    @{
        Name = "zydis"
        Url = "https://github.com/zyantific/zydis.git"
        Commit = "04793b1c8e3c8b4e6301f3c45a08d9103e417f55"
    }
)

$external = Join-Path $root "external"
New-Item -ItemType Directory -Force -Path $external | Out-Null

foreach ($dependency in $dependencies) {
    $path = Join-Path $external $dependency.Name

    if (Test-Path $path) {
        if (-not (Test-Path (Join-Path $path ".git"))) {
            throw "Refusing to overwrite existing non-Git dependency path: $path"
        }

        $origin = (& git -C $path remote get-url origin).Trim()
        if ($LASTEXITCODE -ne 0 -or $origin -ne $dependency.Url) {
            throw "Unexpected Git origin in dependency directory: $path"
        }

        $changes = & git -C $path status --porcelain
        if ($LASTEXITCODE -ne 0) {
            throw "Could not inspect dependency worktree: $path"
        }
        if ($changes) {
            throw "Dependency has local changes; leaving it untouched: $path"
        }

        $currentCommit = (& git -C $path rev-parse HEAD).Trim()
        if ($LASTEXITCODE -ne 0) {
            throw "Could not inspect dependency revision: $path"
        }
        if ($currentCommit -ne $dependency.Commit) {
            & git -C $path fetch origin $dependency.Commit
            if ($LASTEXITCODE -ne 0) {
                throw "Could not fetch pinned revision for $($dependency.Name)."
            }
            & git -C $path checkout --detach $dependency.Commit
            if ($LASTEXITCODE -ne 0) {
                throw "Could not check out pinned revision for $($dependency.Name)."
            }
        }
    }
    else {
        & git clone $dependency.Url $path
        if ($LASTEXITCODE -ne 0) {
            throw "Could not clone dependency $($dependency.Name)."
        }
        & git -C $path checkout --detach $dependency.Commit
        if ($LASTEXITCODE -ne 0) {
            throw "Could not check out pinned revision for $($dependency.Name)."
        }
    }

    Write-Output "$($dependency.Name): $($dependency.Commit)"
}

$zydisPath = Join-Path $external "zydis"
& git -C $zydisPath submodule update --init --recursive
if ($LASTEXITCODE -ne 0) {
    throw "Could not initialize Zydis submodules."
}
