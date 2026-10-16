# Shared build, packaging and signing declarations for both installed products.
function Get-StackProduct {
    param([ValidateSet("Stack", "Burst")][string]$Product = "Stack")

    $manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot "products.json") -Raw | ConvertFrom-Json
    if ($manifest.schemaVersion -ne 1) { throw "Unsupported product manifest schema." }
    $definition = $manifest.products.$Product
    if (-not $definition) { throw "Unknown product: $Product" }
    $definition | Add-Member -NotePropertyName id -NotePropertyValue $Product
    $definition | Add-Member -NotePropertyName componentDefinitions -NotePropertyValue $manifest.components
    return $definition
}

function Get-StackProductSigningFiles {
    param([Parameter(Mandatory = $true)][object]$Definition)
    foreach ($componentName in $Definition.components) {
        $component = $Definition.componentDefinitions.$componentName
        foreach ($relative in $component.signedFiles) { [string]$relative }
    }
}

function Resolve-StackPayloadPath {
    param([string]$Directory, [string]$Relative)
    $rootPath = [System.IO.Path]::GetFullPath($Directory).TrimEnd('\', '/')
    $path = [System.IO.Path]::GetFullPath((Join-Path $rootPath $Relative))
    if (-not $path.StartsWith($rootPath + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Product payload path escapes its root: $Relative"
    }
    return $path
}

function Copy-StackProductPayload {
    param([object]$Definition, [string]$Root, [string]$BuildDir, [string]$StageDir)
    foreach ($componentName in $Definition.components) {
        $component = $Definition.componentDefinitions.$componentName
        if (-not $component) { throw "Missing payload component: $componentName" }
        foreach ($relative in $component.files) {
            Copy-ReleaseFile -SourcePath (Resolve-StackPayloadPath $BuildDir $relative) `
                -DestinationPath (Resolve-StackPayloadPath $StageDir $relative)
        }
        foreach ($relative in $component.directories) {
            Copy-ReleaseDirectoryContents -SourceDir (Resolve-StackPayloadPath $BuildDir $relative) `
                -DestinationDir (Resolve-StackPayloadPath $StageDir $relative)
        }
    }
    # Release-authored documentation wins over any older build-staged copy.
    foreach ($componentName in $Definition.components) {
        $component = $Definition.componentDefinitions.$componentName
        foreach ($file in $component.sourceFiles) {
            Copy-ReleaseFile -SourcePath (Resolve-StackPayloadPath $Root $file.source) `
                -DestinationPath (Resolve-StackPayloadPath $StageDir $file.destination)
        }
    }
}

function Test-StackProductPayload {
    param([object]$Definition, [string]$StageDir, [string]$Version)
    $actual = @(Get-ChildItem -LiteralPath $StageDir -Force | Select-Object -ExpandProperty Name | Sort-Object)
    $expected = @($Definition.rootEntries | Sort-Object)
    if (($actual -join '|') -ne ($expected -join '|')) {
        throw "$($Definition.name) payload root must contain $($expected -join ', '). Found: $($actual -join ', ')"
    }
    foreach ($relative in $Definition.versionedExecutables) {
        $path = Resolve-StackPayloadPath $StageDir $relative
        $binary = Get-Item -LiteralPath $path -ErrorAction Stop
        if ($binary.VersionInfo.ProductVersion -ne $Version) {
            throw "$relative is version '$($binary.VersionInfo.ProductVersion)', expected '$Version'. Rebuild this product before packaging."
        }
    }
    foreach ($relative in @("Stack/User", "Stack/Temp", "Stack Projects")) {
        if (Test-Path -LiteralPath (Resolve-StackPayloadPath $StageDir $relative)) {
            throw "Product payload must not contain user or temporary data: $relative"
        }
    }
    foreach ($relative in @(Get-StackProductSigningFiles -Definition $Definition)) {
        if (-not (Test-Path -LiteralPath (Resolve-StackPayloadPath $StageDir $relative) -PathType Leaf)) {
            throw "Required product component is missing: $relative"
        }
    }
}
