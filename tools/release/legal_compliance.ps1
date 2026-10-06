$ErrorActionPreference = "Stop"

function Get-StackLegalManifest {
    param([Parameter(Mandatory = $true)][string]$Root)

    $path = Join-Path $Root "legal\RELEASE_LEGAL_STATUS.json"
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing Stack legal approval manifest: $path"
    }
    return Get-Content -Raw -LiteralPath $path | ConvertFrom-Json
}

function Test-StackLegalPacket {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [switch]$RequireApproved
    )

    $manifest = Get-StackLegalManifest -Root $Root
    if ($manifest.schemaVersion -ne 1) {
        throw "Unsupported Stack legal manifest schema: $($manifest.schemaVersion)"
    }
    if ($manifest.legalOwner -ne "Darynn Ho") {
        throw "The legal manifest owner must be Darynn Ho."
    }

    $errors = New-Object System.Collections.Generic.List[string]
    foreach ($property in $manifest.documents.PSObject.Properties) {
        $document = $property.Value
        $documentPath = Join-Path $Root ([string]$document.path)
        if (-not (Test-Path -LiteralPath $documentPath -PathType Leaf)) {
            $errors.Add("Missing $($property.Name): $($document.path)")
            continue
        }
        $actualHash = (Get-FileHash -LiteralPath $documentPath -Algorithm SHA256).Hash.ToLowerInvariant()
        $expectedHash = ([string]$document.sha256).ToLowerInvariant()
        if ($actualHash -ne $expectedHash) {
            $errors.Add("$($property.Name) hash changed. Expected $expectedHash; found $actualHash")
        }
        if ([string]::IsNullOrWhiteSpace([string]$document.version)) {
            $errors.Add("$($property.Name) does not have a version.")
        }
    }

    if ($RequireApproved) {
        if ($manifest.approvalState -ne "approved") {
            $errors.Add("Legal approval state is '$($manifest.approvalState)', not 'approved'.")
        }
        if ([string]::IsNullOrWhiteSpace([string]$manifest.approvalDate)) {
            $errors.Add("An approved legal packet must record approvalDate.")
        }
        if ([string]::IsNullOrWhiteSpace([string]$manifest.approvalReference)) {
            $errors.Add("An approved legal packet must record a non-secret external approvalReference.")
        }
    }

    if ($errors.Count -gt 0) {
        throw ("Stack legal validation failed:`n - " + ($errors -join "`n - "))
    }

    return [pscustomobject]@{
        Manifest = $manifest
        EulaVersion = [string]$manifest.documents.eula.version
        EulaSha256 = ([string]$manifest.documents.eula.sha256).ToLowerInvariant()
        PrivacyVersion = [string]$manifest.documents.privacy.version
        ApprovalState = [string]$manifest.approvalState
    }
}

function Get-StackThirdPartyManifest {
    param([Parameter(Mandatory = $true)][string]$Root)

    $path = Join-Path $Root "legal\THIRD_PARTY_COMPONENTS.json"
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Missing third-party component manifest: $path"
    }
    $manifest = Get-Content -Raw -LiteralPath $path | ConvertFrom-Json
    if ($manifest.schemaVersion -ne 1) {
        throw "Unsupported third-party component schema: $($manifest.schemaVersion)"
    }
    return $manifest
}

function Test-StackComponentPresent {
    param(
        [Parameter(Mandatory = $true)][object]$Component,
        [string]$StageDir
    )

    if ([string]$Component.presence -eq "always") {
        return $true
    }
    if ([string]::IsNullOrWhiteSpace($StageDir)) {
        return $false
    }
    $relative = ([string]$Component.presence).Replace('/', '\')
    return Test-Path -LiteralPath (Join-Path $StageDir $relative)
}

function Get-StackApplicableComponents {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [string]$StageDir,
        [ValidateSet("desktop", "website")][string]$Scope = "desktop"
    )

    $manifest = Get-StackThirdPartyManifest -Root $Root
    return @($manifest.components | Where-Object {
        ([string]$_.scope -eq $Scope) -and
        (Test-StackComponentPresent -Component $_ -StageDir $StageDir)
    })
}

