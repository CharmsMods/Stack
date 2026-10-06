#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/ScopedGLObjects.h"
#include <stdexcept>

void RenderPipeline::CaptureRawLayerLocalScope(
    const Stack::Renderer::GraphExecution::GraphExecutionContext& context,
    int operationId, unsigned int texture,
    const std::function<unsigned int(int, const std::string&)>& evalMask) {
    using namespace Stack::RawRecipe;
    const auto found = context.nodes.find(operationId);
    if (!texture || found == context.nodes.end() || found->second->kind != RenderGraphNodeKind::RawOperation ||
        found->second->rawOperation.kind != GraphOperationKind::LocalEv) return;
    const auto& node = *found->second;
    auto recipe = ReadGraphOperation(node.rawOperation);
    recipe.technical.workingSpace = node.rawWorkingSpace;
    const int width = m_Width, height = m_Height;
    const auto native = ResolveGraphNativeExtent(context, node.nodeId, "measurementImageOut");
    const int nativeWidth = native.first, nativeHeight = native.second;
    const auto key = Stack::Renderer::GraphExecution::MakeNodeSocketKey(context.graph.rawLayerScopeNodeId, context.graph.rawLayerScopeSocketId);
    const auto fp = context.imageFingerprintCache.find(key);
    const std::size_t fingerprint = fp == context.imageFingerprintCache.end() ? 0 : fp->second;
    CaptureRawDevelopmentLocalRangeGraphScopeReadback(texture, recipe.localRange, width, height,
        node.rawWorkingSpace, nativeWidth, nativeHeight);
    m_RawDevelopmentGraphScopeReadback.graphInputFingerprint = fingerprint;
    if (!recipe.localRange.areas.empty()) {
        const bool external = node.rawOperation.parameters.value("graphCoverage", false);
        for (auto& area : recipe.localRange.areas) {
            const auto* link = context.FindInputLink(node.nodeId, "area:" + area.id);
            std::vector<int> pending;
            if (link) pending.push_back(link->fromNodeId);
            std::unordered_set<int> visited;
            const RenderGraphNode* generator = nullptr;
            bool ambiguous = false;
            while (!pending.empty()) {
                const int id=pending.back(); pending.pop_back();
                if (!visited.insert(id).second) continue;
                const auto found=context.nodes.find(id);
                if (found==context.nodes.end()) continue;
                const auto* candidate=found->second;
                if (candidate->kind==RenderGraphNodeKind::MaskGenerator &&
                    candidate->maskKind==RenderMaskGeneratorKind::PaintedArea &&
                    candidate->rawCoverage.value("id",std::string{})==area.id) {
                    if (generator && generator!=candidate) ambiguous=true;
                    generator=candidate;
                }
                for (const auto& upstream : context.graph.links)
                    if (upstream.toNodeId==id && upstream.toSocketId!="matchExtent") pending.push_back(upstream.fromNodeId);
            }
            if (generator && !ambiguous) {
                const auto coverage = DeserializeZoneAreas(nlohmann::json::array({generator->rawCoverage}));
                if (!coverage.empty()) { area.strokes = coverage.front().strokes; area.sourceAspect = coverage.front().sourceAspect; }
            }
        }
        const auto coverage = [&](const std::string& id) {
            const auto* link = context.FindInputLink(node.nodeId, "area:" + id);
            const auto mask = link ? evalMask(link->fromNodeId, link->fromSocketId) : 0;
            m_Width = width; m_Height = height;
            return mask;
        };
        std::vector<RawZoneAreaStatistics> statistics;
        Stack::Renderer::ScopedGLTexture measured(m_RawZoneAreaRenderer.Render(texture, texture, width, height,
            recipe, true, m_PreviewMaxDimension <= 0, statistics, m_ShouldCancelRender, fingerprint,
            external ? std::function<unsigned int(const std::string&)>(coverage) : std::function<unsigned int(const std::string&)>{}));
        if (!measured) throw std::runtime_error("Local EV area measurement is unavailable.");
        m_RawDevelopmentGraphScopeReadback.zoneAreas = std::move(statistics);
        m_RawDevelopmentGraphScopeReadback.zoneGuide = m_RawZoneAreaRenderer.PreviewGuide();
        m_RawDevelopmentGraphScopeReadback.zoneReferenceExposureEv = 0;
    }
    if (!m_RawDevelopmentLocalRangeOverlayRequestMode.empty() && m_RawDevelopmentLocalRangeOverlayRequestMode != "none") {
        Stack::Renderer::ScopedGLTexture overlay(RenderRawDevelopmentLocalRangeOverlay(texture, recipe.localRange,
            node.rawWorkingSpace, recipe.technical.processingVersion, m_RawDevelopmentLocalRangeOverlayRequestMode,
            fingerprint, nativeWidth, nativeHeight));
        ClearRawDevelopmentLocalRangeOverlay();
        if (overlay) {
            m_RawDevelopmentLocalRangeOverlayTexture = overlay.Release();
            m_RawDevelopmentLocalRangeOverlayWidth = width; m_RawDevelopmentLocalRangeOverlayHeight = height;
            m_RawDevelopmentLocalRangeOverlayMode = m_RawDevelopmentLocalRangeOverlayRequestMode;
        }
    }
}
