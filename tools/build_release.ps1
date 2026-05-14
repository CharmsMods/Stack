param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $Root "build_codex"
}

& (Join-Path $Root "tools\use_fixed_env.ps1")

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $candidates = @(
        "C:\Program Files\CMake\bin\cmake.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    )
    $cmake = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

if (-not $cmake) {
    Write-Host "CMake was not found. Install CMake or Visual Studio Build Tools with CMake support, then run build.cmd again."
    exit 9009
}

& $cmake -S $Root -B $BuildDir
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $cmake --build $BuildDir --config Release
exit $LASTEXITCODE
