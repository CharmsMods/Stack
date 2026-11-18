param(
    [Parameter(Mandatory = $true)][string]$RepositoryRoot,
    [Parameter(Mandatory = $true)][string]$CheckDirectory,
    [Parameter(Mandatory = $true)][string]$ImGuiDirectory,
    [Parameter(Mandatory = $true)][string]$GlfwDirectory
)

# Requires a Visual Studio developer environment. Uses the real CPU math and
# existing local-motion suite; no camera files, downloads of models or UI tests.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
}
$RepositoryRoot = (Resolve-Path $RepositoryRoot).Path
$ImGuiDirectory = (Resolve-Path $ImGuiDirectory).Path
$GlfwDirectory = (Resolve-Path $GlfwDirectory).Path
New-Item -ItemType Directory -Force $CheckDirectory | Out-Null
$CheckDirectory = (Resolve-Path $CheckDirectory).Path
$options = @('/nologo', '/std:c++17', '/EHsc', '/W4', '/utf-8', '/DNOMINMAX',
    "/I$RepositoryRoot/src", "/I$RepositoryRoot/src/ThirdParty",
    "/I$ImGuiDirectory", "/I$GlfwDirectory/include")
$common = @(
    'src/Raw/MultiFrameDenoise/Contracts.cpp',
    'src/Raw/MultiFrameDenoise/GlobalRegistration.cpp',
    'src/Raw/MultiFrameDenoise/LocalMotion.cpp',
    'src/Raw/MultiFrameDenoise/LocalMotionPatch.cpp',
    'src/Raw/MultiFrameDenoise/PyramidVariance.cpp',
    'src/Raw/MultiFrameDenoise/SameCfaSampler.cpp',
    # SameCfaSampler owns both the cubic math and prepared-sample entry
    # points. Supply their real dependencies even when a check only calls
    # the math; /OPT:REF must not substitute for a complete source closure.
    'src/Raw/MultiFrameDenoise/Preparation.cpp',
    'src/Raw/MultiFrameDenoise/NoiseModel.cpp',
    'src/Raw/RawProcessingMath.cpp',
    'src/Raw/RawTechnicalEvidence.cpp',
    'src/Raw/RawImageData.cpp'
) | ForEach-Object { Join-Path $RepositoryRoot $_ }
Push-Location $CheckDirectory
try {
    Invoke-Checked 'cl.exe' ($options + @('/c', '/MP4', '/O2', '/Gy', '/Gw') + $common)
    $objects = @($common | ForEach-Object {
        Join-Path $CheckDirectory ([IO.Path]::GetFileNameWithoutExtension($_) + '.obj')
    })
    $cases = @(
        @{ Name = 'burst_alignment'; Sources = @('tools/tests/burst_alignment_tests.cpp',
            'src/App/Validation/Suites/MfdPhase5Validation.cpp') },
        @{ Name = 'alignment_proposal'; Sources = @('tools/tests/alignment_proposal_tests.cpp',
            'src/Raw/Bracketing/AlignmentProposalInput.cpp',
            'src/Raw/Bracketing/CaptureMotion.cpp', 'src/NodeMath/ContractTypes.cpp',
            'src/Raw/Bracketing/PreparedTileSampler.cpp',
            'src/Raw/Bracketing/PartialCfaReconstruction.cpp') },
        @{ Name = 'pyramid_variance'; Sources = @('tools/tests/pyramid_variance_tests.cpp') }
    )
    foreach ($case in $cases) {
        Write-Host "Compiling and running $($case.Name)"
        $sources = @($case.Sources | ForEach-Object { Join-Path $RepositoryRoot $_ })
        $executable = Join-Path $CheckDirectory "$($case.Name).exe"
        Invoke-Checked 'cl.exe' ($options + @('/O2', '/Gy', '/Gw', "/Fe:$executable") +
            $sources + $objects + @('/link', '/OPT:REF'))
        Invoke-Checked $executable @()
    }
    # The integrated shipping owners and cache paths need the Windows SDK;
    # syntax checking them does not claim a full application/worker link.
    $integrated = @(
        'src/Raw/Bracketing/LocalAlignment.cpp',
        'src/Raw/Bracketing/BurstAlignment.cpp',
        'src/Raw/Bracketing/Identity.cpp',
        'src/Raw/MultiFrameDenoise/GpuLocalMotion.cpp',
        'src/Raw/MultiFrameDenoise/Processor.cpp',
        'src/Raw/MultiFrameDenoise/StreamingCore.cpp',
        'src/Raw/MultiFrameHdr/Processor.cpp',
        'src/Raw/MultiFrame/MultiFrameGraphExecution.cpp'
    ) | ForEach-Object { Join-Path $RepositoryRoot $_ }
    Write-Host 'Checking integrated Windows processing owners'
    Invoke-Checked 'cl.exe' ($options + @('/Zs') + $integrated)
} finally { Pop-Location }