function Write-StackThirdPartyNotices {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$DestinationPath,
        [string]$StageDir,
        [ValidateSet("desktop", "website")][string]$Scope = "desktop"
    )

    $components = @(Get-StackApplicableComponents -Root $Root -StageDir $StageDir -Scope $Scope)
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# Third-Party Notices")
    $lines.Add("")
    $lines.Add("This file is generated from `legal/THIRD_PARTY_COMPONENTS.json`.")
    $lines.Add("Stack's proprietary terms do not replace or restrict the licenses below.")
    $lines.Add("")
    foreach ($component in $components) {
        $lines.Add("## $($component.name)")
        $lines.Add("")
        $lines.Add("- Version: $($component.version)")
        $lines.Add("- Upstream: $($component.homepage)")
        $lines.Add("- License: $($component.license)")
        $lines.Add("- Copyright: $($component.copyright)")
        if (@($component.licenseFiles).Count -gt 0) {
            $files = @($component.licenseFiles | ForEach-Object { '`' + [string]$_ + '`' }) -join ", "
            $lines.Add("- Included license files: $files")
        }
        if ($component.id -eq "libraw") {
            $lines.Add("")
            $lines.Add("Stack dynamically links LibRaw under the LGPL 2.1 option. The applicable release includes the exact corresponding source archive, and the DLL may be replaced by an interface-compatible modified version as permitted by that license.")
        }
        if ($component.id -eq "ffmpeg-provider") {
            $lines.Add("")
            $lines.Add("The provider directory includes its exact manifest, configuration, license files, notices, and source-reference information.")
        }
        $lines.Add("")
    }

    while ($lines.Count -gt 0 -and $lines[$lines.Count - 1] -eq "") {
        $lines.RemoveAt($lines.Count - 1)
    }

    $destinationDirectory = Split-Path -Parent $DestinationPath
    if ($destinationDirectory) {
        New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    }
    Set-Content -LiteralPath $DestinationPath -Value $lines -Encoding UTF8
}

function Test-StackThirdPartyStage {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$StageDir
    )

    $legalDir = Join-Path $StageDir "Stack\App\Legal"
    foreach ($required in @("EULA.txt", "PRIVACY.md", "LICENSE", "THIRD_PARTY_NOTICES.md")) {
        if (-not (Test-Path -LiteralPath (Join-Path $legalDir $required) -PathType Leaf)) {
            throw "Release stage is missing Stack/App/Legal/$required"
        }
    }

    $thirdPartyDir = Join-Path $legalDir "ThirdParty"
    $components = @(Get-StackApplicableComponents -Root $Root -StageDir $StageDir -Scope desktop)
    foreach ($component in $components) {
        foreach ($licenseFile in @($component.licenseFiles)) {
            $path = Join-Path $thirdPartyDir ([string]$licenseFile)
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                throw "Release stage includes $($component.name) but is missing ThirdParty/$licenseFile"
            }
        }
    }
    return $components
}

function ConvertTo-SpdxId {
    param([Parameter(Mandatory = $true)][string]$Value)
    return ($Value -replace '[^A-Za-z0-9.-]', '-')
}

function Get-SpdxLicenseExpression {
    param([Parameter(Mandatory = $true)][string]$ComponentId)
    switch ($ComponentId) {
        "dear-imgui" { return "MIT" }
        "glfw" { return "Zlib" }
        "nlohmann-json" { return "MIT" }
        "stb" { return "MIT OR Unlicense" }
        "libraw" { return "LGPL-2.1-only" }
        default { return "NOASSERTION" }
    }
}

function Write-StackSpdxSbom {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$StageDir,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$DestinationPath
    )

    $components = @(Test-StackThirdPartyStage -Root $Root -StageDir $StageDir)
    $files = New-Object System.Collections.Generic.List[object]
    $relationships = New-Object System.Collections.Generic.List[object]
    $relationships.Add([ordered]@{
        spdxElementId = "SPDXRef-DOCUMENT"
        relationshipType = "DESCRIBES"
        relatedSpdxElement = "SPDXRef-Package-Stack"
    })

    $index = 0
    foreach ($file in Get-ChildItem -LiteralPath $StageDir -Recurse -File | Sort-Object FullName) {
        if ($file.FullName -eq $DestinationPath) {
            continue
        }
        $index++
        $relative = $file.FullName.Substring($StageDir.Length).TrimStart('\', '/').Replace('\', '/')
        $fileId = "SPDXRef-File-$index"
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $files.Add([ordered]@{
            fileName = "./$relative"
            SPDXID = $fileId
            checksums = @([ordered]@{ algorithm = "SHA256"; checksumValue = $hash })
            licenseConcluded = "NOASSERTION"
            copyrightText = "NOASSERTION"
        })
        $relationships.Add([ordered]@{
            spdxElementId = "SPDXRef-Package-Stack"
            relationshipType = "CONTAINS"
            relatedSpdxElement = $fileId
        })
    }

    $packages = New-Object System.Collections.Generic.List[object]
    $packages.Add([ordered]@{
        name = "Stack"
        SPDXID = "SPDXRef-Package-Stack"
        versionInfo = $Version
        downloadLocation = "https://github.com/CharmsMods/Stack/releases"
        filesAnalyzed = $true
        licenseConcluded = "LicenseRef-Stack-EULA"
        licenseDeclared = "LicenseRef-Stack-EULA"
        copyrightText = "Copyright (c) 2024-2026 Darynn Ho"
    })
    foreach ($component in $components) {
        $componentId = "SPDXRef-Package-$(ConvertTo-SpdxId -Value ([string]$component.id))"
        $packages.Add([ordered]@{
            name = [string]$component.name
            SPDXID = $componentId
            versionInfo = [string]$component.version
            downloadLocation = [string]$component.homepage
            filesAnalyzed = $false
            licenseConcluded = Get-SpdxLicenseExpression -ComponentId ([string]$component.id)
            licenseDeclared = Get-SpdxLicenseExpression -ComponentId ([string]$component.id)
            copyrightText = [string]$component.copyright
        })
        $relationships.Add([ordered]@{
            spdxElementId = "SPDXRef-Package-Stack"
            relationshipType = "DEPENDS_ON"
            relatedSpdxElement = $componentId
        })
    }

    $document = [ordered]@{
        spdxVersion = "SPDX-2.3"
        dataLicense = "CC0-1.0"
        SPDXID = "SPDXRef-DOCUMENT"
        name = "Stack-$Version-win-x64"
        documentNamespace = "https://github.com/CharmsMods/Stack/spdx/$Version/$([guid]::NewGuid())"
        creationInfo = [ordered]@{
            created = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
            creators = @("Tool: Stack release compliance", "Person: Darynn Ho")
        }
        documentDescribes = @("SPDXRef-Package-Stack")
        packages = $packages
        files = $files
        relationships = $relationships
        hasExtractedLicensingInfos = @([ordered]@{
            licenseId = "LicenseRef-Stack-EULA"
            extractedText = "Official Stack binaries are governed by the EULA.txt file included in the package."
            name = "Stack End User License Agreement"
        })
    }

    $destinationDirectory = Split-Path -Parent $DestinationPath
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    $document | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $DestinationPath -Encoding UTF8
}

