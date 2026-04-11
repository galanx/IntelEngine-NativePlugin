# IntelEngine DLL Build Script
# Configures (if needed) and builds the IntelEngine SKSE DLL using VS 2022 + Ninja
#
# Usage:
#   .\BuildDLL.ps1              # Build only (incremental, auto-safe thread count)
#   .\BuildDLL.ps1 -Clean       # Delete build dir and reconfigure from scratch
#   .\BuildDLL.ps1 -Configure   # Force reconfigure without cleaning
#   .\BuildDLL.ps1 -Threads 8   # Override thread count (auto-capped by RAM)

param(
    [switch]$Clean,
    [switch]$Configure,
    [int]$Threads = 0
)

$ErrorActionPreference = "Stop"
$SKSEDir = $PSScriptRoot

# ── Safe Thread Calculation ──
# CommonLibSSE-NG template-heavy compilation uses ~2-3 GB RAM per cl.exe instance.
# Running too many in parallel exhausts RAM and freezes the system.
# Auto-calculate a safe thread count based on available memory, capped by CPU cores.

$ramGB = [Math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
$ramAvailGB = [Math]::Round((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB)
$cpuCores = [Environment]::ProcessorCount

# Reserve 8 GB for OS + other apps, allow ~3 GB per compiler instance from remaining RAM
# (CommonLibSSE-NG template-heavy headers can spike well above 2.5 GB per instance)
$ramSafeThreads = [Math]::Max(1, [Math]::Floor(($ramAvailGB - 8) / 3))
# Cap at quarter CPU cores to avoid thrashing (half was still too aggressive)
$cpuSafeThreads = [Math]::Max(1, [Math]::Floor($cpuCores / 4))

if ($Threads -le 0) {
    # Auto mode: pick the lower of RAM-safe and CPU-safe limits
    $Threads = [Math]::Min($ramSafeThreads, $cpuSafeThreads)
} else {
    # User override: still cap by available RAM to prevent freezes
    $Threads = [Math]::Min($Threads, $ramSafeThreads)
}

# Absolute bounds: at least 1, at most 4 (CommonLibSSE is too heavy for more)
$Threads = [Math]::Max(1, [Math]::Min(4, $Threads))

Write-Host "System: ${ramGB} GB RAM total, ${ramAvailGB} GB available, ${cpuCores} cores" -ForegroundColor Cyan
Write-Host "Safe threads: $Threads (RAM-safe: $ramSafeThreads, CPU-safe: $cpuSafeThreads, max: 8)" -ForegroundColor Cyan

# ── Apply thread limit to vcpkg internal builds ──
# vcpkg uses its own parallelism when building dependencies (e.g. CommonLibSSE-NG).
# Without this, vcpkg defaults to all cores (-j33 on this system) which crashes the PC.
$env:VCPKG_MAX_CONCURRENCY = $Threads

# ── Lower priority for ALL build steps (configure + build) ──
# vcpkg builds heavy template code during configure. Priority must be lowered BEFORE
# cmake configure, not just before the build step, so all cl.exe children are BelowNormal.
$currentProcess = [System.Diagnostics.Process]::GetCurrentProcess()
$originalPriority = $currentProcess.PriorityClass
$currentProcess.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal
Write-Host "Process priority: BelowNormal (OS stays responsive)" -ForegroundColor Cyan

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
Write-Host "Building IntelEngine DLL ($Threads threads, below-normal priority)..."
cmake --build $buildDir --config Release -- "-j$Threads"
$buildExitCode = $LASTEXITCODE

# Restore priority
$currentProcess.PriorityClass = $originalPriority

if ($buildExitCode -ne 0) {
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
