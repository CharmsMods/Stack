#include "Editor/EditorModule.h"

#include "Editor/Layers/ToneLayers.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace {

std::vector<unsigned char> BuildTransparentPixels(int width, int height) {
    if (width <= 0 || height <= 0) {
        return {};
    }
    return std::vector<unsigned char>(static_cast<size_t>(width * height * 4), 0);
}

bool SampleTexturePixel(
    unsigned int texture,
    int width,
    int height,
    float u,
    float v,
    std::array<float, 4>& outRgba) {
    outRgba = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (texture == 0 || width <= 0 || height <= 0) {
        return false;
    }

    const int px = std::clamp(static_cast<int>(std::round(std::clamp(u, 0.0f, 1.0f) * static_cast<float>(std::max(0, width - 1)))), 0, width - 1);
    const int py = std::clamp(static_cast<int>(std::round(std::clamp(v, 0.0f, 1.0f) * static_cast<float>(std::max(0, height - 1)))), 0, height - 1);
    const int readY = std::clamp(height - 1 - py, 0, height - 1);

    GLint prevReadFbo = 0;
    GLint prevDrawFbo = 0;
    GLint prevReadBuffer = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_READ_BUFFER, &prevReadBuffer);

    unsigned int readFbo = 0;
    glGenFramebuffers(1, &readFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    bool success = false;
    if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        while (glGetError() != GL_NO_ERROR) {}
        glReadPixels(px, readY, 1, 1, GL_RGBA, GL_FLOAT, outRgba.data());
        success = glGetError() == GL_NO_ERROR;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDrawFbo));
    glReadBuffer(static_cast<GLenum>(prevReadBuffer));
    if (readFbo != 0) {
        glDeleteFramebuffers(1, &readFbo);
    }
    return success;
}

bool SampleViewportTileSet(
    const EditorRenderWorker::SharedTextureTileSet& tileSet,
    float u,
    float v,
    std::array<float, 4>& outRgba) {
    if (!tileSet.tiled || !tileSet.complete || tileSet.fullWidth <= 0 || tileSet.fullHeight <= 0) {
        return false;
    }
    const int fullX = std::clamp(
        static_cast<int>(std::round(std::clamp(u, 0.0f, 1.0f) * static_cast<float>(std::max(0, tileSet.fullWidth - 1)))),
        0,
        tileSet.fullWidth - 1);
    const int fullY = std::clamp(
        static_cast<int>(std::round(std::clamp(v, 0.0f, 1.0f) * static_cast<float>(std::max(0, tileSet.fullHeight - 1)))),
        0,
        tileSet.fullHeight - 1);
    for (const EditorRenderWorker::SharedTextureTile& tile : tileSet.tiles) {
        if (fullX < tile.x || fullX >= tile.x + tile.width ||
            fullY < tile.y || fullY >= tile.y + tile.height) {
            continue;
        }
        const float localU =
            (static_cast<float>(fullX - tile.haloX) + 0.5f) /
            static_cast<float>(std::max(1, tile.haloWidth));
        const float localV =
            (static_cast<float>(fullY - tile.haloY) + 0.5f) /
            static_cast<float>(std::max(1, tile.haloHeight));
        return SampleTexturePixel(tile.texture, tile.haloWidth, tile.haloHeight, localU, localV, outRgba);
    }
    return false;
}

} // namespace

