param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = ""
)

& (Join-Path $PSScriptRoot "build_stack.ps1") -Root $Root -BuildDir $BuildDir
exit $LASTEXITCODE
