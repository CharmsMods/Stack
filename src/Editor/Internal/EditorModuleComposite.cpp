#include "Editor/EditorModule.h"

#include "Library/LibraryManager.h"
#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>

namespace {

struct AffineTransform2D {
    float m00 = 1.0f;
    float m01 = 0.0f;
    float m02 = 0.0f;
    float m10 = 0.0f;
    float m11 = 1.0f;
    float m12 = 0.0f;
};

float Clamp01(const float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

bool IsValidRect(const EditorModule::CompositeFloatRect& rect) {
    return std::isfinite(rect.x) &&
           std::isfinite(rect.y) &&
           std::isfinite(rect.width) &&
           std::isfinite(rect.height) &&
           rect.width > 0.0f &&
           rect.height > 0.0f;
}

float RectAspectRatio(const EditorModule::CompositeFloatRect& rect) {
    return std::max(1.0f, rect.width) / std::max(1.0f, rect.height);
}

EditorModule::CompositeFloatRect MakeNormalizedRect(const float x1, const float y1, const float x2, const float y2) {
    const float left = std::min(x1, x2);
    const float top = std::min(y1, y2);
    const float right = std::max(x1, x2);
    const float bottom = std::max(y1, y2);
    return { left, top, std::max(1.0f, right - left), std::max(1.0f, bottom - top) };
}

AffineTransform2D BuildSceneTransform(const EditorModule::CompositeSceneItem& item) {
    const float baseWidth = std::max(1.0f, static_cast<float>(item.textureWidth));
    const float baseHeight = std::max(1.0f, static_cast<float>(item.textureHeight));
    const float width = baseWidth * std::max(0.0001f, item.scale.x);
    const float height = baseHeight * std::max(0.0001f, item.scale.y);
    const float cosR = std::cos(item.rotation);
    const float sinR = std::sin(item.rotation);

    AffineTransform2D matrix;
    matrix.m00 = cosR * std::max(0.0001f, item.scale.x);
    matrix.m01 = -sinR * std::max(0.0001f, item.scale.y);
    matrix.m10 = sinR * std::max(0.0001f, item.scale.x);
    matrix.m11 = cosR * std::max(0.0001f, item.scale.y);
    matrix.m02 = item.position.x + width * 0.5f - matrix.m00 * baseWidth * 0.5f - matrix.m01 * baseHeight * 0.5f;
    matrix.m12 = item.position.y + height * 0.5f - matrix.m10 * baseWidth * 0.5f - matrix.m11 * baseHeight * 0.5f;
    return matrix;
}

AffineTransform2D Inverse(const AffineTransform2D& matrix) {
    const float det = matrix.m00 * matrix.m11 - matrix.m01 * matrix.m10;
    if (std::abs(det) < 1e-6f) {
        return {};
    }
    const float invDet = 1.0f / det;
    AffineTransform2D result;
    result.m00 = matrix.m11 * invDet;
    result.m01 = -matrix.m01 * invDet;
    result.m10 = -matrix.m10 * invDet;
    result.m11 = matrix.m00 * invDet;
    result.m02 = -(result.m00 * matrix.m02 + result.m01 * matrix.m12);
    result.m12 = -(result.m10 * matrix.m02 + result.m11 * matrix.m12);
    return result;
}

ImVec2 TransformPoint(const AffineTransform2D& matrix, const ImVec2& point) {
    return ImVec2(
        matrix.m00 * point.x + matrix.m01 * point.y + matrix.m02,
        matrix.m10 * point.x + matrix.m11 * point.y + matrix.m12);
}

std::array<ImVec2, 4> ComputeSceneQuadWorld(const EditorModule::CompositeSceneItem& item) {
    const AffineTransform2D transform = BuildSceneTransform(item);
    const float width = std::max(1.0f, static_cast<float>(item.textureWidth));
    const float height = std::max(1.0f, static_cast<float>(item.textureHeight));
    return {
        TransformPoint(transform, ImVec2(0.0f, 0.0f)),
        TransformPoint(transform, ImVec2(width, 0.0f)),
        TransformPoint(transform, ImVec2(width, height)),
        TransformPoint(transform, ImVec2(0.0f, height))
    };
}

EditorModule::CompositeFloatRect QuadBounds(const std::array<ImVec2, 4>& quad) {
    float minX = quad[0].x;
    float minY = quad[0].y;
    float maxX = quad[0].x;
    float maxY = quad[0].y;
    for (const ImVec2& point : quad) {
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }
    return { minX, minY, std::max(1.0f, maxX - minX), std::max(1.0f, maxY - minY) };
}

void AlphaBlendPixel(
    std::vector<unsigned char>& dstPixels,
    int dstW,
    int dstX,
    int dstY,
    const unsigned char* srcPixel) {
    if (!srcPixel || dstX < 0 || dstY < 0 || dstW <= 0) {
        return;
    }
    const size_t dstIndex = static_cast<size_t>((dstY * dstW + dstX) * 4);
    const float srcA = static_cast<float>(srcPixel[3]) / 255.0f;
    if (srcA <= 0.0f) {
        return;
    }
    const float dstA = static_cast<float>(dstPixels[dstIndex + 3]) / 255.0f;
    const float outA = srcA + dstA * (1.0f - srcA);
    if (outA <= 0.0f) {
        return;
    }
    for (int channel = 0; channel < 3; ++channel) {
        const float src = static_cast<float>(srcPixel[channel]) / 255.0f;
        const float dst = static_cast<float>(dstPixels[dstIndex + channel]) / 255.0f;
        const float out = (src * srcA + dst * dstA * (1.0f - srcA)) / outA;
        dstPixels[dstIndex + channel] = static_cast<unsigned char>(std::round(Clamp01(out) * 255.0f));
    }
    dstPixels[dstIndex + 3] = static_cast<unsigned char>(std::round(Clamp01(outA) * 255.0f));
}

const char* ExportBoundsModeToToken(const EditorModule::CompositeExportBoundsMode mode) {
    return mode == EditorModule::CompositeExportBoundsMode::Custom ? "custom" : "auto";
}

EditorModule::CompositeExportBoundsMode ExportBoundsModeFromToken(const std::string& token) {
    return token == "custom"
        ? EditorModule::CompositeExportBoundsMode::Custom
        : EditorModule::CompositeExportBoundsMode::Auto;
}

const char* ExportBackgroundModeToToken(const EditorModule::CompositeExportBackgroundMode mode) {
    return mode == EditorModule::CompositeExportBackgroundMode::Solid ? "solid" : "transparent";
}

EditorModule::CompositeExportBackgroundMode ExportBackgroundModeFromToken(const std::string& token) {
    return token == "solid"
        ? EditorModule::CompositeExportBackgroundMode::Solid
        : EditorModule::CompositeExportBackgroundMode::Transparent;
}

const char* ExportAspectPresetToToken(const EditorModule::CompositeExportAspectPreset preset) {
    switch (preset) {
        case EditorModule::CompositeExportAspectPreset::Ratio4x3: return "4:3";
        case EditorModule::CompositeExportAspectPreset::Ratio3x2: return "3:2";
        case EditorModule::CompositeExportAspectPreset::Ratio16x9: return "16:9";
        case EditorModule::CompositeExportAspectPreset::Ratio9x16: return "9:16";
        case EditorModule::CompositeExportAspectPreset::Ratio2x3: return "2:3";
        case EditorModule::CompositeExportAspectPreset::Ratio5x4: return "5:4";
        case EditorModule::CompositeExportAspectPreset::Ratio21x9: return "21:9";
        case EditorModule::CompositeExportAspectPreset::Custom: return "custom";
        case EditorModule::CompositeExportAspectPreset::Ratio1x1:
        default:
            return "1:1";
    }
}

EditorModule::CompositeExportAspectPreset ExportAspectPresetFromToken(const std::string& token) {
    if (token == "4:3") return EditorModule::CompositeExportAspectPreset::Ratio4x3;
    if (token == "3:2") return EditorModule::CompositeExportAspectPreset::Ratio3x2;
    if (token == "16:9") return EditorModule::CompositeExportAspectPreset::Ratio16x9;
    if (token == "9:16") return EditorModule::CompositeExportAspectPreset::Ratio9x16;
    if (token == "2:3") return EditorModule::CompositeExportAspectPreset::Ratio2x3;
    if (token == "5:4") return EditorModule::CompositeExportAspectPreset::Ratio5x4;
    if (token == "21:9") return EditorModule::CompositeExportAspectPreset::Ratio21x9;
    if (token == "custom") return EditorModule::CompositeExportAspectPreset::Custom;
    return EditorModule::CompositeExportAspectPreset::Ratio1x1;
}

EditorModule::CompositeSnapModePreset DetermineCompositeSnapModePreset(const EditorModule::CompositeSnapSettings& settings) {
    if (!settings.enabled) {
        return EditorModule::CompositeSnapModePreset::Off;
    }

    const bool fullMatch =
        settings.snapToObjects &&
        settings.snapToCenters &&
        settings.snapToCanvasCenter &&
        settings.snapToExportBounds &&
        settings.rotateSnapStep > 0.0f &&
        settings.scaleSnapStep > 0.0f;
    if (fullMatch) {
        return EditorModule::CompositeSnapModePreset::Full;
    }

    const bool objectOnlyMatch =
        settings.snapToObjects &&
        settings.snapToCenters &&
        settings.snapToCanvasCenter &&
        !settings.snapToExportBounds &&
        settings.rotateSnapStep <= 0.0f &&
        settings.scaleSnapStep <= 0.0f;
    if (objectOnlyMatch) {
        return EditorModule::CompositeSnapModePreset::ObjectOnly;
    }

    return EditorModule::CompositeSnapModePreset::Custom;
}

void RememberCompositeSnapDefaults(EditorModule::CompositeSnapSettings& settings) {
    if (settings.rotateSnapStep > 0.0f) {
        settings.lastNonZeroRotateSnapStep = settings.rotateSnapStep;
    }
    if (settings.scaleSnapStep > 0.0f) {
        settings.lastNonZeroScaleSnapStep = settings.scaleSnapStep;
    }
}

} // namespace

EditorModule::CompositeSceneItem* EditorModule::FindCompositeSceneItem(int outputNodeId) {
    auto it = std::find_if(
        m_CompositeSceneItems.begin(),
        m_CompositeSceneItems.end(),
        [outputNodeId](const CompositeSceneItem& item) { return item.outputNodeId == outputNodeId; });
    return it == m_CompositeSceneItems.end() ? nullptr : &(*it);
}

const EditorModule::CompositeSceneItem* EditorModule::FindCompositeSceneItem(int outputNodeId) const {
    auto it = std::find_if(
        m_CompositeSceneItems.begin(),
        m_CompositeSceneItems.end(),
        [outputNodeId](const CompositeSceneItem& item) { return item.outputNodeId == outputNodeId; });
    return it == m_CompositeSceneItems.end() ? nullptr : &(*it);
}

bool EditorModule::AddGraphImageChainFromFile(const std::string& path, EditorNodeGraph::Vec2 sourcePosition) {
    const int completedBefore = GetCompletedChainCount();
    if (!AddImageNodeFromFile(path, sourcePosition)) {
        return false;
    }

    EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(m_NodeGraph.GetSelectedNodeId());
    EditorNodeGraph::Node* outputNode = m_NodeGraph.AddOutputNode(EditorNodeGraph::Vec2{ sourcePosition.x + 330.0f, sourcePosition.y });
    if (!sourceNode || !outputNode) {
        return false;
    }

    std::string errorMessage;
    if (!ConnectGraphNodes(sourceNode->id, outputNode->id, &errorMessage)) {
        return false;
    }
    if (completedBefore < 2 && GetCompletedChainCount() >= 2) {
        EnsureCompositeNode();
        EnsureExportBoundsSettingsNode();
    }
    EnsureCompositeSceneState(m_LastCompositeCanvasSize);
    MoveCompositeOutputToFront(outputNode->id);
    m_CompositeSelectedOutputNodeId = outputNode->id;
    return true;
}

std::pair<EditorNodeGraph::Vec2, EditorNodeGraph::Vec2> EditorModule::BuildCompositeChainPlacement() const {
    float maxY = 120.0f;
    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        maxY = std::max(maxY, node.position.y);
    }