bool EditorModule::ProbeViewTransformInputStats(int viewTransformNodeId, RenderTextureStats& outStats) const {
    outStats = {};

    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(viewTransformNodeId);
    if (!node ||
        node->kind != EditorNodeGraph::NodeKind::Layer ||
        node->layerType != LayerType::ViewTransform) {
        return false;
    }

    const EditorNodeGraph::Link* input = m_NodeGraph.FindInputLink(viewTransformNodeId, EditorNodeGraph::kImageInputSocketId);
    if (!input) {
        return false;
    }

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;
    if (!TryResolveReferenceSourcePixels(input->fromNodeId, input->fromSocketId, sourcePixels, sourceW, sourceH, sourceCh)) {
        sourcePixels = m_Pipeline.GetSourcePixelsRaw();
        sourceW = m_Pipeline.GetCanvasWidth();
        sourceH = m_Pipeline.GetCanvasHeight();
        sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
    }
    if (sourcePixels.empty() || sourceW <= 0 || sourceH <= 0) {
        return false;
    }

    RenderGraphSnapshot snapshot = BuildGraphSnapshot();
    const int syntheticOutputId = -300000 - viewTransformNodeId;
    RenderGraphNode outputNode;
    outputNode.nodeId = syntheticOutputId;
    outputNode.kind = RenderGraphNodeKind::Output;
    snapshot.nodes.push_back(std::move(outputNode));
    snapshot.links.push_back(RenderGraphLink{
        input->fromNodeId,
        input->fromSocketId,
        syntheticOutputId,
        EditorNodeGraph::kImageInputSocketId
    });
    snapshot.outputNodeId = syntheticOutputId;
    snapshot.outputSocketId = EditorNodeGraph::kImageInputSocketId;

    RenderPipeline probePipeline;
    probePipeline.Initialize();
    probePipeline.LoadSourceFromPixels(sourcePixels.data(), sourceW, sourceH, std::max(1, sourceCh));
    probePipeline.ExecuteGraph(snapshot);
    outStats = probePipeline.GetOutputTextureStats();
    return outStats.valid;
}

int EditorModule::ResolveFocusedToneCurveNodeId() const {
    auto isToneCurveNodeId = [&](int nodeId) {
        const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
        if (!node) {
            return false;
        }
        if (node->kind == EditorNodeGraph::NodeKind::Layer &&
            node->layerType == LayerType::ToneCurve &&
            node->layerIndex >= 0 &&
            node->layerIndex < static_cast<int>(m_Layers.size())) {
            return true;
        }
        return node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
            node->rawDevelop.integratedToneEnabled;
    };

    if (m_CanvasToolKind == CanvasToolKind::ToneCurveTarget &&
        isToneCurveNodeId(m_CanvasToolOwnerNodeId)) {
        return m_CanvasToolOwnerNodeId;
    }
    if (isToneCurveNodeId(m_ActiveComplexNodeId)) {
        return m_ActiveComplexNodeId;
    }
    const int selectedNodeId = m_NodeGraph.GetSelectedNodeId();
    if (isToneCurveNodeId(selectedNodeId)) {
        return selectedNodeId;
    }
    return -1;
}

bool EditorModule::HasFocusedToneCurveViewportInteraction() const {
    const int toneCurveNodeId = ResolveFocusedToneCurveNodeId();
    if (toneCurveNodeId <= 0) {
        return false;
    }

    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(toneCurveNodeId);
    if (!node) {
        return false;
    }

    if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        return IsCanvasToolActiveForNode(toneCurveNodeId, CanvasToolKind::ToneCurveTarget);
    }

    return true;
}

void EditorModule::ClearTrackedToneCurveProbe() {
    if (m_LastToneCurveProbeNodeId <= 0) {
        m_LastToneCurveProbeNodeId = -1;
        return;
    }
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(m_LastToneCurveProbeNodeId);
    if (node &&
        node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(m_Layers.size())) {
        if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(m_Layers[static_cast<std::size_t>(node->layerIndex)].get())) {
            toneCurve->ClearViewportProbe();
            toneCurve->EndViewportTargetDrag();
        }
    } else if (node &&
        node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        ClearIntegratedToneTransientState(m_LastToneCurveProbeNodeId);
    }
    m_LastToneCurveProbeNodeId = -1;
}

