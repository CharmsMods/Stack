param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path,
    [string]$SavedSources = '_workspace/ui-performance-before-20261012',
    [string]$Output = '_workspace/ui-perf-comparison/baseline-src'
)
$ErrorActionPreference = 'Stop'
$baselineRoot = Join-Path $Root $Output
New-Item -ItemType Directory -Path $baselineRoot -Force | Out-Null
$utf8 = New-Object System.Text.UTF8Encoding($false)
$files = @(
    'Editor/NodeGraph/EditorNodeGraphUI.cpp',
    'Editor/NodeGraph/UI/EditorNodeGraphUICamera.cpp',
    'Editor/NodeGraph/UI/EditorNodeGraphUICanvas.cpp',
    'Editor/NodeGraph/UI/EditorNodeGraphUIHitTesting.cpp',
    'Editor/NodeGraph/UI/EditorNodeGraphUILinksAndGroups.cpp',
    'Editor/NodeGraph/UI/EditorNodeGraphUINodeBrowser.cpp',
    'Editor/NodeGraph/UI/EditorNodeGraphUIPreviews.cpp',
    'Editor/Internal/EditorModuleRenderRequests.cpp'
)
function Insert-MatchingFunctionProbe([string]$Text, [string]$Live, [string]$Marker, [string]$Stage) {
    if (!$Text.Contains($Marker)) { throw "Baseline marker missing: $Marker" }
    return $Text.Replace($Marker, $Marker + "`n    Stack::UiPerformance::Span perfTiming(Stack::UiPerformance::Stage::$Stage);")
}
foreach ($relative in $files) {
    $saved = Join-Path $Root "$SavedSources/src/$relative"
    $livePath = Join-Path $Root "src/$relative"
    $text = [IO.File]::ReadAllText($saved).Replace("`r`n", "`n")
    $live = [IO.File]::ReadAllText($livePath).Replace("`r`n", "`n")
    $text = $text.Replace('#include "EditorNodeGraphUI.h"', '#include "Editor/NodeGraph/EditorNodeGraphUI.h"')
    $sourceDirectory = Split-Path $relative -Parent
    $text = [regex]::Replace($text, '#include "([^/"\\]+)"', {
        param($match)
        $qualifiedHeader = ($sourceDirectory + '/' + $match.Groups[1].Value).Replace('\','/')
        if (Test-Path -LiteralPath (Join-Path $Root ('src/' + $qualifiedHeader))) {
            return '#include "' + $qualifiedHeader + '"'
        }
        return $match.Value
    })
    $text = "#include `"App/UiPerformanceTrace.h`"`n#include `"App/UiPerformanceProbe.h`"`n" + $text
    switch ([IO.Path]::GetFileName($relative)) {
        'EditorNodeGraphUI.cpp' {
            $text = Insert-MatchingFunctionProbe $text $live 'void EditorNodeGraphUI::RefreshNodeOrderCache(const EditorNodeGraph::Graph& graph) {' 'NodeOrder'
        }
        'EditorNodeGraphUICanvas.cpp' {
            $text = Insert-MatchingFunctionProbe $text $live '    const GraphRenderOptions& options) {' 'GraphCanvas'
            $text = $text.Replace('    bool draggingMask = false;', '    Stack::UiPerformance::Add(Stack::UiPerformance::Counter::Nodes, graph.GetNodes().size());' + "`n    bool draggingMask = false;")
            $anchor = "    const float graphElementCullMargin = std::clamp(`n        std::min(canvasSize.x, canvasSize.y) * 0.35f,`n        180.0f,`n        520.0f);"
            $start = $live.IndexOf('    if (Stack::UiPerformance::Probe::Active())', $live.IndexOf($anchor))
            $end = $live.IndexOf('    const auto rectOverlapsCanvas', $start)
            $text = $text.Replace($anchor, $anchor + "`n" + $live.Substring($start,$end-$start).TrimEnd())
            $anchor = '        if (EditorNodeGraph::Node* node = FindCachedNode(graph, nodeId)) {'
            $start = $live.IndexOf($anchor) + $anchor.Length
            $end = $live.IndexOf('            RenderNode(editor', $start)
            $text = $text.Replace($anchor, $anchor + $live.Substring($start,$end-$start).TrimEnd())
        }
        'EditorNodeGraphUIHitTesting.cpp' {
            $text = Insert-MatchingFunctionProbe $text $live 'EditorNodeGraph::Link EditorNodeGraphUI::FindLinkAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) {' 'LinkHitTest'
        }
        'EditorNodeGraphUILinksAndGroups.cpp' {
            $text = Insert-MatchingFunctionProbe $text $live 'void EditorNodeGraphUI::RenderLinks(const EditorNodeGraph::Graph& graph) {' 'Links'
            $text = $text.Replace('    Stack::UiPerformance::Span perfTiming(Stack::UiPerformance::Stage::Links);',
                '    Stack::UiPerformance::Span perfTiming(Stack::UiPerformance::Stage::Links);' + "`n    Stack::UiPerformance::Add(Stack::UiPerformance::Counter::Links, graph.GetLinks().size());")
            # Counts come from the same cull boundary as the live source.
            $count = $live.IndexOf('        Stack::UiPerformance::Add(Stack::UiPerformance::Counter::VisibleLinks);')
            if ($count -ge 0) {
                $after = $live.Substring($count).Split("`n")[1]
                if ($after.Trim()) { $text = $text.Replace($after, '        Stack::UiPerformance::Add(Stack::UiPerformance::Counter::VisibleLinks);' + "`n" + $after) }
            }
        }
        'EditorNodeGraphUINodeBrowser.cpp' {
            $text = Insert-MatchingFunctionProbe $text $live '    const ImVec2& workspacePos) {' 'Catalog'
            foreach ($anchor in @('        ImGui::InputTextWithHint("##NodesPanelSearch", prompt, m_NodeBrowserSearchBuffer, sizeof(m_NodeBrowserSearchBuffer), searchFlags);', '        m_NodeBrowserScrollY = ImGui::GetScrollY();')) {
                $start = $live.IndexOf($anchor) + $anchor.Length
                $endMarker = if ($anchor.Contains('InputText')) { '        m_NodeBrowserSearchFocused' } else { '        ImGui::EndChild();' }
                $end = $live.IndexOf($endMarker, $start)
                $text = $text.Replace($anchor,$anchor + $live.Substring($start,$end-$start).TrimEnd())
            }
        }
        'EditorModuleRenderRequests.cpp' {
            $text = Insert-MatchingFunctionProbe $text $live 'std::vector<EditorRenderWorker::PreviewRequest> EditorModule::BuildPreviewRequests() {' 'PreviewRequests'
        }
    }
    [IO.File]::WriteAllText((Join-Path $baselineRoot ([IO.Path]::GetFileName($relative))), $text, $utf8)
}
foreach ($filename in @('EditorModuleViewportPreview.cpp','EditorModuleRenderAdoption.cpp','AppRawMaskToolbar.cpp','AppWindowTitleBarBridge.cpp')) {
    $savedViewportSource = Join-Path $Root "_workspace/ui-perf-comparison/pre-refinement/$filename"
    if (!(Test-Path -LiteralPath $savedViewportSource)) { throw "Pre-refinement source missing: $savedViewportSource" }
    $savedText = [IO.File]::ReadAllText($savedViewportSource)
    if ($filename -eq 'AppRawMaskToolbar.cpp' -or $filename -eq 'AppWindowTitleBarBridge.cpp') {
        $savedText = [regex]::Replace($savedText,'#include "([^/"\\]+)"',{
            param($match)
            $qualified = 'App/' + $match.Groups[1].Value
            if (Test-Path -LiteralPath (Join-Path $Root ('src/' + $qualified))) { return '#include "' + $qualified + '"' }
            return $match.Value
        })
    }
    if ($filename -eq 'AppWindowTitleBarBridge.cpp') {
        $savedText = '#include "App/UiPerformanceTrace.h"' + "`n" + $savedText
        foreach ($marker in @('    float captionButtonOpacity) {','void SyncPassthroughRegions(GLFWwindow* window, const std::vector<ImRect>& screenRects) {')) {
            $savedText = Insert-MatchingFunctionProbe $savedText '' $marker 'NativeChrome'
        }
    }
    [IO.File]::WriteAllText((Join-Path $baselineRoot $filename),$savedText,$utf8)
}
Get-ChildItem -LiteralPath $baselineRoot -File -Filter '*.cpp' | Get-FileHash -Algorithm SHA256 | Select-Object Hash,Path | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $baselineRoot 'source-hashes.json')
Write-Output $baselineRoot
