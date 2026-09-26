# IntelEngine Deployment Verification Script
# Compares repository source assets with the separate Data repository and,
# unless requested otherwise, each configured deployment target.
# Returns exit code 0 on success, 1 on any mismatch.
#
# Usage:
#   .\verify.ps1
#   .\verify.ps1 -Quiet
#   .\verify.ps1 -DataDir D:\git\IntelEngine-GamePlugin -SkipDeployTargets

param(
    [switch]$Quiet,
    [switch]$SkipDeployTargets,
    [string]$DataDir = "E:\Tools\spookys-automod-toolkit\Mods\IntelEngine\Data",
    [string]$TestDest = "E:\Modding\Lorerim\mods\Galanx_IntelEngine",
    [string]$VanillaDest = "E:\Modding\VanillaTest\mods\Galanx_IntelEngine",
    [string]$CKDest = "D:\SkyrimVR-MO2\mods\Galanx_IntelEngine"
)

$ErrorActionPreference = "Continue"
$DataDir = [System.IO.Path]::GetFullPath($DataDir).TrimEnd([char[]]@('\', '/'))

$RepoRoot = $PSScriptRoot
$SKSEDir = Join-Path $RepoRoot "SKSE"
$ExternalSource = Join-Path $SKSEDir "Plugins\SkyrimNet\external\galanx.intelengine"
$ExternalData = Join-Path $DataDir "SKSE\Plugins\SkyrimNet\external\galanx.intelengine"
$PluginConfigSource = Join-Path $SKSEDir "Plugins\SkyrimNet\config\plugins\IntelEngine"
$PluginConfigData = Join-Path $DataDir "SKSE\Plugins\SkyrimNet\config\plugins\IntelEngine"

$deployTargets = @(
    [pscustomobject]@{ Label = "Testing"; Path = $TestDest },
    [pscustomobject]@{ Label = "Vanilla Test"; Path = $VanillaDest },
    [pscustomobject]@{ Label = "CK"; Path = $CKDest }
)

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

$ok = $true
$checked = 0
$mismatches = @()

function Add-Failure {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$Message
    )

    Write-Host "  $Message" -ForegroundColor Red
    $script:ok = $false
    $script:mismatches += $Label
}

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

function Compare-File {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$SourcePath,
        [Parameter(Mandatory = $true)][string]$DestPath,
        [switch]$UseHash
    )

    $script:checked++
    $src = Get-Item -LiteralPath $SourcePath -ErrorAction SilentlyContinue
    $dst = Get-Item -LiteralPath $DestPath -ErrorAction SilentlyContinue

    if (-not $src) {
        Add-Failure -Label $Label -Message "MISSING SOURCE: $Label ($SourcePath)"
        return
    }
    if (-not $dst) {
        Add-Failure -Label $Label -Message "MISSING: $Label (expected $DestPath)"
        return
    }
    if ($src.Length -ne $dst.Length) {
        Add-Failure -Label $Label -Message "MISMATCH: $Label ($($src.Length) bytes at source, $($dst.Length) bytes at destination)"
        return
    }
    if ($UseHash) {
        $srcHash = Get-Sha256Hex -Path $SourcePath
        $dstHash = Get-Sha256Hex -Path $DestPath
        if ($srcHash -ne $dstHash) {
            Add-Failure -Label $Label -Message "HASH MISMATCH: $Label"
            return
        }
    }
    if (-not $Quiet) {
        Write-Host "  OK: $Label ($($src.Length) bytes)" -ForegroundColor Green
    }
}

function Assert-FileAbsent {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $script:checked++
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        Add-Failure -Label $Label -Message "LEGACY FILE PRESENT: $Label ($Path)"
    } elseif (-not $Quiet) {
        Write-Host "  OK: $Label absent" -ForegroundColor Green
    }
}

function Compare-SourceTree {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$SourceRoot,
        [Parameter(Mandatory = $true)][string]$DataRoot,
        [string[]]$ExcludedNames = @()
    )

    if (-not (Test-Path -LiteralPath $SourceRoot -PathType Container)) {
        $script:checked++
        Add-Failure -Label $Label -Message "MISSING SOURCE TREE: $Label ($SourceRoot)"
        return
    }

    $files = @(
        Get-ChildItem -LiteralPath $SourceRoot -Recurse -File -Force |
            Where-Object { $ExcludedNames -notcontains $_.Name }
    )
    if ($files.Count -eq 0) {
        $script:checked++
        Add-Failure -Label $Label -Message "EMPTY SOURCE TREE: $Label ($SourceRoot)"
        return
    }

    foreach ($file in $files) {
        $relativePath = $file.FullName.Substring($SourceRoot.Length + 1)
        $dataFile = Join-Path $DataRoot $relativePath
        Compare-File "$Label source -> Data: $relativePath" $file.FullName $dataFile -UseHash

        if (-not $SkipDeployTargets -and (Test-Path -LiteralPath $dataFile -PathType Leaf)) {
            foreach ($target in $deployTargets) {
                $targetFile = Join-Path $target.Path $dataFile.Substring($DataDir.Length + 1)
                Compare-File "$Label Data -> $($target.Label): $relativePath" $dataFile $targetFile -UseHash
            }
        }
    }
}