void EditorModule::ClearToneCurveViewportProbe() {
    const int toneCurveNodeId = ResolveFocusedToneCurveNodeId();
    if (toneCurveNodeId <= 0) {
        ClearTrackedToneCurveProbe();
        return;
    }

    if (m_LastToneCurveProbeNodeId > 0 && m_LastToneCurveProbeNodeId != toneCurveNodeId) {
        ClearTrackedToneCurveProbe();
    }

    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(toneCurveNodeId);
    if (!node) {
        m_LastToneCurveProbeNodeId = -1;
        return;
    }

    if (node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(m_Layers.size())) {
        if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(m_Layers[static_cast<std::size_t>(node->layerIndex)].get())) {
            toneCurve->ClearViewportProbe();
            toneCurve->EndViewportTargetDrag();
            m_LastToneCurveProbeNodeId = toneCurveNodeId;
        }
        return;
    }

    if (node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        ClearIntegratedToneTransientState(toneCurveNodeId);
        m_LastToneCurveProbeNodeId = toneCurveNodeId;
        return;
    }

    m_LastToneCurveProbeNodeId = -1;
}

bool EditorModule::SampleToneCurveViewportPixel(
    int toneCurveNodeId,
    ToneCurveSamplingBasis basis,
    float u,
    float v,
    std::array<float, 4>& outRgba) const {
    outRgba = { 0.0f, 0.0f, 0.0f, 0.0f };

    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(toneCurveNodeId);
    if (!node) {
        return false;
    }

    if (basis == ToneCurveSamplingBasis::FinalPreview) {
        if (HasViewportOutputTiles()) {
            return SampleViewportTileSet(m_ViewportOutputTiles, u, v, outRgba);
        }
        return m_Pipeline.SampleOutputPixel(u, v, outRgba);
    }

    if (!CanRefreshPreviewLikeNodes()) {
        return false;
    }

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;
    RenderGraphSnapshot snapshot = BuildGraphSnapshot();
    const int syntheticOutputId = -350000 - toneCurveNodeId;
    RenderGraphNode outputNode;
    outputNode.nodeId = syntheticOutputId;
    outputNode.kind = RenderGraphNodeKind::Output;
    snapshot.nodes.push_back(std::move(outputNode));

    if (node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve) {
        const EditorNodeGraph::Link* input = m_NodeGraph.FindInputLink(toneCurveNodeId, EditorNodeGraph::kImageInputSocketId);
        if (!input) {
            return false;
        }

        if (!TryResolveReferenceSourcePixels(input->fromNodeId, input->fromSocketId, sourcePixels, sourceW, sourceH, sourceCh)) {
            if (TryResolveReferenceSourceDimensions(input->fromNodeId, input->fromSocketId, sourceW, sourceH) &&
                sourceW > 0 &&
                sourceH > 0) {
                sourceCh = 4;
                sourcePixels = BuildTransparentPixels(sourceW, sourceH);
            } else {
                sourcePixels = m_Pipeline.GetSourcePixelsRaw();
                sourceW = m_Pipeline.GetCanvasWidth();
                sourceH = m_Pipeline.GetCanvasHeight();
                sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
            }
        }
        snapshot.links.push_back(RenderGraphLink{
            input->fromNodeId,
            input->fromSocketId,
            syntheticOutputId,
            EditorNodeGraph::kImageInputSocketId
        });
    } else if (node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        if (!TryResolveReferenceSourcePixels(toneCurveNodeId, EditorNodeGraph::kImageOutputSocketId, sourcePixels, sourceW, sourceH, sourceCh)) {
            sourcePixels = m_Pipeline.GetSourcePixelsRaw();
            sourceW = m_Pipeline.GetCanvasWidth();
            sourceH = m_Pipeline.GetCanvasHeight();
            sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
        }

        bool foundRawDevelopNode = false;
        for (RenderGraphNode& renderNode : snapshot.nodes) {
            if (renderNode.nodeId == toneCurveNodeId && renderNode.kind == RenderGraphNodeKind::RawDevelop) {
                renderNode.rawDevelop.integratedToneEnabled = false;
                foundRawDevelopNode = true;
                break;
            }
        }
        if (!foundRawDevelopNode) {
            return false;
        }

        snapshot.links.push_back(RenderGraphLink{
            toneCurveNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            syntheticOutputId,
            EditorNodeGraph::kImageInputSocketId
        });
    } else {
        return false;
    }

    if (sourcePixels.empty() || sourceW <= 0 || sourceH <= 0) {
        return false;
    }

    snapshot.outputNodeId = syntheticOutputId;
    snapshot.outputSocketId = EditorNodeGraph::kImageInputSocketId;

    RenderPipeline probePipeline;
    probePipeline.Initialize();
    probePipeline.LoadSourceFromPixels(sourcePixels.data(), sourceW, sourceH, std::max(1, sourceCh));
    probePipeline.ExecuteGraph(snapshot);
    return probePipeline.SampleOutputPixel(u, v, outRgba);
}

