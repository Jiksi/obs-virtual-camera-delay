[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $Path
)

$ErrorActionPreference = 'Stop'
$ArtifactRoot = (Resolve-Path -LiteralPath $Path).Path
$PluginRoot = Join-Path $ArtifactRoot 'obs-virtual-camera-delay'
$RequiredFiles = @(
    (Join-Path $PluginRoot 'bin/64bit/obs-virtual-camera-delay.dll'),
    (Join-Path $PluginRoot 'data/locale/en-US.ini')
)

$NestedArchives = @(Get-ChildItem -LiteralPath $ArtifactRoot -Recurse -File |
    Where-Object Extension -In '.zip', '.7z', '.rar')
if ($NestedArchives.Count -gt 0) {
    $Names = ($NestedArchives.FullName -join ', ')
    throw "Artifact requires another extraction because it contains an archive: $Names"
}

foreach ($RequiredFile in $RequiredFiles) {
    if (-not (Test-Path -LiteralPath $RequiredFile -PathType Leaf)) {
        throw "Artifact is missing required plugin file: $RequiredFile"
    }
}

Write-Host "Artifact layout is ready for single extraction: $ArtifactRoot"
