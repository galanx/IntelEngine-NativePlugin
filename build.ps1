# IntelEngine Build & Deploy Script
# Syncs headers, compiles Papyrus scripts, syncs SKSE assets to Data, and
# deploys the entire Data folder to Testing + CK via robocopy.
#
# Usage:
#   .\build.ps1                    # Build all scripts + deploy everything
#   .\build.ps1 -Scripts Travel    # Build only Travel script + deploy
#   .\build.ps1 -Scripts Travel,MCM,Schedule
#   .\build.ps1 -DeployOnly        # Skip compilation, just sync + deploy
#   .\build.ps1 -SkipDeploy        # Compile only, don't deploy to Testing/CK
#   .\build.ps1 -SkipGit           # Build + deploy but don't commit/push to GitHub

param(
    [string[]]$Scripts,
    [switch]$DeployOnly,
    [switch]$SkipDeploy,
    [switch]$SkipGit
)

$ErrorActionPreference = "Stop"

$Root = "E:\Tools\spookys-automod-toolkit"
$ModRoot = "$Root\Mods\IntelEngine"
$SourceDir = "$ModRoot\Source\Scripts"
$HeadersDir = "$Root\skyrim-script-headers"
$DataDir = "$ModRoot\Data"
$OutputDir = "$DataDir\Scripts"
$SKSEDir = "$ModRoot\SKSE"
$TestDest = "E:\Modding\Lorerim\mods\Galanx_IntelEngine"
$CKDest = "D:\SkyrimVR-MO2\mods\Galanx_IntelEngine"

# =============================================================================
# Step 1: Sync ALL IntelEngine PSC files to headers folder
# =============================================================================
Write-Host "--- Syncing headers ---" -ForegroundColor Cyan
$synced = 0
Get-ChildItem "$SourceDir\IntelEngine*.psc" | ForEach-Object {
    Copy-Item $_.FullName "$HeadersDir\$($_.Name)" -Force
    $synced++
}
Write-Host "  $synced PSC files synced to headers"

# =============================================================================
# Step 2: Compile Papyrus scripts (unless -DeployOnly)
# =============================================================================
if (-not $DeployOnly) {
    $allScripts = @(
        "IntelEngine",
        "IntelEngine_Core",
        "IntelEngine_MCM",
        "IntelEngine_PlayerAlias",
        "IntelEngine_Travel",
        "IntelEngine_NPCTasks",
        "IntelEngine_Schedule"
    )

    if ($Scripts -and $Scripts.Count -gt 0) {
        # Prefix with IntelEngine_ if not already
        $toBuild = $Scripts | ForEach-Object {
            if ($_ -eq "IntelEngine" -or $_.StartsWith("IntelEngine_")) { $_ }
            else { "IntelEngine_$_" }
        }
    } else {
        $toBuild = $allScripts
    }

    Write-Host "`n--- Compiling ---" -ForegroundColor Cyan
    $failed = @()
    foreach ($script in $toBuild) {
        $psc = "$SourceDir\$script.psc"
        if (-not (Test-Path $psc)) {
            Write-Host "  SKIP: $script.psc not found" -ForegroundColor Yellow
            continue
        }

        Write-Host "  Compiling $script..." -NoNewline
        $result = & dotnet run --project "$Root\src\SpookysAutomod.Cli" -- papyrus compile $psc --output $OutputDir --headers $HeadersDir --json 2>&1 | Out-String

        if ($result -match '"success": true') {
            Write-Host " OK" -ForegroundColor Green
        } else {
            Write-Host " FAILED" -ForegroundColor Red
            $failed += $script
            if ($result -match '"errorContext": "(.*?)"') {
                $Matches[1] -split '\\r\\n' | Where-Object { $_ -match 'error|Error' } | ForEach-Object {
                    Write-Host "    $_" -ForegroundColor Red
                }
            }
        }
    }

    if ($failed.Count -gt 0) {
        Write-Host "`nBuild FAILED for: $($failed -join ', ')" -ForegroundColor Red
        exit 1
    }
}

# =============================================================================
# Step 3: Sync SKSE assets → Data folder
# =============================================================================
Write-Host "`n--- Syncing SKSE assets to Data ---" -ForegroundColor Cyan

# Sync DLL (prefer build output, fall back to SKSE\Plugins)
$dllBuild = "$SKSEDir\build\Release\IntelEngine.dll"
$dllFallback = "$SKSEDir\Plugins\IntelEngine.dll"
$dllDest = "$DataDir\SKSE\Plugins\IntelEngine.dll"
if (Test-Path $dllBuild) {
    Copy-Item $dllBuild $dllDest -Force
    Write-Host "  DLL synced (from build output)"
} elseif (Test-Path $dllFallback) {
    Copy-Item $dllFallback $dllDest -Force
    Write-Host "  DLL synced (from SKSE\Plugins)"
} else {
    Write-Host "  WARN: DLL not found" -ForegroundColor Yellow
}

