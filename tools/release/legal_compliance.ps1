$ErrorActionPreference = "Stop"

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
        [ValidateSet("desktop", "website")][string]$Scope = "desktop",
        [string[]]$ComponentIds = @()
    )

    $manifest = Get-StackThirdPartyManifest -Root $Root
    return @($manifest.components | Where-Object {
        ([string]$_.scope -eq $Scope) -and
        (($ComponentIds.Count -eq 0) -or ($_.id -in $ComponentIds)) -and
        (Test-StackComponentPresent -Component $_ -StageDir $StageDir)
    })
}

function Write-StackThirdPartyNotices {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$DestinationPath,
        [string]$StageDir,
        [ValidateSet("desktop", "website")][string]$Scope = "desktop",
        [string[]]$ComponentIds = @()
    )

    $components = @(Get-StackApplicableComponents -Root $Root -StageDir $StageDir -Scope $Scope -ComponentIds $ComponentIds)
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("# Third-Party Notices")
    $lines.Add("")
    $lines.Add("This file is generated from `legal/THIRD_PARTY_COMPONENTS.json`.")
    $lines.Add("Third-party components retain their respective licenses and copyright notices below.")
    $lines.Add("")
    if ($Scope -eq "desktop") {
        $lines.Add("## Adobe DNG technology")
        $lines.Add("")
        $lines.Add("This product includes DNG technology under license by Adobe.")
        $lines.Add("See Adobe-DNG-Notice.txt and the [DNG Specification patent license](https://helpx.adobe.com/camera-raw/desktop/dng-and-file-formats/digital-negative.html#DNGSpecificationpatentlicense).")
        $lines.Add("")
    }
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
        [Parameter(Mandatory = $true)][string]$StageDir,
        [string[]]$ComponentIds = @()
    )

    $legalDir = Join-Path $StageDir "Stack\App\Legal"
    foreach ($required in @("THIRD_PARTY_NOTICES.md", "Adobe-DNG-Notice.txt", "GPL-3.0.txt", "STACK-LICENSE-NOTICE.txt")) {
        if (-not (Test-Path -LiteralPath (Join-Path $legalDir $required) -PathType Leaf)) {
            throw "Release stage is missing Stack/App/Legal/$required"
        }
    }

    # The custom Stack agreement is archived, including its former runtime copy
    # of the repository source license. Reject stale incremental-build payloads.
    foreach ($archived in @("EULA.txt", "PRIVACY.md", "LICENSE", "INFO_BEFORE_INSTALL.txt", "RELEASE_LEGAL_STATUS.json", "archive")) {
        if (Test-Path -LiteralPath (Join-Path $legalDir $archived)) {
            throw "Release stage contains archived Stack agreement material: Stack/App/Legal/$archived. Rebuild before packaging."
        }
    }

    $thirdPartyDir = Join-Path $legalDir "ThirdParty"
    $components = @(Get-StackApplicableComponents -Root $Root -StageDir $StageDir -Scope desktop -ComponentIds $ComponentIds)
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
        "libavoid" { return "LGPL-2.1-or-later" }
        "libraw-classic-demosaic" { return "GPL-3.0-only" }
        "roboto-font" { return "Apache-2.0" }
        "press-start-2p-font" { return "OFL-1.1" }
        "silkscreen-font" { return "OFL-1.1" }
        "phosphor-icons" { return "MIT" }
        "liquidglass" { return "MIT" }
        "glsl-fast-gaussian-blur" { return "MIT" }
        default { return "NOASSERTION" }
    }
}

function Write-StackSpdxSbom {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$StageDir,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$DestinationPath,
        [string]$ProductName = "Stack",
        [string]$ProductId = "Stack",
        [string[]]$ComponentIds = @()
    )

    $productSpdxId = "SPDXRef-Package-$(ConvertTo-SpdxId -Value $ProductId)"
    $components = @(Test-StackThirdPartyStage -Root $Root -StageDir $StageDir -ComponentIds $ComponentIds)
    $files = New-Object System.Collections.Generic.List[object]
    $relationships = New-Object System.Collections.Generic.List[object]
    $relationships.Add([ordered]@{
        spdxElementId = "SPDXRef-DOCUMENT"
        relationshipType = "DESCRIBES"
        relatedSpdxElement = $productSpdxId
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
            spdxElementId = $productSpdxId
            relationshipType = "CONTAINS"
            relatedSpdxElement = $fileId
        })
    }

    $packages = New-Object System.Collections.Generic.List[object]
    $packages.Add([ordered]@{
        name = $ProductName
        SPDXID = $productSpdxId
        versionInfo = $Version
        downloadLocation = "https://github.com/CharmsMods/Stack/releases"
        filesAnalyzed = $true
        licenseConcluded = "NOASSERTION"
        licenseDeclared = "GPL-3.0-only"
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
            spdxElementId = $productSpdxId
            relationshipType = "DEPENDS_ON"
            relatedSpdxElement = $componentId
        })
    }

    $document = [ordered]@{
        spdxVersion = "SPDX-2.3"
        dataLicense = "CC0-1.0"
        SPDXID = "SPDXRef-DOCUMENT"
        name = "$ProductId-$Version-win-x64"
        documentNamespace = "https://github.com/CharmsMods/Stack/spdx/$ProductId/$Version/$([guid]::NewGuid())"
        creationInfo = [ordered]@{
            created = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
            creators = @("Tool: Stack release compliance", "Person: Darynn Ho")
        }
        documentDescribes = @($productSpdxId)
        packages = $packages
        files = $files
        relationships = $relationships
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
