param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = "",
    [switch]$Launch,
    [switch]$SkipClose
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "stack_workflow.ps1")

$Root = (Resolve-Path $Root).Path
$paths = Get-StackPaths -Root $Root
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = $paths.BuildDir
}

Invoke-StackEnvironmentRepair -Root $Root

if (-not $SkipClose) {
    Close-RunningStackProcess
}

$cmake = Resolve-CMakePath
if (-not $cmake) {
    Write-Host "CMake was not found. Install CMake or Visual Studio Build Tools with CMake support, then run stack-tools.cmd or build.cmd again."
    exit 9009
}

Write-Host "Configuring Stack in: $BuildDir"
$configureArgs = @(
    "-S", $Root,
    "-B", $BuildDir,
    "-DSTACK_ENABLE_APPWINDOW_TITLEBAR=ON"
)

$localAppSdkRoot = Join-Path $Root "_workspace\deps\Microsoft.WindowsAppSDK\2.2.0"
if (Test-Path -LiteralPath $localAppSdkRoot) {
    $configureArgs += "-DSTACK_WINDOWS_APP_SDK_ROOT=$localAppSdkRoot"
}

& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Building Stack (Release)..."
& $cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$exePath = Join-Path $BuildDir "Stack.exe"
Write-Host ""
Write-Host "Build complete."
Write-Host "Use this executable: $exePath"

if ($Launch) {
    if (-not (Test-Path -LiteralPath $exePath)) {
        throw "Expected executable was not found after the build: $exePath"
    }

    Write-Host "Launching Stack..."
    Start-Process -FilePath $exePath | Out-Null
}
