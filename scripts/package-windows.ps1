[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ProjectVersion = (Get-Content -LiteralPath (Join-Path $ProjectRoot 'VERSION') -Raw).Trim()
$ReleaseRoot = Join-Path $ProjectRoot "release/$Configuration"
$DistRoot = Join-Path $ProjectRoot 'dist'

if (-not (Test-Path $ReleaseRoot)) {
    throw "Release output not found at $ReleaseRoot. Run scripts/build-windows.ps1 first."
}

New-Item -ItemType Directory -Force -Path $DistRoot | Out-Null
$pluginRoot = Join-Path $ReleaseRoot 'obs-virtual-camera-delay'
$dll = Join-Path $pluginRoot 'bin/64bit/obs-virtual-camera-delay.dll'
$locale = Join-Path $pluginRoot 'data/locale/en-US.ini'
if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) {
    throw "Plugin DLL not found at expected install path: $dll"
}
if (-not (Test-Path -LiteralPath $locale -PathType Leaf)) {
    throw "Locale data not found at expected install path: $locale"
}

$zip = Join-Path $DistRoot "obs-virtual-camera-delay-v$ProjectVersion-windows-x64.zip"
Remove-Item -Force -ErrorAction SilentlyContinue $zip
Compress-Archive -Path (Join-Path $ReleaseRoot '*') -DestinationPath $zip -CompressionLevel Optimal
Write-Host "Package created: $zip"
