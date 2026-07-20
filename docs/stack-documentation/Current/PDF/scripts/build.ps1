[CmdletBinding()]
param(
    [switch]$Open,
    [switch]$Watch,
    [switch]$Clean,
    [switch]$NoPreview
)

$ErrorActionPreference = 'Stop'

$WorkspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$SourcePath = Join-Path $WorkspaceRoot 'source\manual.typ'
$OutputDirectory = Join-Path $WorkspaceRoot 'output\pdf'
$OutputPath = Join-Path $OutputDirectory 'stack-manual.pdf'
$PreviewDirectory = Join-Path $WorkspaceRoot 'tmp\pdfs\pages'
$ToolVersion = '0.14.2'
$ToolRoot = Join-Path $WorkspaceRoot ".tools\typst-$ToolVersion"

function Assert-WorkspacePath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $workspacePrefix = $WorkspaceRoot.TrimEnd('\') + '\'
    $candidate = [System.IO.Path]::GetFullPath($Path)
    if (-not $candidate.StartsWith($workspacePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside the manual workspace: $candidate"
    }
}

function Remove-GeneratedFiles {
    if (Test-Path -LiteralPath $OutputPath) {
        Assert-WorkspacePath -Path $OutputPath
        Remove-Item -LiteralPath $OutputPath -Force
    }

    if (Test-Path -LiteralPath $PreviewDirectory) {
        Assert-WorkspacePath -Path $PreviewDirectory
        Remove-Item -LiteralPath $PreviewDirectory -Recurse -Force
    }
}

function Get-PinnedTypst {
    $existing = Get-ChildItem -LiteralPath $ToolRoot -Filter 'typst.exe' -File -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($existing) {
        return $existing.FullName
    }

    $architecture = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
    switch ($architecture) {
        'X64' {
            $archiveName = 'typst-x86_64-pc-windows-msvc.zip'
            $expectedHash = '51353994ac83218c3497052e89b2c432c53b9d4439cdc1b361e2ea4798ebfc13'
        }
        'Arm64' {
            $archiveName = 'typst-aarch64-pc-windows-msvc.zip'
            $expectedHash = '1c4aaa0de000ab1787dda354c34f4fa1fe3c2525d3d038e692a3d7daa333d551'
        }
        default {
            throw "Unsupported Windows architecture '$architecture'. Install Typst $ToolVersion manually and update scripts/build.ps1."
        }
    }

    $downloadUrl = "https://github.com/typst/typst/releases/download/v$ToolVersion/$archiveName"
    $downloadDirectory = Join-Path $WorkspaceRoot 'tmp\pdfs\downloads'
    $archivePath = Join-Path $downloadDirectory $archiveName

    New-Item -ItemType Directory -Force -Path $downloadDirectory | Out-Null
    Write-Host "Downloading Typst $ToolVersion for $architecture..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -Uri $downloadUrl -OutFile $archivePath -UseBasicParsing

    $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedHash) {
        throw "Typst archive checksum mismatch. Expected $expectedHash, received $actualHash."
    }

    if (Test-Path -LiteralPath $ToolRoot) {
        Assert-WorkspacePath -Path $ToolRoot
        Remove-Item -LiteralPath $ToolRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $ToolRoot | Out-Null
    Expand-Archive -LiteralPath $archivePath -DestinationPath $ToolRoot -Force
    Remove-Item -LiteralPath $archivePath -Force

    $installed = Get-ChildItem -LiteralPath $ToolRoot -Filter 'typst.exe' -File -Recurse |
        Select-Object -First 1
    if (-not $installed) {
        throw 'Typst downloaded successfully, but typst.exe was not found after extraction.'
    }

    Unblock-File -LiteralPath $installed.FullName -ErrorAction SilentlyContinue
    return $installed.FullName
}

if ($Clean) {
    Remove-GeneratedFiles
    Write-Host 'Removed generated PDF and preview pages.'
    exit 0
}

if (-not (Test-Path -LiteralPath $SourcePath)) {
    throw "Document entry point not found: $SourcePath"
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$Typst = Get-PinnedTypst

if ($Watch) {
    Write-Host "Watching source and assets. Press Ctrl+C to stop."
    & $Typst watch $SourcePath $OutputPath --root $WorkspaceRoot
    exit $LASTEXITCODE
}

Write-Host 'Compiling Stack manual PDF...'
& $Typst compile $SourcePath $OutputPath --root $WorkspaceRoot --diagnostic-format short
if ($LASTEXITCODE -ne 0) {
    throw "Typst PDF compilation failed with exit code $LASTEXITCODE."
}

$pdf = Get-Item -LiteralPath $OutputPath
if ($pdf.Length -lt 1000) {
    throw "Generated PDF is unexpectedly small ($($pdf.Length) bytes)."
}

$previewCount = 0
if (-not $NoPreview) {
    if (Test-Path -LiteralPath $PreviewDirectory) {
        Assert-WorkspacePath -Path $PreviewDirectory
        Remove-Item -LiteralPath $PreviewDirectory -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $PreviewDirectory | Out-Null

    $previewPattern = Join-Path $PreviewDirectory 'page-{p}.png'
    Write-Host 'Rendering review PNGs...'
    & $Typst compile $SourcePath $previewPattern --root $WorkspaceRoot --ppi 144 --diagnostic-format short
    if ($LASTEXITCODE -ne 0) {
        throw "Typst preview rendering failed with exit code $LASTEXITCODE."
    }

    $previewCount = @(Get-ChildItem -LiteralPath $PreviewDirectory -Filter 'page-*.png' -File).Count
    if ($previewCount -lt 1) {
        throw 'Preview rendering completed without producing any page images.'
    }
}

Write-Host "Built: $OutputPath"
Write-Host "PDF size: $([math]::Round($pdf.Length / 1KB, 1)) KiB"
if (-not $NoPreview) {
    Write-Host "Review pages: $previewCount in $PreviewDirectory"
}

if ($Open) {
    Start-Process -FilePath $OutputPath
}

