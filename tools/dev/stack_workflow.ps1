function Ensure-StackDirectory {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "Directory path cannot be empty."
    }

    if (-not (Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Force -Path $Path | Out-Null
    }
}

function Test-StackDirectoryHasContent {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return $false
    }

    return $null -ne (Get-ChildItem -LiteralPath $Path -Force | Select-Object -First 1)
}

function Get-StackTimestamp {
    return (Get-Date).ToString("yyyyMMdd-HHmmss")
}

function Invoke-StackEnvironmentRepair {
    param([string]$Root)

    & (Join-Path $Root "tools\dev\use_fixed_env.ps1")
}

function Get-StackPaths {
    param([string]$Root)

    $outputsRoot = Join-Path $Root "outputs"
    $releasesRoot = Join-Path $outputsRoot "releases"

    [pscustomobject]@{
        Root = $Root
        BuildDir = Join-Path $Root "build"
        BuildTreeDir = Join-Path $Root "_workspace\build-tree\stack"
        VersionFile = Join-Path $Root "cmake\StackVersion.cmake"
        OutputsRoot = $outputsRoot
        ReleasesRoot = $releasesRoot
        CurrentReleaseDir = Join-Path $releasesRoot "current"
        LocalTestReleaseDir = Join-Path $releasesRoot "local-test"
        ReleaseArchiveDir = Join-Path $releasesRoot "archive"
        MiscOutputDir = Join-Path $outputsRoot "misc"
        LegacyReleaseDir = Join-Path $outputsRoot "release"
        LegacyReleaseTestDir = Join-Path $outputsRoot "release_test"
        ExtraBuildArchiveRoot = Join-Path $Root "_local_archive\build-folders"
        FfmpegProviderSourceDir = Join-Path $Root "_workspace\ffmpeg-provider"
        FfmpegProviderStageRelativeDir = "Stack\App\Tools\FFmpeg"
    }
}

function Test-StackSemanticVersion {
    param([string]$Version)

    return $Version -match '^\d+\.\d+\.\d+$'
}

function ConvertTo-StackVersionParts {
    param([string]$Version)

    if (-not (Test-StackSemanticVersion -Version $Version)) {
        throw "Version must look like major.minor.patch, for example 1.1.0."
    }

    $parts = $Version.Split('.')
    return [pscustomobject]@{
        Major = [int]$parts[0]
        Minor = [int]$parts[1]
        Patch = [int]$parts[2]
        Version = $Version
        Tag = "v$Version"
    }
}

function Get-NextStackPatchVersion {
    param([string]$Version)

    $versionParts = ConvertTo-StackVersionParts -Version $Version
    return "{0}.{1}.{2}" -f $versionParts.Major, $versionParts.Minor, ($versionParts.Patch + 1)
}

function Move-StackItemToArchive {
    param(
        [string]$SourcePath,
        [string]$ArchiveRoot,
        [string]$Label = ""
    )

    if (-not (Test-Path -LiteralPath $SourcePath)) {
        return $null
    }

    Ensure-StackDirectory -Path $ArchiveRoot

    $item = Get-Item -LiteralPath $SourcePath
    $timestamp = Get-StackTimestamp
    if ([string]::IsNullOrWhiteSpace($Label)) {
        $Label = if ($item.PSIsContainer) { $item.Name } else { $item.BaseName }
    }

    if ($item.PSIsContainer) {
        $destinationName = "{0}-{1}" -f $Label, $timestamp
    }
    else {
        $destinationName = "{0}-{1}{2}" -f $Label, $timestamp, $item.Extension
    }

    $destinationPath = Join-Path $ArchiveRoot $destinationName
    Move-Item -LiteralPath $SourcePath -Destination $destinationPath
    return $destinationPath
}

