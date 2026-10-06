#include "Renderer/Internal/RenderPipelineGraphExecutionRuntime.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "NodeMath/ReductionMath.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Utils/PixelBufferUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Stack::Renderer::GraphExecution {

void GraphExecutionRuntime::BindSpectrumAndScalarEvaluation() {
    evalSpectrumAnalysis = [this](int nodeId, RenderSpectrumAnalysis& analysis) -> bool {
        if (const auto local = spectrumAnalysisCache.find(nodeId);
            local != spectrumAnalysisCache.end()) {
            analysis = local->second;
            return analysis.valid;
        }
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || nodeIt->second == nullptr ||
            nodeIt->second->kind != RenderGraphNodeKind::SpectrumAnalyzer) return false;
        const RenderGraphNode& node = *nodeIt->second;
        const RenderGraphLink* spectrumLink = findInputLink(
            nodeId, EditorNodeGraph::kSpectrumInputSocketId);
        if (spectrumLink == nullptr) return false;

        RenderSpectrumAnalyzerSettings settings = node.spectrumAnalyzerSettings;
        const auto applyBandInput = [&](const char* parameterId, float& destination) {
            const RenderGraphLink* input = findInputLink(
                nodeId, EditorNodeGraph::ParameterInputSocketId(parameterId));
            if (input == nullptr) return true;
            double value = destination;
            if (!evalScalar(input->fromNodeId, input->fromSocketId, value)) return false;
            destination = static_cast<float>(value);
            return true;
        };
        if (!applyBandInput(EditorNodeGraph::kAnalyzerLowParameterId, settings.innerRadius) ||
            !applyBandInput(EditorNodeGraph::kAnalyzerHighParameterId, settings.outerRadius)) {
            return false;
        }
        if (!std::isfinite(settings.innerRadius)) settings.innerRadius = 0.0f;
        if (!std::isfinite(settings.outerRadius)) settings.outerRadius = 0.5f;
        settings.innerRadius = std::clamp(settings.innerRadius, 0.0f, 0.70710678f);
        settings.outerRadius = std::clamp(settings.outerRadius, 0.0f, 0.70710678f);
        if (settings.innerRadius > settings.outerRadius)
            std::swap(settings.innerRadius, settings.outerRadius);

        std::size_t fingerprint = HashValue(node.definitionId);
        HashCombine(fingerprint, HashValue(node.definitionVersion));
        HashCombine(fingerprint, HashValue(node.definitionHash));
        HashCombine(fingerprint, fingerprintFrequency(
            spectrumLink->fromNodeId, spectrumLink->fromSocketId));
        HashCombine(fingerprint, HashValue(settings.innerRadius));
        HashCombine(fingerprint, HashValue(settings.outerRadius));
        HashCombine(fingerprint, HashValue(settings.excludeDc));
        const std::string persistentKey =
            MakeNodeSocketKey(nodeId, EditorNodeGraph::kRadialPowerOutputSocketId);
        if (const auto persistent = pipeline.m_GraphFrequencyAnalysisCache.find(persistentKey);
            persistent != pipeline.m_GraphFrequencyAnalysisCache.end() &&
            persistent->second.fingerprint == fingerprint) {
            analysis = persistent->second;
            spectrumAnalysisCache[nodeId] = analysis;
            return analysis.valid;
        }

        const RenderFrequencyResource spectrum = evalFrequency(
            spectrumLink->fromNodeId, spectrumLink->fromSocketId);
        analysis = pipeline.AnalyzeSpectrum(spectrum, settings, fingerprint);
        spectrumAnalysisCache[nodeId] = analysis;
        if (analysis.valid) {
            pipeline.m_GraphFrequencyAnalysisCache[persistentKey] = analysis;
        } else {
            pipeline.m_GraphFrequencyAnalysisCache.erase(persistentKey);
            pipeline.m_LastGraphExecutionStats.lastSpecializedFailureNodeId = nodeId;
            pipeline.m_LastGraphExecutionStats.lastSpecializedFailure = analysis.error;
        }
        return analysis.valid;
    };

    evalScalar = [&](int nodeId, const std::string& socketId, double& value) -> bool {
        const std::string key = MakeNodeSocketKey(nodeId, socketId);
        if (const auto local = scalarCache.find(key); local != scalarCache.end()) {
            value = local->second;
            ++pipeline.m_LastGraphExecutionStats.reductionCacheHits;
            return true;
        }
        if (!visitingScalars.insert(key).second) {
            pipeline.m_LastGraphExecutionStats.lastReductionFailureNodeId = nodeId;
            pipeline.m_LastGraphExecutionStats.lastReductionFailure =
                "Reduction dependency contains a cycle.";
            return false;
        }
        const auto finishFailure = [&](std::string message) {
            pipeline.m_LastGraphExecutionStats.lastReductionFailureNodeId = nodeId;
            pipeline.m_LastGraphExecutionStats.lastReductionFailure = std::move(message);
            visitingScalars.erase(key);
            return false;
        };
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || nodeIt->second == nullptr)
            return finishFailure("The connected uniform value has no executable source.");
        const RenderGraphNode& node = *nodeIt->second;
        if (node.kind == RenderGraphNodeKind::Value && socketId == EditorNodeGraph::kValueOutputSocketId) {
            if (!std::isfinite(node.scalarValue)) return finishFailure("The connected value is not finite.");
            value = node.scalarValue;
            scalarCache[key] = value;
            visitingScalars.erase(key);
            return true;
        }
        if (node.kind == RenderGraphNodeKind::SpectrumAnalyzer) {
            if (socketId != EditorNodeGraph::kBandPowerOutputSocketId &&
                socketId != EditorNodeGraph::kPeakFrequencyOutputSocketId &&
                socketId != EditorNodeGraph::kPeakDirectionOutputSocketId) {
                return finishFailure("Spectrum Analyzer's radial curve is Data, not a scalar Value.");
            }
            RenderSpectrumAnalysis analysis;
            if (!evalSpectrumAnalysis(nodeId, analysis)) {
                return finishFailure(analysis.error.empty()
                    ? "Spectrum Analyzer could not evaluate its spectrum." : analysis.error);
            }
            if (socketId == EditorNodeGraph::kBandPowerOutputSocketId)
                value = analysis.bandPower;
            else if (socketId == EditorNodeGraph::kPeakFrequencyOutputSocketId)
                value = analysis.peakFrequency;
            else
                value = analysis.peakDirectionDegrees;
            scalarCache[key] = value;
            RenderPipeline::CachedGraphScalar cached;
            cached.fingerprint = fingerprintScalar(nodeId, socketId);
            cached.value = value;
            cached.lastUseSerial = ++pipeline.m_GraphResourceUseSerial;
            pipeline.m_GraphScalarCache[key] = cached;
            visitingScalars.erase(key);
            return true;
        }
        if (node.kind != RenderGraphNodeKind::FieldMean ||
            socketId != EditorNodeGraph::kValueOutputSocketId) {
            return finishFailure(
                "The connected uniform value is not an executable reduction output.");
        }
        const std::size_t fingerprint = fingerprintScalar(nodeId, socketId);
        if (const auto persistent = pipeline.m_GraphScalarCache.find(key);
            persistent != pipeline.m_GraphScalarCache.end() &&
            persistent->second.fingerprint == fingerprint) {
            persistent->second.lastUseSerial = ++pipeline.m_GraphResourceUseSerial;
            value = persistent->second.value;
            scalarCache[key] = value;
            scalarSampleCounts[key] = static_cast<std::size_t>(persistent->second.sampleCount);
            ++pipeline.m_LastGraphExecutionStats.reductionCacheHits;
            pipeline.m_LastGraphExecutionStats.reductions.push_back({
                nodeId, value, persistent->second.sampleCount, true, node.definitionId });
            visitingScalars.erase(key);
            return true;
        }
        ++pipeline.m_LastGraphExecutionStats.reductionCacheMisses;
        const RenderGraphLink* input = findInputLink(
            nodeId, EditorNodeGraph::kReductionFieldInputSocketId);
        if (input == nullptr) {
            pipeline.m_GraphScalarCache.erase(key);
            return finishFailure("Field Mean requires a connected scalar field.");
        }
        const unsigned int texture = evalMask(input->fromNodeId, input->fromSocketId);
        if (texture == 0 || pipeline.m_Width <= 0 || pipeline.m_Height <= 0) {
            pipeline.m_GraphScalarCache.erase(key);
            return finishFailure("Field Mean could not materialize its scalar-field input.");
        }

        constexpr int kReadbackRows = 64;
        const int maximumRows = std::min(kReadbackRows, pipeline.m_Height);
        std::size_t sampleCapacity = 0;
        if (!Stack::PixelBuffer::TryComputePixelElementCount(
                pipeline.m_Width, maximumRows, 1, sampleCapacity)) {
            return finishFailure(
                "Field Mean dimensions exceed CPU readback limits.");
        }
        std::vector<float> samples;
        try {
            samples.resize(sampleCapacity);
        } catch (const std::bad_alloc&) {
            return finishFailure(
                "Field Mean could not allocate its CPU readback.");
        } catch (const std::length_error&) {
            return finishFailure(
                "Field Mean dimensions exceed CPU readback limits.");
        }

        const ScopedFramebufferState savedState(true);
        const Stack::Renderer::GLState::PixelPackState savedPackState;
        savedPackState.ConfigureTightCpuReadback();
        const unsigned int fbo = GLHelpers::CreateFBO(texture);
        if (fbo == 0) {
            savedPackState.Restore();
            savedState.Restore(true);
            return finishFailure("Field Mean could not create a readback target.");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        while (glGetError() != GL_NO_ERROR) {}
        Stack::NodeMath::FieldMeanAccumulator accumulator;
        bool readbackOk = true;
        for (int y = 0; y < pipeline.m_Height && readbackOk; y += kReadbackRows) {
            const int rows = std::min(kReadbackRows, pipeline.m_Height - y);
            glReadPixels(0, y, pipeline.m_Width, rows, GL_RED, GL_FLOAT, samples.data());
            if (glGetError() != GL_NO_ERROR) {
                readbackOk = false;
                break;
            }
            readbackOk = accumulator.Add(
                samples.data(),
                static_cast<std::size_t>(pipeline.m_Width) * static_cast<std::size_t>(rows));
        }
        savedPackState.Restore();
        savedState.Restore(true);
        glDeleteFramebuffers(1, &fbo);
        if (!readbackOk) {
            pipeline.m_GraphScalarCache.erase(key);
            const Stack::NodeMath::FieldMeanResult failed = accumulator.Finish();
            return finishFailure(failed.error.empty()
                ? "Field Mean texture readback failed." : failed.error);
        }
        const Stack::NodeMath::FieldMeanResult mean = accumulator.Finish();
        if (!mean.valid) {
            pipeline.m_GraphScalarCache.erase(key);
            return finishFailure(mean.error);
        }

        value = mean.value;
        scalarCache[key] = value;
        scalarSampleCounts[key] = mean.sampleCount;
        RenderPipeline::CachedGraphScalar cached;
        cached.fingerprint = fingerprint;
        cached.value = mean.value;
        cached.sampleCount = static_cast<std::uint64_t>(mean.sampleCount);
        cached.lastUseSerial = ++pipeline.m_GraphResourceUseSerial;
        pipeline.m_GraphScalarCache[key] = cached;
        ++pipeline.m_LastGraphExecutionStats.reductionPasses;
        pipeline.m_LastGraphExecutionStats.reductions.push_back({
            nodeId, value, cached.sampleCount, false, node.definitionId });
        visitingScalars.erase(key);
        return true;
    };

}

} // namespace Stack::Renderer::GraphExecution
