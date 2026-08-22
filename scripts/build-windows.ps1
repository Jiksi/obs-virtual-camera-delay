[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSVersion -lt [Version]'7.2.0') {
    throw 'PowerShell 7.2 or newer is required.'
}

foreach ($command in @('git', 'cmake', 'curl.exe')) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "Required command '$command' was not found in PATH."
    }
}

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ProjectVersion = (Get-Content -LiteralPath (Join-Path $ProjectRoot 'VERSION') -Raw).Trim()
$BuildRoot = Join-Path $ProjectRoot '.build'
$Workspace = Join-Path $BuildRoot 'obs-plugintemplate'
$ReleaseRoot = Join-Path $ProjectRoot 'release'

New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null

if (-not (Test-Path (Join-Path $Workspace '.git'))) {
    Write-Host 'Cloning official OBS plugin template build infrastructure...'
    git clone --depth 1 https://github.com/obsproject/obs-plugintemplate.git $Workspace
} else {
    Write-Host 'Refreshing official OBS plugin template build infrastructure...'
    git -C $Workspace fetch origin master --depth 1
    git -C $Workspace reset --hard origin/master
}

Write-Host 'Preparing temporary plugin workspace...'
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $Workspace 'src')
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $Workspace 'data')
Copy-Item -Recurse -Force (Join-Path $ProjectRoot 'src') (Join-Path $Workspace 'src')
Copy-Item -Recurse -Force (Join-Path $ProjectRoot 'data') (Join-Path $Workspace 'data')

$buildSpecPath = Join-Path $Workspace 'buildspec.json'
$spec = Get-Content $buildSpecPath -Raw | ConvertFrom-Json
$spec.name = 'obs-virtual-camera-delay'
$spec.displayName = 'OBS Virtual Camera Delay'
$spec.version = $ProjectVersion
$spec.author = 'Jiksi'
$spec.website = 'https://github.com/Jiksi/obs-virtual-camera-delay'
$spec.email = 'noreply@example.com'

# Align with the OBS 32 Windows ABI used for local smoke testing.
$spec.dependencies.'obs-studio'.version = '32.2.2'
$spec.dependencies.'obs-studio'.hashes.'windows-x64' = 'f15f001f1fa526405318835f44f9910046502f496ebc3a30d5296a5018b831aa'
$spec.dependencies.prebuilt.version = '2026-07-15'
$spec.dependencies.prebuilt.hashes.'windows-x64' = '6f90e9598fa10cff5ad23cdcfae49b87868c07bf896b02cd464582b4ce2f2ba9'
$spec.dependencies.qt6.version = '2026-07-15'
$spec.dependencies.qt6.hashes.'windows-x64' = '7c7f985711d80467bdc1795b6592275a27d5b0e5a2c7a61db1f2c1d08d6a5579'
$spec.dependencies.qt6.debugSymbols.'windows-x64' = '471d0b2191c424a520a51d064f3741084f117e1ff6ee5af1d16aabcdbacc6659'
$spec | ConvertTo-Json -Depth 20 | Set-Content -Encoding UTF8 $buildSpecPath

. (Join-Path $PSScriptRoot 'dependency-download.ps1')
$dependenciesRoot = Join-Path $Workspace '.deps'
$prebuilt = $spec.dependencies.prebuilt
$qt6 = $spec.dependencies.qt6
$obsStudio = $spec.dependencies.'obs-studio'

$downloads = @(
    @{ Uri = "$($prebuilt.baseUrl)/$($prebuilt.version)/windows-deps-$($prebuilt.version)-x64.zip"; Destination = Join-Path $dependenciesRoot "windows-deps-$($prebuilt.version)-x64.zip"; Sha256 = $prebuilt.hashes.'windows-x64' },
    @{ Uri = "$($qt6.baseUrl)/$($qt6.version)/windows-deps-qt6-$($qt6.version)-x64.zip"; Destination = Join-Path $dependenciesRoot "windows-deps-qt6-$($qt6.version)-x64.zip"; Sha256 = $qt6.hashes.'windows-x64' },
    @{ Uri = "$($obsStudio.baseUrl)/$($obsStudio.version).zip"; Destination = Join-Path $dependenciesRoot "$($obsStudio.version).zip"; Sha256 = $obsStudio.hashes.'windows-x64' }
)
foreach ($download in $downloads) { Get-VerifiedDownload @download }

$cmake = @'
cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)
project(${_name} VERSION ${_version} LANGUAGES CXX)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)
find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs OBS::obs-frontend-api)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/delayed-virtual-camera-output.cpp
    src/delayed-virtual-camera-output.hpp
    src/frame-buffer.cpp
    src/frame-buffer.hpp
    src/virtual-camera-delay.cpp
    src/virtual-camera-delay.hpp
    src/virtual-camera-delay-controller.cpp
    src/virtual-camera-delay-controller.hpp
)

target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE PLUGIN_VERSION="${_version}")
set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
'@
Set-Content -Encoding UTF8 (Join-Path $Workspace 'CMakeLists.txt') $cmake

Write-Host 'Running deterministic core tests...'
$testBuild = Join-Path $BuildRoot 'tests'
cmake -S (Join-Path $ProjectRoot 'tests') -B $testBuild
if ($LASTEXITCODE -ne 0) { throw "Test configure failed with exit code $LASTEXITCODE." }
cmake --build $testBuild --config $Configuration
if ($LASTEXITCODE -ne 0) { throw "Test build failed with exit code $LASTEXITCODE." }
ctest --test-dir $testBuild -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed with exit code $LASTEXITCODE." }

Remove-Item -Recurse -Force -ErrorAction SilentlyContinue (Join-Path $Workspace 'build_x64')

$previousCI = $env:CI
$env:CI = '1'
try {
    & (Join-Path $Workspace '.github/scripts/Build-Windows.ps1') -Target x64 -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { throw "OBS build helper failed with exit code $LASTEXITCODE." }
} finally {
    $env:CI = $previousCI
}

$workspaceRelease = Join-Path $Workspace "release/$Configuration"
if (-not (Test-Path $workspaceRelease)) {
    throw "Build completed but release output was not found at $workspaceRelease"
}

$targetRelease = Join-Path $ReleaseRoot $Configuration
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $targetRelease
New-Item -ItemType Directory -Force -Path $ReleaseRoot | Out-Null
Copy-Item -Recurse -Force $workspaceRelease $targetRelease

Write-Host ''
Write-Host 'Build complete.'
Write-Host "Plugin package: $targetRelease"
Write-Host 'The DLL should be under obs-virtual-camera-delay/bin/64bit.'
