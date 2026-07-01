#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "Editor/EditorModule.h"

#include <memory>
#include <string>
#include <vector>

void EditorNodeGraphUI::Initialize() {}

EditorNodeGraph::Graph& EditorNodeGraphUI::GetActiveGraph(EditorModule* editor) const {
    return m_RenderGraphOverride ? *m_RenderGraphOverride : editor->GetNodeGraph();
}

std::vector<std::shared_ptr<LayerBase>>* EditorNodeGraphUI::GetActiveLayers(EditorModule* editor) const {
    return m_RenderLayersOverride ? m_RenderLayersOverride : (editor ? &editor->GetLayers() : nullptr);
}

const std::vector<std::shared_ptr<LayerBase>>* EditorNodeGraphUI::GetActiveLayers(const EditorModule* editor) const {
    return m_RenderLayersOverride ? m_RenderLayersOverride : (editor ? &const_cast<EditorModule*>(editor)->GetLayers() : nullptr);
}

NodeSurfaceSpec EditorNodeGraphUI::ResolveLayerSurfaceSpec(const EditorModule* editor, int layerIndex) const {
    const auto* layers = GetActiveLayers(editor);
    if (!layers || layerIndex < 0 || layerIndex >= static_cast<int>(layers->size()) || !(*layers)[layerIndex]) {
        return {};
    }
    return (*layers)[layerIndex]->GetNodeSurfaceSpec();
}

bool EditorNodeGraphUI::ResolveNodeUsesSidebarOnlyComplexEditor(
    const EditorModule* editor,
    const EditorNodeGraph::Node& node) const {
    if (node.kind == EditorNodeGraph::NodeKind::Lut ||
        node.kind == EditorNodeGraph::NodeKind::CustomMask) {
        return true;
    }
    if (node.kind != EditorNodeGraph::NodeKind::Layer) {
        return false;
    }
    return ResolveLayerSurfaceSpec(editor, node.layerIndex).presentation == NodeSurfacePresentation::RichExpandedSurface;
}

bool EditorNodeGraphUI::ResolveNodeHasDedicatedComplexEditor(
    const EditorModule* editor,
    const EditorNodeGraph::Node& node) const {
    if (ResolveNodeUsesSidebarOnlyComplexEditor(editor, node)) {
        return true;
    }
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::RawDevelopment:
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
        case EditorNodeGraph::NodeKind::RawDecode:
        case EditorNodeGraph::NodeKind::RawDevelop:
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
        case EditorNodeGraph::NodeKind::RawDetailFusion:
        case EditorNodeGraph::NodeKind::HdrMerge:
        case EditorNodeGraph::NodeKind::Mfsr:
            return true;
        default:
            return false;
    }
}

bool EditorNodeGraphUI::ResolveLayerUsesRichNodeSurface(const EditorModule* editor, int layerIndex) const {
    if (m_RenderPreviewOnly) {
        return false;
    }
    return editor && editor->LayerUsesRichNodeSurface(layerIndex);
}

EditorNodeGraphUI::PreviewGraphCacheEntry* EditorNodeGraphUI::GetPresetPreviewGraphCacheEntry(const std::string& presetId) {
    if (presetId.empty()) {
        return nullptr;
    }
    auto it = m_PresetPreviewGraphCache.find(presetId);
    return it != m_PresetPreviewGraphCache.end() ? &it->second : nullptr;
}

const EditorNodeGraphUI::PreviewGraphCacheEntry* EditorNodeGraphUI::GetPresetPreviewGraphCacheEntry(const std::string& presetId) const {
    if (presetId.empty()) {
        return nullptr;
    }
    auto it = m_PresetPreviewGraphCache.find(presetId);
    return it != m_PresetPreviewGraphCache.end() ? &it->second : nullptr;
}