    const auto compositeIt = std::find_if(
        m_NodeGraph.GetNodes().begin(),
        m_NodeGraph.GetNodes().end(),
        [](const EditorNodeGraph::Node& node) { return node.kind == EditorNodeGraph::NodeKind::Composite; });
    if (compositeIt != m_NodeGraph.GetNodes().end()) {
        maxY = std::max(maxY, compositeIt->position.y + 160.0f);
    }

    const float rowY = maxY + 150.0f;
    return {
        EditorNodeGraph::Vec2{ 180.0f, rowY },
        EditorNodeGraph::Vec2{ 510.0f, rowY }
    };
}

bool EditorModule::AddCompositeImageChainFromFile(const std::string& path) {
    const int completedBefore = GetCompletedChainCount();
    const auto [sourcePos, outputPos] = BuildCompositeChainPlacement();
    if (!AddImageNodeFromFile(path, sourcePos)) {
        return false;
    }

    EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(m_NodeGraph.GetSelectedNodeId());
    EditorNodeGraph::Node* outputNode = m_NodeGraph.AddOutputNode(outputPos);
    if (!sourceNode || !outputNode) {
        return false;
    }

    std::string errorMessage;
    if (!ConnectGraphNodes(sourceNode->id, outputNode->id, &errorMessage)) {
        return false;
    }
    if (completedBefore < 2 && GetCompletedChainCount() >= 2) {
        EnsureCompositeNode();
        EnsureExportBoundsSettingsNode();
    }
    EnsureCompositeSceneState(m_LastCompositeCanvasSize);
    MoveCompositeOutputToFront(outputNode->id);
    m_CompositeSelectedOutputNodeId = outputNode->id;
    return true;
}

