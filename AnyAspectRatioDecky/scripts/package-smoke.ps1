$ErrorActionPreference = "Stop"

$pluginRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$repositoryRoot = (Resolve-Path (Join-Path $pluginRoot "..")).Path
$distPath = Join-Path $pluginRoot "dist"
if (-not (Test-Path (Join-Path $distPath "index.js"))) {
    throw "Frontend bundle not found. Run 'pnpm build' before packaging."
}

$requiredFiles = @(
    "main.py",
    "package.json",
    "plugin.json",
    "README.md",
    "THIRD_PARTY_NOTICES.md",
    "LICENSE-Decky-Plugin-Template.txt",
    "LICENSE-LGPL-2.1.txt"
)
foreach ($file in $requiredFiles) {
    if (-not (Test-Path (Join-Path $pluginRoot $file))) {
        throw "Required plugin file is missing: $file"
    }
}

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outputRoot = Join-Path $pluginRoot "out"
$stageRoot = Join-Path $outputRoot "smoke-$timestamp"
$packageRoot = Join-Path $stageRoot "AnyAspectRatio"
$archivePath = Join-Path $outputRoot "AnyAspectRatio-smoke-$timestamp.zip"

New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
try {
    Copy-Item -Path $distPath -Destination $packageRoot -Recurse
    foreach ($file in $requiredFiles) {
        Copy-Item -Path (Join-Path $pluginRoot $file) -Destination $packageRoot
    }
    Copy-Item -Path (Join-Path $repositoryRoot "LICENSE") -Destination $packageRoot

    Compress-Archive -Path $packageRoot -DestinationPath $archivePath -CompressionLevel Optimal
    Write-Output "Smoke package created: $archivePath"
}
finally {
    if (Test-Path -LiteralPath $stageRoot) {
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }
}
