param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path,
    [string]$BuildDir = "",
    [string]$OutputDir = "",
    [switch]$PublicRelease,
    [switch]$LocalTestPackage,
    [switch]$SkipBuild,
    [switch]$SkipInstaller,
    [switch]$SkipPortableZip,
    [switch]$SkipHashes
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "..\dev\stack_workflow.ps1")
. (Join-Path $PSScriptRoot "legal_compliance.ps1")

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

$legalStatus = Test-StackLegalPacket -Root $Root -RequireApproved:$PublicRelease

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
        [Parameter(Mandatory = $true)][string]$StageDir
    )
    $stackSigningTargets = @(
        (Join-Path $StageDir "Stack.exe"),
        (Join-Path $StageDir "Stack\App\Runtime\libraw.dll")
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf }

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

function Test-StagedLegalHashes {
    param(
        [Parameter(Mandatory = $true)][object]$Manifest,
        [Parameter(Mandatory = $true)][string]$StageDir
    )
    $nameMap = @{
        sourceLicense = "LICENSE"
        eula = "EULA.txt"
        privacy = "PRIVACY.md"
        installerInformation = "INFO_BEFORE_INSTALL.txt"
    }
    $legalDir = Join-Path $StageDir "Stack\App\Legal"
    foreach ($property in $Manifest.documents.PSObject.Properties) {
        $stageName = $nameMap[$property.Name]
        $stagePath = Join-Path $legalDir $stageName
        if (-not (Test-Path -LiteralPath $stagePath -PathType Leaf)) {
            throw "The build is missing current legal file Stack/App/Legal/$stageName. Rebuild before packaging."
        }
        $stageHash = (Get-FileHash -LiteralPath $stagePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($stageHash -ne ([string]$property.Value.sha256).ToLowerInvariant()) {
            throw "The staged $stageName does not match the legal manifest. Rebuild before packaging."
        }
    }
}

Invoke-StackEnvironmentRepair -Root $Root

if (-not $SkipBuild) {
    & (Join-Path $Root "tools\build\build_stack.ps1") -Root $Root -BuildDir $BuildDir
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$usingCanonicalOutput = [string]::IsNullOrWhiteSpace($OutputDir)
if ($usingCanonicalOutput) {
    Initialize-ReleaseFolders -Paths $paths
    if ($LocalTestPackage) {
        Ensure-StackDirectory -Path $paths.LocalTestReleaseDir
        $archived = Move-StackDirectoryContentsToArchive `
            -SourceDir $paths.LocalTestReleaseDir `
            -ArchiveRoot $paths.ReleaseArchiveDir `
            -Label ("local-test-" + (Get-StackTimestamp))
        if ($archived) {
            Write-Host "Archived the previous local-test package to: $archived"
        }
        $OutputDir = $paths.LocalTestReleaseDir
    }
    else {
        Prepare-CurrentReleaseOutput -Paths $paths
        $OutputDir = $paths.CurrentReleaseDir
    }
}
else {
    Ensure-StackDirectory -Path $OutputDir
}
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)

$versionInfo = Get-StackVersionInfo -VersionFile (Join-Path $Root "cmake\StackVersion.cmake")
if ($LocalTestPackage) {
    $stageDir = Join-Path $OutputDir ("Stack-LOCAL-TEST-{0}-win-x64" -f $versionInfo.Tag)
    $portableZipPath = Join-Path $OutputDir ("Stack-LOCAL-TEST-UNSIGNED-LEGAL-DRAFT-{0}-win-x64.zip" -f $versionInfo.Tag)
    $installerPath = Join-Path $OutputDir ("StackSetup-LOCAL-TEST-UNSIGNED-LEGAL-DRAFT-{0}.exe" -f $versionInfo.Version)
    $sbomOutputPath = Join-Path $OutputDir ("Stack-LOCAL-TEST-{0}.spdx.json" -f $versionInfo.Tag)
}
else {
    $stageDir = Join-Path $OutputDir ("Stack-{0}-win-x64" -f $versionInfo.Tag)
    $portableZipPath = Join-Path $OutputDir ("Stack-{0}-win-x64.zip" -f $versionInfo.Tag)
    $installerPath = Join-Path $OutputDir ("StackSetup-{0}-win-x64.exe" -f $versionInfo.Tag)
    $sbomOutputPath = Join-Path $OutputDir ("Stack-{0}.spdx.json" -f $versionInfo.Tag)
}
$hashesPath = Join-Path $OutputDir "SHA256SUMS.txt"
$markerFile = Join-Path $Root "installer\StackInstalledBuild.marker"
$eulaPath = Join-Path $Root "legal\EULA.txt"
$infoBeforePath = Join-Path $Root "legal\INFO_BEFORE_INSTALL.txt"

if (-not $usingCanonicalOutput) {
    $customArchiveRoot = Join-Path $OutputDir "_archive"
    foreach ($path in @($stageDir, $portableZipPath, $installerPath, $sbomOutputPath, $hashesPath)) {
        $archivedPath = Move-StackItemToArchive -SourcePath $path -ArchiveRoot $customArchiveRoot
        if ($archivedPath) {
            Write-Host "Archived existing release artifact to: $archivedPath"
        }
    }
}

Ensure-StackDirectory -Path $OutputDir
Ensure-StackDirectory -Path $stageDir
Copy-ReleaseFile -SourcePath (Join-Path $BuildDir "Stack.exe") -DestinationPath (Join-Path $stageDir "Stack.exe")
Copy-ReleaseDirectoryContents `
    -SourceDir (Join-Path $BuildDir "Stack\App") `
    -DestinationDir (Join-Path $stageDir "Stack\App")

Copy-OptionalFfmpegProvider -Paths $paths -StageDir $stageDir | Out-Null

if ($LocalTestPackage) {
    $localTestMarker = Join-Path $stageDir "Stack\App\StackLocalTestBuild.marker"
    [System.IO.File]::WriteAllText($localTestMarker, "LOCAL TEST BUILD - NOT FOR PUBLIC DISTRIBUTION`r`n")
}

$stageEntries = @(Get-ChildItem -LiteralPath $stageDir -Force | Select-Object -ExpandProperty Name | Sort-Object)
if (($stageEntries -join '|') -ne 'Stack|Stack.exe') {
    throw "Release staging must contain only Stack.exe and Stack. Found: $($stageEntries -join ', ')"
}
if ((Test-Path -LiteralPath (Join-Path $stageDir "Stack\User")) -or
    (Test-Path -LiteralPath (Join-Path $stageDir "Stack\Temp"))) {
    throw "Release staging must not contain Stack user or temporary data."
}

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

Test-StagedLegalHashes -Manifest $legalStatus.Manifest -StageDir $stageDir
Write-StackThirdPartyNotices `
    -Root $Root `
    -DestinationPath (Join-Path $stageDir "Stack\App\Legal\THIRD_PARTY_NOTICES.md") `
    -StageDir $stageDir
Test-StackThirdPartyStage -Root $Root -StageDir $stageDir | Out-Null

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
    Sign-StackPayload -Configuration $signingConfiguration -StageDir $stageDir
}

$sbomStagePath = Join-Path $stageDir "Stack\App\Legal\Stack.spdx.json"
Write-StackSpdxSbom `
    -Root $Root `
    -StageDir $stageDir `
    -Version $versionInfo.Version `
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
        "/DStackVersion=$($versionInfo.Version)",
        "/DStackVersionTag=$($versionInfo.Tag)",
        "/DStackPublisher=$($versionInfo.Publisher)",
        "/DStackSourceDir=$stageDir",
        "/DStackOutputDir=$OutputDir",
        "/DStackEulaFile=$eulaPath",
        "/DStackInfoBeforeFile=$infoBeforePath",
        "/DStackMarkerFile=$markerFile",
        "/DStackEulaVersion=$($legalStatus.EulaVersion)",
        "/DStackEulaSha256=$($legalStatus.EulaSha256)",
        "/DStackPrivacyVersion=$($legalStatus.PrivacyVersion)",
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
    if ($libRawSourceArchive) {
        $releaseFiles += $libRawSourceArchive
    }
    Write-Sha256Sums -Files $releaseFiles -DestinationPath $hashesPath
}

Write-Host ""
if ($PublicRelease) {
    Write-Host "PUBLIC release packaging complete. Legal approval and Authenticode gates passed."
}
elseif ($LocalTestPackage) {
    Write-Host "LOCAL TEST packaging complete. This package is unsigned, uses draft legal text, and must not be published."
}
else {
    Write-Host "Standard packaging complete. This installer is unsigned. Legal approval state: $($legalStatus.ApprovalState)."
    if ($legalStatus.ApprovalState -ne "approved") {
        Write-Host "Do not publish this package while its legal documents are drafts."
    }
}
Write-Host "Output folder: $OutputDir"
Write-Host "Stage directory: $stageDir"
if (Test-Path -LiteralPath $portableZipPath) { Write-Host "Portable ZIP: $portableZipPath" }
if (Test-Path -LiteralPath $installerPath) { Write-Host "Installer: $installerPath" }
if ($libRawSourceArchive) { Write-Host "LibRaw source: $libRawSourceArchive" }
if (Test-Path -LiteralPath $sbomOutputPath) { Write-Host "SPDX SBOM: $sbomOutputPath" }
if (Test-Path -LiteralPath $hashesPath) { Write-Host "Hashes: $hashesPath" }