bool EditorModule::AddCompositeLibraryAssetChain(const std::string& assetFileName) {
    if (assetFileName.empty()) {
        return false;
    }
    const std::filesystem::path assetPath = LibraryManager::Get().GetAssetsPath() / assetFileName;
    if (!std::filesystem::exists(assetPath)) {
        return false;
    }
    return AddCompositeImageChainFromFile(assetPath.string());
}

bool EditorModule::AddCompositeGeneratorChain(EditorNodeGraph::ImageGeneratorKind generatorKind) {
    const int completedBefore = GetCompletedChainCount();
    const auto [sourcePos, outputPos] = BuildCompositeChainPlacement();
    EditorNodeGraph::Node* generatorNode = m_NodeGraph.AddImageGeneratorNode(generatorKind, sourcePos);
    EditorNodeGraph::Node* outputNode = m_NodeGraph.AddOutputNode(outputPos);
    if (!generatorNode || !outputNode) {
        return false;
    }

    std::string errorMessage;
    if (!ConnectGraphNodes(generatorNode->id, outputNode->id, &errorMessage)) {
        return false;
    }
    if (completedBefore < 2 && GetCompletedChainCount() >= 2) {
        EnsureCompositeNode();
        EnsureExportBoundsSettingsNode();
    }
    EnsureCompositeSceneState(m_LastCompositeCanvasSize);
    MoveCompositeOutputToFront(outputNode->id);
    m_CompositeSelectedOutputNodeId = outputNode->id;
    SelectGraphNode(generatorNode->id);
    return true;
}

void EditorModule::ClearCompositeSceneTextures() {
    for (CompositeSceneItem& item : m_CompositeSceneItems) {
        if (item.texture != 0) {
            glDeleteTextures(1, &item.texture);
            item.texture = 0;
        }
        item.textureWidth = 0;
        item.textureHeight = 0;
        item.rgbaPixels.clear();
        item.cachedRenderRevision = 0;
        item.cachedChainFingerprint = 0;
        item.requestedRenderRevision = 0;
        item.requestedChainFingerprint = 0;
    }
}

void EditorModule::ClearPersistedCompositeState() {
    m_PersistedCompositeSceneEntries.clear();
    m_CompositeZOrder.clear();
    m_CompositeExportSettings = CompositeExportSettings {};
}

void EditorModule::ClearCompositeRuntimeState() {
    ClearCompositeSceneTextures();
    m_CompositeSceneItems.clear();
    ClearPersistedCompositeState();
    m_CompositeSelectedOutputNodeId = -1;
    m_CompositeViewZoom = 1.0f;
    m_CompositeViewPanX = 0.0f;
    m_CompositeViewPanY = 0.0f;
    m_CompositeMoveActive = false;
    m_CompositeDragOutputNodeId = -1;
    m_CompositePanActive = false;
    m_LastCompositeCanvasSize = ImVec2(0.0f, 0.0f);
    m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
    m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
    m_CompositeExportBoundsEditMode = false;
    m_CompositeSnapSettings = CompositeSnapSettings {};
    m_CachedCompletedChains.clear();
    m_CachedCompositeFingerprints.clear();
    m_CachedCompositeLabels.clear();
    m_CachedCompletedChainsStructureRevision = 0;
    m_CachedConnectedOutputCount = 0;
    m_CachedCompositeMetadataStructureRevision = 0;
    m_CachedCompositeMetadataRenderRevision = 0;
    m_CompositeOutputDirtyGenerations.clear();
    m_CompositeOutputRequestedGenerations.clear();
    m_CompositeOutputCompletedGenerations.clear();
    m_LastCompositeSceneSyncStructureRevision = 0;
    m_LastCompositeSceneSyncRenderRevision = 0;
    m_LastCompositeSceneSyncCanvasSize = ImVec2(-1.0f, -1.0f);
}

const EditorModule::PersistedCompositeSceneEntry* EditorModule::FindPersistedCompositeSceneEntry(int outputNodeId) const {
    auto it = std::find_if(
        m_PersistedCompositeSceneEntries.begin(),
        m_PersistedCompositeSceneEntries.end(),
        [outputNodeId](const PersistedCompositeSceneEntry& entry) { return entry.outputNodeId == outputNodeId; });
    return it == m_PersistedCompositeSceneEntries.end() ? nullptr : &(*it);
}

nlohmann::json EditorModule::SerializeCompositePersistence() const {
    nlohmann::json composite = nlohmann::json::object();

    std::vector<PersistedCompositeSceneEntry> entries = m_PersistedCompositeSceneEntries;
    for (const CompositeSceneItem& item : m_CompositeSceneItems) {
        auto it = std::find_if(
            entries.begin(),
            entries.end(),
            [&item](const PersistedCompositeSceneEntry& entry) { return entry.outputNodeId == item.outputNodeId; });
        if (it == entries.end()) {
            PersistedCompositeSceneEntry entry;
            entry.outputNodeId = item.outputNodeId;
            entry.position = item.position;
            entry.scale = item.scale;
            entry.rotation = item.rotation;
            entry.visible = item.visible;
            entry.locked = item.locked;
            entries.push_back(std::move(entry));
        } else {
            it->position = item.position;
            it->scale = item.scale;
            it->rotation = item.rotation;
            it->visible = item.visible;
            it->locked = item.locked;
        }
    }

    nlohmann::json sceneItems = nlohmann::json::array();
    for (const PersistedCompositeSceneEntry& item : entries) {
        const EditorNodeGraph::Node* outputNode = m_NodeGraph.FindNode(item.outputNodeId);
        if (!outputNode || outputNode->kind != EditorNodeGraph::NodeKind::Output) {
            continue;
        }
        sceneItems.push_back({
            { "outputNodeId", item.outputNodeId },
            { "position", { item.position.x, item.position.y } },
            { "scale", { item.scale.x, item.scale.y } },
            { "rotation", item.rotation },
            { "visible", item.visible },
            { "locked", item.locked }
        });
    }
    composite["sceneItems"] = std::move(sceneItems);
    nlohmann::json zOrder = nlohmann::json::array();
    for (int outputNodeId : m_CompositeZOrder) {
        const EditorNodeGraph::Node* outputNode = m_NodeGraph.FindNode(outputNodeId);
        if (outputNode && outputNode->kind == EditorNodeGraph::NodeKind::Output) {
            zOrder.push_back(outputNodeId);
        }
    }
    composite["zOrder"] = std::move(zOrder);
    composite["exportSettings"] = {
        { "boundsMode", ExportBoundsModeToToken(m_CompositeExportSettings.boundsMode) },
        { "backgroundMode", ExportBackgroundModeToToken(m_CompositeExportSettings.backgroundMode) },
        { "backgroundColor", {
            m_CompositeExportSettings.backgroundColor[0],
            m_CompositeExportSettings.backgroundColor[1],
            m_CompositeExportSettings.backgroundColor[2],
            m_CompositeExportSettings.backgroundColor[3]
        } },
        { "customX", m_CompositeExportSettings.customX },
        { "customY", m_CompositeExportSettings.customY },
        { "customWidth", m_CompositeExportSettings.customWidth },
        { "customHeight", m_CompositeExportSettings.customHeight },
        { "aspectPreset", ExportAspectPresetToToken(m_CompositeExportSettings.aspectPreset) },
        { "customAspectRatio", m_CompositeExportSettings.customAspectRatio },
        { "outputWidth", m_CompositeExportSettings.outputWidth },
        { "outputHeight", m_CompositeExportSettings.outputHeight }
    };

    return composite;
}

