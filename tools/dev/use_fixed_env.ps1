$ErrorActionPreference = "Stop"

$segments = New-Object System.Collections.Generic.List[string]
$seen = New-Object 'System.Collections.Generic.HashSet[string]' ([System.StringComparer]::OrdinalIgnoreCase)

function Add-PathSegments {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return
    }

    foreach ($segment in $Value.Split(';')) {
        $expanded = [Environment]::ExpandEnvironmentVariables($segment).Trim()
        if ([string]::IsNullOrWhiteSpace($expanded)) {
            continue
        }

        if ($seen.Add($expanded)) {
            $segments.Add($expanded)
        }
    }
}

Add-PathSegments ([Environment]::GetEnvironmentVariable("Path", "Machine"))
Add-PathSegments ([Environment]::GetEnvironmentVariable("Path", "User"))

$knownToolDirs = @(
    "C:\Program Files\CMake\bin",
    "$env:LOCALAPPDATA\Programs\Python\Python312",
    "$env:LOCALAPPDATA\Programs\Python\Python312\Scripts",
    "C:\Program Files\Git\cmd",
    "$env:LOCALAPPDATA\Programs\Antigravity\bin",
    "$env:LOCALAPPDATA\GitHubDesktop\bin",
    "$env:APPDATA\npm",
    "$env:USERPROFILE\.lmstudio\bin",
    "$env:LOCALAPPDATA\Programs\Ollama",
    "$env:LOCALAPPDATA\Programs\cursor\resources\app\bin",
    "$env:LOCALAPPDATA\OpenAI\Codex\bin"
)

foreach ($dir in $knownToolDirs) {
    if (Test-Path $dir) {
        Add-PathSegments $dir
    }
}

$fixedPath = [string]::Join(';', $segments)
if ([string]::IsNullOrWhiteSpace($fixedPath)) {
    throw "Unable to reconstruct a usable PATH for this session."
}

[Environment]::SetEnvironmentVariable("PATH", $null, "Process")
[Environment]::SetEnvironmentVariable("Path", $fixedPath, "Process")
$env:Path = $fixedPath
