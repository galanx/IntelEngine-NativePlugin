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
$VanillaDest = "E:\Modding\VanillaTest\mods\Galanx_IntelEngine"
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
        "IntelEngine_Schedule",
        "IntelEngine_StoryEngine"
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
# Step 2b: Build Dashboard UI (React/webpack -> Data/PrismaUI/views/)
# =============================================================================
$dashboardDir = "$ModRoot\web\dashboard"
$dashboardDist = "$dashboardDir\dist"
$dashboardDest = "$DataDir\PrismaUI\views\IntelEngine\dashboard"
if (Test-Path "$dashboardDir\package.json") {
    # Check if npm is available (also check default install location for fresh installs)
    $npmCmd = Get-Command npm -ErrorAction SilentlyContinue
    if (-not $npmCmd -and (Test-Path "C:\Program Files\nodejs\npm.cmd")) {
        $env:PATH = "C:\Program Files\nodejs;" + $env:PATH
        $npmCmd = Get-Command npm -ErrorAction SilentlyContinue
    }
    if ($npmCmd) {
        # Install deps if needed (first time only)
        if (-not (Test-Path "$dashboardDir\node_modules")) {
            Write-Host "`n--- Installing Dashboard UI dependencies ---" -ForegroundColor Cyan
            Push-Location $dashboardDir
            & npm install --silent 2>&1 | Out-Null
            Pop-Location
            Write-Host "  npm install complete"
        }

        Write-Host "`n--- Building Dashboard UI ---" -ForegroundColor Cyan
        Push-Location $dashboardDir
        $npmResult = & npm run build 2>&1 | Out-String
        Pop-Location

        if ($LASTEXITCODE -eq 0 -and (Test-Path "$dashboardDist\index.html")) {
            if (-not (Test-Path $dashboardDest)) {
                New-Item -ItemType Directory -Path $dashboardDest -Force | Out-Null
            }
            Copy-Item "$dashboardDist\*" $dashboardDest -Recurse -Force
            $uiFiles = (Get-ChildItem $dashboardDist -Recurse -File).Count
            Write-Host "  Dashboard UI built ($uiFiles files)" -ForegroundColor Green
        } else {
            Write-Host "  WARN: Dashboard UI build failed (non-fatal)" -ForegroundColor Yellow
            if ($npmResult) {
                $npmResult -split "`n" | Select-Object -Last 5 | ForEach-Object {
                    Write-Host "    $_" -ForegroundColor Yellow
                }
            }
        }
    } else {
        Write-Host "`n--- Dashboard UI ---" -ForegroundColor Cyan
        # If a previous build exists, still deploy it
        if (Test-Path "$dashboardDist\index.html") {
            if (-not (Test-Path $dashboardDest)) {
                New-Item -ItemType Directory -Path $dashboardDest -Force | Out-Null
            }
            Copy-Item "$dashboardDist\*" $dashboardDest -Recurse -Force
            Write-Host "  npm not found - deploying cached build" -ForegroundColor Yellow
        } else {
            Write-Host "  SKIP: npm not found (install Node.js to build Dashboard UI)" -ForegroundColor Yellow
        }
    }
}

# =============================================================================
# Step 3: Sync SKSE assets -> Data folder
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

# Sync SkyrimNet config (actions, triggers, factions)
# settings.yaml is NOT deployed — SkyrimNet creates it from manifest defaults on first run.
# factions.yaml is excluded from overwrite — users may have customized factions.
# This prevents overwriting user-configured LLM endpoints, blocklists, hotkeys, and faction configs.
$configSrc = "$SKSEDir\Plugins\SkyrimNet\config"
$configDest = "$DataDir\SKSE\Plugins\SkyrimNet\config"
if (Test-Path $configSrc) {
    if (-not (Test-Path $configDest)) {
        New-Item -ItemType Directory -Path $configDest -Force | Out-Null
    }
    # Copy all config files except user-configurable ones (auto-created by DLL on first run)
    $configFiles = Get-ChildItem $configSrc -Recurse -File | Where-Object { $_.Name -ne "settings.yaml" -and $_.Name -ne "factions.yaml" }
    foreach ($cf in $configFiles) {
        $relativePath = $cf.FullName.Substring($configSrc.Length + 1)
        $destFile = Join-Path $configDest $relativePath
        $destDir = Split-Path $destFile -Parent
        if (-not (Test-Path $destDir)) {
            New-Item -ItemType Directory -Path $destDir -Force | Out-Null
        }
        Copy-Item $cf.FullName $destFile -Force
    }
    Write-Host "  $($configFiles.Count) config files synced (settings.yaml excluded)"
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
# Step 4: Deploy Data folder -> Testing + Vanilla Test + CK (unless -SkipDeploy)
# =============================================================================
if (-not $SkipDeploy) {
    Write-Host "`n--- Deploying Data to Testing + Vanilla Test + CK ---" -ForegroundColor Cyan

    # /XF: never overwrite user-configured files at deploy targets
    $robocopyArgs = @("/E", "/IS", "/IT", "/XF", "settings.yaml", "factions.yaml", "/NFL", "/NDL", "/NJH", "/NJS", "/R:1", "/W:1")

    Write-Host "  Copying to Testing..."
    & robocopy $DataDir $TestDest @robocopyArgs | Out-Null
    $fileCount = (Get-ChildItem $DataDir -Recurse -File).Count
    Write-Host "  -> $fileCount files deployed to Testing" -ForegroundColor Green

    Write-Host "  Copying to Vanilla Test..."
    & robocopy $DataDir $VanillaDest @robocopyArgs | Out-Null
    Write-Host "  -> $fileCount files deployed to Vanilla Test" -ForegroundColor Green

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
