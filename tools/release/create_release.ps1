param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$BuildDir = "",
    [string]$OutputDir = "",
    [ValidateSet("Stack", "Burst")][string]$Product = "Stack",
    [switch]$PublicRelease,
    [switch]$LocalTestPackage,
    [switch]$SkipBuild,
    [switch]$SkipInstaller,
    [switch]$SkipPortableZip,
    [switch]$SkipHashes,
    [string]$PythonExecutable = "python",
    [string]$ReviewedBuildCommit = "",
    [switch]$CorrespondingSourceReviewed
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "..\dev\stack_workflow.ps1")
. (Join-Path $PSScriptRoot "legal_compliance.ps1")
. (Join-Path $PSScriptRoot "product_payload.ps1")
$productDefinition = Get-StackProduct -Product $Product

if ($PublicRelease -and $LocalTestPackage) {
    throw "Cannot specify both -PublicRelease and -LocalTestPackage."
}
if ($PublicRelease -and ($SkipInstaller -or $SkipPortableZip -or $SkipHashes)) {
    throw "Public releases must include the signed installer, portable ZIP, and final SHA-256 sums."
}

$Root = (Resolve-Path $Root).Path
$paths = Get-StackPaths -Root $Root
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = $paths.BuildDir
}
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

function Resolve-StackSignToolPath {
    if (-not [string]::IsNullOrWhiteSpace($env:STACK_SIGNTOOL_PATH)) {
        if (-not (Test-Path -LiteralPath $env:STACK_SIGNTOOL_PATH -PathType Leaf)) {
            throw "STACK_SIGNTOOL_PATH does not point to a file: $($env:STACK_SIGNTOOL_PATH)"
        }
        return (Resolve-Path -LiteralPath $env:STACK_SIGNTOOL_PATH).Path
    }
    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) {
        return $command.Source
    }
    throw "Public release signing requires signtool.exe or STACK_SIGNTOOL_PATH."
}

function Get-StackSigningConfiguration {
    $missing = @()
    foreach ($name in @("STACK_SIGNING_CERT_SHA1", "STACK_SIGNING_SUBJECT", "STACK_TIMESTAMP_URL")) {
        if ([string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable($name))) {
            $missing += $name
        }
    }
    if ($missing.Count -gt 0) {
        throw "Public release signing is not configured. Missing environment variables: $($missing -join ', ')"
    }
    return [pscustomobject]@{
        SignTool = Resolve-StackSignToolPath
        CertificateSha1 = $env:STACK_SIGNING_CERT_SHA1.Replace(" ", "")
        AllowedSubject = $env:STACK_SIGNING_SUBJECT
        TimestampUrl = $env:STACK_TIMESTAMP_URL
        InnoToolName = "StackReleaseSign"
    }
}

function Invoke-StackSignFile {
    param(
        [Parameter(Mandatory = $true)][object]$Configuration,
        [Parameter(Mandatory = $true)][string]$Path
    )
    & $Configuration.SignTool sign `
        /fd SHA256 `
        /sha1 $Configuration.CertificateSha1 `
        /tr $Configuration.TimestampUrl `
        /td SHA256 `
        $Path
    if ($LASTEXITCODE -ne 0) {
        throw "Authenticode signing failed for $Path"
    }
}

function Sign-StackPayload {
    param(
        [Parameter(Mandatory = $true)][object]$Configuration,
        [Parameter(Mandatory = $true)][string]$StageDir,
        [Parameter(Mandatory = $true)][object]$Definition
    )
    $stackSigningTargets = @(Get-StackProductSigningFiles -Definition $Definition | Sort-Object -Unique |
        ForEach-Object { Resolve-StackPayloadPath $StageDir $_ })

    foreach ($path in $stackSigningTargets) {
        Invoke-StackSignFile -Configuration $Configuration -Path $path
    }

    $targetSet = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($path in $stackSigningTargets) {
        [void]$targetSet.Add([System.IO.Path]::GetFullPath($path))
    }

    $otherPeFiles = @(Get-ChildItem -LiteralPath $StageDir -Recurse -File | Where-Object {
        $_.Extension -in @(".exe", ".dll") -and
        -not $targetSet.Contains($_.FullName)
    })
    foreach ($file in $otherPeFiles) {
        $existing = Get-AuthenticodeSignature -LiteralPath $file.FullName
        if ($existing.Status -eq "Valid" -and
            $existing.SignerCertificate -and
            $existing.SignerCertificate.Subject -like "*Microsoft*") {
            continue
        }
        throw "Public packaging will not claim ownership of undeclared executable code: $($file.FullName). Add an explicit reviewed signing policy for this component before release."
    }
}

Invoke-StackEnvironmentRepair -Root $Root

# A source revision is not proof of binary provenance. Public packaging requires
# the maintainer to review the matching build and all corresponding-source inputs.
if ($PublicRelease -and (-not $CorrespondingSourceReviewed -or
    [string]::IsNullOrWhiteSpace($ReviewedBuildCommit))) {
    throw "Public packaging requires -ReviewedBuildCommit <full SHA> and -CorrespondingSourceReviewed after the checks in legal/SOURCE_RELEASE.md. These are maintainer attestations, not automatic build proof."
}
$sourceHelper = Join-Path $PSScriptRoot "source_bundle.py"
$sourceRevision = $null
if (-not $LocalTestPackage) {
    $sourceCheckArgs = @($sourceHelper, "--root", $Root, "--check-only")
    if ($ReviewedBuildCommit) { $sourceCheckArgs += @("--reviewed-build-commit", $ReviewedBuildCommit) }
    if ($CorrespondingSourceReviewed) { $sourceCheckArgs += "--corresponding-source-reviewed" }
    if ($PublicRelease) { $sourceCheckArgs += "--public" }
    $sourceCheck = & $PythonExecutable @sourceCheckArgs
    if ($LASTEXITCODE -ne 0) { throw "Source preflight failed. Commit intended inputs, or use -LocalTestPackage for a non-distributable test." }
    $sourceRevision = ([string]($sourceCheck -join "`n") | ConvertFrom-Json).commit
    if ([string]::IsNullOrWhiteSpace($sourceRevision)) { throw "Source preflight did not return a commit." }
}

