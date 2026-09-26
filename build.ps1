# IntelEngine Build & Deploy Script
# Builds repository assets, stages them in the separate Data repository, and
# optionally deploys Data to the configured mod targets.
#
# Usage:
#   .\build.ps1
#   .\build.ps1 -Scripts Travel,MCM
#   .\build.ps1 -DeployOnly -SkipGit
#   .\build.ps1 -DeployOnly -SkipDeploy -SkipGit
#   .\build.ps1 -DataDir D:\git\IntelEngine-GamePlugin -SkipGit

param(
    [string[]]$Scripts,
    [switch]$DeployOnly,
    [switch]$SkipDeploy,
    [switch]$SkipGit,
    [string]$DataDir = "E:\Tools\spookys-automod-toolkit\Mods\IntelEngine\Data",
    [string]$ToolkitRoot = "E:\Tools\spookys-automod-toolkit",
    [string]$TestDest = "E:\Modding\Lorerim\mods\Galanx_IntelEngine",
    [string]$VanillaDest = "E:\Modding\VanillaTest\mods\Galanx_IntelEngine",
    [string]$CKDest = "D:\SkyrimVR-MO2\mods\Galanx_IntelEngine"
)

$ErrorActionPreference = "Stop"
$DataDir = [System.IO.Path]::GetFullPath($DataDir).TrimEnd([char[]]@('\', '/'))

$RepoRoot = $PSScriptRoot
$SourceDir = Join-Path $RepoRoot "Source\Scripts"
$SKSEDir = Join-Path $RepoRoot "SKSE"
$HeadersDir = [System.IO.Path]::Combine($ToolkitRoot, "skyrim-script-headers")
$CompilerProject = [System.IO.Path]::Combine($ToolkitRoot, "src\SpookysAutomod.Cli")
$OutputDir = Join-Path $DataDir "Scripts"
$ExternalSource = Join-Path $SKSEDir "Plugins\SkyrimNet\external\galanx.intelengine"
$ExternalData = Join-Path $DataDir "SKSE\Plugins\SkyrimNet\external\galanx.intelengine"
$PluginConfigSource = Join-Path $SKSEDir "Plugins\SkyrimNet\config\plugins\IntelEngine"
$PluginConfigData = Join-Path $DataDir "SKSE\Plugins\SkyrimNet\config\plugins\IntelEngine"

$legacyActionFiles = @(
    "intel_travel.yaml",
    "intel_fetchnpc.yaml",
    "intel_escorttarget.yaml",
    "intel_searchforactor.yaml",
    "intel_delivermessage.yaml",
    "intel_canceltask.yaml",
    "intel_changespeed.yaml",
    "intel_schedulemeeting.yaml",
    "intel_schedulefetch.yaml",
    "intel_scheduledelivery.yaml",
    "intel_report_player_conduct.yaml",
    "cat_travel.yaml",
    "cat_scheduling.yaml",
    "cat_communication.yaml"
)

$legacyPromptFiles = @(
    "intel_story_dm.prompt",
    "intel_story_npc_dm.prompt",
    "intel_political_dm.prompt",
    "intel_schedule_safety_net.prompt",
    "submodules\character_bio\0197_intel_received_messages.prompt",
    "submodules\character_bio\0198_intel_schedule_awareness.prompt",
    "submodules\character_bio\0199_intel_meeting_outcome.prompt",
    "submodules\character_bio\0200_intel_gossip.prompt",
    "submodules\character_bio\0800_intel_facts.prompt",
    "submodules\character_bio\0801_intel_task_awareness.prompt",
    "submodules\character_bio\0810_intel_political_awareness.prompt"
)

function Copy-TreeFiles {
    param(
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$DestinationRoot,
        [Parameter(Mandatory = $true)][string]$Label,
        [string[]]$ExcludedNames = @()
    )

    if (-not (Test-Path -LiteralPath $SourceRoot -PathType Container)) {
        throw "$Label source not found: $SourceRoot"
    }

    $files = @(
        Get-ChildItem -LiteralPath $SourceRoot -Recurse -File -Force |
            Where-Object { $ExcludedNames -notcontains $_.Name }
    )
    if ($files.Count -eq 0) {
        throw "$Label source contains no files: $SourceRoot"
    }

    foreach ($file in $files) {
        $relativePath = $file.FullName.Substring($SourceRoot.Length + 1)
        $destinationFile = Join-Path $DestinationRoot $relativePath
        $destinationDirectory = Split-Path $destinationFile -Parent
        if (-not (Test-Path -LiteralPath $destinationDirectory)) {
            New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
        }
        Copy-Item -LiteralPath $file.FullName -Destination $destinationFile -Force
    }

    return $files.Count
}

function Remove-LegacyIntelEngineContent {
    param([Parameter(Mandatory = $true)][string]$DestinationRoot)

    $removed = 0
    foreach ($name in $legacyActionFiles) {
        $path = Join-Path $DestinationRoot "SKSE\Plugins\SkyrimNet\config\actions\$name"
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Remove-Item -LiteralPath $path -Force
            $removed++
        }
    }
    foreach ($relativePath in $legacyPromptFiles) {
        $path = Join-Path $DestinationRoot "SKSE\Plugins\SkyrimNet\prompts\$relativePath"
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Remove-Item -LiteralPath $path -Force
            $removed++
        }
    }

    return $removed
}

