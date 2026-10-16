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

void GraphExecutionRuntime::BindScalarEvaluation() {
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
                "Uniform value dependency contains a cycle.";
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
        if (const auto unavailable=graph.unavailableOutputs.find(nodeId);unavailable!=graph.unavailableOutputs.end())
            return finishFailure(unavailable->second);
        const RenderGraphNode& node = *nodeIt->second;
        if (node.kind == RenderGraphNodeKind::Value && socketId == EditorNodeGraph::kValueOutputSocketId) {
            if (!std::isfinite(node.scalarValue)) return finishFailure("The connected value is not finite.");
            value = node.scalarValue;
            scalarCache[key] = value;
            visitingScalars.erase(key);
            return true;
        }
        
        if (node.kind == RenderGraphNodeKind::DataMath &&
            node.dataMathSettings.domain == Stack::NodeMath::ArithmeticDomain::Values &&
            socketId == EditorNodeGraph::kImageOutputSocketId) {
            std::vector<double> inputs;
            const bool average = node.dataMathMode == RenderDataMathMode::Average;
            const bool unary = node.dataMathMode == RenderDataMathMode::Clamp || node.dataMathMode == RenderDataMathMode::Remap;
            const int count = average ? EditorNodeGraph::kMaxDataMathInputCount : (unary ? 1 : 2);
            for (int i = 0; i < count; ++i) {
                const auto* link = findInputLink(nodeId, EditorNodeGraph::DataMathInputSocketId(i));
                double operand = i == 0 ? node.dataMathSettings.constantA : node.dataMathSettings.constantB;
                if (link && !evalScalar(link->fromNodeId, link->fromSocketId, operand))
                    return finishFailure(pipeline.m_LastGraphExecutionStats.lastReductionFailure);
                if (!average || link) inputs.push_back(operand);
            }
            std::string error;
            if (!Stack::NodeMath::EvaluateUniformArithmetic(node.dataMathMode, node.dataMathSettings, inputs, value, error))
                return finishFailure(std::move(error));
            scalarCache[key] = value;
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
