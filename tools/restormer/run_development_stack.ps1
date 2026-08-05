param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDirectory,

    [string]$StackExecutable = "build\Stack.exe",

    [switch]$ForceCpu
)

$resolvedPackage = (Resolve-Path -LiteralPath $PackageDirectory).Path
$resolvedStack = (Resolve-Path -LiteralPath $StackExecutable).Path
$manifest = Join-Path $resolvedPackage "manifest.json"
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) {
    throw "Restormer development package manifest not found: $manifest"
}

$manifestData = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
if ($manifestData.packageId -ne "stack-restormer-denoise-v1" -or
    -not $manifestData.developmentPackage) {
    throw "The selected directory is not a Restormer V1 development package."
}

$env:STACK_RESTORMER_DENOISE_DIR = $resolvedPackage
$env:STACK_ALLOW_LOCAL_RESTORMER_PACKAGE = "1"
if ($ForceCpu) {
    $env:STACK_RESTORMER_FORCE_CPU = "1"
} else {
    Remove-Item Env:STACK_RESTORMER_FORCE_CPU -ErrorAction SilentlyContinue
}

Write-Host "Starting Stack with Restormer package $($manifestData.packageVersion)"
& $resolvedStack
exit $LASTEXITCODE