# =============================================================================
# External bundle: repository source -> Data -> configured targets
# =============================================================================
Write-Host "`n=== SkyrimNet External Bundle ===" -ForegroundColor Cyan
Compare-SourceTree `
    -Label "External bundle" `
    -SourceRoot $ExternalSource `
    -DataRoot $ExternalData

# =============================================================================
# IntelEngine plugin config remains outside the external bundle
# =============================================================================
Write-Host "`n=== IntelEngine Plugin Configuration ===" -ForegroundColor Cyan
Compare-SourceTree `
    -Label "Plugin config" `
    -SourceRoot $PluginConfigSource `
    -DataRoot $PluginConfigData `
    -ExcludedNames @("settings.yaml", "factions.yaml")

# =============================================================================
# Known retired loose files must be absent; shared roots remain untouched
# =============================================================================
Write-Host "`n=== Retired IntelEngine Loose Content ===" -ForegroundColor Cyan
$rootsToCheck = @(
    [pscustomobject]@{ Label = "Repository"; Path = $RepoRoot },
    [pscustomobject]@{ Label = "Data"; Path = $DataDir }
)
if (-not $SkipDeployTargets) {
    $rootsToCheck += $deployTargets
}

foreach ($root in $rootsToCheck) {
    foreach ($name in $legacyActionFiles) {
        Assert-FileAbsent `
            "$($root.Label) legacy action $name" `
            (Join-Path $root.Path "SKSE\Plugins\SkyrimNet\config\actions\$name")
    }
    foreach ($relativePath in $legacyPromptFiles) {
        Assert-FileAbsent `
            "$($root.Label) legacy prompt $relativePath" `
            (Join-Path $root.Path "SKSE\Plugins\SkyrimNet\prompts\$relativePath")
    }
}

# =============================================================================
# Optional compiled/package assets: Data -> configured targets
# =============================================================================
if (-not $SkipDeployTargets) {
    Write-Host "`n=== DLL ===" -ForegroundColor Cyan
    $dllBuild = Join-Path $SKSEDir "build\Release\IntelEngine.dll"
    $dllFallback = Join-Path $SKSEDir "Plugins\IntelEngine.dll"
    $dllData = Join-Path $DataDir "SKSE\Plugins\IntelEngine.dll"
    $dllSource = $null
    if (Test-Path -LiteralPath $dllBuild -PathType Leaf) {
        $dllSource = $dllBuild
    } elseif (Test-Path -LiteralPath $dllFallback -PathType Leaf) {
        $dllSource = $dllFallback
    }

    if ($dllSource) {
        Compare-File "DLL repository -> Data" $dllSource $dllData -UseHash
    } elseif (-not $Quiet) {
        Write-Host "  SKIP: no repository DLL found" -ForegroundColor Yellow
    }

    if (Test-Path -LiteralPath $dllData -PathType Leaf) {
        foreach ($target in $deployTargets) {
            Compare-File "DLL Data -> $($target.Label)" $dllData (Join-Path $target.Path "SKSE\Plugins\IntelEngine.dll") -UseHash
        }
    }

    Write-Host "`n=== Compiled Scripts (PEX) ===" -ForegroundColor Cyan
    Get-ChildItem -Path (Join-Path $DataDir "Scripts\IntelEngine*.pex") -ErrorAction SilentlyContinue | ForEach-Object {
        foreach ($target in $deployTargets) {
            Compare-File "PEX $($_.Name) -> $($target.Label)" $_.FullName (Join-Path $target.Path "Scripts\$($_.Name)") -UseHash
        }
    }

    Write-Host "`n=== PrismaUI ===" -ForegroundColor Cyan
    Get-ChildItem -LiteralPath (Join-Path $DataDir "PrismaUI") -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object {
        $relativePath = $_.FullName.Substring($DataDir.Length + 1)
        foreach ($target in $deployTargets) {
            Compare-File "PrismaUI $relativePath -> $($target.Label)" $_.FullName (Join-Path $target.Path $relativePath) -UseHash
        }
    }

    Write-Host "`n=== Plugins (ESP/ESL) ===" -ForegroundColor Cyan
    $plugins = @(
        Get-ChildItem -Path (Join-Path $DataDir "*.esp"), (Join-Path $DataDir "*.esl") -ErrorAction SilentlyContinue
    )
    if ($plugins.Count -eq 0) {
        if (-not $Quiet) {
            Write-Host "  No ESP/ESL plugins in Data (OK)" -ForegroundColor Gray
        }
    } else {
        foreach ($plugin in $plugins) {
            foreach ($target in $deployTargets) {
                Compare-File "Plugin $($plugin.Name) -> $($target.Label)" $plugin.FullName (Join-Path $target.Path $plugin.Name) -UseHash
            }
        }
    }
}

# =============================================================================
# Summary
# =============================================================================
Write-Host ""
if ($ok) {
    Write-Host "VERIFIED: $checked checks passed" -ForegroundColor Green
    exit 0
}

Write-Host "FAILED: $($mismatches.Count) mismatches out of $checked checks" -ForegroundColor Red
$mismatches | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
exit 1
