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

void GraphExecutionRuntime::BindScalarFingerprint() {
    fingerprintScalar = [this](int nodeId, const std::string& socketId) -> std::size_t {
        const std::string key = MakeNodeSocketKey(nodeId, socketId);
        if (const auto cached = scalarFingerprintCache.find(key);
            cached != scalarFingerprintCache.end()) {
            return cached->second;
        }
        if (!fingerprintingScalars.insert(key).second) return 0;
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || nodeIt->second == nullptr) {
            fingerprintingScalars.erase(key);
            return 0;
        }
        const RenderGraphNode& node = *nodeIt->second;
        if (node.kind == RenderGraphNodeKind::Value && socketId == EditorNodeGraph::kValueOutputSocketId) {
            auto fingerprint = HashValue(node.scalarValue);
            HashCombine(fingerprint,HashValue(node.definitionId));
            HashCombine(fingerprint,HashValue(node.definitionVersion));
            HashCombine(fingerprint,HashValue(node.definitionHash));
            fingerprintingScalars.erase(key);
            scalarFingerprintCache[key] = fingerprint;
            return fingerprint;
        }
        const bool fieldMeanOutput =
            node.kind == RenderGraphNodeKind::FieldMean &&
            socketId == EditorNodeGraph::kValueOutputSocketId;
        const bool analyzerOutput =
            node.kind == RenderGraphNodeKind::SpectrumAnalyzer &&
            (socketId == EditorNodeGraph::kBandPowerOutputSocketId ||
             socketId == EditorNodeGraph::kPeakFrequencyOutputSocketId ||
             socketId == EditorNodeGraph::kPeakDirectionOutputSocketId);
        if (!fieldMeanOutput && !analyzerOutput) {
            fingerprintingScalars.erase(key);
            return 0;
        }
        std::size_t fingerprint = HashValue(static_cast<int>(node.kind));
        HashCombine(fingerprint, HashValue(node.nodeId));
        HashCombine(fingerprint, HashValue(node.definitionId));
        HashCombine(fingerprint, HashValue(node.definitionVersion));
        HashCombine(fingerprint, HashValue(node.definitionHash));
        HashCombine(fingerprint, HashValue(socketId));
        if (fieldMeanOutput) {
            HashCombine(fingerprint, HashValue(Stack::NodeMath::kFieldMeanAlgorithmVersion));
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
            const RenderGraphLink* input = findInputLink(
                node.nodeId, EditorNodeGraph::kReductionFieldInputSocketId);
            HashCombine(fingerprint, input
                ? fingerprintMask(input->fromNodeId, input->fromSocketId)
                : 0);
        } else {
            const RenderGraphLink* input = findInputLink(
                node.nodeId, EditorNodeGraph::kSpectrumInputSocketId);
            HashCombine(fingerprint, input
                ? fingerprintFrequency(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(node.spectrumAnalyzerSettings.innerRadius));
            HashCombine(fingerprint, HashValue(node.spectrumAnalyzerSettings.outerRadius));
            HashCombine(fingerprint, HashValue(node.spectrumAnalyzerSettings.excludeDc));
            for (const char* parameterId : {
                     EditorNodeGraph::kAnalyzerLowParameterId,
                     EditorNodeGraph::kAnalyzerHighParameterId}) {
                const RenderGraphLink* parameterInput = findInputLink(
                    node.nodeId, EditorNodeGraph::ParameterInputSocketId(parameterId));
                HashCombine(fingerprint, parameterInput
                    ? fingerprintScalar(
                        parameterInput->fromNodeId,
                        parameterInput->fromSocketId)
                    : 0);
            }
        }
        fingerprintingScalars.erase(key);
        scalarFingerprintCache[key] = fingerprint;
        return fingerprint;
    };

}

} // namespace Stack::Renderer::GraphExecution
