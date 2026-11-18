param(
    [Parameter(Mandatory = $true)][string]$RepositoryRoot,
    [Parameter(Mandatory = $true)][string]$CheckDirectory,
    [Parameter(Mandatory = $true)][string]$ImGuiDirectory,
    [Parameter(Mandatory = $true)][string]$GlfwDirectory
)

# Run from a Visual Studio developer environment. This bounded check deliberately
# avoids the full application's codec, asset-baking and packaging dependencies.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Command failed with exit code $LASTEXITCODE"
    }
}

$RepositoryRoot = (Resolve-Path $RepositoryRoot).Path
$ImGuiDirectory = (Resolve-Path $ImGuiDirectory).Path
$GlfwDirectory = (Resolve-Path $GlfwDirectory).Path
New-Item -ItemType Directory -Force $CheckDirectory | Out-Null
$CheckDirectory = (Resolve-Path $CheckDirectory).Path
$generatedDirectory = Join-Path $CheckDirectory 'generated'
New-Item -ItemType Directory -Force "$generatedDirectory/src/App" | Out-Null

# Use the same version source and template as the application, not a test stub.
$cmakeRepository = $RepositoryRoot.Replace('\', '/')
$cmakeGenerated = $generatedDirectory.Replace('\', '/')
@"
include("$cmakeRepository/cmake/StackVersion.cmake")
configure_file("$cmakeRepository/src/App/AppVersion.h.in"
    "$cmakeGenerated/src/App/AppVersion.h" @ONLY)
"@ | Set-Content -LiteralPath "$CheckDirectory/version.cmake" -Encoding utf8
Invoke-Checked 'cmake' @('-P', "$CheckDirectory/version.cmake")

$compileOptions = @(
    '/nologo', '/std:c++17', '/EHsc', '/W4', '/utf-8', '/DNOMINMAX',
    "/I$RepositoryRoot/src", "/I$RepositoryRoot/src/ThirdParty",
    "/I$ImGuiDirectory", "/I$GlfwDirectory/include",
    "/I$generatedDirectory/src", "/I$generatedDirectory/src/App"
)

# These are the production dependencies used by StackColorWarpCachePolicyTests,
# plus Local EV curve normalization and the node-owned cloud preparation helper.
# Function sections let the focused checks discard unrelated recipe operations.
$commonSources = @(
    'src/Hdr/SdrAppearance.cpp',
    'src/Raw/RawDevelopmentRecipe.cpp',
    'src/Raw/Tone/SceneTone.cpp',
    'src/Raw/Curves/LocalEvCurveNormalization.cpp',
    'src/Raw/ColorWarpMath.cpp',
    'src/Raw/RawColorWarpRecipe.cpp',
    'src/Raw/RawColorWarpMask.cpp',
    'src/Raw/ImageGuidance.cpp',
    'src/Raw/RawZoneAreaRecipe.cpp',
    'src/Raw/RawZoneAreaGain.cpp',
    'src/Raw/RawColorCalibration.cpp',
    'src/Raw/Detail/DetailContrast.cpp',
    'src/Raw/Denoise/RawDenoiseControlMap.cpp',
    'src/Editor/NodeEditors/ColorWarpCloudPreparation.cpp',
    'src/Editor/NodeEditors/ColorWarpFeedback.cpp'
) | ForEach-Object { Join-Path $RepositoryRoot $_ }

Push-Location $CheckDirectory
try {
    Write-Host 'Compiling shared production math and cloud preparation'
    Invoke-Checked 'cl.exe' ($compileOptions + @('/c', '/MP4', '/O2', '/Gy', '/Gw') + $commonSources)
    $commonObjects = @($commonSources | ForEach-Object {
        Join-Path $CheckDirectory ([IO.Path]::GetFileNameWithoutExtension($_) + '.obj')
    })
    foreach ($test in @(
        'color_warp_cache_policy_tests',
        'prepared_tonal_curve_tests',
        'color_warp_cloud_preparation_tests',
        'cooperative_gpu_scheduling_tests',
        'compatible_transfer_policy_tests'
    )) {
        Write-Host "Compiling and running $test"
        $objects = if ($test -in @('cooperative_gpu_scheduling_tests', 'compatible_transfer_policy_tests')) {
            @()
        } else { $commonObjects }
        Invoke-Checked 'cl.exe' ($compileOptions + @(
            '/O2', '/Gy', '/Gw', "$RepositoryRoot/tools/tests/$test.cpp", "/Fe:$test.exe"
        ) + $objects + @('/link', '/OPT:REF'))
        Invoke-Checked (Join-Path $CheckDirectory "$test.exe") @()
    }

    # Compile the real Windows HDR bodies and their existing hidden validation.
    # The wrapper supplies the includes normally provided by HdrDisplayValidation.
    @'
#include "Renderer/GLLoader.h"
#include <iostream>
#include <stdexcept>
#include "App/Validation/Suites/WindowsHdrValidation.h"
'@ | Set-Content -LiteralPath "$CheckDirectory/windows_hdr_validation.cpp" -Encoding utf8

    Write-Host 'Checking changed production translation units with the Windows SDK'
    $syntaxSources = @(
        'src/Editor/EditorModule.cpp',
        'src/Editor/EditorRenderWorker.cpp',
        'src/Editor/Internal/EditorModuleRendering.cpp',
        'src/Editor/Internal/EditorModuleRenderScheduling.cpp',
        'src/Editor/Internal/EditorModuleViewportPreview.cpp',
        'src/Editor/Internal/EditorModuleViewportSections.cpp',
        'src/Editor/Internal/EditorModuleRenderAdoption.cpp',
        'src/Editor/Internal/EditorRenderWorkerViewportPreview.cpp',
        'src/Editor/Internal/EditorRenderWorkerGraph.cpp',
        'src/Editor/Internal/RawLab/EditorModuleRawLabColor.cpp',
        'src/Editor/Internal/RawLab/RawLabColorCloud.cpp',
        'src/Editor/Internal/RawLab/EditorModuleRawOperationBinding.cpp',
        'src/Editor/NodeEditors/EditorModuleNodeSelection.cpp',
        'src/Editor/NodeEditors/EditorModuleNodeEditorHost.cpp',
        'src/Editor/NodeEditors/EditorModuleFocusedNodeEditor.cpp',
        'src/Editor/Layers/ToneLayerRendering.cpp',
        'src/Renderer/FullscreenQuad.cpp',
        'src/Renderer/CooperativeGpuQueue.cpp',
        'src/Renderer/GLLoader.cpp',
        'src/Renderer/Internal/RenderPipelineGraphRawDevelopmentOutputStages.cpp',
        'src/Renderer/Internal/RenderPipelineGraphTextureCache.cpp',
        'src/Renderer/Internal/RenderPipelineRawPreprocessTelemetry.cpp',
        'src/Renderer/Internal/RenderPipelinePrograms.cpp',
        'src/Renderer/Internal/RenderPipelineResources.cpp',
        'src/Raw/RawGpuPipeline.cpp',
        'src/Raw/Demosaic/DemosaicTexture.cpp',
        'src/Raw/Demosaic/GpuDemosaic.cpp',
        'src/Hdr/WindowTransfer.cpp',
        'src/Hdr/WindowPresenter.cpp',
        'src/Hdr/Presentation.cpp',
        'src/App/Validation/Suites/ColorWarpMoveValidation.cpp',
        'src/App/Validation/Suites/ViewControlsPanelValidation.cpp'
    ) | ForEach-Object { Join-Path $RepositoryRoot $_ }
    Invoke-Checked 'cl.exe' ($compileOptions + @('/Zs') + $syntaxSources +
        @("$CheckDirectory/windows_hdr_validation.cpp"))
    # AppShell.cpp's suspended-window pacing body runs in the extracted lifecycle
    # check. Its generated embedded assets require the separate product build.
} finally {
    Pop-Location
}
