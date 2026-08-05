#include "Renderer/RenderPipeline.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "NodeMath/TechnicalImageMath.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/ScopedGLObjects.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <sstream>
#include <unordered_set>

using namespace Stack::Renderer::GraphExecution;

namespace {

using Stack::NodeMath::PointwiseOperation;
using Stack::NodeMath::PointwisePixel;
using Stack::NodeMath::PointwiseSourceLocation;
using Stack::NodeMath::PointwiseValueId;

constexpr std::size_t kPointwiseProgramCacheMaximumEntries = 64;

PointwisePixel Splat(double value) {
    return { value, value, value, value };
}

std::vector<int> SourceNodeIds(const std::vector<Stack::NodeMath::PointwiseDiagnostic>& diagnostics) {
    std::vector<int> result;
    for (const auto& diagnostic : diagnostics) {
        for (const auto& source : diagnostic.sources) {
            if (source.authoredNodeId >= 0 &&
                std::find(result.begin(), result.end(), source.authoredNodeId) == result.end()) {
                result.push_back(source.authoredNodeId);
            }
        }
    }
    return result;
}

std::string FirstDiagnosticMessage(const std::vector<Stack::NodeMath::PointwiseDiagnostic>& diagnostics) {
    return diagnostics.empty() ? std::string() : diagnostics.front().message;
}

} // namespace