function Move-StackDirectoryContentsToArchive {
    param(
        [string]$SourceDir,
        [string]$ArchiveRoot,
        [string]$Label
    )

    if (-not (Test-StackDirectoryHasContent -Path $SourceDir)) {
        return $null
    }

    Ensure-StackDirectory -Path $ArchiveRoot
    $destinationPath = Join-Path $ArchiveRoot ("{0}-{1}" -f $Label, (Get-StackTimestamp))
    New-Item -ItemType Directory -Force -Path $destinationPath | Out-Null

    Get-ChildItem -LiteralPath $SourceDir -Force | ForEach-Object {
        Move-Item -LiteralPath $_.FullName -Destination $destinationPath
    }

    return $destinationPath
}

function Get-CurrentReleaseArchiveLabel {
    param([string]$CurrentReleaseDir)

    if (-not (Test-Path -LiteralPath $CurrentReleaseDir)) {
        return "release-current"
    }

    $stageDir = Get-ChildItem -LiteralPath $CurrentReleaseDir -Directory -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "Stack-v*-win-x64" } |
        Select-Object -First 1
    if ($stageDir) {
        return $stageDir.Name
    }

    $installer = Get-ChildItem -LiteralPath $CurrentReleaseDir -File -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like "StackSetup-v*-win-x64.exe" } |
        Select-Object -First 1
    if ($installer) {
        return $installer.BaseName
    }

    return "release-current"
}

function Initialize-ReleaseFolders {
    param([object]$Paths)

    Ensure-StackDirectory -Path $Paths.OutputsRoot
    Ensure-StackDirectory -Path $Paths.ReleasesRoot
    Ensure-StackDirectory -Path $Paths.CurrentReleaseDir
    Ensure-StackDirectory -Path $Paths.LocalTestReleaseDir
    Ensure-StackDirectory -Path $Paths.ReleaseArchiveDir
    Ensure-StackDirectory -Path $Paths.MiscOutputDir

    if (Test-Path -LiteralPath $Paths.LegacyReleaseDir) {
        $movedLegacy = Move-StackItemToArchive -SourcePath $Paths.LegacyReleaseDir -ArchiveRoot $Paths.ReleaseArchiveDir -Label "legacy-release"
        if ($movedLegacy) {
            Write-Host "Archived legacy release folder to: $movedLegacy"
        }
    }

    if (Test-Path -LiteralPath $Paths.LegacyReleaseTestDir) {
        $movedLegacyTest = Move-StackItemToArchive -SourcePath $Paths.LegacyReleaseTestDir -ArchiveRoot $Paths.ReleaseArchiveDir -Label "legacy-release-test"
        if ($movedLegacyTest) {
            Write-Host "Archived legacy release test folder to: $movedLegacyTest"
        }
    }
}

function Prepare-CurrentReleaseOutput {
    param([object]$Paths)

    Ensure-StackDirectory -Path $Paths.CurrentReleaseDir
    $label = Get-CurrentReleaseArchiveLabel -CurrentReleaseDir $Paths.CurrentReleaseDir
    $archivedCurrent = Move-StackDirectoryContentsToArchive -SourceDir $Paths.CurrentReleaseDir -ArchiveRoot $Paths.ReleaseArchiveDir -Label $label
    if ($archivedCurrent) {
        Write-Host "Archived previous current release to: $archivedCurrent"
    }

    Ensure-StackDirectory -Path $Paths.CurrentReleaseDir
}

