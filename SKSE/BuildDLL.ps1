# IntelEngine DLL Build Script
# Reconfigures safely and builds the IntelEngine SKSE DLL using VS 2022 + Ninja
#
# Usage:
#   .\BuildDLL.ps1                                      # Incremental build in SKSE/build
#   .\BuildDLL.ps1 -Clean                               # Delete default build dir and reconfigure
#   .\BuildDLL.ps1 -Threads 2                           # Override thread count (auto-capped by RAM)
#   .\BuildDLL.ps1 -BuildDir .\build-fresh -Threads 2   # Isolated build directory

param(
    [switch]$Clean,
    [int]$Threads = 0,
    [string]$BuildDir
)

$ErrorActionPreference = "Stop"
$SKSEDir = $PSScriptRoot

function Get-Sha256Hex {
    param([Parameter(Mandatory = $true)][string]$Path)

    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $sha256 = [System.Security.Cryptography.SHA256]::Create()
        try {
            return ([System.BitConverter]::ToString($sha256.ComputeHash($stream))).Replace("-", "")
        } finally {
            $sha256.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
}

# ── Safe Thread Calculation ──
$ramGB = [Math]::Round((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
$ramAvailGB = [Math]::Round((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB)
$cpuCores = [Environment]::ProcessorCount
$ramSafeThreads = [Math]::Max(1, [Math]::Floor(($ramAvailGB - 8) / 3))
$cpuSafeThreads = [Math]::Max(1, [Math]::Floor($cpuCores / 4))
if ($Threads -le 0) {
    $Threads = [Math]::Min($ramSafeThreads, $cpuSafeThreads)
} else {
    $Threads = [Math]::Min($Threads, $ramSafeThreads)
}
$Threads = [Math]::Max(1, [Math]::Min(4, $Threads))
Write-Host "System: ${ramGB} GB RAM total, ${ramAvailGB} GB available, ${cpuCores} cores" -ForegroundColor Cyan
Write-Host "Safe threads: $Threads (RAM-safe: $ramSafeThreads, CPU-safe: $cpuSafeThreads, max: 4)" -ForegroundColor Cyan

# ── Apply thread limit to vcpkg internal builds ──
$previousVcpkgConcurrency = $env:VCPKG_MAX_CONCURRENCY

$defaultBuildDir = Join-Path $SKSEDir "build"
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $buildDir = $defaultBuildDir
} else {
    $buildDir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDir)
}
$canonicalDll = Join-Path $defaultBuildDir "Release\IntelEngine.dll"
$vcpkgToolchain = "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"

# ── VS 2022 Dev Shell ──
$vsPath = "C:\Program Files\Microsoft Visual Studio\2022\Community"
if (!(Test-Path "$vsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll")) {
    throw "ERROR: VS 2022 Community not found at $vsPath"
}
Import-Module "$vsPath\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vsPath -Arch amd64 -SkipAutomaticLocation | Out-Null

# ── Lower priority and isolate configure/build from installed-mod destinations ──
$currentProcess = [System.Diagnostics.Process]::GetCurrentProcess()
$originalPriority = $currentProcess.PriorityClass
$previousSkyrimFolder = $env:SKYRIM_FOLDER
$previousSkyrimModsFolder = $env:SKYRIM_MODS_FOLDER
try {
    $env:VCPKG_MAX_CONCURRENCY = $Threads
    $currentProcess.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal
    Write-Host "Process priority: BelowNormal (OS stays responsive)" -ForegroundColor Cyan
    Remove-Item Env:SKYRIM_FOLDER -ErrorAction SilentlyContinue
    Remove-Item Env:SKYRIM_MODS_FOLDER -ErrorAction SilentlyContinue

    if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
        Write-Host "Cleaning build directory: $buildDir"
        Remove-Item -LiteralPath $buildDir -Recurse -Force
    }

    # Regenerate cached post-build commands with deployment variables unset.
    Write-Host "Configuring CMake in $buildDir..."
    cmake -B $buildDir -S $SKSEDir `
        -G Ninja `
        -DCMAKE_BUILD_TYPE=Release `
        "-DCMAKE_TOOLCHAIN_FILE=$vcpkgToolchain" `
        -DVCPKG_TARGET_TRIPLET=x64-windows-static `
        -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
    if ($LASTEXITCODE -ne 0) {
        throw "ERROR: CMake configure failed (exit code $LASTEXITCODE)"
    }

    Write-Host "Building IntelEngine DLL ($Threads threads, below-normal priority)..."
    cmake --build $buildDir --config Release -- "-j$Threads"
    $buildExitCode = $LASTEXITCODE
    if ($buildExitCode -ne 0) {
        throw "ERROR: Build failed (exit code $buildExitCode)"
    }

    $outputDll = Join-Path $buildDir "IntelEngine.dll"
    if (-not (Test-Path -LiteralPath $outputDll -PathType Leaf)) {
        throw "ERROR: Successful build did not produce expected DLL: $outputDll"
    }

    New-Item -ItemType Directory -Path (Split-Path $canonicalDll -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $outputDll -Destination $canonicalDll -Force

    $buildHash = Get-Sha256Hex -Path $outputDll
    $canonicalHash = Get-Sha256Hex -Path $canonicalDll
    if ($buildHash -ne $canonicalHash) {
        throw "ERROR: SHA-256 mismatch: fresh output $outputDll ($buildHash), canonical output $canonicalDll ($canonicalHash)"
    }

    $size = (Get-Item -LiteralPath $canonicalDll).Length
    Write-Host "SUCCESS: IntelEngine.dll ($size bytes); SHA-256 $buildHash" -ForegroundColor Green
    Write-Host "  Fresh output: $outputDll"
    Write-Host "  Canonical output: $canonicalDll"
} finally {
    $currentProcess.PriorityClass = $originalPriority
    if ($null -eq $previousSkyrimFolder) {
        Remove-Item Env:SKYRIM_FOLDER -ErrorAction SilentlyContinue
    } else {
        $env:SKYRIM_FOLDER = $previousSkyrimFolder
    }
    if ($null -eq $previousSkyrimModsFolder) {
        Remove-Item Env:SKYRIM_MODS_FOLDER -ErrorAction SilentlyContinue
    } else {
        $env:SKYRIM_MODS_FOLDER = $previousSkyrimModsFolder
    }
    if ($null -eq $previousVcpkgConcurrency) {
        Remove-Item Env:VCPKG_MAX_CONCURRENCY -ErrorAction SilentlyContinue
    } else {
        $env:VCPKG_MAX_CONCURRENCY = $previousVcpkgConcurrency
    }
}