if (-not $SkipBuild) {
    & (Join-Path $Root "tools\build\build_stack.ps1") -Root $Root -BuildDir $BuildDir -Product $Product
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$usingCanonicalOutput = [string]::IsNullOrWhiteSpace($OutputDir)
if ($usingCanonicalOutput) {
    Initialize-ReleaseFolders -Paths $paths
    if ($LocalTestPackage) {
        $OutputDir = $paths.LocalTestReleaseDir
    }
    else {
        $OutputDir = $paths.CurrentReleaseDir
    }
}
else {
    Ensure-StackDirectory -Path $OutputDir
}
$outputRoot = [System.IO.Path]::GetFullPath($OutputDir)
$OutputDir = Resolve-StackPayloadPath $outputRoot $Product
Ensure-StackDirectory -Path $OutputDir

$versionInfo = Get-StackVersionInfo -VersionFile (Join-Path $Root "cmake\StackVersion.cmake")
if ($LocalTestPackage) {
    $stageDir = Join-Path $OutputDir ("{0}-LOCAL-TEST-{1}-win-x64" -f $productDefinition.artifactName, $versionInfo.Tag)
    $portableZipPath = Join-Path $OutputDir ("{0}-LOCAL-TEST-UNSIGNED-{1}-win-x64.zip" -f $productDefinition.artifactName, $versionInfo.Tag)
    $installerPath = Join-Path $OutputDir ("{0}-LOCAL-TEST-UNSIGNED-{1}.exe" -f $productDefinition.installerName, $versionInfo.Version)
    $sbomOutputPath = Join-Path $OutputDir ("{0}-LOCAL-TEST-{1}.spdx.json" -f $productDefinition.artifactName, $versionInfo.Tag)
}
else {
    $stageDir = Join-Path $OutputDir ("{0}-{1}-win-x64" -f $productDefinition.artifactName, $versionInfo.Tag)
    $portableZipPath = Join-Path $OutputDir ("{0}-{1}-win-x64.zip" -f $productDefinition.artifactName, $versionInfo.Tag)
    $installerPath = Join-Path $OutputDir ("{0}-{1}-win-x64.exe" -f $productDefinition.installerName, $versionInfo.Tag)
    $sbomOutputPath = Join-Path $OutputDir ("{0}-{1}.spdx.json" -f $productDefinition.artifactName, $versionInfo.Tag)
}
$hashesPath = Join-Path $OutputDir ($productDefinition.artifactName + "-SHA256SUMS.txt")
$markerFile = Join-Path $Root "installer\StackInstalledBuild.marker"

$productArchiveRoot = if ($usingCanonicalOutput) {
    Join-Path $paths.ReleaseArchiveDir $(if ($LocalTestPackage) { "local-test\$Product" } else { $Product })
} else { Join-Path $outputRoot "_archive\$Product" }
# Only this product's directory is rotated. Building Burst must not archive Stack.
$verifiedProductOutput = Resolve-StackPayloadPath $outputRoot $Product
if (-not $OutputDir.Equals($verifiedProductOutput, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Unexpected product output directory: $OutputDir"
}
$archivedPath = Move-StackDirectoryContentsToArchive -SourceDir $OutputDir `
    -ArchiveRoot $productArchiveRoot -Label $productDefinition.artifactName
if ($archivedPath) {
    Write-Host "Archived previous $($productDefinition.name) artifacts to: $archivedPath"
}

Ensure-StackDirectory -Path $OutputDir
Ensure-StackDirectory -Path $stageDir
Copy-StackProductPayload -Definition $productDefinition -Root $Root -BuildDir $BuildDir -StageDir $stageDir

# These inventories belong to release generation, not the developer payload.
# Regenerate only the selected product's current metadata below.
foreach ($inventoryName in @("Stack.spdx.json", "StackBurst.spdx.json")) {
    $inventoryPath = Resolve-StackPayloadPath $stageDir ("Stack/App/Legal/" + $inventoryName)
    if (Test-Path -LiteralPath $inventoryPath -PathType Leaf) {
        Remove-Item -LiteralPath $inventoryPath -Force
    }
}

if ($Product -eq "Stack") { Copy-OptionalFfmpegProvider -Paths $paths -StageDir $stageDir | Out-Null }

if ($LocalTestPackage) {
    $localTestMarker = Join-Path $stageDir ("Stack\App\" + $productDefinition.artifactName + "LocalTestBuild.marker")
    [System.IO.File]::WriteAllText($localTestMarker, "LOCAL TEST BUILD - NOT FOR PUBLIC DISTRIBUTION`r`n")
}

Test-StackProductPayload -Definition $productDefinition -StageDir $stageDir -Version $versionInfo.Version

$modelArtifacts = @(Get-ChildItem -LiteralPath $stageDir -Recurse -File | Where-Object {
    $_.Extension.ToLowerInvariant() -in @(
        ".onnx", ".ort", ".pb", ".pt", ".pth", ".safetensors", ".tflite")
})
if ($modelArtifacts.Count -gt 0) {
    $relativeModels = @($modelArtifacts | ForEach-Object {
        $_.FullName.Substring($stageDir.Length).TrimStart('\', '/')
    })
    throw "Model artifacts cannot ship until an explicit reviewed redistribution ledger and required notices are implemented. Found: $($relativeModels -join ', ')"
}

Write-StackThirdPartyNotices `
    -Root $Root `
    -DestinationPath (Join-Path $stageDir "Stack\App\Legal\THIRD_PARTY_NOTICES.md") `
    -ComponentIds @($productDefinition.thirdPartyIds) `
    -StageDir $stageDir
Test-StackThirdPartyStage -Root $Root -StageDir $stageDir -ComponentIds @($productDefinition.thirdPartyIds) | Out-Null

$libRawSourceArchive = $null
if (Test-Path -LiteralPath (Join-Path $stageDir "Stack\App\Runtime\libraw.dll")) {
    $libRawSourceArchive = New-StackLibRawSourceArchive `
        -Root $Root `
        -BuildTreeDir $paths.BuildTreeDir `
        -OutputDir $OutputDir
}

$signingConfiguration = $null
if ($PublicRelease) {
    $signingConfiguration = Get-StackSigningConfiguration
    Sign-StackPayload -Configuration $signingConfiguration -StageDir $stageDir -Definition $productDefinition
}

# Capture final signed payload hashes, not pre-signing bytes. The helper checks
# the same revision and clean tree again after the build/staging steps.
$sourceBundleDir = $null
$sourceBundleZipPath = $null
if (-not $LocalTestPackage) {
    $sourceBundleDir = Join-Path $OutputDir ($productDefinition.artifactName + "-source")
    $sourceArgs = @($sourceHelper, "--root", $Root, "--output", $sourceBundleDir,
        "--revision", $sourceRevision, "--stage", $stageDir)
    if ($ReviewedBuildCommit) { $sourceArgs += @("--reviewed-build-commit", $ReviewedBuildCommit) }
    if ($CorrespondingSourceReviewed) { $sourceArgs += "--corresponding-source-reviewed" }
    if ($PublicRelease) { $sourceArgs += "--public" }
    if ($libRawSourceArchive) { $sourceArgs += @("--dependency-artifact", $libRawSourceArchive) }
    $libavoidSource = Join-Path $stageDir "Stack\App\Legal\ThirdParty\LibavoidSource"
    if (Test-Path -LiteralPath $libavoidSource -PathType Container) {
        $sourceArgs += @("--dependency-artifact", $libavoidSource)
    }
    & $PythonExecutable @sourceArgs
    if ($LASTEXITCODE -ne 0) { throw "Source bundle generation failed; do not distribute this package." }
    $sourceBundleZipPath = Join-Path $OutputDir ("{0}-{1}-source-bundle.zip" -f $productDefinition.artifactName, $versionInfo.Tag)
    Compress-Archive -Path (Join-Path $sourceBundleDir '*') -DestinationPath $sourceBundleZipPath -CompressionLevel Optimal -Force
}

$sbomStagePath = Join-Path $stageDir ("Stack\App\Legal\" + $productDefinition.artifactName + ".spdx.json")
Write-StackSpdxSbom `
    -Root $Root `
    -StageDir $stageDir `
    -Version $versionInfo.Version `
    -ProductName $productDefinition.name -ProductId $productDefinition.artifactName `
    -ComponentIds @($productDefinition.thirdPartyIds) `
    -DestinationPath $sbomStagePath
Copy-ReleaseFile -SourcePath $sbomStagePath -DestinationPath $sbomOutputPath

if (-not $SkipPortableZip) {
    Compress-Archive -Path (Join-Path $stageDir '*') -DestinationPath $portableZipPath -CompressionLevel Optimal -Force
}

if (-not $SkipInstaller) {
    $isccPath = Resolve-IsccPath
    if (-not $isccPath) {
        throw "Inno Setup 6 (ISCC.exe) was not found. Install Inno Setup 6 or use a local test with -SkipInstaller."
    }

    $isccArguments = @(
        "/DStackProduct=$Product",
        "/DStackShellClsid=$(if ($LocalTestPackage) { $productDefinition.localTestShellClsid } else { $productDefinition.shellClsid })",
        "/DStackVersion=$($versionInfo.Version)",
        "/DStackVersionTag=$($versionInfo.Tag)",
        "/DStackPublisher=$($versionInfo.Publisher)",
        "/DStackSourceDir=$stageDir",
        "/DStackOutputDir=$OutputDir",
        "/DStackMarkerFile=$markerFile",
        "/DStackLocalTest=$([int][bool]$LocalTestPackage)",
        "/DStackPublicRelease=$([int][bool]$PublicRelease)"
    )
    if ($PublicRelease) {
        $innoSignCommand = '"' + $signingConfiguration.SignTool + '" sign /fd SHA256 /sha1 ' +
            $signingConfiguration.CertificateSha1 + ' /tr ' + $signingConfiguration.TimestampUrl +
            ' /td SHA256 $f'
        $isccArguments += "/S$($signingConfiguration.InnoToolName)=$innoSignCommand"
        $isccArguments += "/DStackInnoSignToolName=$($signingConfiguration.InnoToolName)"
    }
    $isccArguments += (Join-Path $Root "installer\StackInstaller.iss")

    & $isccPath @isccArguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

if ($PublicRelease) {
    Test-StackPublicSignatures `
        -StageDir $stageDir `
        -InstallerPath $installerPath `
        -AllowedStackSubject $signingConfiguration.AllowedSubject
}

if (-not $SkipHashes) {
    $releaseFiles = @($installerPath, $portableZipPath, $sbomOutputPath)
    if ($sourceBundleZipPath) { $releaseFiles += $sourceBundleZipPath }
    if ($libRawSourceArchive) {
        $releaseFiles += $libRawSourceArchive
    }
    Write-Sha256Sums -Files $releaseFiles -DestinationPath $hashesPath
}

Write-Host ""
if ($PublicRelease) {
    Write-Host "PUBLIC packaging checks completed; source correspondence and completeness rely on the maintainer review. Publish the source bundle alongside the binaries."
}
elseif ($LocalTestPackage) {
    Write-Host "LOCAL TEST packaging complete. This package is unsigned, uses an isolated profile, and must not be published."
}
else {
    Write-Host "Standard packaging complete. Unsigned; source bundle produced, but binary correspondence and complete corresponding source still require review."
}
Write-Host "Output folder: $OutputDir"
Write-Host "Stage directory: $stageDir"
if (Test-Path -LiteralPath $portableZipPath) { Write-Host "Portable ZIP: $portableZipPath" }
if (Test-Path -LiteralPath $installerPath) { Write-Host "Installer: $installerPath" }
if ($libRawSourceArchive) { Write-Host "LibRaw source: $libRawSourceArchive" }
if (Test-Path -LiteralPath $sbomOutputPath) { Write-Host "SPDX SBOM: $sbomOutputPath" }
if (Test-Path -LiteralPath $hashesPath) { Write-Host "Hashes: $hashesPath" }

if ($sourceBundleDir) { Write-Host "Source bundle and provenance: $sourceBundleDir" }
if ($sourceBundleZipPath) { Write-Host "Source bundle ZIP: $sourceBundleZipPath" }