function Resolve-CMakePath {
    $cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
    if ($cmake) {
        return $cmake
    }

    $candidates = @(
        "C:\Program Files\CMake\bin\cmake.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )

    return $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

function Close-RunningStackProcess {
    $runningProcesses = Get-Process -Name Stack, StackModelService -ErrorAction SilentlyContinue
    if ($runningProcesses) {
        Write-Host "Closing running Stack processes so the build can finish cleanly..."
        $runningProcesses | Stop-Process -Force
    }
}

function Get-StackVersionInfo {
    param([string]$VersionFile)

    $content = Get-Content -Raw $VersionFile
    $major = [int]([regex]::Match($content, 'STACK_VERSION_MAJOR\s+(\d+)').Groups[1].Value)
    $minor = [int]([regex]::Match($content, 'STACK_VERSION_MINOR\s+(\d+)').Groups[1].Value)
    $patch = [int]([regex]::Match($content, 'STACK_VERSION_PATCH\s+(\d+)').Groups[1].Value)
    $publisherMatch = [regex]::Match($content, 'STACK_PUBLISHER\s+"([^"]+)"')
    $publisher = if ($publisherMatch.Success) { $publisherMatch.Groups[1].Value } else { "Charm" }

    $version = "$major.$minor.$patch"
    [pscustomobject]@{
        Major = $major
        Minor = $minor
        Patch = $patch
        Version = $version
        Tag = "v$version"
        Publisher = $publisher
    }
}

function Set-StackVersionInfo {
    param(
        [string]$VersionFile,
        [string]$Version
    )

    $versionParts = ConvertTo-StackVersionParts -Version $Version
    $currentVersionInfo = Get-StackVersionInfo -VersionFile $VersionFile
    if ($currentVersionInfo.Version -eq $versionParts.Version) {
        return
    }

    $content = Get-Content -Raw $VersionFile

    $updatedContent = $content
    $updatedContent = [regex]::Replace($updatedContent, 'set\(STACK_VERSION_MAJOR\s+\d+\)', "set(STACK_VERSION_MAJOR $($versionParts.Major))")
    $updatedContent = [regex]::Replace($updatedContent, 'set\(STACK_VERSION_MINOR\s+\d+\)', "set(STACK_VERSION_MINOR $($versionParts.Minor))")
    $updatedContent = [regex]::Replace($updatedContent, 'set\(STACK_VERSION_PATCH\s+\d+\)', "set(STACK_VERSION_PATCH $($versionParts.Patch))")

    if ($updatedContent -eq $content) {
        throw "Stack version file was not updated. Expected version markers were not found in $VersionFile"
    }

    Set-Content -LiteralPath $VersionFile -Value $updatedContent -Encoding UTF8
}

function Resolve-IsccPath {
    $candidates = @(
        (Get-Command ISCC.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue),
        (Join-Path $env:LOCALAPPDATA "Programs\Inno Setup 6\ISCC.exe"),
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        "C:\Program Files\Inno Setup 6\ISCC.exe"
    ) | Where-Object { $_ -and (Test-Path $_) }

    return $candidates | Select-Object -First 1
}

function Copy-ReleaseFile {
    param(
        [string]$SourcePath,
        [string]$DestinationPath
    )

    if (-not (Test-Path -LiteralPath $SourcePath)) {
        throw "Required release file is missing: $SourcePath"
    }

    $destinationDir = Split-Path -Parent $DestinationPath
    if ($destinationDir) {
        Ensure-StackDirectory -Path $destinationDir
    }

    Copy-Item -LiteralPath $SourcePath -Destination $DestinationPath -Force
}

function Copy-ReleaseDirectoryContents {
    param(
        [string]$SourceDir,
        [string]$DestinationDir
    )

    if (-not (Test-Path -LiteralPath $SourceDir)) {
        return
    }

    Ensure-StackDirectory -Path $DestinationDir
    Get-ChildItem -LiteralPath $SourceDir -Force | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $DestinationDir -Recurse -Force
    }
}

function Test-StackRelativePathInsideDirectory {
    param(
        [string]$BaseDir,
        [string]$RelativePath
    )

    if ([string]::IsNullOrWhiteSpace($BaseDir) -or [string]::IsNullOrWhiteSpace($RelativePath)) {
        return $false
    }

    if ([System.IO.Path]::IsPathRooted($RelativePath)) {
        return $false
    }

    $baseFullPath = [System.IO.Path]::GetFullPath($BaseDir)
    $candidateFullPath = [System.IO.Path]::GetFullPath((Join-Path $BaseDir $RelativePath))
    if (-not $baseFullPath.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
        $baseFullPath = $baseFullPath + [System.IO.Path]::DirectorySeparatorChar
    }

    return $candidateFullPath.StartsWith($baseFullPath, [System.StringComparison]::OrdinalIgnoreCase)
}

function Resolve-StackProviderRelativePath {
    param(
        [string]$ProviderDir,
        [string]$RelativePath
    )

    if (-not (Test-StackRelativePathInsideDirectory -BaseDir $ProviderDir -RelativePath $RelativePath)) {
        return $null
    }

    return [System.IO.Path]::GetFullPath((Join-Path $ProviderDir $RelativePath))
}

function Test-StackFfmpegLicenseMarkerApproved {
    param([string]$License)

    if ([string]::IsNullOrWhiteSpace($License)) {
        return $false
    }

    $lowerLicense = $License.ToLowerInvariant()
    if ($lowerLicense.Contains("nonfree") -or $lowerLicense.Contains("proprietary")) {
        return $false
    }

    if ($lowerLicense.Contains("lgpl")) {
        return $true
    }

    return -not $lowerLicense.Contains("gpl")
}

function Get-StackManifestStringArray {
    param(
        [object]$Manifest,
        [string]$PropertyName
    )

    $property = $Manifest.PSObject.Properties[$PropertyName]
    if (-not $property) {
        return $null
    }

    if ($null -eq $property.Value) {
        return $null
    }

    if ($property.Value -is [System.Array]) {
        return @($property.Value | ForEach-Object { [string]$_ })
    }

    return @([string]$property.Value)
}

function Test-StackFfmpegProviderFileList {
    param(
        [object]$Status,
        [object]$Manifest,
        [string]$PropertyName,
        [switch]$Required
    )

    $files = Get-StackManifestStringArray -Manifest $Manifest -PropertyName $PropertyName
    if ($null -eq $files) {
        if ($Required) {
            [void]$Status.Errors.Add("FFmpeg provider manifest is missing required $PropertyName entries.")
        }
        return
    }

    $files = @($files)
    if ($files.Count -eq 0) {
        if ($Required) {
            [void]$Status.Errors.Add("FFmpeg provider manifest is missing required $PropertyName entries.")
        }
        return
    }

    foreach ($file in $files) {
        if ([string]::IsNullOrWhiteSpace($file)) {
            [void]$Status.Errors.Add("FFmpeg provider manifest has an empty $PropertyName entry.")
            continue
        }

        $resolvedPath = Resolve-StackProviderRelativePath -ProviderDir $Status.ProviderDirectory -RelativePath $file
        if (-not $resolvedPath) {
            [void]$Status.Errors.Add("FFmpeg provider $PropertyName entry must be a relative path inside the provider directory: $file")
            continue
        }

        if (-not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
            [void]$Status.Errors.Add("FFmpeg provider $PropertyName file is missing: $file")
        }
    }
}

function Get-StackFfmpegProviderStatus {
    param([string]$ProviderDir)

    $status = [pscustomobject]@{
        ProviderDirectory = $ProviderDir
        ManifestPath = Join-Path $ProviderDir "ffmpeg-provider.json"
        ExecutablePath = ""
        ProviderName = ""
        FfmpegVersion = ""
        License = ""
        ConfigureLine = ""
        ProviderDirectoryPresent = $false
        ManifestPresent = $false
        ExecutablePresent = $false
        ApprovedForRedistribution = $false
        Available = $false
        UnsafeBuild = $false
        Message = ""
        Warnings = [System.Collections.Generic.List[string]]::new()
        Errors = [System.Collections.Generic.List[string]]::new()
    }

    $status.ProviderDirectoryPresent = Test-Path -LiteralPath $status.ProviderDirectory -PathType Container
    $status.ManifestPresent = Test-Path -LiteralPath $status.ManifestPath -PathType Leaf

    if (-not $status.ProviderDirectoryPresent -and -not $status.ManifestPresent) {
        $status.Message = "No optional FFmpeg provider found. Stack will package and run without packaged video encoding."
        return $status
    }

    if (-not $status.ManifestPresent) {
        [void]$status.Errors.Add("FFmpeg provider directory exists but ffmpeg-provider.json is missing.")
        $status.Message = "An optional FFmpeg provider was found but is not packageable."
        return $status
    }

    try {
        $manifest = Get-Content -Raw -LiteralPath $status.ManifestPath | ConvertFrom-Json
    }
    catch {
        [void]$status.Errors.Add("FFmpeg provider manifest could not be parsed: $($_.Exception.Message)")
        $status.Message = "An optional FFmpeg provider was found but is not packageable."
        return $status
    }

    $status.ProviderName = [string]$manifest.providerName
    $status.FfmpegVersion = [string]$manifest.ffmpegVersion
    $status.License = [string]$manifest.license
    $status.ConfigureLine = [string]$manifest.configureLine
    $status.ApprovedForRedistribution = [bool]$manifest.approvedForRedistribution

    if ($manifest.schema -ne "stack.ffmpegProvider") {
        [void]$status.Errors.Add("FFmpeg provider manifest schema is not stack.ffmpegProvider.")
    }

    if ([int]$manifest.schemaVersion -ne 1) {
        [void]$status.Errors.Add("FFmpeg provider manifest schemaVersion is not 1.")
    }

    if ([string]::IsNullOrWhiteSpace($manifest.binaryFile)) {
        [void]$status.Errors.Add("FFmpeg provider manifest is missing binaryFile.")
    }
    else {
        $resolvedExecutablePath = Resolve-StackProviderRelativePath -ProviderDir $status.ProviderDirectory -RelativePath ([string]$manifest.binaryFile)
        if (-not $resolvedExecutablePath) {
            [void]$status.Errors.Add("FFmpeg provider binaryFile must be a relative path inside the provider directory.")
        }
        else {
            $status.ExecutablePath = $resolvedExecutablePath
            $status.ExecutablePresent = Test-Path -LiteralPath $status.ExecutablePath -PathType Leaf
            if (-not $status.ExecutablePresent) {
                [void]$status.Errors.Add("FFmpeg provider executable is missing.")
            }
        }
    }

    if ([string]::IsNullOrWhiteSpace($status.ProviderName)) {
        [void]$status.Warnings.Add("FFmpeg provider manifest is missing providerName.")
    }

    if ([string]::IsNullOrWhiteSpace($status.FfmpegVersion)) {
        [void]$status.Warnings.Add("FFmpeg provider manifest is missing ffmpegVersion.")
    }

    if ([string]::IsNullOrWhiteSpace($status.License)) {
        [void]$status.Errors.Add("FFmpeg provider manifest is missing license.")
    }
    elseif (-not (Test-StackFfmpegLicenseMarkerApproved -License $status.License)) {
        $status.UnsafeBuild = $true
        [void]$status.Errors.Add("FFmpeg provider license marker is not approved for Stack packaging.")
    }

    $lowerConfigureLine = $status.ConfigureLine.ToLowerInvariant()
    if ($lowerConfigureLine.Contains("--enable-nonfree")) {
        $status.UnsafeBuild = $true
        [void]$status.Errors.Add("FFmpeg provider configure line includes --enable-nonfree.")
    }

    if ($lowerConfigureLine.Contains("--enable-gpl")) {
        $status.UnsafeBuild = $true
        [void]$status.Errors.Add("FFmpeg provider configure line includes --enable-gpl.")
    }

    if (-not $status.ApprovedForRedistribution) {
        [void]$status.Errors.Add("FFmpeg provider manifest is not approved for redistribution.")
    }

    Test-StackFfmpegProviderFileList -Status $status -Manifest $manifest -PropertyName "licenseFiles" -Required
    Test-StackFfmpegProviderFileList -Status $status -Manifest $manifest -PropertyName "sourceReferenceFiles" -Required
    Test-StackFfmpegProviderFileList -Status $status -Manifest $manifest -PropertyName "noticeFiles"

    if ($status.Errors.Count -eq 0) {
        $status.Available = $true
        $status.Message = "Approved optional FFmpeg provider is ready for packaging."
    }
    else {
        $status.Message = "An optional FFmpeg provider was found but is not packageable."
    }

    return $status
}

function Show-StackFfmpegProviderStatus {
    param([object]$Status)

    Write-Host $Status.Message
    Write-Host "Provider folder: $($Status.ProviderDirectory)"
    if ($Status.ExecutablePath) {
        Write-Host "Executable:      $($Status.ExecutablePath)"
    }
    if ($Status.ProviderName) {
        Write-Host "Provider:        $($Status.ProviderName)"
    }
    if ($Status.FfmpegVersion) {
        Write-Host "FFmpeg version:  $($Status.FfmpegVersion)"
    }
    if ($Status.License) {
        Write-Host "License marker:  $($Status.License)"
    }
    foreach ($warning in $Status.Warnings) {
        Write-Host "Warning: $warning"
    }
    foreach ($error in $Status.Errors) {
        Write-Host "Error: $error"
    }
}

function Copy-OptionalFfmpegProvider {
    param(
        [object]$Paths,
        [string]$StageDir
    )

    $status = Get-StackFfmpegProviderStatus -ProviderDir $Paths.FfmpegProviderSourceDir
    Show-StackFfmpegProviderStatus -Status $status

    if (-not $status.ProviderDirectoryPresent -and -not $status.ManifestPresent) {
        return $false
    }

    if ($status.Errors.Count -gt 0) {
        $message = "Optional FFmpeg provider failed packaging validation."
        foreach ($error in $status.Errors) {
            $message += "`n - $error"
        }
        throw $message
    }

    $destinationDir = Join-Path $StageDir $Paths.FfmpegProviderStageRelativeDir
    Ensure-StackDirectory -Path $destinationDir
    Copy-ReleaseDirectoryContents -SourceDir $Paths.FfmpegProviderSourceDir -DestinationDir $destinationDir
    Write-Host "Copied optional FFmpeg provider to: $destinationDir"
    return $true
}

function New-LicensePageFile {
    param(
        [string]$RootDir,
        [string]$DestinationPath
    )

    $eulaPath = Join-Path $RootDir "legal\EULA.txt"
    if (-not (Test-Path -LiteralPath $eulaPath)) {
        throw "A Stack binary EULA is required before packaging an installer."
    }
    Ensure-StackDirectory -Path (Split-Path -Parent $DestinationPath)
    Copy-Item -LiteralPath $eulaPath -Destination $DestinationPath -Force
}

function Write-Sha256Sums {
    param(
        [string[]]$Files,
        [string]$DestinationPath
    )

    $lines = foreach ($file in $Files) {
        if (-not (Test-Path -LiteralPath $file)) {
            continue
        }

        $hash = Get-FileHash -LiteralPath $file -Algorithm SHA256
        '{0} *{1}' -f $hash.Hash.ToLowerInvariant(), (Split-Path -Leaf $file)
    }

    if ($lines.Count -gt 0) {
        Set-Content -LiteralPath $DestinationPath -Value $lines -Encoding ASCII
    }
}

function Get-ExtraBuildFolders {
    param([string]$Root)

    Get-ChildItem -LiteralPath $Root -Directory -Force |
        Where-Object { $_.Name -match '^build[-_].+' } |
        Sort-Object Name
}

function Move-ExtraBuildFoldersToArchive {
    param([string]$Root)

    $folders = @(Get-ExtraBuildFolders -Root $Root)
    if ($folders.Count -eq 0) {
        return @()
    }

    $paths = Get-StackPaths -Root $Root
    Ensure-StackDirectory -Path $paths.ExtraBuildArchiveRoot

    $sessionArchiveDir = Join-Path $paths.ExtraBuildArchiveRoot (Get-StackTimestamp)
    Ensure-StackDirectory -Path $sessionArchiveDir

    $movedPaths = @()
    foreach ($folder in $folders) {
        $destinationPath = Join-Path $sessionArchiveDir $folder.Name
        Move-Item -LiteralPath $folder.FullName -Destination $destinationPath
        $movedPaths += $destinationPath
    }

    return $movedPaths
}

function Open-StackPath {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Path not found: $Path"
    }

    Start-Process explorer.exe $Path | Out-Null
}
