param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$BuildDir = "",
    [string]$BuildTreeDir = "",
    [int]$Parallel = 14,
    [switch]$Launch,
    [switch]$SkipClose
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "..\dev\stack_workflow.ps1")

$Root = (Resolve-Path $Root).Path
$paths = Get-StackPaths -Root $Root
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = $paths.BuildDir
}
if ([string]::IsNullOrWhiteSpace($BuildTreeDir)) {
    $BuildTreeDir = $paths.BuildTreeDir
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

Write-Host "Configuring private build tree in: $BuildTreeDir"
Write-Host "Deploying runnable Stack to: $BuildDir"
$configureArgs = @(
    "-S", $Root,
    "-B", $BuildTreeDir,
    "-DSTACK_DEPLOY_DIR=$BuildDir",
    "-DSTACK_ENABLE_APPWINDOW_TITLEBAR=ON",
    "-DSTACK_ENABLE_RESTORMER_EXECUTION=OFF",
    "-DSTACK_BUILD_MODEL_SERVICE=OFF",
    "-DSTACK_BUILD_AUXILIARY_TOOLS=OFF"
    "-DSTACK_MSVC_COMPILE_JOBS=$Parallel"
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
$buildArgs = @("--build", $BuildTreeDir, "--config", "Release")
if ($Parallel -gt 0) {
    $buildArgs += @("--parallel", $Parallel.ToString())
}
& $cmake @buildArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$exePath = Join-Path $BuildDir "Stack.exe"
Copy-OptionalFfmpegProvider -Paths $paths -StageDir $BuildDir | Out-Null
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
