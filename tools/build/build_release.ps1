param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$BuildDir = "",
    [ValidateSet("Stack", "Burst")][string]$Product = "Stack"
)

& (Join-Path $PSScriptRoot "build_stack.ps1") -Root $Root -BuildDir $BuildDir -Product $Product
exit $LASTEXITCODE