void EditorModule::DeserializeCompositePersistence(const nlohmann::json& pipelineData) {
    ClearPersistedCompositeState();
    if (!pipelineData.is_object()) {
        return;
    }

    const nlohmann::json composite = pipelineData.value("editorComposite", nlohmann::json::object());
    if (!composite.is_object()) {
        return;
    }

    const nlohmann::json sceneItems = composite.value("sceneItems", nlohmann::json::array());
    if (sceneItems.is_array()) {
        for (const auto& item : sceneItems) {
            if (!item.is_object()) {
                continue;
            }
            PersistedCompositeSceneEntry entry;
            entry.outputNodeId = item.value("outputNodeId", -1);
            const nlohmann::json position = item.value("position", nlohmann::json::array());
            const nlohmann::json scale = item.value("scale", nlohmann::json::array());
            if (position.is_array() && position.size() >= 2) {
                entry.position.x = position[0].get<float>();
                entry.position.y = position[1].get<float>();
            }
            if (scale.is_array() && scale.size() >= 2) {
                entry.scale.x = scale[0].get<float>();
                entry.scale.y = scale[1].get<float>();
            }
            entry.rotation = item.value("rotation", 0.0f);
            entry.visible = item.value("visible", true);
            entry.locked = item.value("locked", false);
            if (entry.outputNodeId > 0) {
                m_PersistedCompositeSceneEntries.push_back(std::move(entry));
            }
        }
    }

    const nlohmann::json zOrder = composite.value("zOrder", nlohmann::json::array());
    if (zOrder.is_array()) {
        for (const auto& value : zOrder) {
            if (value.is_number_integer()) {
                m_CompositeZOrder.push_back(value.get<int>());
            }
        }
    }

    const nlohmann::json exportSettings = composite.value("exportSettings", nlohmann::json::object());
    if (exportSettings.is_object()) {
        m_CompositeExportSettings.boundsMode = ExportBoundsModeFromToken(exportSettings.value("boundsMode", std::string("auto")));
        m_CompositeExportSettings.backgroundMode = ExportBackgroundModeFromToken(exportSettings.value("backgroundMode", std::string("transparent")));
        const nlohmann::json backgroundColor = exportSettings.value("backgroundColor", nlohmann::json::array());
        for (int i = 0; i < 4; ++i) {
            if (backgroundColor.is_array() && backgroundColor.size() > static_cast<size_t>(i)) {
                m_CompositeExportSettings.backgroundColor[i] = backgroundColor[i].get<float>();
            }
        }
        m_CompositeExportSettings.customX = exportSettings.value("customX", m_CompositeExportSettings.customX);
        m_CompositeExportSettings.customY = exportSettings.value("customY", m_CompositeExportSettings.customY);
        m_CompositeExportSettings.customWidth = exportSettings.value("customWidth", m_CompositeExportSettings.customWidth);
        m_CompositeExportSettings.customHeight = exportSettings.value("customHeight", m_CompositeExportSettings.customHeight);
        m_CompositeExportSettings.aspectPreset = ExportAspectPresetFromToken(exportSettings.value("aspectPreset", std::string("1:1")));
        m_CompositeExportSettings.customAspectRatio = exportSettings.value("customAspectRatio", m_CompositeExportSettings.customAspectRatio);
        m_CompositeExportSettings.outputWidth = exportSettings.value("outputWidth", m_CompositeExportSettings.outputWidth);
        m_CompositeExportSettings.outputHeight = exportSettings.value("outputHeight", m_CompositeExportSettings.outputHeight);
    }
}

void EditorModule::ClearCompositeTransientInteractionState() {
    m_CompositeSelectedOutputNodeId = -1;
    EndCompositeMove();
    EndCompositePan();
}

std::vector<unsigned char> EditorModule::GetCompositePixelsForOutputNode(int outputNodeId, int& outW, int& outH) {
    outW = 0;
    outH = 0;
    if (outputNodeId <= 0) {
        return {};
    }

    RefreshCompletedChainCacheIfNeeded();
    const auto chainIt = std::find_if(
        m_CachedCompletedChains.begin(),
        m_CachedCompletedChains.end(),
        [outputNodeId](const CachedCompositeChainState& chain) { return chain.info.outputNodeId == outputNodeId; });

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;

    if (chainIt != m_CachedCompletedChains.end()) {
        const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(chainIt->info.sourceNodeId);
        if (sourceNode &&
            sourceNode->kind == EditorNodeGraph::NodeKind::Image &&
            !sourceNode->image.pixels.empty() &&
            sourceNode->image.width > 0 &&
            sourceNode->image.height > 0) {
            sourcePixels = sourceNode->image.pixels;
            sourceW = sourceNode->image.width;
            sourceH = sourceNode->image.height;
            sourceCh = std::max(1, sourceNode->image.channels);
        }
    }

    if (sourcePixels.empty() || sourceW <= 0 || sourceH <= 0) {
        sourceW = 256;
        sourceH = 256;
        sourceCh = 4;
        sourcePixels.assign(static_cast<size_t>(sourceW * sourceH * sourceCh), 0);
        for (size_t i = 3; i < sourcePixels.size(); i += 4) {
            sourcePixels[i] = 255;
        }
    }

    m_CompositePreviewPipeline.LoadSourceFromPixels(sourcePixels.data(), sourceW, sourceH, sourceCh);
    RenderGraphSnapshot snapshot = BuildGraphSnapshot();
    snapshot.outputNodeId = outputNodeId;
    m_CompositePreviewPipeline.ExecuteGraph(snapshot);
    return m_CompositePreviewPipeline.GetOutputPixels(outW, outH);
}

