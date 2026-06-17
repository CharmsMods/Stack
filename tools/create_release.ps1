param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = "",
    [string]$OutputDir = "",
    [switch]$SkipBuild,
    [switch]$SkipInstaller,
    [switch]$SkipPortableZip,
    [switch]$SkipHashes
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "stack_workflow.ps1")

$Root = (Resolve-Path $Root).Path
$paths = Get-StackPaths -Root $Root
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = $paths.BuildDir
}

Invoke-StackEnvironmentRepair -Root $Root

if (-not $SkipBuild) {
    & (Join-Path $Root "tools\build_stack.ps1") -Root $Root -BuildDir $BuildDir
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$usingCanonicalOutput = [string]::IsNullOrWhiteSpace($OutputDir)
if ($usingCanonicalOutput) {
    Initialize-ReleaseFolders -Paths $paths
    Prepare-CurrentReleaseOutput -Paths $paths
    $OutputDir = $paths.CurrentReleaseDir
}
else {
    Ensure-StackDirectory -Path $OutputDir

    $customArchiveRoot = Join-Path $OutputDir "_archive"
    $pathsToArchive = @(
        (Join-Path $OutputDir "StackInstallerLicensePage.txt"),
        (Join-Path $OutputDir "SHA256SUMS.txt")
    )

    foreach ($path in $pathsToArchive) {
        $archivedPath = Move-StackItemToArchive -SourcePath $path -ArchiveRoot $customArchiveRoot
        if ($archivedPath) {
            Write-Host "Archived existing file to: $archivedPath"
        }
    }
}

$versionInfo = Get-StackVersionInfo -VersionFile (Join-Path $Root "cmake\StackVersion.cmake")
$stageDir = Join-Path $OutputDir ("Stack-{0}-win-x64" -f $versionInfo.Tag)
$portableZipPath = Join-Path $OutputDir ("Stack-{0}-win-x64.zip" -f $versionInfo.Tag)
$installerPath = Join-Path $OutputDir ("StackSetup-{0}-win-x64.exe" -f $versionInfo.Tag)
$hashesPath = Join-Path $OutputDir "SHA256SUMS.txt"
$licensePagePath = Join-Path $OutputDir "StackInstallerLicensePage.txt"
$markerFile = Join-Path $Root "installer\StackInstalledBuild.marker"

if (-not $usingCanonicalOutput) {
    $customArchiveRoot = Join-Path $OutputDir "_archive"
    foreach ($path in @($stageDir, $portableZipPath, $installerPath)) {
        $archivedPath = Move-StackItemToArchive -SourcePath $path -ArchiveRoot $customArchiveRoot
        if ($archivedPath) {
            Write-Host "Archived existing release artifact to: $archivedPath"
        }
    }
}

Ensure-StackDirectory -Path $OutputDir
Ensure-StackDirectory -Path $stageDir

Copy-ReleaseFile -SourcePath (Join-Path $BuildDir "Stack.exe") -DestinationPath (Join-Path $stageDir "Stack.exe")
Copy-ReleaseFile -SourcePath (Join-Path $BuildDir "THIRD_PARTY_NOTICES.md") -DestinationPath (Join-Path $stageDir "THIRD_PARTY_NOTICES.md")
Copy-ReleaseDirectoryContents -SourceDir (Join-Path $BuildDir "licenses") -DestinationDir (Join-Path $stageDir "licenses")

$projectLicensePath = Join-Path $Root "LICENSE"
if (Test-Path -LiteralPath $projectLicensePath) {
    Copy-ReleaseFile -SourcePath $projectLicensePath -DestinationPath (Join-Path $stageDir "LICENSE")
}

$libRawPath = Join-Path $BuildDir "libraw.dll"
if (Test-Path -LiteralPath $libRawPath) {
    Copy-ReleaseFile -SourcePath $libRawPath -DestinationPath (Join-Path $stageDir "libraw.dll")
}

New-LicensePageFile -RootDir $Root -DestinationPath $licensePagePath

if (-not $SkipPortableZip) {
    Compress-Archive -Path (Join-Path $stageDir '*') -DestinationPath $portableZipPath -CompressionLevel Optimal -Force
}

if (-not $SkipInstaller) {
    $isccPath = Resolve-IsccPath
    if (-not $isccPath) {
        throw "Inno Setup 6 (ISCC.exe) was not found. Install Inno Setup 6 or rerun with -SkipInstaller."
    }

    & $isccPath `
        "/DStackVersion=$($versionInfo.Version)" `
        "/DStackVersionTag=$($versionInfo.Tag)" `
        "/DStackPublisher=$($versionInfo.Publisher)" `
        "/DStackSourceDir=$stageDir" `
        "/DStackOutputDir=$OutputDir" `
        "/DStackLicensePageFile=$licensePagePath" `
        "/DStackMarkerFile=$markerFile" `
        (Join-Path $Root "installer\StackInstaller.iss")

    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

if (-not $SkipHashes) {
    $releaseFiles = @()
    if (Test-Path -LiteralPath $installerPath) {
        $releaseFiles += $installerPath
    }
    if (Test-Path -LiteralPath $portableZipPath) {
        $releaseFiles += $portableZipPath
    }
    Write-Sha256Sums -Files $releaseFiles -DestinationPath $hashesPath
}

Write-Host ""
Write-Host "Release packaging complete."
Write-Host "Current release folder: $OutputDir"
Write-Host "Stage directory: $stageDir"
if (Test-Path -LiteralPath $portableZipPath) {
    Write-Host "Portable ZIP: $portableZipPath"
}
if (Test-Path -LiteralPath $installerPath) {
    Write-Host "Installer: $installerPath"
}
if (Test-Path -LiteralPath $hashesPath) {
    Write-Host "Hashes: $hashesPath"
}
if ($usingCanonicalOutput) {
    Write-Host "Older packaged releases are archived in: $($paths.ReleaseArchiveDir)"
}
