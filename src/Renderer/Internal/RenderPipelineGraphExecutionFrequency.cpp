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

void GraphExecutionRuntime::BindFrequencyFingerprints() {
    const auto hashResponseSettings = [](std::size_t& fingerprint,
                                          const RenderFrequencyResponseSettings& settings) {
        HashCombine(fingerprint, HashValue(static_cast<int>(settings.mode)));
        HashCombine(fingerprint, HashValue(static_cast<int>(settings.profile)));
        HashCombine(fingerprint, HashValue(settings.lowCutoff));
        HashCombine(fingerprint, HashValue(settings.highCutoff));
        HashCombine(fingerprint, HashValue(settings.transitionWidth));
        HashCombine(fingerprint, HashValue(settings.butterworthOrder));
        HashCombine(fingerprint, HashValue(settings.notches.size()));
        for (const RenderFrequencyNotch& notch : settings.notches) {
            HashCombine(fingerprint, HashValue(notch.id));
            HashCombine(fingerprint, HashValue(notch.frequency));
            HashCombine(fingerprint, HashValue(notch.directionDegrees));
            HashCombine(fingerprint, HashValue(notch.width));
        }
    };

    fingerprintResponse = [this, hashResponseSettings](int nodeId) -> std::size_t {
        if (const auto cached = responseFingerprintCache.find(nodeId);
            cached != responseFingerprintCache.end()) return cached->second;
        if (!fingerprintingResponses.insert(nodeId).second) return 0;
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || nodeIt->second == nullptr ||
            nodeIt->second->kind != RenderGraphNodeKind::FrequencyResponse) {
            fingerprintingResponses.erase(nodeId);
            return 0;
        }
        const RenderGraphNode& node = *nodeIt->second;
        std::size_t fingerprint = HashValue(static_cast<int>(node.kind));
        HashCombine(fingerprint, HashValue(node.definitionId));
        HashCombine(fingerprint, HashValue(node.definitionVersion));
        HashCombine(fingerprint, HashValue(node.definitionHash));
        hashResponseSettings(fingerprint, node.frequencyResponseSettings);
        for (const std::string& parameterId : {
                 std::string(EditorNodeGraph::kLowCutoffParameterId),
                 std::string(EditorNodeGraph::kHighCutoffParameterId),
                 std::string(EditorNodeGraph::kTransitionWidthParameterId),
                 std::string(EditorNodeGraph::kButterworthOrderParameterId)}) {
            const RenderGraphLink* input = findInputLink(
                nodeId, EditorNodeGraph::ParameterInputSocketId(parameterId));
            HashCombine(fingerprint, input
                ? fingerprintScalar(input->fromNodeId, input->fromSocketId) : 0);
        }
        for (std::size_t notchIndex = 0;
             notchIndex < node.frequencyResponseSettings.notches.size();
             ++notchIndex) {
            for (const char* field : { "frequency", "direction", "width" }) {
                const RenderGraphLink* input = findInputLink(
                    nodeId,
                        EditorNodeGraph::ParameterInputSocketId(
                            EditorNodeGraph::FrequencyNotchParameterId(
                                node.frequencyResponseSettings.notches[notchIndex].id,
                                field)));
                HashCombine(fingerprint, input
                    ? fingerprintScalar(
                        input->fromNodeId, input->fromSocketId)
                    : 0);
            }
        }
        fingerprintingResponses.erase(nodeId);
        responseFingerprintCache[nodeId] = fingerprint;
        return fingerprint;
    };

    fingerprintFrequency = [this](int nodeId, const std::string& socketId) -> std::size_t {
        const std::string key = MakeNodeSocketKey(nodeId, socketId);
        if (const auto cached = frequencyFingerprintCache.find(key);
            cached != frequencyFingerprintCache.end()) return cached->second;
        if (!fingerprintingFrequency.insert(key).second) return 0;
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || nodeIt->second == nullptr) {
            fingerprintingFrequency.erase(key);
            return 0;
        }
        const RenderGraphNode& node = *nodeIt->second;
        std::size_t fingerprint = HashValue(static_cast<int>(node.kind));
        HashCombine(fingerprint, HashValue(node.nodeId));
        HashCombine(fingerprint, HashValue(node.definitionId));
        HashCombine(fingerprint, HashValue(node.definitionVersion));
        HashCombine(fingerprint, HashValue(node.definitionHash));
        HashCombine(fingerprint, HashValue(socketId));
        if (node.kind == RenderGraphNodeKind::FrequencyFft) {
            const RenderGraphLink* input = findInputLink(
                nodeId, EditorNodeGraph::kChannelInputSocketId);
            HashCombine(fingerprint, input
                ? fingerprintMask(input->fromNodeId, input->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(
                static_cast<int>(node.frequencyFftSettings.edgePolicy)));
            HashCombine(fingerprint, HashValue(pipeline.m_Width));
            HashCombine(fingerprint, HashValue(pipeline.m_Height));
        } else if (node.kind == RenderGraphNodeKind::ApplyFrequencyResponse) {
            const RenderGraphLink* spectrum = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumInputSocketId);
            const RenderGraphLink* response = findInputLink(
                nodeId, EditorNodeGraph::kFrequencyResponseInputSocketId);
            const RenderGraphLink* strength = findInputLink(
                nodeId,
                EditorNodeGraph::ParameterInputSocketId(
                    EditorNodeGraph::kStrengthParameterId));
            HashCombine(fingerprint, spectrum
                ? fingerprintFrequency(spectrum->fromNodeId, spectrum->fromSocketId) : 0);
            HashCombine(fingerprint, response
                ? fingerprintResponse(response->fromNodeId) : 0);
            HashCombine(fingerprint, strength
                ? fingerprintScalar(strength->fromNodeId, strength->fromSocketId)
                : HashValue(node.applyFrequencyResponseSettings.strength));
        } else if (node.kind == RenderGraphNodeKind::CombineSpectra) {
            const RenderGraphLink* a = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumInputASocketId);
            const RenderGraphLink* b = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumInputBSocketId);
            HashCombine(fingerprint, a
                ? fingerprintFrequency(a->fromNodeId, a->fromSocketId) : 0);
            HashCombine(fingerprint, b
                ? fingerprintFrequency(b->fromNodeId, b->fromSocketId) : 0);
            HashCombine(fingerprint, HashValue(
                static_cast<int>(node.combineSpectraSettings.mode)));
        } else if (node.kind == RenderGraphNodeKind::SpectrumSeparate) {
            const RenderGraphLink* spectrum = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumInputSocketId);
            HashCombine(fingerprint, spectrum
                ? fingerprintFrequency(spectrum->fromNodeId, spectrum->fromSocketId) : 0);
        } else if (node.kind == RenderGraphNodeKind::SpectrumRecombine) {
            const RenderGraphLink* magnitude = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumMagnitudeInputSocketId);
            const RenderGraphLink* phase = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumPhaseInputSocketId);
            HashCombine(fingerprint, magnitude
                ? fingerprintFrequency(magnitude->fromNodeId, magnitude->fromSocketId) : 0);
            HashCombine(fingerprint, phase
                ? fingerprintFrequency(phase->fromNodeId, phase->fromSocketId) : 0);
        } else {
            fingerprint = 0;
        }
        fingerprintingFrequency.erase(key);
        frequencyFingerprintCache[key] = fingerprint;
        return fingerprint;
    };

}