bool EditorModule::MoveCompositeOutputToIndex(int outputNodeId, int targetIndex) {
    auto it = std::find(m_CompositeZOrder.begin(), m_CompositeZOrder.end(), outputNodeId);
    if (it == m_CompositeZOrder.end()) {
        return false;
    }
    targetIndex = std::clamp(targetIndex, 0, static_cast<int>(m_CompositeZOrder.size()) - 1);
    const int currentIndex = static_cast<int>(std::distance(m_CompositeZOrder.begin(), it));
    if (currentIndex == targetIndex) {
        return false;
    }

    m_CompositeZOrder.erase(it);
    m_CompositeZOrder.insert(m_CompositeZOrder.begin() + targetIndex, outputNodeId);
    return true;
}

void EditorModule::MoveCompositeOutputToFront(int outputNodeId) {
    MoveCompositeOutputToIndex(outputNodeId, 0);
}

bool EditorModule::ReorderCompositeOutputBefore(int draggedOutputNodeId, int targetOutputNodeId) {
    auto draggedIt = std::find(m_CompositeZOrder.begin(), m_CompositeZOrder.end(), draggedOutputNodeId);
    auto targetIt = std::find(m_CompositeZOrder.begin(), m_CompositeZOrder.end(), targetOutputNodeId);
    if (draggedIt == m_CompositeZOrder.end() || targetIt == m_CompositeZOrder.end() || draggedOutputNodeId == targetOutputNodeId) {
        return false;
    }

    int targetIndex = static_cast<int>(std::distance(m_CompositeZOrder.begin(), targetIt));
    if (draggedIt < targetIt) {
        --targetIndex;
    }
    m_CompositeZOrder.erase(draggedIt);
    m_CompositeZOrder.insert(m_CompositeZOrder.begin() + targetIndex, draggedOutputNodeId);
    return true;
}

void EditorModule::ToggleCompositeScaleOriginMode() {
    m_CompositeScaleOriginMode =
        (m_CompositeScaleOriginMode == CompositeScaleOriginMode::Opposite)
        ? CompositeScaleOriginMode::Center
        : CompositeScaleOriginMode::Opposite;
}

void EditorModule::ClampCompositeViewPanToContent(const ImVec2& canvasSize) {
    if (canvasSize.x <= 1.0f || canvasSize.y <= 1.0f) {
        return;
    }

    CompositeFloatRect contentBounds {};
    bool foundBounds = TryGetCompositeAutoExportBounds(contentBounds);
    if (m_CompositeExportSettings.boundsMode == CompositeExportBoundsMode::Custom) {
        const CompositeFloatRect exportBounds {
            m_CompositeExportSettings.customX,
            m_CompositeExportSettings.customY,
            m_CompositeExportSettings.customWidth,
            m_CompositeExportSettings.customHeight
        };
        if (IsValidRect(exportBounds)) {
            if (!foundBounds) {
                contentBounds = exportBounds;
                foundBounds = true;
            } else {
                const float left = std::min(contentBounds.x, exportBounds.x);
                const float top = std::min(contentBounds.y, exportBounds.y);
                const float right = std::max(contentBounds.x + contentBounds.width, exportBounds.x + exportBounds.width);
                const float bottom = std::max(contentBounds.y + contentBounds.height, exportBounds.y + exportBounds.height);
                contentBounds = MakeNormalizedRect(left, top, right, bottom);
            }
        }
    }
    if (!foundBounds) {
        return;
    }

    const float visibleWorldW = canvasSize.x / std::max(0.001f, m_CompositeViewZoom);
    const float visibleWorldH = canvasSize.y / std::max(0.001f, m_CompositeViewZoom);
    const float graceX = std::max(96.0f, visibleWorldW * 0.35f);
    const float graceY = std::max(96.0f, visibleWorldH * 0.35f);
    const float minCenterX = contentBounds.x + contentBounds.width * 0.5f - contentBounds.width * 0.5f - graceX;
    const float maxCenterX = contentBounds.x + contentBounds.width * 0.5f + contentBounds.width * 0.5f + graceX;
    const float minCenterY = contentBounds.y + contentBounds.height * 0.5f - contentBounds.height * 0.5f - graceY;
    const float maxCenterY = contentBounds.y + contentBounds.height * 0.5f + contentBounds.height * 0.5f + graceY;

    m_CompositeViewPanX = std::clamp(m_CompositeViewPanX, minCenterX, maxCenterX);
    m_CompositeViewPanY = std::clamp(m_CompositeViewPanY, minCenterY, maxCenterY);
}

void EditorModule::BeginCompositeMove(int outputNodeId, const ImVec2& mouseWorld) {
    CompositeSceneItem* item = FindCompositeSceneItem(outputNodeId);
    if (!item || item->locked) {
        return;
    }
    m_CompositeSelectedOutputNodeId = outputNodeId;
    m_CompositeMoveActive = true;
    m_CompositeDragOutputNodeId = outputNodeId;
    m_CompositeDragStartMouseWorld = mouseWorld;
    m_CompositeDragStartPosition = item->position;
}

void EditorModule::UpdateCompositeMove(const ImVec2& mouseWorld) {
    if (!m_CompositeMoveActive || m_CompositeDragOutputNodeId <= 0) {
        return;
    }
    CompositeSceneItem* item = FindCompositeSceneItem(m_CompositeDragOutputNodeId);
    if (!item) {
        EndCompositeMove();
        return;
    }
    const ImVec2 delta(mouseWorld.x - m_CompositeDragStartMouseWorld.x, mouseWorld.y - m_CompositeDragStartMouseWorld.y);
    item->position = ImVec2(m_CompositeDragStartPosition.x + delta.x, m_CompositeDragStartPosition.y + delta.y);
}

void EditorModule::EndCompositeMove() {
    m_CompositeMoveActive = false;
    m_CompositeDragOutputNodeId = -1;
}

void EditorModule::BeginCompositePan(const ImVec2& mouseScreen) {
    m_CompositePanActive = true;
    m_CompositePanStartMouseScreen = mouseScreen;
    m_CompositePanStartX = m_CompositeViewPanX;
    m_CompositePanStartY = m_CompositeViewPanY;
}

void EditorModule::UpdateCompositePan(const ImVec2& mouseScreen) {
    if (!m_CompositePanActive) {
        return;
    }
    const ImVec2 delta(mouseScreen.x - m_CompositePanStartMouseScreen.x, mouseScreen.y - m_CompositePanStartMouseScreen.y);
    m_CompositeViewPanX = m_CompositePanStartX - delta.x / std::max(0.001f, m_CompositeViewZoom);
    m_CompositeViewPanY = m_CompositePanStartY - delta.y / std::max(0.001f, m_CompositeViewZoom);
}

void EditorModule::EndCompositePan() {
    m_CompositePanActive = false;
}

