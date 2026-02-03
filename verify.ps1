# IntelEngine Deployment Verification Script
# Compares file sizes between source-of-truth locations, Data/, Testing, and CK.
# Returns exit code 0 on success, 1 on any mismatch.
#
# Usage:
#   .\verify.ps1           # Full verification (all pairs)
#   .\verify.ps1 -Quiet    # Only print failures + summary

param(
    [switch]$Quiet
)

$ErrorActionPreference = "Continue"

$Root      = "E:\Tools\spookys-automod-toolkit"
$ModRoot   = "$Root\Mods\IntelEngine"
$DataDir   = "$ModRoot\Data"
$SKSEDir   = "$ModRoot\SKSE"
$TestDest  = "E:\Modding\Lorerim\mods\Galanx_IntelEngine"
$CKDest    = "D:\SkyrimVR-MO2\mods\Galanx_IntelEngine"

$ok        = $true
$checked   = 0
$mismatches = @()

function Compare-File {
    param(
        [string]$Label,
        [string]$SourcePath,
        [string]$DestPath
    )

    $script:checked++
    $src = Get-Item $SourcePath -ErrorAction SilentlyContinue
    $dst = Get-Item $DestPath -ErrorAction SilentlyContinue

    if (-not $src) {
        if (-not $Quiet) { Write-Host "  SKIP: $Label (source not found: $SourcePath)" -ForegroundColor Yellow }
        return
    }
    if (-not $dst) {
        Write-Host "  MISSING: $Label" -ForegroundColor Red
        Write-Host "    Expected: $DestPath" -ForegroundColor Red
        $script:ok = $false
        $script:mismatches += $Label
        return
    }
    if ($src.Length -ne $dst.Length) {
        Write-Host "  MISMATCH: $Label" -ForegroundColor Red
        Write-Host "    Source: $($src.Length) bytes ($SourcePath)" -ForegroundColor Red
        Write-Host "    Dest:   $($dst.Length) bytes ($DestPath)" -ForegroundColor Red
        $script:ok = $false
        $script:mismatches += $Label
        return
    }
    if (-not $Quiet) {
        Write-Host "  OK: $Label ($($src.Length) bytes)" -ForegroundColor Green
    }
}

# =============================================================================
# DLL: Build → Data → Testing → CK
# =============================================================================
Write-Host "`n=== DLL ===" -ForegroundColor Cyan
Compare-File "DLL: Build -> Data" `
    "$SKSEDir\build\Release\IntelEngine.dll" `
    "$DataDir\SKSE\Plugins\IntelEngine.dll"

Compare-File "DLL: Data -> Testing" `
    "$DataDir\SKSE\Plugins\IntelEngine.dll" `
    "$TestDest\SKSE\Plugins\IntelEngine.dll"

Compare-File "DLL: Data -> CK" `
    "$DataDir\SKSE\Plugins\IntelEngine.dll" `
    "$CKDest\SKSE\Plugins\IntelEngine.dll"

# =============================================================================
# PEX: Data → Testing → CK
# =============================================================================
Write-Host "`n=== Compiled Scripts (PEX) ===" -ForegroundColor Cyan
Get-ChildItem "$DataDir\Scripts\IntelEngine*.pex" -ErrorAction SilentlyContinue | ForEach-Object {
    $name = $_.Name
    Compare-File "PEX $name -> Testing" `
        $_.FullName `
        "$TestDest\Scripts\$name"

    Compare-File "PEX $name -> CK" `
        $_.FullName `
        "$CKDest\Scripts\$name"
}

# =============================================================================
# Action YAMLs: SKSE source → Data
# =============================================================================
Write-Host "`n=== Action YAMLs ===" -ForegroundColor Cyan
Get-ChildItem "$SKSEDir\Plugins\SkyrimNet\config\actions\*.yaml" -ErrorAction SilentlyContinue | ForEach-Object {
    $name = $_.Name
    Compare-File "YAML $name SKSE -> Data" `
        $_.FullName `
        "$DataDir\SKSE\Plugins\SkyrimNet\config\actions\$name"
}

# =============================================================================
# Prompt files: SKSE source → Data
# =============================================================================
Write-Host "`n=== Prompt Files ===" -ForegroundColor Cyan
Get-ChildItem "$SKSEDir\Plugins\SkyrimNet\prompts" -Recurse -File -ErrorAction SilentlyContinue | ForEach-Object {
    $relPath = $_.FullName.Substring("$SKSEDir\Plugins\SkyrimNet\prompts\".Length)
    Compare-File "Prompt $relPath SKSE -> Data" `
        $_.FullName `
        "$DataDir\SKSE\Plugins\SkyrimNet\prompts\$relPath"
}

# =============================================================================
# ESP/ESL plugins: Data → Testing → CK
# =============================================================================
Write-Host "`n=== Plugins (ESP/ESL) ===" -ForegroundColor Cyan
$plugins = @(Get-ChildItem "$DataDir\*.esp", "$DataDir\*.esl" -ErrorAction SilentlyContinue)
if ($plugins.Count -eq 0) {
    if (-not $Quiet) { Write-Host "  No ESP/ESL plugins in Data (OK)" -ForegroundColor Gray }
} else {
    foreach ($p in $plugins) {
        $name = $p.Name
        Compare-File "Plugin $name -> Testing" $p.FullName "$TestDest\$name"
        Compare-File "Plugin $name -> CK" $p.FullName "$CKDest\$name"
    }
}

# =============================================================================
# Summary
# =============================================================================
Write-Host ""
if ($ok) {
    Write-Host "VERIFIED: $checked checks passed" -ForegroundColor Green
    exit 0
} else {
    Write-Host "FAILED: $($mismatches.Count) mismatches out of $checked checks" -ForegroundColor Red
    $mismatches | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    exit 1
}
