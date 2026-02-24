# IntelEngine DLL Build Script
# Configures (if needed) and builds the IntelEngine SKSE DLL using VS 2022 + Ninja
#
# Usage:
#   .\BuildDLL.ps1              # Build only (incremental)
#   .\BuildDLL.ps1 -Clean       # Delete build dir and reconfigure from scratch
#   .\BuildDLL.ps1 -Configure   # Force reconfigure without cleaning

param(
    [switch]$Clean,
    [switch]$Configure,
    [int]$Threads = 16
)

$ErrorActionPreference = "Stop"
$SKSEDir = $PSScriptRoot

# ── VS 2022 Dev Shell ──
$vsPath = "C:\Program Files\Microsoft Visual Studio\2022\Community"
if (!(Test-Path "$vsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll")) {
    Write-Host "ERROR: VS 2022 Community not found at $vsPath" -ForegroundColor Red
    exit 1
}
Import-Module "$vsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vsPath -Arch amd64 -SkipAutomaticLocation | Out-Null

# ── Paths ──
$buildDir = "$SKSEDir\build"
$vcpkgToolchain = "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
$overlayTriplets = "E:/Tools/spookys-automod-toolkit/build-config/skyrimnet-triplets"

# ── Clean ──
if ($Clean -and (Test-Path $buildDir)) {
    Write-Host "Cleaning build directory..."
    Remove-Item -Recurse -Force $buildDir
}

# ── Configure (if needed or forced) ──
$needsConfigure = $Clean -or $Configure -or !(Test-Path "$buildDir\build.ninja")
if ($needsConfigure) {
    Write-Host "Configuring CMake..."
    cmake -B $buildDir -S $SKSEDir `
        -G Ninja `
        -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain" `
        -DVCPKG_TARGET_TRIPLET=x64-windows-static `
        "-DVCPKG_OVERLAY_TRIPLETS=$overlayTriplets" `
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: CMake configure failed" -ForegroundColor Red
        exit 1
    }
}

# ── Build ──
Write-Host "Building IntelEngine DLL ($Threads threads)..."
cmake --build $buildDir --config Release -- "-j$Threads"
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build failed" -ForegroundColor Red
    exit 1
}

# ── Copy to Release subfolder (where build.ps1 expects it) ──
$outputDll = "$buildDir\IntelEngine.dll"
$releaseDir = "$buildDir\Release"
if (Test-Path $outputDll) {
    if (!(Test-Path $releaseDir)) { New-Item -ItemType Directory -Path $releaseDir | Out-Null }
    Copy-Item $outputDll "$releaseDir\IntelEngine.dll" -Force
    $size = (Get-Item "$releaseDir\IntelEngine.dll").Length
    Write-Host "SUCCESS: IntelEngine.dll ($size bytes) -> $releaseDir" -ForegroundColor Green
} else {
    Write-Host "ERROR: Build output not found at $outputDll" -ForegroundColor Red
    exit 1
}