void EditorModule::SyncCompositeSceneItems(const ImVec2& canvasSize) {
    RefreshCompositeMetadataCacheIfNeeded();
    if (m_CachedCompletedChains.size() >= 2) {
        EnsureCompositeNode();
        EnsureExportBoundsSettingsNode();
    }

    auto upsertPersistedEntry = [this](const CompositeSceneItem& item) {
        auto it = std::find_if(
            m_PersistedCompositeSceneEntries.begin(),
            m_PersistedCompositeSceneEntries.end(),
            [&item](const PersistedCompositeSceneEntry& entry) { return entry.outputNodeId == item.outputNodeId; });
        if (it == m_PersistedCompositeSceneEntries.end()) {
            PersistedCompositeSceneEntry entry;
            entry.outputNodeId = item.outputNodeId;
            entry.position = item.position;
            entry.scale = item.scale;
            entry.rotation = item.rotation;
            entry.visible = item.visible;
            entry.locked = item.locked;
            m_PersistedCompositeSceneEntries.push_back(std::move(entry));
        } else {
            it->position = item.position;
            it->scale = item.scale;
            it->rotation = item.rotation;
            it->visible = item.visible;
            it->locked = item.locked;
        }
    };

    for (const CompositeSceneItem& existing : m_CompositeSceneItems) {
        if (existing.outputNodeId > 0) {
            upsertPersistedEntry(existing);
        }
    }

    std::vector<int> activeOutputIds;
    activeOutputIds.reserve(m_CachedCompletedChains.size());
    for (const CachedCompositeChainState& chain : m_CachedCompletedChains) {
        activeOutputIds.push_back(chain.info.outputNodeId);
    }

    std::vector<int> newOrder;
    newOrder.reserve(activeOutputIds.size());
    for (int outputNodeId : m_CompositeZOrder) {
        if (std::find(activeOutputIds.begin(), activeOutputIds.end(), outputNodeId) != activeOutputIds.end()) {
            newOrder.push_back(outputNodeId);
        }
    }
    for (int outputNodeId : activeOutputIds) {
        if (std::find(newOrder.begin(), newOrder.end(), outputNodeId) == newOrder.end()) {
            newOrder.push_back(outputNodeId);
        }
    }
    m_CompositeZOrder = std::move(newOrder);

    for (CompositeSceneItem& existing : m_CompositeSceneItems) {
        if (std::find(activeOutputIds.begin(), activeOutputIds.end(), existing.outputNodeId) == activeOutputIds.end() &&
            existing.texture != 0) {
            glDeleteTextures(1, &existing.texture);
            existing.texture = 0;
        }
    }

    std::vector<CompositeSceneItem> updatedItems;
    updatedItems.reserve(activeOutputIds.size());
    for (int outputNodeId : activeOutputIds) {
        CompositeSceneItem item;
        if (const CompositeSceneItem* existing = FindCompositeSceneItem(outputNodeId)) {
            item = *existing;
        } else if (const PersistedCompositeSceneEntry* persisted = FindPersistedCompositeSceneEntry(outputNodeId)) {
            item.outputNodeId = outputNodeId;
            item.position = persisted->position;
            item.scale = persisted->scale;
            item.rotation = persisted->rotation;
            item.visible = persisted->visible;
            item.locked = persisted->locked;
            item.placementInitialized = true;
        } else {
            item.outputNodeId = outputNodeId;
        }

        const auto chainIt = std::find_if(
            m_CachedCompletedChains.begin(),
            m_CachedCompletedChains.end(),
            [outputNodeId](const CachedCompositeChainState& chain) { return chain.info.outputNodeId == outputNodeId; });
        if (chainIt != m_CachedCompletedChains.end()) {
            item.label = chainIt->label;
            item.requestedChainFingerprint = chainIt->fingerprint;
        } else if (item.label.empty()) {
            item.label = "Output " + std::to_string(outputNodeId);
        }

        if (!item.placementInitialized && item.textureWidth > 0 && item.textureHeight > 0 && canvasSize.x > 1.0f && canvasSize.y > 1.0f) {
            const int zIndex = static_cast<int>(std::distance(
                m_CompositeZOrder.begin(),
                std::find(m_CompositeZOrder.begin(), m_CompositeZOrder.end(), outputNodeId)));
            const float visibleWorldW = canvasSize.x / std::max(0.001f, m_CompositeViewZoom);
            const float visibleWorldH = canvasSize.y / std::max(0.001f, m_CompositeViewZoom);
            const float targetW = visibleWorldW * 0.42f;
            const float targetH = visibleWorldH * 0.42f;
            const float fitScale = std::clamp(
                std::min(targetW / static_cast<float>(item.textureWidth), targetH / static_cast<float>(item.textureHeight)),
                0.1f,
                1.0f);
            item.scale = ImVec2(fitScale, fitScale);
            const float drawW = static_cast<float>(item.textureWidth) * item.scale.x;
            const float drawH = static_cast<float>(item.textureHeight) * item.scale.y;
            const ImVec2 stagger(
                (static_cast<float>(zIndex % 3) - 1.0f) * 38.0f,
                (static_cast<float>(zIndex % 4) - 1.5f) * 26.0f);
            item.position = ImVec2(
                m_CompositeViewPanX - drawW * 0.5f + stagger.x,
                m_CompositeViewPanY - drawH * 0.5f + stagger.y);
            item.placementInitialized = true;
        }

        updatedItems.push_back(std::move(item));
    }

    m_CompositeSceneItems = std::move(updatedItems);
    if (m_CompositeSelectedOutputNodeId > 0 &&
        std::none_of(
            m_CompositeSceneItems.begin(),
            m_CompositeSceneItems.end(),
            [this](const CompositeSceneItem& item) { return item.outputNodeId == m_CompositeSelectedOutputNodeId; })) {
        m_CompositeSelectedOutputNodeId = -1;
    }
}

void EditorModule::EnsureCompositeSceneState(const ImVec2& canvasSize) {
    const std::uint64_t structureRevision = m_NodeGraph.GetStructureRevision();
    const bool canvasChanged =
        std::abs(canvasSize.x - m_LastCompositeSceneSyncCanvasSize.x) > 0.5f ||
        std::abs(canvasSize.y - m_LastCompositeSceneSyncCanvasSize.y) > 0.5f;
    const bool revisionsChanged =
        m_LastCompositeSceneSyncStructureRevision != structureRevision;
    const bool needsPlacementPass =
        canvasSize.x > 1.0f &&
        canvasSize.y > 1.0f &&
        std::any_of(
            m_CompositeSceneItems.begin(),
            m_CompositeSceneItems.end(),
            [](const CompositeSceneItem& item) {
                return !item.placementInitialized && item.textureWidth > 0 && item.textureHeight > 0;
            });
    if (!revisionsChanged && !canvasChanged && !needsPlacementPass) {
        return;
    }

    SyncCompositeSceneItems(canvasSize);
    m_LastCompositeSceneSyncStructureRevision = structureRevision;
    m_LastCompositeSceneSyncCanvasSize = canvasSize;
}

