param(
    [Parameter(Mandatory = $true)]
    [string]$StageDir,
    [ValidateRange(5, 120)]
    [int]$WaitSeconds = 20,
    [switch]$Interactive,
    [switch]$KeepArtifacts
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-StackCondition {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Assert-StackValue {
    param(
        $Actual,
        $Expected,
        [string]$Name
    )

    if ($Actual -ne $Expected) {
        throw "$Name must be '$Expected'; found '$Actual'."
    }
}

function Assert-StackFloat {
    param(
        [double]$Actual,
        [double]$Expected,
        [string]$Name
    )

    if ([Math]::Abs($Actual - $Expected) -gt 0.0005) {
        throw "$Name must be $Expected; found $Actual."
    }
}

function Stop-ValidationProcess {
    param([System.Diagnostics.Process]$Process)

    if ($null -eq $Process) {
        return
    }
    try {
        if ($Process.HasExited) {
            return
        }
        if ($Process.CloseMainWindow()) {
            if ($Process.WaitForExit(5000)) {
                return
            }
        }
        $Process.Kill()
        $Process.WaitForExit()
    }
    catch {
        Write-Warning "Could not close the fresh-profile validation process: $($_.Exception.Message)"
    }
}

$stagePath = (Resolve-Path -LiteralPath $StageDir).Path
$expectedStageEntries = @("Stack", "Stack.exe")
$actualStageEntries = @(
    Get-ChildItem -LiteralPath $stagePath -Force |
        Select-Object -ExpandProperty Name |
        Sort-Object
)
Assert-StackCondition `
    (($actualStageEntries -join "|") -eq ($expectedStageEntries -join "|")) `
    "The staged release root must contain only Stack.exe and Stack. Found: $($actualStageEntries -join ', ')."

$sourceExe = Join-Path $stagePath "Stack.exe"
$sourceStack = Join-Path $stagePath "Stack"
$sourceApp = Join-Path $sourceStack "App"
$sourceRuntime = Join-Path $sourceApp "Runtime"
$sourceLibRaw = Join-Path $sourceRuntime "libraw.dll"
$sourceBootstrap = Join-Path $sourceRuntime "Microsoft.WindowsAppRuntime.Bootstrap.dll"
foreach ($requiredPath in @($sourceExe, $sourceApp, $sourceLibRaw, $sourceBootstrap)) {
    Assert-StackCondition (Test-Path -LiteralPath $requiredPath) "Required staged payload is missing: $requiredPath"
}
Assert-StackCondition `
    (-not (Test-Path -LiteralPath (Join-Path $sourceStack "User"))) `
    "The staged release contains Stack\User. Fresh-profile validation requires an app-only stage."
Assert-StackCondition `
    (-not (Test-Path -LiteralPath (Join-Path $sourceStack "Temp"))) `
    "The staged release contains Stack\Temp. Fresh-profile validation requires an app-only stage."
Assert-StackCondition `
    (-not (Test-Path -LiteralPath (Join-Path $stagePath "libraw.dll"))) `
    "libraw.dll is flattened beside Stack.exe; keep it under Stack\App\Runtime."
Assert-StackCondition `
    (-not (Test-Path -LiteralPath (Join-Path $stagePath "Microsoft.WindowsAppRuntime.Bootstrap.dll"))) `
    "The bootstrap DLL is flattened beside Stack.exe; keep it under Stack\App\Runtime."

$validationRoot = Join-Path `
    ([System.IO.Path]::GetTempPath()) `
    ("Stack-fresh-profile-validation-{0}" -f [Guid]::NewGuid().ToString("N"))
$payloadDir = Join-Path $validationRoot "Payload"
$workingDir = Join-Path $validationRoot "UnrelatedWorkingDirectory"
$payloadStack = Join-Path $payloadDir "Stack"
$payloadExe = Join-Path $payloadDir "Stack.exe"
$settingsPath = Join-Path $payloadStack "User\Settings\StackSettings.json"
$tracePath = Join-Path $payloadStack "Temp\Logs\appwindow_titlebar_trace.log"
$process = $null

try {
    New-Item -ItemType Directory -Path $payloadDir, $workingDir, $payloadStack | Out-Null
    Copy-Item -LiteralPath $sourceExe -Destination $payloadExe
    Copy-Item -LiteralPath $sourceApp -Destination $payloadStack -Recurse

    Assert-StackCondition (-not (Test-Path -LiteralPath (Join-Path $payloadStack "User"))) "The disposable payload was not fresh before launch."
    Assert-StackCondition (-not (Test-Path -LiteralPath (Join-Path $payloadStack "Temp"))) "The disposable payload was not fresh before launch."

    $previousTrace = [Environment]::GetEnvironmentVariable("STACK_APPWINDOW_TITLEBAR_TRACE", "Process")
    try {
        [Environment]::SetEnvironmentVariable("STACK_APPWINDOW_TITLEBAR_TRACE", "1", "Process")
        $process = Start-Process `
            -FilePath $payloadExe `
            -WorkingDirectory $workingDir `
            -PassThru
    }
    finally {
        [Environment]::SetEnvironmentVariable("STACK_APPWINDOW_TITLEBAR_TRACE", $previousTrace, "Process")
    }

    $deadline = [DateTime]::UtcNow.AddSeconds($WaitSeconds)
    $modulePaths = @()
    do {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
        if ($process.HasExited) {
            throw "Stack exited before fresh-profile validation completed (exit code $($process.ExitCode))."
        }
        try {
            $modulePaths = @($process.Modules | ForEach-Object { $_.FileName })
        }
        catch {
            $modulePaths = @()
        }
        $hasSettings = Test-Path -LiteralPath $settingsPath
        $hasEnabledTitleBar = $false
        if (Test-Path -LiteralPath $tracePath) {
            $hasEnabledTitleBar = [bool](Select-String -LiteralPath $tracePath -Pattern "event=enabled .* active=1 " -Quiet)
        }
        $hasLibRawModule = [bool]($modulePaths | Where-Object {
            [string]::Equals($_, (Join-Path $payloadStack "App\Runtime\libraw.dll"), [StringComparison]::OrdinalIgnoreCase)
        })
        $hasBootstrapModule = [bool]($modulePaths | Where-Object {
            [string]::Equals($_, (Join-Path $payloadStack "App\Runtime\Microsoft.WindowsAppRuntime.Bootstrap.dll"), [StringComparison]::OrdinalIgnoreCase)
        })
    } while (
        [DateTime]::UtcNow -lt $deadline -and
        (-not $hasSettings -or -not $hasLibRawModule -or -not $hasBootstrapModule)
    )

    Assert-StackCondition $hasSettings "Stack did not create versioned settings within $WaitSeconds seconds."
    Assert-StackCondition $hasLibRawModule "Stack did not load LibRaw from Stack\App\Runtime."
    Assert-StackCondition $hasBootstrapModule "Stack did not load the bootstrap DLL from Stack\App\Runtime."

    $settings = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
    $appearance = $settings.appearance
    Assert-StackValue $settings.version 13 "settings version"
    Assert-StackValue $appearance.activePresetId "solarized" "active preset"
    Assert-StackValue $appearance.graphVisualMode "Classic" "graph visual mode"
    Assert-StackValue $appearance.graphSpotlightHaloOutlines $false "graph halo outlines"
    Assert-StackValue $appearance.graphDottedMaskLinks $false "dotted mask links"
    Assert-StackValue $appearance.graphStraightLinks $false "straight links"
    Assert-StackFloat $appearance.graphLineOpacity 1.0 "graph line opacity"
    Assert-StackFloat $appearance.graphPanSensitivity 0.28 "graph pan sensitivity"
    Assert-StackFloat $appearance.graphNodeSliderDragSensitivity 0.16 "node slider sensitivity"
    Assert-StackValue $appearance.graphConnectionLabels "Adaptive" "connection label visibility"
    Assert-StackValue $appearance.graphConnectionTextLayout "Floating" "connection text layout"
    Assert-StackFloat $appearance.graphConnectionTextSize 11.0 "connection text size"
    Assert-StackValue $appearance.graphConnectionTextSizing "Fixed" "connection text sizing"
    Assert-StackValue $appearance.graphConnectionTextOutline $false "connection text outline"
    Assert-StackValue $appearance.experimentalIslandEnabled $false "Island option"
    Assert-StackCondition `
        ($null -eq $appearance.PSObject.Properties["seamlessSurfacesEnabled"]) `
        "The fresh profile serialized the obsolete seamless-workspace toggle."
    Assert-StackValue $appearance.backgroundImageEnabled $false "background image enabled"
    Assert-StackValue $appearance.backgroundImagePath "" "background image path"
    Assert-StackFloat $appearance.backgroundImageStrength 1.0 "background strength"
    Assert-StackFloat $appearance.uiSurfaceTransparency 0.0 "UI surface transparency"
    Assert-StackCondition (@($appearance.backgroundImages).Count -eq 0) "The fresh profile contains personal background records."
    Assert-StackCondition (@($appearance.presets).Count -eq 0) "The fresh profile contains personal appearance presets."
    Assert-StackValue $appearance.viewportTiling.mode "Off" "viewport tiling mode"
    Assert-StackValue $appearance.viewportTiling.tileSize 1024 "viewport tile size"
    Assert-StackValue $appearance.viewportTiling.autoPixelThresholdMegapixels 32 "viewport auto threshold"
    Assert-StackValue $appearance.viewportTiling.progressive $false "progressive tiling"
    Assert-StackValue $appearance.viewportTiling.debugOverlay $false "tiling debug overlay"

    $workingDirectoryEntries = @(Get-ChildItem -LiteralPath $workingDir -Force)
    Assert-StackCondition ($workingDirectoryEntries.Count -eq 0) "Stack wrote files into the unrelated working directory."
    $payloadRootEntries = @(
        Get-ChildItem -LiteralPath $payloadDir -Force |
            Select-Object -ExpandProperty Name |
            Sort-Object
    )
    Assert-StackCondition `
        (($payloadRootEntries -join "|") -eq ($expectedStageEntries -join "|")) `
        "Stack wrote an unexpected item beside Stack.exe: $($payloadRootEntries -join ', ')."

    if ($Interactive) {
        Write-Host ""
        Write-Host "Fresh validation payload: $payloadDir"
        Write-Host "Inspect the Graph view, then close Stack or press Enter here to close it."
        Read-Host | Out-Null
    }

    Stop-ValidationProcess -Process $process
    $process = $null

    Assert-StackCondition (Test-Path -LiteralPath $tracePath) "Stack did not write the AppWindow title-bar trace under Stack\Temp\Logs."
    $hasEnabledTitleBar = [bool](
        Select-String -LiteralPath $tracePath -Pattern "event=enabled .* active=1 " -Quiet
    )
    Assert-StackCondition $hasEnabledTitleBar "The managed AppWindow title-bar bridge did not report event=enabled with active=1."

    Write-Host "Fresh-profile validation passed."
    Write-Host "  Settings: canonical version-13 profile"
    Write-Host "  Runtime: managed LibRaw and AppWindow bootstrap DLLs"
    Write-Host "  Writes: Stack\User and Stack\Temp only"
    if ($KeepArtifacts) {
        Write-Host "  Disposable payload retained at: $validationRoot"
    }
}
finally {
    Stop-ValidationProcess -Process $process
    if (-not $KeepArtifacts -and (Test-Path -LiteralPath $validationRoot)) {
        $resolvedValidationRoot = (Resolve-Path -LiteralPath $validationRoot).Path
        $resolvedTempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\')
        Assert-StackCondition `
            ($resolvedValidationRoot.StartsWith($resolvedTempRoot + '\', [StringComparison]::OrdinalIgnoreCase)) `
            "Refusing to remove a validation directory outside the system temp directory: $resolvedValidationRoot"
        Remove-Item -LiteralPath $resolvedValidationRoot -Recurse
    }
}