function New-StackLibRawSourceArchive {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$BuildTreeDir,
        [Parameter(Mandatory = $true)][string]$OutputDir
    )

    $expectedCommit = "b860248a89d9082b8e0a1e202e516f46af9adb29"
    $sourceDir = Join-Path $BuildTreeDir "_deps\libraw-src"
    if (-not (Test-Path -LiteralPath (Join-Path $sourceDir ".git"))) {
        throw "LibRaw source checkout was not found at $sourceDir. Build with LibRaw enabled first."
    }
    $actualCommit = (& git -C $sourceDir rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $expectedCommit) {
        throw "LibRaw source commit mismatch. Expected $expectedCommit; found $actualCommit"
    }

    $archivePath = Join-Path $OutputDir "LibRaw-0.22.1-source.zip"
    $temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("stack-libraw-source-" + [guid]::NewGuid().ToString("N"))
    $sourceStage = Join-Path $temporaryRoot "LibRaw-0.22.1"
    try {
        New-Item -ItemType Directory -Path $sourceStage -Force | Out-Null
        $temporaryArchive = Join-Path $temporaryRoot "upstream.zip"
        & git -C $sourceDir archive --format=zip --prefix=LibRaw-0.22.1/ -o $temporaryArchive $expectedCommit
        if ($LASTEXITCODE -ne 0) {
            throw "git archive failed for LibRaw."
        }
        Expand-Archive -LiteralPath $temporaryArchive -DestinationPath $temporaryRoot -Force
        Copy-Item -LiteralPath (Join-Path $Root "legal\LIBRAW_BUILD_INFO.txt") -Destination (Join-Path $sourceStage "STACK-BUILD-INFO.txt")
        Set-Content -LiteralPath (Join-Path $sourceStage "STACK-PATCHES.txt") -Value "Stack applies no source patch to LibRaw 0.22.1." -Encoding UTF8
        Compress-Archive -Path $sourceStage -DestinationPath $archivePath -CompressionLevel Optimal -Force
    }
    finally {
        if (Test-Path -LiteralPath $temporaryRoot) {
            Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
        }
    }
    return $archivePath
}

function Test-StackAuthenticodeFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$AllowedSubject
    )

    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne "Valid" -or $null -eq $signature.SignerCertificate) {
        throw "Authenticode validation failed for ${Path}: $($signature.Status) $($signature.StatusMessage)"
    }
    if ($signature.SignerCertificate.Subject -notlike "*$AllowedSubject*") {
        throw "Unexpected signer for ${Path}: $($signature.SignerCertificate.Subject)"
    }
    return $signature
}

function Test-StackPublicSignatures {
    param(
        [Parameter(Mandatory = $true)][string]$StageDir,
        [Parameter(Mandatory = $true)][string]$InstallerPath,
        [Parameter(Mandatory = $true)][string]$AllowedStackSubject
    )

    $peFiles = @(Get-ChildItem -LiteralPath $StageDir -Recurse -File | Where-Object {
        $_.Extension -in @(".exe", ".dll")
    })
    foreach ($file in $peFiles) {
        $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
        if ($signature.Status -ne "Valid" -or $null -eq $signature.SignerCertificate) {
            throw "Authenticode validation failed for $($file.FullName): $($signature.Status)"
        }
        $subject = $signature.SignerCertificate.Subject
        if ($subject -notlike "*$AllowedStackSubject*" -and $subject -notlike "*Microsoft*") {
            throw "Unexpected signer for $($file.FullName): $subject"
        }
    }
    Test-StackAuthenticodeFile -Path $InstallerPath -AllowedSubject $AllowedStackSubject | Out-Null
}