RenderPipeline::PointwiseFusionPlan RenderPipeline::BuildPointwiseFusionPlan(
    const GraphExecutionContext& executionContext,
    int nodeId,
    const std::string& socketId,
    const std::unordered_set<int>& disabledNodes) const {
    PointwiseFusionPlan plan;
    if (disabledNodes.find(nodeId) != disabledNodes.end() ||
        IsScalarRenderSocket(executionContext, nodeId, socketId)) {
        return plan;
    }

    Stack::NodeMath::PointwiseProgram program;
    std::unordered_set<int> lowering;

    auto sourceFor = [](const RenderGraphNode& node, std::string inputPort) {
        PointwiseSourceLocation source;
        source.authoredNodeId = node.nodeId;
        source.definitionId = node.definitionId.empty()
            ? "stack:unresolved-render-node" : node.definitionId;
        source.inputPortId = std::move(inputPort);
        source.outputPortId = EditorNodeGraph::kImageOutputSocketId;
        return source;
    };

    std::function<bool(int, const std::string&, PointwiseValueId&)> lowerNode;
    auto appendBoundaryInput = [&](const RenderGraphLink& link, PointwiseValueId& value) -> bool {
        if (plan.inputNodeId >= 0 &&
            (plan.inputNodeId != link.fromNodeId || plan.inputSocketId != link.fromSocketId)) {
            return false;
        }
        plan.inputNodeId = link.fromNodeId;
        plan.inputSocketId = link.fromSocketId;
        const auto sourceNode = executionContext.nodes.find(link.fromNodeId);
        PointwiseSourceLocation source;
        source.authoredNodeId = link.fromNodeId;
        source.definitionId = sourceNode != executionContext.nodes.end() && sourceNode->second
            ? sourceNode->second->definitionId : std::string();
        source.outputPortId = link.fromSocketId;
        value = Stack::NodeMath::AppendPointwiseInput(program, std::move(source));
        return true;
    };

    auto mayInlineUpstream = [&](const RenderGraphLink& link) {
        if (lowering.size() >= 48 ||
            disabledNodes.find(link.fromNodeId) != disabledNodes.end() ||
            executionContext.OutputUseCount(link.fromNodeId, link.fromSocketId) != 1) {
            return false;
        }
        const auto upstream = executionContext.nodes.find(link.fromNodeId);
        return upstream != executionContext.nodes.end() && upstream->second != nullptr &&
            (upstream->second->kind == RenderGraphNodeKind::DataMath ||
             upstream->second->kind == RenderGraphNodeKind::TechnicalImage);
    };

    lowerNode = [&](int currentNodeId, const std::string& currentSocketId, PointwiseValueId& output) -> bool {
        if (!lowering.insert(currentNodeId).second) return false;
        const auto finish = [&](bool success) {
            lowering.erase(currentNodeId);
            return success;
        };
        const auto found = executionContext.nodes.find(currentNodeId);
        if (found == executionContext.nodes.end() || found->second == nullptr ||
            IsScalarRenderSocket(executionContext, currentNodeId, currentSocketId)) {
            return finish(false);
        }
        const RenderGraphNode& node = *found->second;

        const RenderGraphLink* imageLink = nullptr;
        PointwiseOperation operation = PointwiseOperation::Identity;
        std::string operationInputPort = EditorNodeGraph::kImageInputSocketId;
        bool dataMathInputIsA = true;

        if (node.kind == RenderGraphNodeKind::TechnicalImage) {
            imageLink = executionContext.FindInputLink(node.nodeId, EditorNodeGraph::kImageInputSocketId);
            if (imageLink == nullptr) return finish(false);
            if (node.technicalImageOperation == Stack::NodeMath::TechnicalImageOperation::Exposure &&
                executionContext.FindInputLink(
                    node.nodeId, EditorNodeGraph::kExposureValueInputSocketId) != nullptr) {
                return finish(false);
            }
            switch (node.technicalImageOperation) {
                case Stack::NodeMath::TechnicalImageOperation::AssignSrgb:
                case Stack::NodeMath::TechnicalImageOperation::AssignLinearSrgb:
                case Stack::NodeMath::TechnicalImageOperation::AssignLinearDisplayP3:
                    operation = PointwiseOperation::Identity;
                    break;
                case Stack::NodeMath::TechnicalImageOperation::Exposure:
                    operation = PointwiseOperation::ExposureEv;
                    break;
                case Stack::NodeMath::TechnicalImageOperation::Premultiply:
                    operation = PointwiseOperation::Premultiply;
                    break;
                case Stack::NodeMath::TechnicalImageOperation::Unpremultiply:
                    operation = PointwiseOperation::Unpremultiply;
                    break;
                case Stack::NodeMath::TechnicalImageOperation::SrgbDecode:
                case Stack::NodeMath::TechnicalImageOperation::SrgbEncode:
                case Stack::NodeMath::TechnicalImageOperation::LinearSrgbToDisplayP3:
                case Stack::NodeMath::TechnicalImageOperation::LinearDisplayP3ToSrgb:
                    return finish(false);
            }
        } else if (node.kind == RenderGraphNodeKind::DataMath) {
            if (executionContext.FindInputLink(node.nodeId, EditorNodeGraph::kMaskInputSocketId) != nullptr) {
                return finish(false);
            }
            const RenderGraphLink* inputA = executionContext.FindInputLink(node.nodeId, EditorNodeGraph::kMixInputASocketId);
            const RenderGraphLink* inputB = executionContext.FindInputLink(node.nodeId, EditorNodeGraph::kMixInputBSocketId);
            if ((inputA != nullptr && IsScalarRenderSocket(executionContext, inputA->fromNodeId, inputA->fromSocketId)) ||
                (inputB != nullptr && IsScalarRenderSocket(executionContext, inputB->fromNodeId, inputB->fromSocketId))) {
                return finish(false);
            }
            switch (node.dataMathMode) {
                case RenderDataMathMode::Clamp:
                    if (inputA == nullptr || inputB != nullptr) return finish(false);
                    imageLink = inputA;
                    operation = PointwiseOperation::Clamp;
                    break;
                case RenderDataMathMode::Add: operation = PointwiseOperation::Add; break;
                case RenderDataMathMode::Subtract: operation = PointwiseOperation::Subtract; break;
                case RenderDataMathMode::Multiply: operation = PointwiseOperation::Multiply; break;
                case RenderDataMathMode::Min: operation = PointwiseOperation::Minimum; break;
                case RenderDataMathMode::Max: operation = PointwiseOperation::Maximum; break;
                case RenderDataMathMode::Difference: operation = PointwiseOperation::AbsoluteDifference; break;
                case RenderDataMathMode::Divide:
                case RenderDataMathMode::Average:
                case RenderDataMathMode::Remap:
                case RenderDataMathMode::ImageAverage:
                    return finish(false);
            }
            if (node.dataMathMode != RenderDataMathMode::Clamp) {
                if ((inputA == nullptr) == (inputB == nullptr)) return finish(false);
                imageLink = inputA != nullptr ? inputA : inputB;
                dataMathInputIsA = inputA != nullptr;
                operationInputPort = dataMathInputIsA
                    ? EditorNodeGraph::kMixInputASocketId
                    : EditorNodeGraph::kMixInputBSocketId;
            }
        } else {
            return finish(false);
        }

        PointwiseValueId inputValue = -1;
        if (mayInlineUpstream(*imageLink)) {
            if (!lowerNode(imageLink->fromNodeId, imageLink->fromSocketId, inputValue)) {
                if (!appendBoundaryInput(*imageLink, inputValue)) return finish(false);
            }
        } else if (!appendBoundaryInput(*imageLink, inputValue)) {
            return finish(false);
        }

        const PointwiseSourceLocation source = sourceFor(node, operationInputPort);
        if (node.kind == RenderGraphNodeKind::TechnicalImage) {
            if (operation == PointwiseOperation::ExposureEv) {
                const PointwiseValueId exposure = Stack::NodeMath::AppendPointwiseConstant(
                    program, Splat(node.technicalExposureValue), source);
                output = Stack::NodeMath::AppendPointwiseOperation(
                    program, operation, { inputValue, exposure }, source);
            } else {
                output = Stack::NodeMath::AppendPointwiseOperation(
                    program, operation, { inputValue }, source);
            }
        } else if (operation == PointwiseOperation::Clamp) {
            const PointwiseValueId low = Stack::NodeMath::AppendPointwiseConstant(
                program, Splat(node.dataMathSettings.minValue), source);
            const PointwiseValueId high = Stack::NodeMath::AppendPointwiseConstant(
                program, Splat(node.dataMathSettings.maxValue), source);
            output = Stack::NodeMath::AppendPointwiseOperation(
                program, operation, { inputValue, low, high }, source);
        } else {
            const double constantValue = dataMathInputIsA
                ? node.dataMathSettings.constantB
                : node.dataMathSettings.constantA;
            const PointwiseValueId constant = Stack::NodeMath::AppendPointwiseConstant(
                program, Splat(constantValue), source);
            const std::vector<PointwiseValueId> operands = dataMathInputIsA
                ? std::vector<PointwiseValueId>{ inputValue, constant }
                : std::vector<PointwiseValueId>{ constant, inputValue };
            output = Stack::NodeMath::AppendPointwiseOperation(program, operation, operands, source);
        }
        plan.authoredNodeIds.push_back(node.nodeId);
        return finish(true);
    };

    PointwiseValueId root = -1;
    if (!lowerNode(nodeId, socketId, root) || plan.authoredNodeIds.size() < 2) {
        return plan;
    }
    program.rootId = root;
    plan.optimized = Stack::NodeMath::OptimizePointwiseProgram(program);
    if (!plan.optimized.success) {
        plan.failure = FirstDiagnosticMessage(plan.optimized.diagnostics);
        plan.failureNodeIds = SourceNodeIds(plan.optimized.diagnostics);
        return plan;
    }
    plan.shader = Stack::NodeMath::GeneratePointwiseShader(plan.optimized);
    if (!plan.shader.success) {
        plan.failure = FirstDiagnosticMessage(plan.shader.diagnostics);
        plan.failureNodeIds = SourceNodeIds(plan.shader.diagnostics);
        return plan;
    }
    plan.physicalPlan = Stack::NodeMath::BuildPointwisePhysicalPlan(
        plan.optimized.program, m_Width, m_Height);
    if (!plan.physicalPlan.valid) {
        plan.failure = FirstDiagnosticMessage(plan.physicalPlan.diagnostics);
        plan.failureNodeIds = SourceNodeIds(plan.physicalPlan.diagnostics);
        return plan;
    }
    plan.valid = true;
    return plan;
}