# Sync SkyrimNet prompts
$promptSrc = "$SKSEDir\Plugins\SkyrimNet\prompts"
$promptDest = "$DataDir\SKSE\Plugins\SkyrimNet\prompts"
if (Test-Path $promptSrc) {
    if (-not (Test-Path $promptDest)) {
        New-Item -ItemType Directory -Path $promptDest -Force | Out-Null
    }
    Copy-Item "$promptSrc\*" $promptDest -Recurse -Force
    $count = (Get-ChildItem $promptSrc -Recurse -File).Count
    Write-Host "  $count prompt files synced"
}

# Sync SkyrimNet config (actions)
$configSrc = "$SKSEDir\Plugins\SkyrimNet\config"
$configDest = "$DataDir\SKSE\Plugins\SkyrimNet\config"
if (Test-Path $configSrc) {
    if (-not (Test-Path $configDest)) {
        New-Item -ItemType Directory -Path $configDest -Force | Out-Null
    }
    Copy-Item "$configSrc\*" $configDest -Recurse -Force
    $count = (Get-ChildItem $configSrc -Recurse -File).Count
    Write-Host "  $count config files synced"
}

# Sync PSC source files to Data
$dataSourceDir = "$DataDir\Source\Scripts"
if (-not (Test-Path $dataSourceDir)) {
    New-Item -ItemType Directory -Path $dataSourceDir -Force | Out-Null
}
Get-ChildItem "$SourceDir\IntelEngine*.psc" | ForEach-Object {
    Copy-Item $_.FullName "$dataSourceDir\$($_.Name)" -Force
}
Write-Host "  Source scripts synced to Data"

# =============================================================================
# Step 4: Deploy Data folder → Testing + CK (unless -SkipDeploy)
# =============================================================================
if (-not $SkipDeploy) {
    Write-Host "`n--- Deploying Data to Testing + CK ---" -ForegroundColor Cyan

    $robocopyArgs = @("/E", "/IS", "/IT", "/NFL", "/NDL", "/NJH", "/NJS", "/R:1", "/W:1")

    Write-Host "  Copying to Testing..."
    & robocopy $DataDir $TestDest @robocopyArgs | Out-Null
    $fileCount = (Get-ChildItem $DataDir -Recurse -File).Count
    Write-Host "  -> $fileCount files deployed to Testing" -ForegroundColor Green

    Write-Host "  Copying to CK..."
    & robocopy $DataDir $CKDest @robocopyArgs | Out-Null
    Write-Host "  -> $fileCount files deployed to CK" -ForegroundColor Green
}

# =============================================================================
# Step 5: Verify deployment
# =============================================================================
Write-Host "`n--- Verifying deployment ---" -ForegroundColor Cyan
& "$ModRoot\verify.ps1" -Quiet
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n--- Build complete (VERIFICATION FAILED) ---" -ForegroundColor Red
    exit 1
}

# =============================================================================
# Step 6: Git commit + push to both repos (unless -SkipGit)
# =============================================================================
if (-not $SkipGit) {
    Write-Host "`n--- Pushing to Git ---" -ForegroundColor Cyan

    # Build a commit message from what was built
    if ($DeployOnly) {
        $commitMsg = "Deploy: sync assets"
    } elseif ($Scripts -and $Scripts.Count -gt 0) {
        $commitMsg = "Build: $($Scripts -join ', ')"
    } else {
        $commitMsg = "Build: all scripts"
    }

    # NativePlugin repo (Mods/IntelEngine/)
    Push-Location $ModRoot
    $nativeChanges = git status --porcelain 2>&1
    if ($nativeChanges) {
        git add -A
        git commit -m $commitMsg
        git push
        Write-Host "  NativePlugin pushed" -ForegroundColor Green
    } else {
        Write-Host "  NativePlugin: no changes" -ForegroundColor DarkGray
    }
    Pop-Location

    # GamePlugin repo (Mods/IntelEngine/Data/)
    Push-Location $DataDir
    $gameChanges = git status --porcelain 2>&1
    if ($gameChanges) {
        git add -A
        git commit -m $commitMsg
        git push
        Write-Host "  GamePlugin pushed" -ForegroundColor Green
    } else {
        Write-Host "  GamePlugin: no changes" -ForegroundColor DarkGray
    }
    Pop-Location
}

Write-Host "`n--- Build complete ---" -ForegroundColor Green
