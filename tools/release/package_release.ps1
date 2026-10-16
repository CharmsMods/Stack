param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$BuildDir = "",
    [string]$OutputDir = "",
    [ValidateSet("Stack", "Burst")][string]$Product = "Stack",
    [switch]$PublicRelease,
    [switch]$LocalTestPackage,
    [switch]$SkipBuild,
    [switch]$SkipInstaller,
    [switch]$SkipPortableZip,
    [switch]$SkipHashes
)

& (Join-Path $PSScriptRoot "create_release.ps1") `
    -Root $Root `
    -BuildDir $BuildDir `
    -OutputDir $OutputDir `
    -Product $Product `
    -PublicRelease:$PublicRelease `
    -LocalTestPackage:$LocalTestPackage `
    -SkipBuild:$SkipBuild `
    -SkipInstaller:$SkipInstaller `
    -SkipPortableZip:$SkipPortableZip `
    -SkipHashes:$SkipHashes
exit $LASTEXITCODE
