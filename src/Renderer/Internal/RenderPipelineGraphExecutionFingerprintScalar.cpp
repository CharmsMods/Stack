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
        if (node.kind == RenderGraphNodeKind::DataMath &&
            node.dataMathSettings.domain == Stack::NodeMath::ArithmeticDomain::Values &&
            socketId == EditorNodeGraph::kImageOutputSocketId) {
            std::size_t fingerprint = HashValue(node.definitionHash);
            HashCombine(fingerprint, HashValue(static_cast<int>(node.dataMathMode)));
            const auto& settings = node.dataMathSettings;
            for (const auto value : {settings.constantA, settings.constantB, settings.minValue,
                    settings.maxValue, settings.outMin, settings.outMax}) HashCombine(fingerprint, HashValue(value));
            const bool average = node.dataMathMode == RenderDataMathMode::Average;
            const bool unary = node.dataMathMode == RenderDataMathMode::Clamp || node.dataMathMode == RenderDataMathMode::Remap;
            const int count = average ? EditorNodeGraph::kMaxDataMathInputCount : (unary ? 1 : 2);
            for (int i = 0; i < count; ++i) {
                const auto* input = findInputLink(nodeId, EditorNodeGraph::DataMathInputSocketId(i));
                HashCombine(fingerprint, HashValue(i));
                HashCombine(fingerprint, input ? fingerprintScalar(input->fromNodeId, input->fromSocketId) : 0);
            }
            scalarFingerprintCache[key] = fingerprint;
            fingerprintingScalars.erase(key);
            return fingerprint;
        }
        const bool fieldMeanOutput =
            node.kind == RenderGraphNodeKind::FieldMean &&
            socketId == EditorNodeGraph::kValueOutputSocketId;
        if (!fieldMeanOutput) {
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
        }
        fingerprintingScalars.erase(key);
        scalarFingerprintCache[key] = fingerprint;
        return fingerprint;
    };

}

} // namespace Stack::Renderer::GraphExecution
