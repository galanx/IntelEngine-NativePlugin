param(
    [Parameter(Mandatory = $true)][string]$DataDir
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$dist = Join-Path $repoRoot "web\dashboard\dist"
$destinations = @(
    (Join-Path $DataDir "PrismaUI\views\IntelEngine\dashboard"),
    (Join-Path $DataDir "Magelight\IntelEngine\dashboard")
)
$sourceFiles = @(Get-ChildItem -LiteralPath $dist -Recurse -File)
if ($sourceFiles.Count -eq 0) { throw "Dashboard dist is empty: $dist" }

foreach ($destination in $destinations) {
    $destFiles = @(Get-ChildItem -LiteralPath $destination -Recurse -File)
    if ($destFiles.Count -ne $sourceFiles.Count) {
        throw "Dashboard file count differs: $destination ($($destFiles.Count) != $($sourceFiles.Count))"
    }
    foreach ($source in $sourceFiles) {
        $relativePath = $source.FullName.Substring($dist.Length + 1)
        $staged = Join-Path $destination $relativePath
        if (-not (Test-Path -LiteralPath $staged -PathType Leaf) -or
            (Get-FileHash -LiteralPath $source.FullName -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $staged -Algorithm SHA256).Hash) {
            throw "Dashboard asset differs: $destination\$relativePath"
        }
    }
}

Write-Host "Dashboard dist matches both PrismaUI and Magelight staging trees." -ForegroundColor Green
