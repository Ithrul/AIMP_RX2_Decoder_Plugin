param(
    [switch]$AllowMissingX86,
    [switch]$AllowMissingX64
)

$root         = Split-Path -Parent $PSScriptRoot
$packageRoot  = Join-Path $root "out/package/aimp_rx2_plugin"
$dllX86       = Join-Path $packageRoot "aimp_rx2_plugin.dll"
$dllX64       = Join-Path $packageRoot "x64/aimp_rx2_plugin.dll"
$zipPath      = Join-Path $root "out/package/aimp_rx2_plugin.zip"

if (-not (Test-Path $packageRoot)) {
    Write-Error "Package folder '$packageRoot' not found. Build the plugin first."
    exit 1
}

$hasX86 = Test-Path $dllX86
$hasX64 = Test-Path $dllX64

if (-not $hasX86 -and -not $AllowMissingX86) {
    Write-Warning "x86 plugin missing at '$dllX86'."
}
if (-not $hasX64 -and -not $AllowMissingX64) {
    Write-Warning "x64 plugin missing at '$dllX64'."
}
if (-not $hasX86 -and -not $hasX64) {
    Write-Error "No plugin binaries found; cannot package."
    exit 1
}

if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}

Push-Location (Join-Path $root "out/package")
try {
    Compress-Archive -Path "aimp_rx2_plugin" -DestinationPath $zipPath -Force
    Write-Host "Created package: $zipPath"
}
finally {
    Pop-Location
}