bool EditorModule::TryGetCompositeAutoExportBounds(CompositeFloatRect& outBounds) const {
    bool found = false;
    CompositeFloatRect bounds {};
    for (const int outputNodeId : m_CompositeZOrder) {
        const CompositeSceneItem* item = FindCompositeSceneItem(outputNodeId);
        if (!item || !item->visible || item->textureWidth <= 0 || item->textureHeight <= 0) {
            continue;
        }
        const CompositeFloatRect itemBounds = QuadBounds(ComputeSceneQuadWorld(*item));
        if (!IsValidRect(itemBounds)) {
            continue;
        }
        if (!found) {
            bounds = itemBounds;
            found = true;
        } else {
            const float left = std::min(bounds.x, itemBounds.x);
            const float top = std::min(bounds.y, itemBounds.y);
            const float right = std::max(bounds.x + bounds.width, itemBounds.x + itemBounds.width);
            const float bottom = std::max(bounds.y + bounds.height, itemBounds.y + itemBounds.height);
            bounds = MakeNormalizedRect(left, top, right, bottom);
        }
    }
    if (!found) {
        return false;
    }
    outBounds = bounds;
    return true;
}

float EditorModule::GetCurrentCompositeExportAspectRatio() const {
    if (m_CompositeExportSettings.boundsMode == CompositeExportBoundsMode::Custom) {
        const CompositeFloatRect customBounds {
            m_CompositeExportSettings.customX,
            m_CompositeExportSettings.customY,
            m_CompositeExportSettings.customWidth,
            m_CompositeExportSettings.customHeight
        };
        if (IsValidRect(customBounds)) {
            return RectAspectRatio(customBounds);
        }
    } else {
        CompositeFloatRect autoBounds {};
        if (TryGetCompositeAutoExportBounds(autoBounds)) {
            return RectAspectRatio(autoBounds);
        }
    }

    switch (m_CompositeExportSettings.aspectPreset) {
        case CompositeExportAspectPreset::Ratio4x3: return 4.0f / 3.0f;
        case CompositeExportAspectPreset::Ratio3x2: return 3.0f / 2.0f;
        case CompositeExportAspectPreset::Ratio16x9: return 16.0f / 9.0f;
        case CompositeExportAspectPreset::Ratio9x16: return 9.0f / 16.0f;
        case CompositeExportAspectPreset::Ratio2x3: return 2.0f / 3.0f;
        case CompositeExportAspectPreset::Ratio5x4: return 5.0f / 4.0f;
        case CompositeExportAspectPreset::Ratio21x9: return 21.0f / 9.0f;
        case CompositeExportAspectPreset::Custom: return std::max(0.0001f, m_CompositeExportSettings.customAspectRatio);
        case CompositeExportAspectPreset::Ratio1x1:
        default:
            return 1.0f;
    }
}

void EditorModule::UpdateCompositeCustomExportAspectFromBounds() {
    m_CompositeExportSettings.customWidth = std::max(1.0f, m_CompositeExportSettings.customWidth);
    m_CompositeExportSettings.customHeight = std::max(1.0f, m_CompositeExportSettings.customHeight);
    m_CompositeExportSettings.customAspectRatio =
        std::max(1.0f, m_CompositeExportSettings.customWidth) / std::max(1.0f, m_CompositeExportSettings.customHeight);
}

void EditorModule::SyncCompositeExportResolutionFromWidth() {
    m_CompositeExportSettings.outputWidth = std::max(1, m_CompositeExportSettings.outputWidth);
    const float ratio = GetCurrentCompositeExportAspectRatio();
    m_CompositeExportSettings.outputHeight = std::max(
        1,
        static_cast<int>(std::round(static_cast<float>(m_CompositeExportSettings.outputWidth) / std::max(0.0001f, ratio))));
}

void EditorModule::SyncCompositeExportResolutionFromHeight() {
    m_CompositeExportSettings.outputHeight = std::max(1, m_CompositeExportSettings.outputHeight);
    const float ratio = GetCurrentCompositeExportAspectRatio();
    m_CompositeExportSettings.outputWidth = std::max(
        1,
        static_cast<int>(std::round(static_cast<float>(m_CompositeExportSettings.outputHeight) * std::max(0.0001f, ratio))));
}

EditorModule::CompositeFloatRect EditorModule::GetCompositeViewWorldRect(const ImVec2& canvasSize) const {
    const float width = std::max(1.0f, canvasSize.x) / std::max(0.001f, m_CompositeViewZoom);
    const float height = std::max(1.0f, canvasSize.y) / std::max(0.001f, m_CompositeViewZoom);
    return {
        m_CompositeViewPanX - width * 0.5f,
        m_CompositeViewPanY - height * 0.5f,
        width,
        height
    };
}

void EditorModule::UseCompositeViewAsExportBounds(const ImVec2& canvasSize) {
    const CompositeFloatRect viewRect = GetCompositeViewWorldRect(canvasSize);
    m_CompositeExportSettings.boundsMode = CompositeExportBoundsMode::Custom;
    m_CompositeExportSettings.customX = viewRect.x;
    m_CompositeExportSettings.customY = viewRect.y;
    m_CompositeExportSettings.customWidth = viewRect.width;
    m_CompositeExportSettings.customHeight = viewRect.height;
    m_CompositeExportSettings.aspectPreset = CompositeExportAspectPreset::Custom;
    UpdateCompositeCustomExportAspectFromBounds();
    m_CompositeExportSettings.outputWidth = std::max(1, static_cast<int>(std::round(std::max(1.0f, canvasSize.x))));
    SyncCompositeExportResolutionFromWidth();
}