void EditorModule::UpdateToneCurveViewportProbe(float u, float v) {
    if (m_CanvasToolKind == CanvasToolKind::PickColor || m_CanvasToolKind == CanvasToolKind::AdjustAberrationCenter) {
        ClearTrackedToneCurveProbe();
        return;
    }

    const int toneCurveNodeId = ResolveFocusedToneCurveNodeId();
    if (toneCurveNodeId <= 0) {
        ClearTrackedToneCurveProbe();
        return;
    }

    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(toneCurveNodeId);
    if (!node) {
        ClearTrackedToneCurveProbe();
        return;
    }

    ToneCurveLayer integratedTone;
    ToneCurveLayer* toneCurve = nullptr;
    if (node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(m_Layers.size())) {
        toneCurve = dynamic_cast<ToneCurveLayer*>(m_Layers[static_cast<std::size_t>(node->layerIndex)].get());
    } else if (node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        if (node->rawDevelop.integratedToneLayerJson.is_object()) {
            integratedTone.Deserialize(node->rawDevelop.integratedToneLayerJson);
        }
        RestoreIntegratedToneTransientState(toneCurveNodeId, integratedTone);
        toneCurve = &integratedTone;
    }
    if (!toneCurve) {
        ClearTrackedToneCurveProbe();
        return;
    }

    if (m_LastToneCurveProbeNodeId > 0 && m_LastToneCurveProbeNodeId != toneCurveNodeId) {
        ClearTrackedToneCurveProbe();
    }

    std::array<float, 4> rgba {};
    ToneCurveSamplingBasis sampledBasis = toneCurve->GetSamplingBasis();
    bool sampled = SampleToneCurveViewportPixel(toneCurveNodeId, sampledBasis, u, v, rgba);
    if (!sampled && sampledBasis == ToneCurveSamplingBasis::CurveInput) {
        sampledBasis = ToneCurveSamplingBasis::FinalPreview;
        sampled = SampleToneCurveViewportPixel(toneCurveNodeId, sampledBasis, u, v, rgba);
    }
    if (!sampled) {
        toneCurve->ClearViewportProbe();
        if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
            StoreIntegratedToneTransientState(toneCurveNodeId, *toneCurve);
        }
        m_LastToneCurveProbeNodeId = toneCurveNodeId;
        return;
    }

    toneCurve->UpdateViewportProbe(sampledBasis, u, v, rgba);
    if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        StoreIntegratedToneTransientState(toneCurveNodeId, *toneCurve);
    }
    m_LastToneCurveProbeNodeId = toneCurveNodeId;
}

