param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = "",
    [string]$OutputDir = "",
    [switch]$SkipBuild,
    [switch]$SkipInstaller,
    [switch]$SkipPortableZip,
    [switch]$SkipHashes
)

& (Join-Path $PSScriptRoot "create_release.ps1") `
    -Root $Root `
    -BuildDir $BuildDir `
    -OutputDir $OutputDir `
    -SkipBuild:$SkipBuild `
    -SkipInstaller:$SkipInstaller `
    -SkipPortableZip:$SkipPortableZip `
    -SkipHashes:$SkipHashes
exit $LASTEXITCODE
