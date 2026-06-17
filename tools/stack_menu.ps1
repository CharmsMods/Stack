param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "stack_workflow.ps1")

$Root = (Resolve-Path $Root).Path

function Pause-StackMenu {
    [void](Read-Host "Press Enter to continue")
}

function Show-StackMenuHeader {
    param([object]$Paths)

    $versionInfo = Get-StackVersionInfo -VersionFile $Paths.VersionFile
    $suggestedVersion = Get-NextStackPatchVersion -Version $versionInfo.Version

    Clear-Host
    Write-Host "Stack Build and Release Menu"
    Write-Host "============================"
    Write-Host ""
    Write-Host "Use this menu if you are not sure which script to run."
    Write-Host ""
    Write-Host "Version"
    Write-Host "-------"
    Write-Host "Current version:        $($versionInfo.Version)"
    Write-Host "Suggested next patch:   $suggestedVersion"
    Write-Host ""
    Write-Host "Main paths"
    Write-Host "----------"
    Write-Host "Official app build:     $($Paths.BuildDir)\Stack.exe"
    Write-Host "Current release folder: $($Paths.CurrentReleaseDir)"
    Write-Host "Release archive:        $($Paths.ReleaseArchiveDir)"
    Write-Host ""
    Write-Host "Actions"
    Write-Host "-------"
    Write-Host "1. Build the app"
    Write-Host "2. Build the app and launch it"
    Write-Host "3. Package a full release (build + installer + zip + hashes)"
    Write-Host "4. Package a release without the installer"
    Write-Host "5. Validate the current build"
    Write-Host "6. Open the build folder"
    Write-Host "7. Open the current release folder"
    Write-Host "8. Open the release archive folder"
    Write-Host "9. Archive extra old build folders"
    Write-Host "10. Show the important paths again"
    Write-Host "Q. Quit"
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

    & $exePath --validate-layer-registry
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Validation passed."
    }
    else {
        Write-Host "Validation failed with exit code $LASTEXITCODE."
    }
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

    Write-Host ""
    Write-Host "Current Stack version: $($versionInfo.Version)"
    Write-Host "Version format:        major.minor.patch"
    Write-Host "Major number:          big release line"
    Write-Host "Minor number:          feature release inside that line"
    Write-Host "Patch number:          small update counter"
    Write-Host ""
    Write-Host "Suggested next version for this ${ActionLabel}: $suggestedVersion"
    Write-Host "Press Enter to use $suggestedVersion."
    Write-Host "Type K to keep the current version $($versionInfo.Version)."
    Write-Host "Type another version like 1.1.0 to use that instead."
    Write-Host "Type C to cancel."

    while ($true) {
        $versionInput = Read-Host "Version for this $ActionLabel"
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
                    & (Join-Path $Root "tools\build_stack.ps1") -Root $Root -BuildDir $paths.BuildDir
                }
                Pause-StackMenu
            }
            "2" {
                if (Confirm-StackVersionForAction -Paths $paths -ActionLabel "build") {
                    & (Join-Path $Root "tools\build_stack.ps1") -Root $Root -BuildDir $paths.BuildDir -Launch
                }
                Pause-StackMenu
            }
            "3" {
                if (Confirm-StackVersionForAction -Paths $paths -ActionLabel "release package") {
                    & (Join-Path $Root "tools\create_release.ps1") -Root $Root -BuildDir $paths.BuildDir
                }
                Pause-StackMenu
            }
            "4" {
                if (Confirm-StackVersionForAction -Paths $paths -ActionLabel "release package") {
                    & (Join-Path $Root "tools\create_release.ps1") -Root $Root -BuildDir $paths.BuildDir -SkipInstaller
                }
                Pause-StackMenu
            }
            "5" {
                Invoke-StackValidation -Paths $paths
                Pause-StackMenu
            }
            "6" {
                Open-StackPath -Path $paths.BuildDir
                Pause-StackMenu
            }
            "7" {
                Open-StackPath -Path $paths.CurrentReleaseDir
                Pause-StackMenu
            }
            "8" {
                Open-StackPath -Path $paths.ReleaseArchiveDir
                Pause-StackMenu
            }
            "9" {
                Invoke-ExtraBuildFolderArchive -Root $Root
                Pause-StackMenu
            }
            "10" {
                $versionInfo = Get-StackVersionInfo -VersionFile $paths.VersionFile
                $suggestedVersion = Get-NextStackPatchVersion -Version $versionInfo.Version
                Write-Host ""
                Write-Host "Current version:        $($versionInfo.Version)"
                Write-Host "Suggested next patch:   $suggestedVersion"
                Write-Host "Version file:           $($paths.VersionFile)"
                Write-Host "Official build folder: $($paths.BuildDir)"
                Write-Host "Use this executable:   $(Join-Path $paths.BuildDir 'Stack.exe')"
                Write-Host "Current release:       $($paths.CurrentReleaseDir)"
                Write-Host "Release archive:       $($paths.ReleaseArchiveDir)"
                Write-Host "Extra build archive:   $($paths.ExtraBuildArchiveRoot)"
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