function Deploy-Data {
    param(
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$Label
    )

    Write-Host "  Copying to $Label..."
    $robocopyArgs = @(
        $DataDir,
        $Destination,
        "/E",
        "/IS",
        "/IT",
        "/XF",
        "settings.yaml",
        "factions.yaml",
        "content-registry.json",
        "content-registry.json.tmp",
        "content-registry.json.corrupt*",
        "/XD",
        (Join-Path $DataDir "SKSE\Plugins\SkyrimNet\library"),
        (Join-Path $DataDir "SKSE\Plugins\SkyrimNet\overlay"),
        (Join-Path $DataDir "SKSE\Plugins\SkyrimNet\saves"),
        "/NFL",
        "/NDL",
        "/NJH",
        "/NJS",
        "/R:1",
        "/W:1"
    )
    & robocopy @robocopyArgs | Out-Null
    $robocopyExitCode = $LASTEXITCODE
    if ($robocopyExitCode -ge 8) {
        throw "robocopy to $Label failed with exit code $robocopyExitCode"
    }

    $removed = Remove-LegacyIntelEngineContent -DestinationRoot $Destination
    Write-Host "  -> deployed to $Label; removed $removed known legacy files" -ForegroundColor Green
}

# =============================================================================
# Step 1: Sync headers and compile Papyrus scripts (unless -DeployOnly)
# =============================================================================
if (-not $DeployOnly) {
    if (-not (Test-Path -LiteralPath $HeadersDir -PathType Container)) {
        throw "Papyrus headers directory not found: $HeadersDir"
    }

    Write-Host "--- Syncing headers ---" -ForegroundColor Cyan
    $synced = 0
    Get-ChildItem -Path (Join-Path $SourceDir "IntelEngine*.psc") | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $HeadersDir $_.Name) -Force
        $synced++
    }
    Write-Host "  $synced PSC files synced to headers"

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
        $toBuild = $Scripts | ForEach-Object {
            if ($_ -eq "IntelEngine" -or $_.StartsWith("IntelEngine_")) { $_ }
            else { "IntelEngine_$_" }
        }
    } else {
        $toBuild = $allScripts
    }

    if (-not (Test-Path -LiteralPath $OutputDir)) {
        New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    }

    Write-Host "`n--- Compiling ---" -ForegroundColor Cyan
    $failed = @()
    foreach ($script in $toBuild) {
        $psc = Join-Path $SourceDir "$script.psc"
        if (-not (Test-Path -LiteralPath $psc)) {
            Write-Host "  SKIP: $script.psc not found" -ForegroundColor Yellow
            continue
        }

        Write-Host "  Compiling $script..." -NoNewline
        $result = & dotnet run --project $CompilerProject -- papyrus compile $psc --output $OutputDir --headers $HeadersDir --json 2>&1 | Out-String

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
# Step 2: Build Dashboard UI (React/webpack -> Data/PrismaUI/views/)
# =============================================================================
$dashboardDir = Join-Path $RepoRoot "web\dashboard"
$dashboardDist = Join-Path $dashboardDir "dist"
$dashboardDest = Join-Path $DataDir "PrismaUI\views\IntelEngine\dashboard"
if (Test-Path -LiteralPath (Join-Path $dashboardDir "package.json")) {
    $npmCmd = Get-Command npm -ErrorAction SilentlyContinue
    if (-not $npmCmd -and (Test-Path -LiteralPath "C:\Program Files\nodejs\npm.cmd")) {
        $env:PATH = "C:\Program Files\nodejs;" + $env:PATH
        $npmCmd = Get-Command npm -ErrorAction SilentlyContinue
    }
    if ($npmCmd) {
        if (-not (Test-Path -LiteralPath (Join-Path $dashboardDir "node_modules"))) {
            Write-Host "`n--- Installing Dashboard UI dependencies ---" -ForegroundColor Cyan
            Push-Location $dashboardDir
            & npm install --silent 2>&1 | Out-Null
            Pop-Location
            Write-Host "  npm install complete"
        }

        Write-Host "`n--- Building Dashboard UI ---" -ForegroundColor Cyan
        Push-Location $dashboardDir
        $previousErrorActionPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = "Continue"
            $npmResult = & npm run build 2>&1 | Out-String
            $npmExitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $previousErrorActionPreference
            Pop-Location
        }

        if ($npmExitCode -eq 0 -and (Test-Path -LiteralPath (Join-Path $dashboardDist "index.html"))) {
            if (-not (Test-Path -LiteralPath $dashboardDest)) {
                New-Item -ItemType Directory -Path $dashboardDest -Force | Out-Null
            }
            Copy-Item -Path (Join-Path $dashboardDist "*") -Destination $dashboardDest -Recurse -Force
            $uiFiles = @(Get-ChildItem -LiteralPath $dashboardDist -Recurse -File).Count
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
        if (Test-Path -LiteralPath (Join-Path $dashboardDist "index.html")) {
            if (-not (Test-Path -LiteralPath $dashboardDest)) {
                New-Item -ItemType Directory -Path $dashboardDest -Force | Out-Null
            }
            Copy-Item -Path (Join-Path $dashboardDist "*") -Destination $dashboardDest -Recurse -Force
            Write-Host "  npm not found - deploying cached build" -ForegroundColor Yellow
        } else {
            Write-Host "  SKIP: npm not found (install Node.js to build Dashboard UI)" -ForegroundColor Yellow
        }
    }
}

# =============================================================================
# Step 3: Stage repository assets in the Data repository
# =============================================================================
Write-Host "`n--- Staging repository assets in Data ---" -ForegroundColor Cyan
if (-not (Test-Path -LiteralPath $DataDir)) {
    New-Item -ItemType Directory -Path $DataDir -Force | Out-Null
}

$dllBuild = Join-Path $SKSEDir "build\Release\IntelEngine.dll"
$dllFallback = Join-Path $SKSEDir "Plugins\IntelEngine.dll"
$dllDest = Join-Path $DataDir "SKSE\Plugins\IntelEngine.dll"
if (Test-Path -LiteralPath $dllBuild) {
    New-Item -ItemType Directory -Path (Split-Path $dllDest -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $dllBuild -Destination $dllDest -Force
    Write-Host "  DLL synced (from repository build output)"
} elseif (Test-Path -LiteralPath $dllFallback) {
    New-Item -ItemType Directory -Path (Split-Path $dllDest -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $dllFallback -Destination $dllDest -Force
    Write-Host "  DLL synced (from repository SKSE assets)"
} else {
    Write-Host "  WARN: DLL not found" -ForegroundColor Yellow
}

$removedFromData = Remove-LegacyIntelEngineContent -DestinationRoot $DataDir
Write-Host "  $removedFromData known legacy action/prompt files removed from Data"

$externalCount = Copy-TreeFiles -SourceRoot $ExternalSource -DestinationRoot $ExternalData -Label "External bundle"
Write-Host "  $externalCount external bundle files staged"

$configCount = Copy-TreeFiles `
    -SourceRoot $PluginConfigSource `
    -DestinationRoot $PluginConfigData `
    -Label "IntelEngine plugin configuration" `
    -ExcludedNames @("settings.yaml", "factions.yaml")
Write-Host "  $configCount plugin config files staged (settings.yaml and factions.yaml preserved)"

$dataSourceDir = Join-Path $DataDir "Source\Scripts"
if (-not (Test-Path -LiteralPath $dataSourceDir)) {
    New-Item -ItemType Directory -Path $dataSourceDir -Force | Out-Null
}
Get-ChildItem -Path (Join-Path $SourceDir "IntelEngine*.psc") | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $dataSourceDir $_.Name) -Force
}
Write-Host "  Source scripts synced to Data"

# =============================================================================
# Step 4: Deploy Data without mirroring shared roots (unless -SkipDeploy)
# =============================================================================
if (-not $SkipDeploy) {
    Write-Host "`n--- Deploying Data to configured mod targets ---" -ForegroundColor Cyan
    Deploy-Data -Destination $TestDest -Label "Testing"
    Deploy-Data -Destination $VanillaDest -Label "Vanilla Test"
    Deploy-Data -Destination $CKDest -Label "CK"
}

# =============================================================================
# Step 5: Verify staged/deployed files
# =============================================================================
Write-Host "`n--- Verifying staged/deployed files ---" -ForegroundColor Cyan
$verifyArgs = @{
    Quiet = $true
    DataDir = $DataDir
    TestDest = $TestDest
    VanillaDest = $VanillaDest
    CKDest = $CKDest
    SkipDeployTargets = $SkipDeploy.IsPresent
}
& (Join-Path $RepoRoot "verify.ps1") @verifyArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n--- Build complete (VERIFICATION FAILED) ---" -ForegroundColor Red
    exit 1
}

# =============================================================================
# Step 6: Publish only when explicitly allowed by omitting -SkipGit
# =============================================================================
if (-not $SkipGit) {
    Write-Host "`n--- Publishing repository changes ---" -ForegroundColor Cyan

    if ($DeployOnly) {
        $commitMsg = "Deploy: sync assets"
    } elseif ($Scripts -and $Scripts.Count -gt 0) {
        $commitMsg = "Build: $($Scripts -join ', ')"
    } else {
        $commitMsg = "Build: all scripts"
    }

    Push-Location $RepoRoot
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
} else {
    Write-Host "`n--- Git publishing skipped (-SkipGit) ---" -ForegroundColor DarkGray
}

Write-Host "`n--- Build complete ---" -ForegroundColor Green
