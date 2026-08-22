[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ReleaseRoot = Join-Path $ProjectRoot "release/$Configuration"
$DistRoot = Join-Path $ProjectRoot 'dist'

if (-not (Test-Path $ReleaseRoot)) {
    throw "Release output not found at $ReleaseRoot. Run scripts/build-windows.ps1 first."
}

New-Item -ItemType Directory -Force -Path $DistRoot | Out-Null
$zip = Join-Path $DistRoot "obs-virtual-camera-delay-$Configuration-windows-x64.zip"
Remove-Item -Force -ErrorAction SilentlyContinue $zip
Compress-Archive -Path (Join-Path $ReleaseRoot '*') -DestinationPath $zip -CompressionLevel Optimal
Write-Host "Package created: $zip"
