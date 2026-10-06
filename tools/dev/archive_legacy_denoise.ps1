param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "stack_workflow.ps1")

$Root = (Resolve-Path -LiteralPath $Root).Path
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $Root "build"
}
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
$expectedBuildDir = [System.IO.Path]::GetFullPath((Join-Path $Root "build"))
if (-not $BuildDir.Equals($expectedBuildDir, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to archive from an unexpected deployment directory: $BuildDir"
}

$source = Join-Path $BuildDir "denoise"
if (-not (Test-Path -LiteralPath $source -PathType Container)) {
    Write-Host "No legacy denoise directory is present."
    exit 0
}

Close-RunningStackProcess

$archiveRoot = Join-Path $Root "_local_archive\runtime-layout"
$sessionRoot = Join-Path $archiveRoot (Get-StackTimestamp)
$destination = Join-Path $sessionRoot "denoise"
Ensure-StackDirectory -Path $sessionRoot
if (Test-Path -LiteralPath $destination) {
    throw "Archive destination already exists: $destination"
}

$sourcePrefix = $source.TrimEnd('\') + '\'
$inventory = @(
    Get-ChildItem -LiteralPath $source -File -Recurse -Force | ForEach-Object {
        [pscustomobject]@{
            RelativePath = $_.FullName.Substring($sourcePrefix.Length)
            SizeBytes = $_.Length
            LastWriteTimeUtc = $_.LastWriteTimeUtc.ToString("o")
        }
    } | Sort-Object RelativePath
)
$inventoryPath = Join-Path $sessionRoot "denoise-inventory.csv"
$inventory | Export-Csv -LiteralPath $inventoryPath -NoTypeInformation -Encoding UTF8
$totalBytes = ($inventory | Measure-Object -Property SizeBytes -Sum).Sum

Move-Item -LiteralPath $source -Destination $destination
if (-not (Test-Path -LiteralPath $destination -PathType Container)) {
    throw "Legacy denoise archive move did not produce the expected destination."
}

$archivedFiles = @(Get-ChildItem -LiteralPath $destination -File -Recurse -Force)
$archivedBytes = ($archivedFiles | Measure-Object -Property Length -Sum).Sum
if ($archivedFiles.Count -ne $inventory.Count -or $archivedBytes -ne $totalBytes) {
    throw "Legacy denoise archive verification failed."
}

$summaryPath = Join-Path $sessionRoot "README.txt"
@(
    "Stack legacy denoise archive"
    "Source: $source"
    "Destination: $destination"
    "Files: $($inventory.Count)"
    "Bytes: $totalBytes"
    "Reason: Legacy ONNX/CUDA denoise payload is not loaded by the current application and is excluded from deployment and release packaging."
) | Set-Content -LiteralPath $summaryPath -Encoding UTF8

Write-Host "Archived legacy denoise payload to: $destination"
Write-Host "Inventory: $inventoryPath"
Write-Host "Files: $($inventory.Count)"
Write-Host "Bytes: $totalBytes"
