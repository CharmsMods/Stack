param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "stack_workflow.ps1")
. (Join-Path $PSScriptRoot "..\release\legal_compliance.ps1")

$Root = (Resolve-Path $Root).Path

function Pause-StackMenu {
    Write-Host ""
    [void](Read-Host "Press Enter to continue")
}

function Write-StackMenuChoice {
    param(
        [string]$Key,
        [string]$Label,
        [switch]$Highlight
    )

    Write-Host ("  {0,2}  " -f $Key) -ForegroundColor Cyan -NoNewline
    if ($Highlight) {
        Write-Host $Label -ForegroundColor Cyan
    }
    else {
        Write-Host $Label
    }
}

function Show-StackMenuHeader {
    param([object]$Paths)

    $versionInfo = Get-StackVersionInfo -VersionFile $Paths.VersionFile
    $legalManifest = Get-StackLegalManifest -Root $Paths.Root

    Clear-Host
    Write-Host "STACK TOOLS" -ForegroundColor Cyan
    Write-Host "Version $($versionInfo.Version)  |  Legal: $($legalManifest.approvalState)"
    Write-Host "--------------------------------------------------------" -ForegroundColor DarkGray
    Write-Host ""
    Write-Host "Build and package"
    Write-StackMenuChoice "1" "Build app"
    Write-StackMenuChoice "2" "Build and launch"
    Write-StackMenuChoice "3" "Create installer + portable ZIP (unsigned)" -Highlight
    Write-StackMenuChoice "4" "Signed public release"
    Write-StackMenuChoice "5" "Local test installer (isolated profile)"
    Write-Host ""
    Write-Host "Check and maintain"
    Write-StackMenuChoice "6" "Validate current build"
    Write-StackMenuChoice "10" "Archive old build folders"
    Write-StackMenuChoice "11" "FFmpeg provider status"
    Write-StackMenuChoice "12" "Version and full paths"
    Write-Host ""
    Write-Host "Open folders"
    Write-Host "   7  Build     8  Current release     9  Release archive"
    Write-Host ""
    Write-StackMenuChoice "Q" "Quit"
    Write-Host ""
}

function Invoke-StackValidation {
    param([object]$Paths)

    $exePath = Join-Path $Paths.BuildDir "Stack.exe"
    if (-not (Test-Path -LiteralPath $exePath)) {
        Write-Host "No build was found yet."
        Write-Host "Build the app first so there is a Stack.exe to validate."
        return
    }

    $allPassed = $true

    & $exePath --validate-layer-registry
    if ($LASTEXITCODE -ne 0) {
        $allPassed = $false
        Write-Host "Layer registry validation failed with exit code $LASTEXITCODE."
    }

    & $exePath --validate-ffmpeg-provider
    if ($LASTEXITCODE -ne 0) {
        $allPassed = $false
        Write-Host "FFmpeg provider validation failed with exit code $LASTEXITCODE."
    }

    if ($allPassed) {
        Write-Host "Validation passed."
    }
    else {
        Write-Host "Validation failed."
    }
}

function Show-OptionalFfmpegProviderStatus {
    param([object]$Paths)

    $status = Get-StackFfmpegProviderStatus -ProviderDir $Paths.FfmpegProviderSourceDir
    Show-StackFfmpegProviderStatus -Status $status
}

function Invoke-ExtraBuildFolderArchive {
    param([string]$Root)

    $folders = @(Get-ExtraBuildFolders -Root $Root)
    if ($folders.Count -eq 0) {
        Write-Host "No extra build folders were found."
        Write-Host "The official build folder is already just: build"
        return
    }

    Write-Host "These extra build folders were found:"
    foreach ($folder in $folders) {
        Write-Host " - $($folder.Name)"
    }

    Write-Host ""
    $confirmation = Read-Host "Type ARCHIVE to move them into _local_archive\\build-folders"
    if ($confirmation -cne "ARCHIVE") {
        Write-Host "Archive action cancelled."
        return
    }

    $movedPaths = @(Move-ExtraBuildFoldersToArchive -Root $Root)
    if ($movedPaths.Count -eq 0) {
        Write-Host "Nothing was moved."
        return
    }

    Write-Host "Archived build folders:"
    foreach ($path in $movedPaths) {
        Write-Host " - $path"
    }
}

function Confirm-StackVersionForAction {
    param(
        [object]$Paths,
        [string]$ActionLabel
    )

    $versionInfo = Get-StackVersionInfo -VersionFile $Paths.VersionFile
    $suggestedVersion = Get-NextStackPatchVersion -Version $versionInfo.Version

    Clear-Host
    Write-Host "Version for this $ActionLabel" -ForegroundColor Cyan
    Write-Host "Current: $($versionInfo.Version)"
    Write-Host "--------------------------------------------------------" -ForegroundColor DarkGray
    Write-Host ""
    Write-Host "  Enter  Use next patch $suggestedVersion"
    Write-Host "      K  Keep $($versionInfo.Version)"
    Write-Host "      C  Cancel"
    Write-Host ""
    Write-Host "Or type a version as major.minor.patch, for example 1.3.0."
    Write-Host ""

    while ($true) {
        $versionInput = Read-Host "Version"
        if ($null -eq $versionInput) {
            return $false
        }

        $trimmedInput = $versionInput.Trim()
        if ([string]::IsNullOrWhiteSpace($trimmedInput)) {
            Set-StackVersionInfo -VersionFile $Paths.VersionFile -Version $suggestedVersion
            Write-Host "Using suggested version $suggestedVersion."
            return $true
        }

        if ($trimmedInput -match '^[cC]$') {
            Write-Host "Cancelled."
            return $false
        }

        if ($trimmedInput -match '^[kK]$') {
            Write-Host "Keeping current version $($versionInfo.Version)."
            return $true
        }

        if (-not (Test-StackSemanticVersion -Version $trimmedInput)) {
            Write-Host "Please use a version like 1.1.0."
            continue
        }

        if ($trimmedInput -eq $versionInfo.Version) {
            Write-Host "Version is already $trimmedInput."
            return $true
        }

        Set-StackVersionInfo -VersionFile $Paths.VersionFile -Version $trimmedInput
        Write-Host "Updated Stack version to $trimmedInput."
        return $true
    }
}

