#pragma once

#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/Internal/RenderPipelineGraphSchedule.h"
#include "Renderer/RenderTiling.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Stack::Renderer::GraphExecution {

class GraphExecutionRuntime {
public:
    GraphExecutionRuntime(
        RenderPipeline& pipeline,
        const RenderGraphSnapshot& graph,
        const GraphTopologyIndex& topology,
        const GraphEvaluationSchedule& evaluationSchedule,
        bool rawDevelopmentSideEffectsRequested);

    void Execute();
    ~GraphExecutionRuntime();

private:
    const RenderGraphLink* findInputLink(
        int nodeId,
        const std::string& socketId) const {
        return executionContext.FindInputLink(nodeId, socketId);
    }

    unsigned int createTarget() {
        return pipeline.CreateGraphRenderTargetTexture();
    }

    template <typename RenderFn>
    bool renderToTexture(unsigned int& texture, RenderFn&& renderFn) {
        if (pipeline.RenderIntoGraphTargetTexture(
                texture,
                std::forward<RenderFn>(renderFn))) {
            return true;
        }
        if (texture != 0) {
            glDeleteTextures(1, &texture);
            texture = 0;
        }
        return false;
    }

    template <typename RenderFn>
    bool renderPassToTexture(unsigned int& texture, RenderFn&& renderFn) {
        bool passExecuted = false;
        const bool targetRendered = pipeline.RenderIntoGraphTargetTexture(
            texture,
            [&](unsigned int fbo) {
                passExecuted = renderFn(fbo);
            });
        if (targetRendered && passExecuted) {
            return true;
        }
        if (texture != 0) {
            glDeleteTextures(1, &texture);
            texture = 0;
        }
        return false;
    }



    void BindMaskFingerprint();
    void BindScalarFingerprint();
    void BindImageFingerprint();


    void BindMaskEvaluation();
    void BindScalarEvaluation();
    void BindImageEvaluation();
    void PreparePhysicalSchedule();
    bool ApplyUniformInputs(const RenderGraphNode& authored, RenderGraphNode& evaluated);
    std::pair<int,int> ProjectMaskCanvasExtent(const RenderGraphNode& node) const;
    void BindViewportEvaluation();
    unsigned int EvaluateViewportOutput(int nodeId, const std::string& socket,
        const std::function<unsigned int(int, const std::string&)>& evaluate);
    unsigned int ViewportInput(unsigned int texture, const std::string& sourceKey,
        const RenderGraphViewportEndpoint& source, const RenderGraphViewportEndpoint& consumer);
    void RecordViewportGeometry(int nodeId, const std::string& socket, bool renderedPass = false);
    void HashViewportDemand(std::size_t& fingerprint, int nodeId, const std::string& socket) const;
    RenderGraphViewportPlan viewportPlan;
    RenderGraphViewportEndpoint viewportConsumer;
    std::unordered_map<std::string, Stack::Renderer::ViewportPreviewGeometry> viewportGeometry;
    std::vector<unsigned int> viewportTemporaryTextures;
    std::unordered_map<std::string, unsigned int> viewportInputCache;

    RenderPipeline& pipeline;
    const RenderGraphSnapshot& graph;
    const GraphTopologyIndex& topology;
    const GraphEvaluationSchedule& evaluationSchedule;
    bool rawDevelopmentSideEffectsRequested = false;
    GraphExecutionContext executionContext;
    const GraphTopologyIndex::NodeLookup& nodes;
    std::unordered_map<std::string, unsigned int>& imageCache;
    std::unordered_map<std::string, unsigned int>& maskCache;
    std::unordered_map<std::string, std::size_t>& imageFingerprintCache;
    std::unordered_map<std::string, std::size_t>& maskFingerprintCache;
    std::unordered_set<std::string>& visitingImages;
    std::unordered_set<std::string>& visitingMasks;
    std::unordered_set<std::string>& fingerprintingImages;
    std::unordered_set<std::string>& fingerprintingMasks;
    std::unordered_map<std::string, double> scalarCache;
    std::unordered_map<std::string, std::pair<int, int>> localTextureExtents;
    std::unordered_map<std::string, std::size_t> scalarSampleCounts;
    std::unordered_map<std::string, std::size_t> scalarFingerprintCache;
    std::unordered_set<std::string> visitingScalars;
    std::unordered_set<std::string> fingerprintingScalars;
    std::function<unsigned int(int, const std::string&)> evalMask;
    std::function<unsigned int(int, const std::string&)> evalImage;
    std::function<bool(int, const std::string&, double&)> evalScalar;
    std::function<std::size_t(int, const std::string&)> fingerprintMask;
    std::function<std::size_t(int, const std::string&)> fingerprintImage;
    std::function<std::size_t(int, const std::string&)> fingerprintScalar;
    std::function<std::string(int, const std::string&)> resolveChannelRole;
    std::unordered_set<int> pointwiseFusionDisabledNodes;
    std::vector<ScheduledGraphOutput> physicalOutputs;
    std::unordered_map<std::string, RenderPipeline::PointwiseFusionPlan> pointwiseFusionPlans;
};

} // namespace Stack::Renderer::GraphExecution
