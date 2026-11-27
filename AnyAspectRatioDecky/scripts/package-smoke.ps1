param(
    [string]$Version = "",
    [switch]$SkipSteamCheck
)

$ErrorActionPreference = "Stop"

$pluginRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$repositoryRoot = (Resolve-Path (Join-Path $pluginRoot "..")).Path
$distPath = Join-Path $pluginRoot "dist"
if (-not (Test-Path (Join-Path $distPath "index.js"))) {
    throw "Frontend bundle not found. Run 'pnpm build' before packaging."
}

if ($SkipSteamCheck) {
    if ($Version) {
        throw "-SkipSteamCheck is not allowed for a release package (-Version $Version)."
    }
    Write-Warning "Skipping the Steam metadata check; catalog install_dir/executable are unverified."
}
else {
    & python (Join-Path $PSScriptRoot "verify_catalog_steam.py")
    $steamCheckExitCode = $LASTEXITCODE
    if ($steamCheckExitCode -eq 2) {
        throw "Steam metadata check could not reach Steam. Retry online, or pass -SkipSteamCheck for a local smoke build."
    }
    if ($steamCheckExitCode -ne 0) {
        throw "Catalog does not match Steam metadata; fix catalog.json or scripts\steam_catalog_exceptions.json."
    }
    & python (Join-Path $PSScriptRoot "verify_author_pages.py")
    $pagesExitCode = $LASTEXITCODE
    if ($pagesExitCode -eq 3) {
        Write-Warning "A mod site refused the automated check; open the pages marked 'blocked' above by hand."
    }
    elseif ($pagesExitCode -eq 2) {
        throw "Author page check could not reach a mod site. Retry online."
    }
    elseif ($pagesExitCode -ne 0) {
        throw "An author-download page is gone; update or remove its catalog entry."
    }
}

$requiredFiles = @(
    "main.py",
    "package.json",
    "plugin.json",
    "catalog.json",
    "README.md",
    "THIRD_PARTY_NOTICES.md",
    "THIRD_PARTY_LICENSES.txt",
    "LICENSE-Decky-Plugin-Template.txt",
    "LICENSE-LGPL-2.1.txt"
)
$runtimeFiles = @(
    "helper\__init__.py",
    "helper\build_flavor.py",
    "helper\errors.py",
    "helper\validated_types.py",
    "helper\validator.py",
    "installer\__init__.py",
    "installer\apply.py",
    "installer\backups.py",
    "installer\game_folder.py",
    "installer\launcher.py",
    "installer\mod_ini.py",
    "installer\mod_logs.py",
    "installer\plugin_updater.py",
    "installer\remove.py",
    "installer\staging.py",
    "logger\__init__.py",
    "logger\plugin_logger.py",
    "scanner\__init__.py",
    "scanner\author_downloads.py",
    "scanner\local_archives.py",
    "scanner\mod_catalog.py",
    "scanner\non_steam_shortcuts.py",
    "scanner\steam_layout.py",
    "scanner\steam_library_scanner.py",
    "scanner\supported_game_scanner.py",
    "store\__init__.py",
    "store\dependencies.py",
    "store\mod_sources.py",
    "store\offline_pack.py",
    "store\plugin_store.py",
    "utils\__init__.py",
    "utils\ini.py",
    "utils\sizes.py",
    "utils\steam_vdf.py",
    "utils\streams.py",
    "utils\text_files.py",
    "utils\files.py",
    "utils\folders.py",
    "utils\json_codec.py",
    "utils\network.py",
    "utils\retry.py",
    "utils\zips.py"
)
foreach ($file in $requiredFiles) {
    if (-not (Test-Path (Join-Path $pluginRoot $file))) {
        throw "Required plugin file is missing: $file"
    }
}
foreach ($file in $runtimeFiles) {
    if (-not (Test-Path (Join-Path $pluginRoot $file) -PathType Leaf)) {
        throw "Required runtime module is missing: $file"
    }
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outputRoot = Join-Path $pluginRoot "out"
$stageRoot = Join-Path $outputRoot "smoke-$timestamp"
$packageRoot = Join-Path $stageRoot "AnyAspectRatio"
$archiveName = if ($Version) { "AnyAspectRatio-$Version.zip" } else { "AnyAspectRatio-smoke-$timestamp.zip" }
$archivePath = Join-Path $outputRoot $archiveName

New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
try {
    Copy-Item -Path $distPath -Destination $packageRoot -Recurse
    foreach ($file in $requiredFiles) {
        Copy-Item -Path (Join-Path $pluginRoot $file) -Destination $packageRoot
    }
    foreach ($file in $runtimeFiles) {
        $sourcePath = Join-Path $pluginRoot $file
        $destinationPath = Join-Path $packageRoot $file
        $destinationDirectory = Split-Path -Parent $destinationPath
        New-Item -ItemType Directory -Force -Path $destinationDirectory | Out-Null
        Copy-Item -LiteralPath $sourcePath -Destination $destinationPath
    }
    Copy-Item -Path (Join-Path $repositoryRoot "LICENSE") -Destination $packageRoot

    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::Open(
        $archivePath,
        [System.IO.Compression.ZipArchiveMode]::Create
    )
    try {
        $rootPrefix = $packageRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) +
            [System.IO.Path]::DirectorySeparatorChar
        foreach ($fileInfo in Get-ChildItem -LiteralPath $packageRoot -Recurse -File) {
            $relativePath = $fileInfo.FullName.Substring($rootPrefix.Length).Replace("\", "/")
            $entry = $archive.CreateEntry(
                "AnyAspectRatio/$relativePath",
                [System.IO.Compression.CompressionLevel]::Optimal
            )
            $source = [System.IO.File]::OpenRead($fileInfo.FullName)
            $destination = $entry.Open()
            try {
                $source.CopyTo($destination)
            }
            finally {
                $destination.Dispose()
                $source.Dispose()
            }
        }
    }
    finally {
        $archive.Dispose()
    }
    Write-Output "Smoke package created: $archivePath"
}
finally {
    if (Test-Path -LiteralPath $stageRoot) {
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }
}