RenderPipeline::GraphNodeRenderResult RenderPipeline::RenderPointwiseFusionPlan(
    const PointwiseFusionPlan& plan,
    unsigned int inputTexture,
    bool executionInspectionEnabled) {
    GraphNodeRenderResult result;
    if (!plan.valid || inputTexture == 0 || !plan.shader.success) return result;

    static const char* vertexSource = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    bool programCacheHit = false;
    unsigned int program = 0;
    auto cached = m_PointwiseProgramCache.find(plan.shader.structureFingerprint);
    if (cached != m_PointwiseProgramCache.end() && cached->second.program != 0) {
        cached->second.lastUseSerial = ++m_GraphResourceUseSerial;
        program = cached->second.program;
        programCacheHit = true;
        ++m_LastGraphExecutionStats.pointwiseProgramCacheHits;
    } else {
        ++m_LastGraphExecutionStats.pointwiseProgramCacheMisses;
        std::string compileError;
        Stack::Renderer::ScopedGLProgram compiledProgram(
            GLHelpers::CreateShaderProgram(
                vertexSource,
                plan.shader.fragmentSource.c_str(),
                &compileError));
        if (!compiledProgram) {
            m_LastGraphExecutionStats.lastPointwiseFailure =
                compileError.empty() ? "Generated pointwise shader compilation failed." : compileError;
            m_LastGraphExecutionStats.lastPointwiseFailureNodeIds = plan.authoredNodeIds;
            return result;
        }
        if (cached != m_PointwiseProgramCache.end()) {
            m_PointwiseProgramCache.erase(cached);
        }
        const std::uint64_t programUseSerial =
            ++m_GraphResourceUseSerial;
        const auto inserted = m_PointwiseProgramCache.try_emplace(
            plan.shader.structureFingerprint,
            CachedPointwiseProgram{
                compiledProgram.Get(),
                programUseSerial,
                plan.shader.fragmentSource.size()
            });
        if (!inserted.second) {
            m_LastGraphExecutionStats.lastPointwiseFailure =
                "Generated pointwise program cache rejected a unique program identity.";
            m_LastGraphExecutionStats.lastPointwiseFailureNodeIds =
                plan.authoredNodeIds;
            return result;
        }
        program = compiledProgram.Release();
        if (m_PointwiseProgramCache.size() >
            kPointwiseProgramCacheMaximumEntries) {
            auto victim = m_PointwiseProgramCache.end();
            for (auto candidate = m_PointwiseProgramCache.begin();
                 candidate != m_PointwiseProgramCache.end();
                 ++candidate) {
                if (candidate == inserted.first) {
                    continue;
                }
                if (victim == m_PointwiseProgramCache.end() ||
                    candidate->second.lastUseSerial <
                        victim->second.lastUseSerial) {
                    victim = candidate;
                }
            }
            if (victim != m_PointwiseProgramCache.end()) {
                if (victim->second.program != 0) glDeleteProgram(victim->second.program);
                m_PointwiseProgramCache.erase(victim);
            }
        }
    }

    Stack::Renderer::ScopedGLTexture outputTexture(
        CreateGraphRenderTargetTexture());
    if (!outputTexture) return result;
    const auto begin = std::chrono::steady_clock::now();
    while (glGetError() != GL_NO_ERROR) {}
    const bool rendered = RenderIntoGraphTargetTexture(outputTexture.Get(), [&](unsigned int targetFbo) {
        glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(program, "uInput"), 0);
        for (const auto& uniform : plan.shader.uniforms) {
            glUniform4f(
                glGetUniformLocation(program, uniform.name.c_str()),
                static_cast<float>(uniform.value[0]),
                static_cast<float>(uniform.value[1]),
                static_cast<float>(uniform.value[2]),
                static_cast<float>(uniform.value[3]));
        }
        m_Quad.Draw();
        glActiveTexture(GL_TEXTURE0);
    });
    const GLenum renderError = glGetError();
    const double submitMilliseconds = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
    if (!rendered || renderError != GL_NO_ERROR) {
        std::ostringstream failure;
        failure << "Generated pointwise pass failed";
        if (renderError != GL_NO_ERROR) failure << " with GL error " << renderError;
        failure << '.';
        m_LastGraphExecutionStats.lastPointwiseFailure = failure.str();
        m_LastGraphExecutionStats.lastPointwiseFailureNodeIds = plan.authoredNodeIds;
        return result;
    }

    ++m_LastGraphExecutionStats.fusedPointwiseGroups;
    m_LastGraphExecutionStats.fusedPointwiseNodes += static_cast<int>(plan.authoredNodeIds.size());
    m_LastGraphExecutionStats.avoidedPointwisePasses +=
        std::max(0, static_cast<int>(plan.authoredNodeIds.size()) - 1);
    PointwiseExecutionGroupStats group;
    group.authoredNodeIds = plan.authoredNodeIds;
    group.operationCount = static_cast<int>(plan.authoredNodeIds.size());
    group.avoidedPassCount = std::max(0, group.operationCount - 1);
    group.targetBytes = plan.physicalPlan.bytesPerTarget;
    group.cpuSubmitMilliseconds = executionInspectionEnabled ? submitMilliseconds : 0.0;
    group.programCacheHit = programCacheHit;
    group.semanticFingerprint = plan.shader.semanticFingerprint;
    group.programFingerprint = plan.shader.structureFingerprint;
    m_LastGraphExecutionStats.pointwiseGroups.push_back(std::move(group));
    result.texture = outputTexture.Release();
    result.owned = true;
    return result;
}

void RenderPipeline::DestroyPointwiseProgramCache() {
    for (auto& [fingerprint, cached] : m_PointwiseProgramCache) {
        (void)fingerprint;
        if (cached.program != 0) glDeleteProgram(cached.program);
    }
    m_PointwiseProgramCache.clear();
}