void EditorModule::BeginToneCurveViewportTargetDrag(float u, float v) {
    const int toneCurveNodeId = m_CanvasToolKind == CanvasToolKind::ToneCurveTarget
        ? m_CanvasToolOwnerNodeId
        : ResolveFocusedToneCurveNodeId();
    if (toneCurveNodeId <= 0) {
        return;
    }

    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(toneCurveNodeId);
    if (!node) {
        return;
    }

    ToneCurveLayer integratedTone;
    ToneCurveLayer* toneCurve = nullptr;
    if (node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(m_Layers.size())) {
        toneCurve = dynamic_cast<ToneCurveLayer*>(m_Layers[static_cast<std::size_t>(node->layerIndex)].get());
    } else if (node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        if (node->rawDevelop.integratedToneLayerJson.is_object()) {
            integratedTone.Deserialize(node->rawDevelop.integratedToneLayerJson);
        }
        RestoreIntegratedToneTransientState(toneCurveNodeId, integratedTone);
        toneCurve = &integratedTone;
    }
    if (!toneCurve) {
        return;
    }

    std::array<float, 4> rgba {};
    ToneCurveSamplingBasis sampledBasis = toneCurve->GetSamplingBasis();
    bool sampled = SampleToneCurveViewportPixel(toneCurveNodeId, sampledBasis, u, v, rgba);
    if (!sampled && sampledBasis == ToneCurveSamplingBasis::CurveInput) {
        sampledBasis = ToneCurveSamplingBasis::FinalPreview;
        sampled = SampleToneCurveViewportPixel(toneCurveNodeId, sampledBasis, u, v, rgba);
    }
    if (!sampled) {
        return;
    }

    if (m_LastToneCurveProbeNodeId > 0 && m_LastToneCurveProbeNodeId != toneCurveNodeId) {
        ClearTrackedToneCurveProbe();
    }
    if (toneCurve->BeginViewportTargetDrag(sampledBasis, u, v, rgba)) {
        if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
            node->rawDevelop.integratedToneLayerJson = toneCurve->Serialize();
            StoreIntegratedToneTransientState(toneCurveNodeId, *toneCurve);
        }
        m_LastToneCurveProbeNodeId = toneCurveNodeId;
        MarkRenderDirty(toneCurveNodeId);
    }
}

void EditorModule::UpdateToneCurveViewportTargetDrag(float deltaCurveY) {
    if (m_CanvasToolKind != CanvasToolKind::ToneCurveTarget || m_CanvasToolOwnerNodeId <= 0) {
        return;
    }
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(m_CanvasToolOwnerNodeId);
    if (!node) {
        return;
    }

    ToneCurveLayer integratedTone;
    ToneCurveLayer* toneCurve = nullptr;
    if (node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(m_Layers.size())) {
        toneCurve = dynamic_cast<ToneCurveLayer*>(m_Layers[static_cast<std::size_t>(node->layerIndex)].get());
    } else if (node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        if (node->rawDevelop.integratedToneLayerJson.is_object()) {
            integratedTone.Deserialize(node->rawDevelop.integratedToneLayerJson);
        }
        RestoreIntegratedToneTransientState(m_CanvasToolOwnerNodeId, integratedTone);
        toneCurve = &integratedTone;
    }
    if (!toneCurve) {
        return;
    }

    toneCurve->UpdateViewportTargetDrag(deltaCurveY);
    if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        node->rawDevelop.integratedToneLayerJson = toneCurve->Serialize();
        StoreIntegratedToneTransientState(m_CanvasToolOwnerNodeId, *toneCurve);
    }
    MarkRenderDirty(m_CanvasToolOwnerNodeId);
}

void EditorModule::EndToneCurveViewportTargetDrag() {
    const int toneCurveNodeId = m_CanvasToolKind == CanvasToolKind::ToneCurveTarget
        ? m_CanvasToolOwnerNodeId
        : m_LastToneCurveProbeNodeId;
    if (toneCurveNodeId <= 0) {
        return;
    }

    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(toneCurveNodeId);
    if (!node) {
        return;
    }

    ToneCurveLayer integratedTone;
    ToneCurveLayer* toneCurve = nullptr;
    if (node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(m_Layers.size())) {
        toneCurve = dynamic_cast<ToneCurveLayer*>(m_Layers[static_cast<std::size_t>(node->layerIndex)].get());
    } else if (node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        if (node->rawDevelop.integratedToneLayerJson.is_object()) {
            integratedTone.Deserialize(node->rawDevelop.integratedToneLayerJson);
        }
        RestoreIntegratedToneTransientState(toneCurveNodeId, integratedTone);
        toneCurve = &integratedTone;
    }
    if (!toneCurve) {
        return;
    }

    toneCurve->EndViewportTargetDrag();
    if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        StoreIntegratedToneTransientState(toneCurveNodeId, *toneCurve);
    }
}