void GraphExecutionRuntime::BindFrequencyEvaluation() {
    evalResponse = [this](int nodeId, RenderFrequencyResponseSettings& response) -> bool {
        if (const auto cached = responseCache.find(nodeId); cached != responseCache.end()) {
            response = cached->second;
            return true;
        }
        if (!visitingResponses.insert(nodeId).second) return false;
        const auto finish = [&](bool ok) {
            visitingResponses.erase(nodeId);
            return ok;
        };
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || nodeIt->second == nullptr ||
            nodeIt->second->kind != RenderGraphNodeKind::FrequencyResponse) return finish(false);
        response = nodeIt->second->frequencyResponseSettings;
        const auto applyScalar = [&](const char* parameterId, float& destination) {
            const RenderGraphLink* input = findInputLink(
                nodeId, EditorNodeGraph::ParameterInputSocketId(parameterId));
            if (input == nullptr) return true;
            double value = destination;
            if (!evalScalar(input->fromNodeId, input->fromSocketId, value)) return false;
            destination = static_cast<float>(value);
            return true;
        };
        if (!applyScalar(EditorNodeGraph::kLowCutoffParameterId, response.lowCutoff) ||
            !applyScalar(EditorNodeGraph::kHighCutoffParameterId, response.highCutoff) ||
            !applyScalar(EditorNodeGraph::kTransitionWidthParameterId, response.transitionWidth) ||
            !applyScalar(EditorNodeGraph::kButterworthOrderParameterId, response.butterworthOrder)) {
            return finish(false);
        }
        for (std::size_t notchIndex = 0;
             notchIndex < response.notches.size();
             ++notchIndex) {
            const auto applyNotchScalar = [&](const char* field, float& target) {
                const std::string parameterId =
                    EditorNodeGraph::FrequencyNotchParameterId(
                        response.notches[notchIndex].id, field);
                const RenderGraphLink* input = findInputLink(
                    nodeId,
                    EditorNodeGraph::ParameterInputSocketId(parameterId));
                if (input == nullptr) return true;
                double value = target;
                if (!evalScalar(
                        input->fromNodeId, input->fromSocketId, value)) return false;
                target = static_cast<float>(value);
                return true;
            };
            RenderFrequencyNotch& notch = response.notches[notchIndex];
            if (!applyNotchScalar("frequency", notch.frequency) ||
                !applyNotchScalar("direction", notch.directionDegrees) ||
                !applyNotchScalar("width", notch.width)) {
                return finish(false);
            }
        }
        CanonicalizeFrequencyResponseSettings(response);
        responseCache[nodeId] = response;
        return finish(true);
    };

    resolveChannelRole = [this](int nodeId, const std::string& socketId) -> std::string {
        int currentNodeId = nodeId;
        std::string currentSocketId = socketId;
        std::unordered_set<std::string> visited;
        visited.reserve(nodes.size());

        while (visited.insert(
                MakeNodeSocketKey(
                    currentNodeId,
                    currentSocketId)).second) {
            const auto nodeIt = nodes.find(currentNodeId);
            if (nodeIt == nodes.end() || nodeIt->second == nullptr) {
                break;
            }
            const RenderGraphNode& node = *nodeIt->second;
            if (node.kind == RenderGraphNodeKind::ChannelSplit) {
                return currentSocketId;
            }

            const RenderGraphLink* input = nullptr;
            switch (node.kind) {
                case RenderGraphNodeKind::FrequencyFilter:
                case RenderGraphNodeKind::FrequencyFft:
                    input = findInputLink(
                        node.nodeId,
                        EditorNodeGraph::kChannelInputSocketId);
                    break;
                case RenderGraphNodeKind::FrequencyIfft:
                case RenderGraphNodeKind::SpectrumView:
                case RenderGraphNodeKind::SpectrumSeparate:
                case RenderGraphNodeKind::SpectrumAnalyzer:
                    input = findInputLink(
                        node.nodeId,
                        EditorNodeGraph::kSpectrumInputSocketId);
                    break;
                case RenderGraphNodeKind::ApplyFrequencyResponse:
                    input = findInputLink(
                        node.nodeId,
                        EditorNodeGraph::kSpectrumInputSocketId);
                    break;
                case RenderGraphNodeKind::CombineSpectra:
                    input = findInputLink(
                        node.nodeId,
                        EditorNodeGraph::kSpectrumInputASocketId);
                    if (!input) {
                        input = findInputLink(
                            node.nodeId,
                            EditorNodeGraph::kSpectrumInputBSocketId);
                    }
                    break;
                case RenderGraphNodeKind::SpectrumRecombine:
                    input = findInputLink(
                        node.nodeId,
                        EditorNodeGraph::kSpectrumMagnitudeInputSocketId);
                    if (!input) {
                        input = findInputLink(
                            node.nodeId,
                            EditorNodeGraph::kSpectrumPhaseInputSocketId);
                    }
                    break;
                default:
                    break;
            }
            if (!input) {
                break;
            }
            currentNodeId = input->fromNodeId;
            currentSocketId = input->fromSocketId;
        }
        return currentSocketId.empty()
            ? std::string("channel")
            : currentSocketId;
    };

    evalFrequency = [this](int nodeId, const std::string& socketId) -> RenderFrequencyResource {
        const std::string key = MakeNodeSocketKey(nodeId, socketId);
        if (const auto cached = frequencyCache.find(key); cached != frequencyCache.end()) {
            ++pipeline.m_LastGraphExecutionStats.frequencyCacheHits;
            pipeline.m_Width = cached->second.sourceWidth;
            pipeline.m_Height = cached->second.sourceHeight;
            return cached->second;
        }
        if (!visitingFrequency.insert(key).second) return {};
        const auto finish = [&](RenderFrequencyResource resource) {
            visitingFrequency.erase(key);
            return resource;
        };
        const auto nodeIt = nodes.find(nodeId);
        if (nodeIt == nodes.end() || nodeIt->second == nullptr) return finish({});
        const RenderGraphNode& node = *nodeIt->second;
        const std::size_t fingerprint = fingerprintFrequency(nodeId, socketId);
        if (fingerprint == 0) return finish({});
        if (const auto persistent = pipeline.m_GraphFrequencyCache.find(key);
            persistent != pipeline.m_GraphFrequencyCache.end() &&
            persistent->second.fingerprint == fingerprint &&
            persistent->second.resource.valid &&
            persistent->second.resource.texture != 0) {
            persistent->second.lastUseSerial = ++pipeline.m_GraphResourceUseSerial;
            ++pipeline.m_LastGraphExecutionStats.frequencyCacheHits;
            frequencyCache[key] = persistent->second.resource;
            pipeline.m_Width = persistent->second.resource.sourceWidth;
            pipeline.m_Height = persistent->second.resource.sourceHeight;
            return finish(persistent->second.resource);
        }
        ++pipeline.m_LastGraphExecutionStats.frequencyCacheMisses;

        RenderFrequencyResource result;
        if (node.kind == RenderGraphNodeKind::FrequencyFft) {
            const RenderGraphLink* input = findInputLink(
                nodeId, EditorNodeGraph::kChannelInputSocketId);
            const unsigned int channel = input
                ? evalMask(input->fromNodeId, input->fromSocketId) : 0;
            const int sourceWidth = pipeline.m_Width;
            const int sourceHeight = pipeline.m_Height;
            if (channel != 0) {
                result = pipeline.RenderFourierTransform(
                    channel, sourceWidth, sourceHeight,
                    node.frequencyFftSettings.edgePolicy,
                    input
                        ? resolveChannelRole(
                            input->fromNodeId, input->fromSocketId)
                        : std::string("channel"));
            }
        } else if (node.kind == RenderGraphNodeKind::ApplyFrequencyResponse) {
            const RenderGraphLink* spectrumLink = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumInputSocketId);
            const RenderGraphLink* responseLink = findInputLink(
                nodeId, EditorNodeGraph::kFrequencyResponseInputSocketId);
            RenderFrequencyResponseSettings response;
            double strength = node.applyFrequencyResponseSettings.strength;
            const RenderGraphLink* strengthLink = findInputLink(
                nodeId,
                EditorNodeGraph::ParameterInputSocketId(
                    EditorNodeGraph::kStrengthParameterId));
            const bool strengthReady = strengthLink == nullptr ||
                evalScalar(strengthLink->fromNodeId, strengthLink->fromSocketId, strength);
            if (spectrumLink != nullptr && responseLink != nullptr &&
                strengthReady && evalResponse(responseLink->fromNodeId, response)) {
                const double canonicalStrength =
                    std::isfinite(strength)
                        ? std::clamp(strength, 0.0, 1.0)
                        : 1.0;
                result = pipeline.RenderApplyFrequencyResponse(
                    evalFrequency(spectrumLink->fromNodeId, spectrumLink->fromSocketId),
                    response,
                    static_cast<float>(canonicalStrength));
            }
        } else if (node.kind == RenderGraphNodeKind::CombineSpectra) {
            const RenderGraphLink* a = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumInputASocketId);
            const RenderGraphLink* b = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumInputBSocketId);
            if (a != nullptr && b != nullptr) {
                const RenderFrequencyResource spectrumA =
                    evalFrequency(a->fromNodeId, a->fromSocketId);
                const RenderFrequencyResource spectrumB =
                    evalFrequency(b->fromNodeId, b->fromSocketId);
                result = pipeline.RenderCombineSpectra(
                    spectrumA, spectrumB, node.combineSpectraSettings.mode);
                if (spectrumA.valid && spectrumB.valid && !result.valid) {
                    pipeline.m_LastGraphExecutionStats.lastSpecializedFailureNodeId = nodeId;
                    pipeline.m_LastGraphExecutionStats.lastSpecializedFailure =
                        "Combine Spectra requires matching source role, source and padded "
                        "extents, padding origin, edge policy, precision, normalization, "
                        "and coordinate convention. Rebuild both inputs from compatible Channels.";
                }
            }
        } else if (node.kind == RenderGraphNodeKind::SpectrumSeparate) {
            const RenderGraphLink* spectrum = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumInputSocketId);
            if (spectrum != nullptr) {
                const RenderFrequencyResourceKind componentKind =
                    socketId == EditorNodeGraph::kSpectrumMagnitudeOutputSocketId
                        ? RenderFrequencyResourceKind::Magnitude
                        : RenderFrequencyResourceKind::Phase;
                result = pipeline.RenderSpectrumComponent(
                    evalFrequency(spectrum->fromNodeId, spectrum->fromSocketId),
                    componentKind);
            }
        } else if (node.kind == RenderGraphNodeKind::SpectrumRecombine) {
            const RenderGraphLink* magnitude = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumMagnitudeInputSocketId);
            const RenderGraphLink* phase = findInputLink(
                nodeId, EditorNodeGraph::kSpectrumPhaseInputSocketId);
            if (magnitude != nullptr && phase != nullptr) {
                const RenderFrequencyResource magnitudeResource =
                    evalFrequency(magnitude->fromNodeId, magnitude->fromSocketId);
                const RenderFrequencyResource phaseResource =
                    evalFrequency(phase->fromNodeId, phase->fromSocketId);
                result = pipeline.RenderRecombineSpectrum(
                    magnitudeResource, phaseResource);
                if (magnitudeResource.valid && phaseResource.valid && !result.valid) {
                    pipeline.m_LastGraphExecutionStats.lastSpecializedFailureNodeId = nodeId;
                    pipeline.m_LastGraphExecutionStats.lastSpecializedFailure =
                        "Recombine Spectrum requires Magnitude and Phase from compatible "
                        "transform metadata. Use outputs from matching Separate Spectrum chains.";
                }
            }
        }

        if (result.valid) {
            if (pipeline.StoreFrequencyCacheEntry(
                    key, result, fingerprint, true)) {
                try {
                    frequencyCache[key] = result;
                } catch (const std::bad_alloc&) {
                    frequencyCache.erase(key);
                } catch (const std::length_error&) {
                    frequencyCache.erase(key);
                }
                pipeline.m_Width = result.sourceWidth;
                pipeline.m_Height = result.sourceHeight;
            } else {
                if (result.texture != 0) {
                    glDeleteTextures(1, &result.texture);
                }
                result = {};
            }
        } else {
            const auto stale = pipeline.m_GraphFrequencyCache.find(key);
            if (stale != pipeline.m_GraphFrequencyCache.end()) {
                pipeline.DeleteFrequencyCacheEntry(stale->second);
                pipeline.m_GraphFrequencyCache.erase(stale);
            }
            if (pipeline.m_LastGraphExecutionStats.lastSpecializedFailureNodeId != nodeId) {
                pipeline.m_LastGraphExecutionStats.lastSpecializedFailureNodeId = nodeId;
                pipeline.m_LastGraphExecutionStats.lastSpecializedFailure =
                    "The typed frequency stage could not satisfy its spectrum contract.";
            }
        }
        return finish(result);
    };

}

} // namespace Stack::Renderer::GraphExecution