Invoke-StackEnvironmentRepair -Root $Root
$paths = Get-StackPaths -Root $Root
Initialize-ReleaseFolders -Paths $paths

while ($true) {
    Show-StackMenuHeader -Paths $paths
    $rawChoice = Read-Host "Choose an option"
    if ($null -eq $rawChoice) {
        break
    }

    $choice = $rawChoice.Trim().ToUpperInvariant()
    $shouldExitMenu = $false

    try {
        switch ($choice) {
            "1" {
                if (Confirm-StackVersionForAction -Paths $paths -ActionLabel "build") {
                    & (Join-Path $Root "tools\build\build_stack.ps1") -Root $Root -BuildDir $paths.BuildDir
                }
                Pause-StackMenu
            }
            "2" {
                if (Confirm-StackVersionForAction -Paths $paths -ActionLabel "build") {
                    & (Join-Path $Root "tools\build\build_stack.ps1") -Root $Root -BuildDir $paths.BuildDir -Launch
                }
                Pause-StackMenu
            }
            "3" {
                if (Confirm-StackVersionForAction -Paths $paths -ActionLabel "release package") {
                    & (Join-Path $Root "tools\release\create_release.ps1") -Root $Root -BuildDir $paths.BuildDir
                }
                Pause-StackMenu
            }
            "4" {
                $publicLegalReady = $true
                try {
                    Test-StackLegalPacket -Root $Root -RequireApproved | Out-Null
                }
                catch {
                    $publicLegalReady = $false
                    Write-Host "Public packaging is blocked before the version is changed:"
                    Write-Host $_.Exception.Message
                }
                if ($publicLegalReady -and
                    (Confirm-StackVersionForAction -Paths $paths -ActionLabel "public release package")) {
                    & (Join-Path $Root "tools\release\create_release.ps1") -Root $Root -BuildDir $paths.BuildDir -PublicRelease
                }
                Pause-StackMenu
            }
            "5" {
                if (Confirm-StackVersionForAction -Paths $paths -ActionLabel "local test package") {
                    & (Join-Path $Root "tools\release\create_release.ps1") -Root $Root -BuildDir $paths.BuildDir -LocalTestPackage
                }
                Pause-StackMenu
            }
            "6" {
                Invoke-StackValidation -Paths $paths
                Pause-StackMenu
            }
            "7" {
                Open-StackPath -Path $paths.BuildDir
                Pause-StackMenu
            }
            "8" {
                Open-StackPath -Path $paths.CurrentReleaseDir
                Pause-StackMenu
            }
            "9" {
                Open-StackPath -Path $paths.ReleaseArchiveDir
                Pause-StackMenu
            }
            "10" {
                Invoke-ExtraBuildFolderArchive -Root $Root
                Pause-StackMenu
            }
            "11" {
                Show-OptionalFfmpegProviderStatus -Paths $paths
                Pause-StackMenu
            }
            "12" {
                $versionInfo = Get-StackVersionInfo -VersionFile $paths.VersionFile
                $suggestedVersion = Get-NextStackPatchVersion -Version $versionInfo.Version
                Clear-Host
                Write-Host "Version and paths" -ForegroundColor Cyan
                Write-Host ""
                Write-Host "Current version:        $($versionInfo.Version)"
                Write-Host "Suggested next patch:   $suggestedVersion"
                Write-Host "Version file:           $($paths.VersionFile)"
                Write-Host "Official build folder:  $($paths.BuildDir)"
                Write-Host "Use this executable:    $(Join-Path $paths.BuildDir 'Stack.exe')"
                Write-Host "Current release:        $($paths.CurrentReleaseDir)"
                Write-Host "Local test output:      $($paths.LocalTestReleaseDir)"
                Write-Host "Release archive:        $($paths.ReleaseArchiveDir)"
                Write-Host "Extra build archive:    $($paths.ExtraBuildArchiveRoot)"
                Write-Host "FFmpeg provider source: $($paths.FfmpegProviderSourceDir)"
                Write-Host "FFmpeg package path:    $($paths.FfmpegProviderStageRelativeDir)"
                Pause-StackMenu
            }
            "Q" {
                $shouldExitMenu = $true
            }
            default {
                Write-Host "Please choose one of the listed options."
                Pause-StackMenu
            }
        }

        if ($shouldExitMenu) {
            break
        }
    }
    catch {
        Write-Host ""
        Write-Host "That action stopped because of an error:"
        Write-Host $_.Exception.Message
        Pause-StackMenu
    }
}
