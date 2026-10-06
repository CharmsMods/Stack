#pragma once

#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/Internal/RenderPipelineGraphSchedule.h"

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

    static void CanonicalizeFrequencyResponseSettings(
        RenderFrequencyResponseSettings& response) {
        const auto finiteOr = [](float value, float fallback) {
            return std::isfinite(value) ? value : fallback;
        };
        response.lowCutoff = std::clamp(
            finiteOr(response.lowCutoff, 0.08f), 0.0f, 0.5f);
        response.highCutoff = std::clamp(
            finiteOr(response.highCutoff, 0.25f), 0.0f, 0.5f);
        if (response.lowCutoff > response.highCutoff) {
            std::swap(response.lowCutoff, response.highCutoff);
        }
        response.transitionWidth = std::clamp(
            finiteOr(response.transitionWidth, 0.025f),
            0.000001f,
            0.5f);
        response.butterworthOrder = std::clamp(
            finiteOr(response.butterworthOrder, 2.0f),
            1.0f,
            12.0f);
        if (response.notches.size() > 16u) {
            response.notches.resize(16u);
        }
        for (RenderFrequencyNotch& notch : response.notches) {
            notch.frequency = std::clamp(
                finiteOr(notch.frequency, 0.25f), 0.0f, 0.5f);
            notch.directionDegrees = std::clamp(
                finiteOr(notch.directionDegrees, 0.0f),
                -180.0f,
                180.0f);
            notch.width = std::clamp(
                finiteOr(notch.width, 0.025f),
                0.001f,
                0.25f);
        }
    }

    void BindMaskFingerprint();
    void BindScalarFingerprint();
    void BindImageFingerprint();
    void BindFrequencyFingerprints();
    void BindFrequencyEvaluation();
    void BindMaskEvaluation();
    void BindSpectrumAndScalarEvaluation();
    void BindImageEvaluation();

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
    std::unordered_map<std::string, RenderFrequencyResource> frequencyCache;
    std::unordered_map<int, RenderFrequencyResponseSettings> responseCache;
    std::unordered_map<std::string, std::size_t> frequencyFingerprintCache;
    std::unordered_map<int, std::size_t> responseFingerprintCache;
    std::unordered_set<std::string> visitingFrequency;
    std::unordered_set<std::string> fingerprintingFrequency;
    std::set<int> visitingResponses;
    std::set<int> fingerprintingResponses;
    std::unordered_map<int, RenderSpectrumAnalysis> spectrumAnalysisCache;
    std::function<unsigned int(int, const std::string&)> evalMask;
    std::function<unsigned int(int, const std::string&)> evalImage;
    std::function<bool(int, const std::string&, double&)> evalScalar;
    std::function<std::size_t(int, const std::string&)> fingerprintMask;
    std::function<std::size_t(int, const std::string&)> fingerprintImage;
    std::function<std::size_t(int, const std::string&)> fingerprintScalar;
    std::function<std::size_t(int, const std::string&)> fingerprintFrequency;
    std::function<std::size_t(int)> fingerprintResponse;
    std::function<RenderFrequencyResource(int, const std::string&)> evalFrequency;
    std::function<bool(int, RenderFrequencyResponseSettings&)> evalResponse;
    std::function<bool(int, RenderSpectrumAnalysis&)> evalSpectrumAnalysis;
    std::function<std::string(int, const std::string&)> resolveChannelRole;
    std::unordered_set<int> pointwiseFusionDisabledNodes;
};

} // namespace Stack::Renderer::GraphExecution
