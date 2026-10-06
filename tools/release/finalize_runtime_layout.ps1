param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "..\dev\stack_workflow.ps1")

$Root = (Resolve-Path -LiteralPath $Root).Path
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $Root "build"
}
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
$expectedBuildDir = [System.IO.Path]::GetFullPath((Join-Path $Root "build"))
if (-not $BuildDir.Equals($expectedBuildDir, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to finalize an unexpected deployment directory: $BuildDir"
}

Close-RunningStackProcess

$requiredFiles = @(
    "Stack.exe",
    "Stack\App\Runtime\libraw.dll",
    "Stack\App\Runtime\Microsoft.WindowsAppRuntime.Bootstrap.dll",
    "Stack\App\Legal\LICENSE",
    "Stack\App\Legal\EULA.txt",
    "Stack\App\Legal\PRIVACY.md",
    "Stack\App\Legal\INFO_BEFORE_INSTALL.txt",
    "Stack\App\Legal\THIRD_PARTY_NOTICES.md",
    "Stack\App\Legal\ThirdParty\DearImGui-MIT.txt",
    "Stack\App\Legal\ThirdParty\GLFW-zlib-libpng.txt",
    "Stack\App\Legal\ThirdParty\NlohmannJSON-MIT.txt",
    "Stack\App\Legal\ThirdParty\stb-MIT-or-Public-Domain.txt",
    "Stack\App\Legal\ThirdParty\LibRaw-LGPL-2.1.txt",
    "Stack\App\Legal\ThirdParty\LibRaw-COPYRIGHT.txt",
    "Stack\App\Legal\ThirdParty\Microsoft-WindowsAppSDK-License.txt",
    "Stack\App\Legal\ThirdParty\OpenCV-Apache-2.0.txt",
    "Stack\App\Legal\ThirdParty\OpenCV-COPYRIGHT.txt",
    "Stack\App\Legal\ThirdParty\OpenCV-zlib.txt",
    "Stack\App\Legal\ThirdParty\OpenCV-SoftFloat-FDLIBM.txt",
    "Stack\App\Legal\ThirdParty\OpenCV-FLANN-BSD.txt",
    "Stack\User\Settings\StackSettings.json",
    "Stack\User\Settings\RawWorkspaceState.json",
    "Stack\User\Settings\LibraryViewState.json",
    "Stack\User\Settings\StackUpdateState.json",
    "Stack\User\Settings\imgui.ini",
    "Stack\User\Settings\.runtime-layout-v2.migrated"
)
foreach ($relativePath in $requiredFiles) {
    $path = Join-Path $BuildDir $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required migrated or shipped file is missing: $path"
    }
}

$requiredDirectories = @(
    "Stack\User\Library",
    "Stack\User\Presets",
    "Stack\User\Media\Backgrounds",
    "Stack\Temp\Cache",
    "Stack\Temp\Logs",
    "Stack\Temp\Updates"
)
foreach ($relativePath in $requiredDirectories) {
    $path = Join-Path $BuildDir $relativePath
    if (-not (Test-Path -LiteralPath $path -PathType Container)) {
        throw "Required runtime directory is missing: $path"
    }
}

$legacyDataDirectories = @("Cache", "Library", "Logs", "Presets", "UpdateCache")
foreach ($name in $legacyDataDirectories) {
    $path = Join-Path $BuildDir $name
    if ((Test-Path -LiteralPath $path -PathType Container) -and
        (Get-ChildItem -LiteralPath $path -Force | Select-Object -First 1)) {
        throw "Legacy data remains in $path. Refusing to delete it; inspect the migration log first."
    }
}

$obsoleteFiles = @(
    "DirectML.dll",
    "libraw.dll",
    "Microsoft.Windows.AI.MachineLearning.dll",
    "Microsoft.WindowsAppRuntime.Bootstrap.dll",
    "onnxruntime.dll",
    "StackModelService.exe",
    "THIRD_PARTY_NOTICES.md"
)
foreach ($name in $obsoleteFiles) {
    $path = Join-Path $BuildDir $name
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        Remove-Item -LiteralPath $path -Force
    }
}

foreach ($name in $legacyDataDirectories) {
    $path = Join-Path $BuildDir $name
    if (Test-Path -LiteralPath $path -PathType Container) {
        Remove-Item -LiteralPath $path -Force
    }
}

$legacyLicenses = Join-Path $BuildDir "licenses"
if (Test-Path -LiteralPath $legacyLicenses -PathType Container) {
    Remove-Item -LiteralPath $legacyLicenses -Recurse -Force
}

$remaining = @(Get-ChildItem -LiteralPath $BuildDir -Force | Select-Object -ExpandProperty Name | Sort-Object)
if (($remaining -join '|') -ne 'Stack|Stack.exe') {
    throw "Deployment root still contains unexpected entries: $($remaining -join ', ')"
}

Write-Host "Runtime layout finalized. Deployment root contains only Stack.exe and Stack."