bool EditorModule::BuildCompositeExportRaster(std::vector<unsigned char>& outPixels, int& outW, int& outH) {
    outPixels.clear();
    outW = 0;
    outH = 0;

    EnsureCompositeSceneState(ImVec2(0.0f, 0.0f));

    CompositeFloatRect worldBounds {};
    if (m_CompositeExportSettings.boundsMode == CompositeExportBoundsMode::Custom) {
        worldBounds = {
            m_CompositeExportSettings.customX,
            m_CompositeExportSettings.customY,
            m_CompositeExportSettings.customWidth,
            m_CompositeExportSettings.customHeight
        };
        if (!IsValidRect(worldBounds)) {
            return false;
        }
    } else if (!TryGetCompositeAutoExportBounds(worldBounds)) {
        return false;
    }

    outW = std::max(1, m_CompositeExportSettings.outputWidth);
    outH = std::max(
        1,
        static_cast<int>(std::round(static_cast<float>(outW) / std::max(0.0001f, RectAspectRatio(worldBounds)))));

    outPixels.assign(static_cast<size_t>(outW * outH * 4), 0);
    if (m_CompositeExportSettings.backgroundMode == CompositeExportBackgroundMode::Solid) {
        const unsigned char bgR = static_cast<unsigned char>(std::round(Clamp01(m_CompositeExportSettings.backgroundColor[0]) * 255.0f));
        const unsigned char bgG = static_cast<unsigned char>(std::round(Clamp01(m_CompositeExportSettings.backgroundColor[1]) * 255.0f));
        const unsigned char bgB = static_cast<unsigned char>(std::round(Clamp01(m_CompositeExportSettings.backgroundColor[2]) * 255.0f));
        const unsigned char bgA = static_cast<unsigned char>(std::round(Clamp01(m_CompositeExportSettings.backgroundColor[3]) * 255.0f));
        for (int y = 0; y < outH; ++y) {
            for (int x = 0; x < outW; ++x) {
                const size_t index = static_cast<size_t>((y * outW + x) * 4);
                outPixels[index + 0] = bgR;
                outPixels[index + 1] = bgG;
                outPixels[index + 2] = bgB;
                outPixels[index + 3] = bgA;
            }
        }
    }

    const float worldStepX = worldBounds.width / static_cast<float>(outW);
    const float worldStepY = worldBounds.height / static_cast<float>(outH);
    for (auto it = m_CompositeZOrder.rbegin(); it != m_CompositeZOrder.rend(); ++it) {
        const CompositeSceneItem* item = FindCompositeSceneItem(*it);
        if (!item || !item->visible || item->rgbaPixels.empty() || item->textureWidth <= 0 || item->textureHeight <= 0) {
            continue;
        }

        const AffineTransform2D inverse = Inverse(BuildSceneTransform(*item));
        const CompositeFloatRect itemBounds = QuadBounds(ComputeSceneQuadWorld(*item));
        const int minX = std::max(0, static_cast<int>(std::floor(((itemBounds.x - worldBounds.x) / std::max(0.0001f, worldBounds.width)) * outW)));
        const int maxX = std::min(outW - 1, static_cast<int>(std::ceil((((itemBounds.x + itemBounds.width) - worldBounds.x) / std::max(0.0001f, worldBounds.width)) * outW)));
        const int minY = std::max(0, static_cast<int>(std::floor(((itemBounds.y - worldBounds.y) / std::max(0.0001f, worldBounds.height)) * outH)));
        const int maxY = std::min(outH - 1, static_cast<int>(std::ceil((((itemBounds.y + itemBounds.height) - worldBounds.y) / std::max(0.0001f, worldBounds.height)) * outH)));

        for (int dstY = minY; dstY <= maxY; ++dstY) {
            const float worldY = worldBounds.y + (static_cast<float>(dstY) + 0.5f) * worldStepY;
            for (int dstX = minX; dstX <= maxX; ++dstX) {
                const float worldX = worldBounds.x + (static_cast<float>(dstX) + 0.5f) * worldStepX;
                const ImVec2 local = TransformPoint(inverse, ImVec2(worldX, worldY));
                if (local.x < 0.0f ||
                    local.y < 0.0f ||
                    local.x >= static_cast<float>(item->textureWidth) ||
                    local.y >= static_cast<float>(item->textureHeight)) {
                    continue;
                }

                const int srcX = std::clamp(static_cast<int>(std::floor(local.x)), 0, item->textureWidth - 1);
                const int srcY = std::clamp(static_cast<int>(std::floor(local.y)), 0, item->textureHeight - 1);
                const size_t srcIndex = static_cast<size_t>((srcY * item->textureWidth + srcX) * 4);
                AlphaBlendPixel(outPixels, outW, dstX, dstY, item->rgbaPixels.data() + srcIndex);
            }
        }
    }

    return !outPixels.empty();
}

EditorModule::CompositeSnapModePreset EditorModule::GetCompositeSnapModePreset() const {
    return DetermineCompositeSnapModePreset(m_CompositeSnapSettings);
}

void EditorModule::ApplyCompositeSnapModePreset(const CompositeSnapModePreset preset) {
    if (preset == CompositeSnapModePreset::Custom) {
        m_CompositeSnapSettings.enabled =
            m_CompositeSnapSettings.snapToObjects ||
            m_CompositeSnapSettings.snapToCenters ||
            m_CompositeSnapSettings.snapToCanvasCenter ||
            m_CompositeSnapSettings.snapToExportBounds ||
            m_CompositeSnapSettings.rotateSnapStep > 0.0f ||
            m_CompositeSnapSettings.scaleSnapStep > 0.0f;
        return;
    }

    RememberCompositeSnapDefaults(m_CompositeSnapSettings);
    switch (preset) {
        case CompositeSnapModePreset::Full:
            m_CompositeSnapSettings.enabled = true;
            m_CompositeSnapSettings.snapToObjects = true;
            m_CompositeSnapSettings.snapToCenters = true;
            m_CompositeSnapSettings.snapToCanvasCenter = true;
            m_CompositeSnapSettings.snapToExportBounds = true;
            m_CompositeSnapSettings.rotateSnapStep =
                (m_CompositeSnapSettings.lastNonZeroRotateSnapStep > 0.0f)
                    ? m_CompositeSnapSettings.lastNonZeroRotateSnapStep
                    : 15.0f;
            m_CompositeSnapSettings.scaleSnapStep =
                (m_CompositeSnapSettings.lastNonZeroScaleSnapStep > 0.0f)
                    ? m_CompositeSnapSettings.lastNonZeroScaleSnapStep
                    : 0.1f;
            break;
        case CompositeSnapModePreset::ObjectOnly:
            m_CompositeSnapSettings.enabled = true;
            m_CompositeSnapSettings.snapToObjects = true;
            m_CompositeSnapSettings.snapToCenters = true;
            m_CompositeSnapSettings.snapToCanvasCenter = true;
            m_CompositeSnapSettings.snapToExportBounds = false;
            m_CompositeSnapSettings.rotateSnapStep = 0.0f;
            m_CompositeSnapSettings.scaleSnapStep = 0.0f;
            break;
        case CompositeSnapModePreset::Off:
            m_CompositeSnapSettings.enabled = false;
            m_CompositeSnapSettings.snapToObjects = false;
            m_CompositeSnapSettings.snapToCenters = false;
            m_CompositeSnapSettings.snapToCanvasCenter = false;
            m_CompositeSnapSettings.snapToExportBounds = false;
            m_CompositeSnapSettings.rotateSnapStep = 0.0f;
            m_CompositeSnapSettings.scaleSnapStep = 0.0f;
            break;
        case CompositeSnapModePreset::Custom:
        default:
            break;
    }
}
