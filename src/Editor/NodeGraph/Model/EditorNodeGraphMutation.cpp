#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Editor/NodeGraph/GraphOutputSemantics.h"
#include "Editor/NodeGraph/Model/EditorNodeGraphConnectionRules.h"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace EditorNodeGraph {
namespace {

bool IsChannelSocketId(const std::string& socketId) {
    return socketId == "r" || socketId == "g" || socketId == "b" || socketId == "a";
}

void SetConnectionErrorNoThrow(
    std::string* errorMessage,
    const char* message) noexcept {
    if (!errorMessage) return;
    try {
        *errorMessage = message;
    } catch (...) {
        errorMessage->clear();
    }
}

bool IsUniformOrResourceSocketType(SocketType type) {
    switch (type) {
        case SocketType::Boolean:
        case SocketType::Integer:
        case SocketType::Scalar:
        case SocketType::Vector2:
        case SocketType::Vector3:
        case SocketType::Vector4:
        case SocketType::Matrix3:
        case SocketType::Matrix4:
        case SocketType::Curve:
        case SocketType::Coordinate:
        case SocketType::Histogram:
        case SocketType::Statistics:
        case SocketType::Metadata:
        case SocketType::Handle:
        case SocketType::Value:
            return true;
        default:
            return false;
    }
}

bool IsSpecializedFrequencySocketType(SocketType type) {
    return type == SocketType::Spectrum ||
        type == SocketType::FrequencyResponse ||
        type == SocketType::SpectrumMagnitude ||
        type == SocketType::SpectrumPhase;
}


bool IsDataMathImageInputSocketId(const std::string& socketId) {
    return EditorNodeGraph::IsDataMathInputSocketId(socketId) ||
        socketId == EditorNodeGraph::kDataMathBaseInputSocketId;
}

bool IsScalarAverageNode(const EditorNodeGraph::Node& node) {
    return node.kind == EditorNodeGraph::NodeKind::DataMath &&
        node.dataMathMode == EditorNodeGraph::DataMathMode::Average;
}

bool IsImageAverageNode(const EditorNodeGraph::Node& node) {
    return node.kind == EditorNodeGraph::NodeKind::DataMath &&
        node.dataMathMode == EditorNodeGraph::DataMathMode::ImageAverage;
}

bool DataMathAllowsScalarTarget(const EditorNodeGraph::Node& node, const std::string& socketId) {
    if (node.kind != EditorNodeGraph::NodeKind::DataMath || IsImageAverageNode(node)) {
        return false;
    }
    if (IsScalarAverageNode(node)) {
        return EditorNodeGraph::IsDataMathInputSocketId(socketId);
    }
    return socketId == EditorNodeGraph::kMaskInputSocketId;
}

bool DataMathAllowsScalarToImageTarget(const EditorNodeGraph::Node& node, const std::string& socketId) {
    return node.kind == EditorNodeGraph::NodeKind::DataMath &&
        !IsScalarAverageNode(node) &&
        !IsImageAverageNode(node) &&
        IsDataMathImageInputSocketId(socketId);
}

bool DataMathAllowsFullImageTarget(const EditorNodeGraph::Node& node, const std::string& socketId) {
    if (node.kind != EditorNodeGraph::NodeKind::DataMath || IsScalarAverageNode(node)) {
        return false;
    }
    if (IsImageAverageNode(node)) {
        return EditorNodeGraph::IsDataMathInputSocketId(socketId);
    }
    return IsDataMathImageInputSocketId(socketId);
}

Link MakeSocketLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) {
    Link link;
    link.fromNodeId = fromNodeId;
    link.fromSocketId = fromSocketId;
    link.toNodeId = toNodeId;
    link.toSocketId = toSocketId;
    return link;
}

enum class MfsrGraphInputFamily {
    Unknown,
    RawDerived,
    RasterDerived,
    Mixed
};

MfsrGraphInputFamily MergeMfsrGraphInputFamily(MfsrGraphInputFamily a, MfsrGraphInputFamily b) {
    if (a == MfsrGraphInputFamily::Mixed || b == MfsrGraphInputFamily::Mixed) {
        return MfsrGraphInputFamily::Mixed;
    }
    if (a == MfsrGraphInputFamily::Unknown) {
        return b;
    }
    if (b == MfsrGraphInputFamily::Unknown || a == b) {
        return a;
    }
    return MfsrGraphInputFamily::Mixed;
}

MfsrGraphInputFamily ResolveMfsrGraphInputFamily(
    const Graph& graph,
    int nodeId,
    const std::string& socketId) {
    MfsrGraphInputFamily family = MfsrGraphInputFamily::Unknown;
    std::unordered_set<std::string> visited;
    visited.reserve(graph.GetNodes().size());
    std::vector<std::pair<int, std::string>> pending{
        { nodeId, socketId }
    };

    while (!pending.empty()) {
        const auto [currentNodeId, currentSocketId] =
            std::move(pending.back());
        pending.pop_back();
        const std::string key =
            std::to_string(currentNodeId) + "\x1f" + currentSocketId;
        if (!visited.insert(key).second) {
            continue;
        }

        const Node* node = graph.FindNode(currentNodeId);
        if (!node) {
            continue;
        }

        const auto addInput = [&](const std::string& inputSocketId) {
            if (const Link* upstream =
                    graph.FindInputLink(currentNodeId, inputSocketId)) {
                pending.emplace_back(
                    upstream->fromNodeId,
                    upstream->fromSocketId);
                return true;
            }
            return false;
        };
        const auto addChannels = [&]() {
            addInput("r");
            addInput("g");
            addInput("b");
            addInput("a");
        };

        switch (node->kind) {
            case NodeKind::RawSource:
            case NodeKind::RawDevelopment:
            case NodeKind::RawNeuralDenoise:
            case NodeKind::RawDecode:
            case NodeKind::RawDevelop:
                family = MergeMfsrGraphInputFamily(
                    family,
                    MfsrGraphInputFamily::RawDerived);
                break;
            case NodeKind::Image:
            case NodeKind::ImageGenerator:
                family = MergeMfsrGraphInputFamily(
                    family,
                    MfsrGraphInputFamily::RasterDerived);
                break;
            case NodeKind::Layer:
            case NodeKind::TechnicalImage:
            case NodeKind::Reformat:
            case NodeKind::RawDetailAutoMask:
            case NodeKind::RawDetailFusion:
            case NodeKind::ChannelSplit:
            case NodeKind::ImageToMask:
                addInput(kImageInputSocketId);
                break;
            case NodeKind::Compound: {
                std::vector<std::string> dependencies;
                std::string error;
                if (graph.ResolveCompoundOutputInputDependencies(
                        currentNodeId,
                        currentSocketId,
                        dependencies,
                        &error)) {
                    for (const std::string& dependency : dependencies) {
                        addInput(dependency);
                    }
                }
                break;
            }
            case NodeKind::Lut:
                if (!addInput(kImageInputSocketId)) {
                    addChannels();
                }
                break;
            case NodeKind::HdrMerge:
                if (currentSocketId == kHdrMergeInput1SocketId ||
                    currentSocketId == kHdrMergeInput2SocketId ||
                    currentSocketId == kHdrMergeInput3SocketId) {
                    addInput(currentSocketId);
                } else {
                    addInput(kHdrMergeInput1SocketId);
                    addInput(kHdrMergeInput2SocketId);
                    addInput(kHdrMergeInput3SocketId);
                }
                break;
            case NodeKind::Mfsr:
                if (IsMfsrInputSocketId(currentSocketId)) {
                    addInput(currentSocketId);
                } else {
                    for (int inputIndex = 0;
                         inputIndex < kMaxMfsrInputCount;
                         ++inputIndex) {
                        addInput(MfsrInputSocketId(inputIndex));
                    }
                }
                break;
            case NodeKind::Mix:
                addInput(kMixInputASocketId);
                addInput(kMixInputBSocketId);
                break;
            case NodeKind::DataMath:
                for (int inputIndex = 0;
                     inputIndex < kMaxDataMathInputCount;
                     ++inputIndex) {
                    addInput(DataMathInputSocketId(inputIndex));
                }
                addInput(kDataMathBaseInputSocketId);
                break;
            case NodeKind::ChannelCombine:
                if (!addInput(kImageInputSocketId)) {
                    addChannels();
                }
                break;
            case NodeKind::ConstantChannel:
                addInput(kMatchExtentInputSocketId);
                break;
            case NodeKind::Output:
                addInput(kImageInputSocketId);
                break;
            case NodeKind::FrequencyFilter:
            case NodeKind::FrequencyFft:
                addInput(kChannelInputSocketId);
                break;
            case NodeKind::FrequencyIfft:
            case NodeKind::SpectrumView:
            case NodeKind::SpectrumSeparate:
            case NodeKind::SpectrumAnalyzer:
                addInput(kSpectrumInputSocketId);
                break;
            case NodeKind::ApplyFrequencyResponse:
                addInput(kSpectrumInputSocketId);
                break;
            case NodeKind::CombineSpectra:
                addInput(kSpectrumInputASocketId);
                addInput(kSpectrumInputBSocketId);
                break;
            case NodeKind::SpectrumRecombine:
                addInput(kSpectrumMagnitudeInputSocketId);
                addInput(kSpectrumPhaseInputSocketId);
                break;
            case NodeKind::SpectrumMath:
                addInput(kMixInputASocketId);
                addInput(kMixInputBSocketId);
                break;
            case NodeKind::MagnitudePhase:
                if (node->magnitudePhaseMode ==
                    MagnitudePhaseMode::Recombine) {
                    addInput("magnitude");
                    addInput("phase");
                } else {
                    addInput(kImageInputSocketId);
                }
                break;
            case NodeKind::MaskGenerator:
            case NodeKind::Value:
            case NodeKind::FieldMean:
            case NodeKind::FrequencyResponse:
            case NodeKind::FrequencyMask:
            case NodeKind::MaskCombine:
            case NodeKind::MaskUtility:
            case NodeKind::CustomMask:
            case NodeKind::Composite:
            case NodeKind::Scope:
            case NodeKind::Preview:
                break;
        }

        if (family == MfsrGraphInputFamily::Mixed) {
            return family;
        }
    }
    return family;
}

bool MfsrGraphInputFamiliesConflict(MfsrGraphInputFamily a, MfsrGraphInputFamily b) {
    if (a == MfsrGraphInputFamily::Mixed || b == MfsrGraphInputFamily::Mixed) {
        return true;
    }
    return a != MfsrGraphInputFamily::Unknown &&
        b != MfsrGraphInputFamily::Unknown &&
        a != b;
}

bool CanConnectMfsrInputFamily(
    const Graph& graph,
    int fromNodeId,
    const std::string& fromSocketId,
    int toNodeId,
    const std::string& toSocketId,
    std::string* errorMessage) {
    const MfsrGraphInputFamily candidateFamily =
        ResolveMfsrGraphInputFamily(graph, fromNodeId, fromSocketId);
    if (candidateFamily == MfsrGraphInputFamily::Mixed) {
        if (errorMessage) {
            *errorMessage = "MFSR inputs cannot use an upstream stream that already mixes RAW-derived and raster-derived frames.";
        }
        return false;
    }

    for (const Link& link : graph.GetLinks()) {
        if (link.toNodeId != toNodeId ||
            link.toSocketId == toSocketId ||
            !IsMfsrInputSocketId(link.toSocketId)) {
            continue;
        }
        const MfsrGraphInputFamily existingFamily =
            ResolveMfsrGraphInputFamily(graph, link.fromNodeId, link.fromSocketId);
        if (MfsrGraphInputFamiliesConflict(candidateFamily, existingFamily)) {
            if (errorMessage) {
                *errorMessage = "MFSR inputs cannot mix RAW-derived and raster-derived frames.";
            }
            return false;
        }
    }

    return true;
}

} // namespace

bool Graph::IsOutputConnected() const {
    return !GetCompletedChains().empty();
}

bool Graph::IsOutputChannelInspection(int outputNodeId) const {
    const Node* output = FindNode(outputNodeId);
    if (!output || output->kind != NodeKind::Output) {
        return false;
    }
    const Link* input =
        FindInputLink(outputNodeId, kImageInputSocketId);
    return input &&
        IsScalarSocketStream(
            input->fromNodeId,
            input->fromSocketId);
}

bool Graph::TryConnect(int fromNodeId, int toNodeId, std::string* errorMessage) {
    const Node* from = FindNode(fromNodeId);
    const Node* to = FindNode(toNodeId);
    if (!from || !to) {
        if (errorMessage) *errorMessage = "Invalid node connection.";
        return false;
    }
    return TryConnectSockets(fromNodeId, DefaultOutputSocket(*from), toNodeId, DefaultInputSocket(*to), errorMessage);
}

bool Graph::CanConnectSockets(
    int fromNodeId,
    const std::string& fromSocketId,
    int toNodeId,
    const std::string& toSocketId,
    std::string* normalizedToSocketId,
    std::string* errorMessage) const {
    if (normalizedToSocketId) {
        *normalizedToSocketId = toSocketId;
    }

    const Node* from = FindNode(fromNodeId);
    const Node* to = FindNode(toNodeId);
    SocketDefinition fromSocket;
    SocketDefinition toSocket;
    if (!from || !to || fromNodeId == toNodeId || !FindSocket(fromNodeId, fromSocketId, &fromSocket) || !FindSocket(toNodeId, toSocketId, &toSocket)) {
        if (errorMessage) *errorMessage = "Invalid socket connection.";
        return false;
    }
    if (fromSocket.direction != SocketDirection::Output || toSocket.direction != SocketDirection::Input) {
        if (errorMessage) *errorMessage = "That pin direction is not valid.";
        return false;
    }
    if (from->kind == NodeKind::Output || to->kind == NodeKind::Image ||
        to->kind == NodeKind::RawSource ||
        to->kind == NodeKind::RawDevelopment ||
        from->kind == NodeKind::Composite ||
        to->kind == NodeKind::Composite) {
        if (errorMessage) *errorMessage = "That pin direction is not valid.";
        return false;
    }

    bool fromScalarStreamResolved = false;
    bool fromIsScalarStream = false;
    const auto isFromScalarStream = [&]() {
        if (!fromScalarStreamResolved) {
            fromIsScalarStream =
                IsScalarSocketStream(fromNodeId, fromSocketId);
            fromScalarStreamResolved = true;
        }
        return fromIsScalarStream;
    };

    if (to->kind == NodeKind::Output && to->outputSettings.maskOutput &&
        toSocketId == kImageInputSocketId) {
        const auto description = DescribeGraphOutput(*this, fromNodeId, fromSocketId);
        const bool coverage = fromSocket.type == SocketType::Mask ||
            fromSocket.type == SocketType::ScalarField || fromSocket.type == SocketType::Channel ||
            (fromSocket.type == SocketType::Image && (isFromScalarStream() ||
                description.descriptor.logicalType == Stack::NodeMath::LogicalValueType::Invalid));
        if (!coverage) {
            if (errorMessage) *errorMessage = "Mask Output needs one channel. Select a channel or convert the image to a mask.";
            return false;
        }
        if (WouldCreateCycle(fromNodeId, fromSocketId, toNodeId, toSocketId)) {
            if (errorMessage) *errorMessage = "That connection would create a cycle.";
            return false;
        }
        return true;
    }

    if (to->kind == NodeKind::MaskGenerator && toSocketId == kMatchExtentInputSocketId) {
        if (fromSocket.type != SocketType::Image) {
            if (errorMessage) *errorMessage = "A mask reference requires an image input.";
            return false;
        }
        if (WouldCreateCycle(fromNodeId, fromSocketId, toNodeId, toSocketId)) {
            if (errorMessage) *errorMessage = "That connection would create a cycle.";
            return false;
        }
        return true;
    }

    if (((to->kind == NodeKind::RawOperation &&
        (toSocketId == kImageInputSocketId || toSocketId == "referenceIn")) ||
        (to->role == Stack::GraphModel::NodeRole::LayerResult && toSocketId == kImageInputSocketId)) &&
        (fromSocket.type != SocketType::Image || isFromScalarStream())) {
        if (errorMessage) *errorMessage = "Photo operations require a full scene-linear color image.";
        return false;
    }

    std::string semanticError;
    if (!EditorNodeGraphDefinitions::ValidateInputDescriptor(*this, *to, toSocketId,
            DescribeGraphOutput(*this, fromNodeId, fromSocketId).descriptor, semanticError)) {
        if (errorMessage) *errorMessage = semanticError;
        return false;
    }

    const bool toScope = to->kind == NodeKind::Scope && toSocketId == kScopeInputSocketId;
    if (toScope) {
        if (fromSocket.type != SocketType::Image &&
            fromSocket.type != SocketType::Channel &&
            fromSocket.type != SocketType::ScalarField &&
            !(fromSocket.type == SocketType::Mask && isFromScalarStream())) {
            if (errorMessage) *errorMessage = "Scopes can analyze image or scalar outputs.";
            return false;
        }
        return true;
    }

    const bool toPreview = to->kind == NodeKind::Preview && toSocketId == kPreviewInputSocketId;
    if (toPreview) {
        if (fromSocket.type != SocketType::Image &&
            fromSocket.type != SocketType::Channel &&
            fromSocket.type != SocketType::ScalarField &&
            !(fromSocket.type == SocketType::Mask && isFromScalarStream())) {
            if (errorMessage) *errorMessage = "Preview nodes can inspect image or scalar outputs.";
            return false;
        }
        return true;
    }

    if (to->kind == NodeKind::Lut &&
        toSocketId == kImageInputSocketId) {
        const std::string upstreamChannel =
            ResolveSocketChannel(fromNodeId, fromSocketId);
        if (!upstreamChannel.empty()) {
            const bool ok =
                CanConnectSockets(
                    fromNodeId,
                    fromSocketId,
                    toNodeId,
                    upstreamChannel,
                    nullptr,
                    errorMessage);
            if (ok && normalizedToSocketId) {
                *normalizedToSocketId = upstreamChannel;
            }
            return ok;
        }
    }

    if (ConnectionRules::IsChannelProcessingBridge(
            *this,
            fromNodeId,
            fromSocketId,
            fromSocket,
            *to,
            toSocketId,
            toSocket)) {
        if (WouldCreateCycle(
                fromNodeId,
                fromSocketId,
                toNodeId,
                toSocketId)) {
            if (errorMessage) {
                *errorMessage =
                    "That connection would create a cycle.";
            }
            return false;
        }
        return true;
    }

    if (fromSocket.type == SocketType::Raw || toSocket.type == SocketType::Raw) {
        const bool validMfdBinding =
            fromSocket.type == SocketType::Raw &&
            toSocket.type == SocketType::Raw &&
            from->kind == NodeKind::RawProjectFrame &&
            to->kind == NodeKind::MultiFrameDenoise &&
            fromSocketId == kRawOutputSocketId &&
            toSocketId == MfdFrameInputSocketId(from->rawProjectFrame.frameId) &&
            from->rawProjectFrame.sourceSetId ==
                to->multiFrameDenoise.sourceSetId;
        const bool validHdrBinding =
            fromSocket.type == SocketType::Raw &&
            toSocket.type == SocketType::Raw &&
            from->kind == NodeKind::RawProjectFrame &&
            to->kind == NodeKind::MultiFrameHdr &&
            fromSocketId == kRawOutputSocketId &&
            toSocketId == MfdFrameInputSocketId(from->rawProjectFrame.frameId) &&
            from->rawProjectFrame.sourceSetId == to->multiFrameHdr.sourceSetId;
        const bool validRawPipelineLink =
            fromSocket.type == SocketType::Raw &&
            toSocket.type == SocketType::Raw &&
            (from->kind == NodeKind::RawSource || from->kind == NodeKind::RawNeuralDenoise) &&
            fromSocketId == kRawOutputSocketId &&
            (to->kind == NodeKind::RawNeuralDenoise || to->kind == NodeKind::RawDecode || to->kind == NodeKind::RawDevelop) &&
            toSocketId == kRawInputSocketId;
        if (!validMfdBinding && !validHdrBinding && !validRawPipelineLink) {
            if (errorMessage) *errorMessage =
                "RAW sockets connect through the RAW pipeline or from a managed RAW Project Frame into its burst input.";
            return false;
        }
        if (WouldCreateCycle(fromNodeId, fromSocketId, toNodeId, toSocketId)) {
            if (errorMessage) *errorMessage = "That connection would create a cycle.";
            return false;
        }
        return true;
    }

    if (IsSpecializedFrequencySocketType(fromSocket.type) ||
        IsSpecializedFrequencySocketType(toSocket.type)) {
        if (fromSocket.type != toSocket.type) {
            if (errorMessage) {
                *errorMessage =
                    "Frequency sockets require an exact type match. Use Channel Split/Combine for images, "
                    "Fourier Transform/Inverse Fourier Transform for Channel and Spectrum, or "
                    "Separate/Recombine Spectrum for Magnitude and Phase.";
            }
            return false;
        }
        if (WouldCreateCycle(fromNodeId, fromSocketId, toNodeId, toSocketId)) {
            if (errorMessage) *errorMessage = "That connection would create a cycle.";
            return false;
        }
        return true;
    }

    if (fromSocket.type == SocketType::Channel ||
        toSocket.type == SocketType::Channel) {
        if (fromSocket.type != toSocket.type) {
            if (errorMessage) {
                *errorMessage =
                    "A Channel connects only to a declared Channel-capable input, including single-channel roles such as Mask. "
                    "Use Channel Split/Combine when converting between Image and Channel.";
            }
            return false;
        }
        if (WouldCreateCycle(fromNodeId, fromSocketId, toNodeId, toSocketId)) {
            if (errorMessage) *errorMessage = "That connection would create a cycle.";
            return false;
        }
        return true;
    }

    if (IsUniformOrResourceSocketType(fromSocket.type) || IsUniformOrResourceSocketType(toSocket.type)) {
        if (fromSocket.type != toSocket.type) {
            if (errorMessage) *errorMessage = "Typed value sockets require an exact type match; use an explicit conversion or broadcast node.";
            return false;
        }
        if (from->kind == NodeKind::Value) {
            const auto valueIssues =
                Stack::NodeMath::ValidateFirstClassValue(from->value.value);
            if (from->value.value.availability !=
                    Stack::NodeMath::ValueAvailability::Known ||
                !valueIssues.empty()) {
                if (errorMessage) {
                    *errorMessage = !valueIssues.empty()
                        ? "The connected typed value is invalid: " +
                            valueIssues.front().message
                        : (from->value.value.message.empty()
                            ? "The connected typed value is not known."
                            : from->value.value.message);
                }
                return false;
            }
        }
        if (to->kind != NodeKind::Compound && to->kind != NodeKind::Output &&
            !EditorNodeGraphDefinitions::AcceptsTypedParameterInput(*to, toSocketId, toSocket.type)) {
            if (errorMessage) *errorMessage = "This typed value input is not implemented by the selected node definition.";
            return false;
        }
        if (WouldCreateCycle(fromNodeId, fromSocketId, toNodeId, toSocketId)) {
            if (errorMessage) *errorMessage = "That connection would create a cycle.";
            return false;
        }
        return true;
    }

    if (fromSocket.type == SocketType::Mask || toSocket.type == SocketType::Mask ||
        fromSocket.type == SocketType::ScalarField || toSocket.type == SocketType::ScalarField) {
        const bool validScalarSource =
            ((from->kind == NodeKind::MaskGenerator ||
              from->kind == NodeKind::MaskCombine ||
              from->kind == NodeKind::MaskUtility ||
              from->kind == NodeKind::CustomMask ||
              from->kind == NodeKind::ImageToMask ||
              from->kind == NodeKind::RawDetailAutoMask ||
              from->kind == NodeKind::RawDetailFusion ||
              from->kind == NodeKind::FrequencyMask ||
              from->kind == NodeKind::MagnitudePhase) && fromSocketId == kMaskOutputSocketId) ||
            from->kind == NodeKind::Compound ||
            (from->kind == NodeKind::ChannelSplit && IsChannelSocketId(fromSocketId)) ||
            isFromScalarStream();

        const bool fromScalarField = fromSocket.type == SocketType::Mask || fromSocket.type == SocketType::ScalarField;
        const bool toScalarField = toSocket.type == SocketType::Mask || toSocket.type == SocketType::ScalarField;
        const bool isScalarToScalar = fromScalarField && toScalarField;
        const bool isScalarImageToScalar =
            fromSocket.type == SocketType::Image &&
            toScalarField &&
            isFromScalarStream();
        const bool isScalarToImage = fromScalarField && toSocket.type == SocketType::Image;

        if (isScalarToScalar || isScalarImageToScalar) {
            const bool validScalarTarget =
                to->kind == NodeKind::Compound || IsScalarTargetSocket(toNodeId, toSocketId);
            if (!validScalarSource || !validScalarTarget) {
                if (errorMessage) *errorMessage = "Scalar outputs can connect to masks, mix factors, scalar utilities, Data Math masks, channels, or RGBA outputs.";
                return false;
            }
        } else if (isScalarToImage) {
            const bool validImageTarget =
                (to->kind == NodeKind::Layer && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::Lut && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::TechnicalImage && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::Reformat && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::RawDetailAutoMask && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::RawDetailFusion && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::Mix && (toSocketId == kMixInputASocketId || toSocketId == kMixInputBSocketId)) ||
                (to->kind == NodeKind::ImageToMask && toSocketId == kImageToMaskInputSocketId) ||
                (to->kind == NodeKind::ChannelSplit && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::FrequencyFft && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::FrequencyIfft && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::SpectrumView && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::SpectrumAnalyzer && toSocketId == kImageInputSocketId) ||
                (to->kind == NodeKind::MagnitudePhase && toSocketId == kImageInputSocketId) ||
                to->kind == NodeKind::Compound ||
                (to->kind == NodeKind::SpectrumMath && (toSocketId == kMixInputASocketId || toSocketId == kMixInputBSocketId)) ||
                DataMathAllowsScalarToImageTarget(*to, toSocketId);
            if (!validScalarSource || !validImageTarget) {
                if (errorMessage) *errorMessage = "Scalar outputs can connect to image inputs, Reformat, Blend Images, Data Math, scalar converters, split nodes, or the output node.";
                return false;
            }
        } else {
            if (errorMessage) {
                *errorMessage = "A full image cannot connect directly to this single-channel input. Add an explicit Luminance Mask, Channel Split, or another extraction node.";
            }
            return false;
        }

        if (WouldCreateCycle(fromNodeId, fromSocketId, toNodeId, toSocketId)) {
            if (errorMessage) *errorMessage = "That connection would create a cycle.";
            return false;
        }
        return true;
    }

    if (fromSocket.type != SocketType::Image || toSocket.type != SocketType::Image) {
        if (errorMessage) *errorMessage = "Only image sockets can be connected in the render chain in this pass.";
        return false;
    }
    if (to->kind == NodeKind::Mfsr && IsMfsrInputSocketId(toSocketId)) {
        if (isFromScalarStream()) {
            if (errorMessage) *errorMessage = "MFSR inputs require full image frames, not scalar streams.";
            return false;
        }
        if (!CanConnectMfsrInputFamily(*this, fromNodeId, fromSocketId, toNodeId, toSocketId, errorMessage)) {
            return false;
        }
    }
    if (!IsRenderChainNode(*from) || !IsRenderChainNode(*to) || to->kind == NodeKind::Image || to->kind == NodeKind::RawSource || to->kind == NodeKind::RawDevelopment || to->kind == NodeKind::RawDecode || to->kind == NodeKind::RawDevelop || from->kind == NodeKind::Output) {
        if (errorMessage) *errorMessage = "Only image-producing nodes, layer, mix, and output nodes can be in the render chain.";
        return false;
    }
    if (to->kind == NodeKind::Mix && toSocketId != kMixInputASocketId && toSocketId != kMixInputBSocketId) {
        if (errorMessage) *errorMessage = "Image links must target Mix input A or B.";
        return false;
    }
    if (to->kind == NodeKind::Layer && toSocketId != kImageInputSocketId) {
        if (errorMessage) *errorMessage = "Image links must target the layer image input.";
        return false;
    }
    if (to->kind == NodeKind::Layer &&
        to->layerType == LayerType::ViewTransform &&
        toSocketId == kImageInputSocketId &&
        AnalyzeScenePath(*this, fromNodeId).hasViewTransform) {
        if (errorMessage) {
            *errorMessage =
                "This path already contains a View Transform. Turn Display Mapping off in the RAW View tab before adding a graph View Transform.";
        }
        return false;
    }
    if (to->kind == NodeKind::Lut && toSocketId != kImageInputSocketId) {
        if (errorMessage) *errorMessage = "Image links must target the LUT image input.";
        return false;
    }
    if (to->kind == NodeKind::TechnicalImage && toSocketId != kImageInputSocketId) {
        if (errorMessage) *errorMessage = "Image links must target the Technical Image node input.";
        return false;
    }
    if (to->kind == NodeKind::Reformat && toSocketId != kImageInputSocketId) {
        if (errorMessage) *errorMessage = "Image links must target the Reformat image input.";
        return false;
    }
    if (to->kind == NodeKind::RawDetailAutoMask && toSocketId != kImageInputSocketId) {
        if (errorMessage) *errorMessage = "Image links must target the RAW Detail Auto Mask image input.";
        return false;
    }
    if (to->kind == NodeKind::RawDetailFusion && toSocketId != kImageInputSocketId) {
        if (errorMessage) *errorMessage = "Image links must target the RAW Detail Fusion image input.";
        return false;
    }
    if (to->kind == NodeKind::HdrMerge &&
        toSocketId != kHdrMergeInput1SocketId &&
        toSocketId != kHdrMergeInput2SocketId &&
        toSocketId != kHdrMergeInput3SocketId) {
        if (errorMessage) *errorMessage = "Image links must target an HDR Merge image input.";
        return false;
    }
    if (to->kind == NodeKind::Mfsr && !IsMfsrInputSocketId(toSocketId)) {
        if (errorMessage) *errorMessage = "Image links must target an MFSR frame input.";
        return false;
    }
    if (to->kind == NodeKind::Output && toSocketId != kImageInputSocketId) {
        if (errorMessage) *errorMessage = "Image links must target the output image input.";
        return false;
    }
    if (to->kind == NodeKind::ImageToMask && toSocketId != kImageToMaskInputSocketId) {
        if (errorMessage) *errorMessage = "Image links must target the luminance image input.";
        return false;
    }
    if (to->kind == NodeKind::ChannelSplit && toSocketId != kImageInputSocketId) {
        if (errorMessage) *errorMessage = "Image links must target the split image input.";
        return false;
    }
    if ((to->kind == NodeKind::FrequencyFft ||
         to->kind == NodeKind::FrequencyIfft ||
         to->kind == NodeKind::SpectrumView ||
         to->kind == NodeKind::MagnitudePhase ||
         to->kind == NodeKind::SpectrumAnalyzer) &&
        toSocketId != kImageInputSocketId) {
        if (errorMessage) *errorMessage = "Image links must target the frequency node image input.";
        return false;
    }
    if (to->kind == NodeKind::SpectrumMath &&
        toSocketId != kMixInputASocketId &&
        toSocketId != kMixInputBSocketId) {
        if (errorMessage) *errorMessage = "Image links must target Spectrum Math input A or B.";
        return false;
    }
    if (to->kind == NodeKind::DataMath &&
        IsImageAverageNode(*to) &&
        isFromScalarStream()) {
        if (errorMessage) *errorMessage = "Average Images inputs require full image streams.";
        return false;
    }
    if (to->kind == NodeKind::DataMath && !DataMathAllowsFullImageTarget(*to, toSocketId)) {
        if (errorMessage) *errorMessage = IsScalarAverageNode(*to)
            ? "Average inputs require scalar masks or channel streams."
            : "Image links must target a Data Math input or the masked Base input.";
        return false;
    }
    if (to->kind != NodeKind::RawOperation && to->kind != NodeKind::Compound && to->kind != NodeKind::Layer && to->kind != NodeKind::Lut && to->kind != NodeKind::TechnicalImage && to->kind != NodeKind::Reformat && to->kind != NodeKind::RawDetailAutoMask && to->kind != NodeKind::RawDetailFusion && to->kind != NodeKind::HdrMerge && to->kind != NodeKind::Mfsr && to->kind != NodeKind::Output && to->kind != NodeKind::Mix && to->kind != NodeKind::ImageToMask && to->kind != NodeKind::ChannelSplit && to->kind != NodeKind::DataMath && to->kind != NodeKind::FrequencyFft && to->kind != NodeKind::FrequencyIfft && to->kind != NodeKind::SpectrumView && to->kind != NodeKind::SpectrumMath && to->kind != NodeKind::MagnitudePhase && to->kind != NodeKind::SpectrumAnalyzer) {
        if (errorMessage) *errorMessage = "Image links must target a compatible compound, layer, LUT, Technical Image, HDR Merge, MFSR, blend node, data math node, split node, scalar converter, or the output.";
        return false;
    }
    if (WouldCreateCycle(fromNodeId, fromSocketId, toNodeId, toSocketId)) {
        if (errorMessage) *errorMessage = "That connection would create a cycle.";
        return false;
    }

    return true;
}

bool Graph::IsScalarTargetSocket(int nodeId, const std::string& socketId) const {
    const Node* to = FindNode(nodeId);
    SocketDefinition toSocket;
    if (!to ||
        !FindSocket(nodeId, socketId, &toSocket) ||
        toSocket.direction != SocketDirection::Input ||
        (toSocket.type != SocketType::Mask && toSocket.type != SocketType::ScalarField)) {
        return false;
    }

    return to->kind == NodeKind::Compound ||
        (to->kind == NodeKind::RawOperation && (socketId == kMaskInputSocketId || socketId.rfind("gradient:", 0) == 0 || socketId.rfind("area:", 0) == 0)) ||
        (to->kind == NodeKind::Layer && socketId == kMaskInputSocketId) ||
        (to->kind == NodeKind::Lut && socketId == kMaskInputSocketId) ||
        (to->kind == NodeKind::RawDevelop && socketId == kMaskInputSocketId) ||
        (to->kind == NodeKind::RawDetailFusion && socketId == kMaskInputSocketId) ||
        (to->kind == NodeKind::Mix && socketId == kMixFactorSocketId) ||
        (to->kind == NodeKind::FieldMean && socketId == kReductionFieldInputSocketId) ||
        DataMathAllowsScalarTarget(*to, socketId) ||
        (to->kind == NodeKind::SpectrumMath && socketId == kMaskInputSocketId) ||
        (to->kind == NodeKind::MagnitudePhase && (socketId == "magnitude" || socketId == "phase")) ||
        (to->kind == NodeKind::MaskCombine &&
            (socketId == kMaskCombineInputASocketId || socketId == kMaskCombineInputBSocketId)) ||
        (to->kind == NodeKind::MaskUtility && socketId == kMaskUtilityInputSocketId) ||
        (to->kind == NodeKind::Lut && IsChannelSocketId(socketId)) ||
        (to->kind == NodeKind::ChannelCombine && IsChannelSocketId(socketId));
}

bool Graph::CanInsertImageToScalarExtractor(
    int fromNodeId,
    const std::string& fromSocketId,
    int toNodeId,
    const std::string& toSocketId,
    std::string* errorMessage) const {
    const Node* from = FindNode(fromNodeId);
    const Node* to = FindNode(toNodeId);
    SocketDefinition fromSocket;
    SocketDefinition toSocket;
    if (!from || !to || fromNodeId == toNodeId ||
        !FindSocket(fromNodeId, fromSocketId, &fromSocket) ||
        !FindSocket(toNodeId, toSocketId, &toSocket)) {
        if (errorMessage) *errorMessage = "Invalid socket connection.";
        return false;
    }
    if (fromSocket.direction != SocketDirection::Output || toSocket.direction != SocketDirection::Input) {
        if (errorMessage) *errorMessage = "That pin direction is not valid.";
        return false;
    }
    if (from->kind == NodeKind::Output || to->kind == NodeKind::Image ||
        to->kind == NodeKind::RawSource ||
        to->kind == NodeKind::RawDevelopment ||
        from->kind == NodeKind::Composite ||
        to->kind == NodeKind::Composite) {
        if (errorMessage) *errorMessage = "That pin direction is not valid.";
        return false;
    }
    if (fromSocket.type != SocketType::Image || IsScalarSocketStream(fromNodeId, fromSocketId)) {
        if (errorMessage) *errorMessage = "Only full image outputs need a scalar extractor.";
        return false;
    }
    if (!IsScalarTargetSocket(toNodeId, toSocketId)) {
        if (errorMessage) *errorMessage = "That input does not accept scalar fields.";
        return false;
    }
    if (!IsRenderChainNode(*from)) {
        if (errorMessage) *errorMessage = "Only render-chain image outputs can be extracted to scalar fields.";
        return false;
    }
    if (WouldCreateCycle(fromNodeId, fromSocketId, toNodeId, toSocketId)) {
        if (errorMessage) *errorMessage = "That connection would create a cycle.";
        return false;
    }
    return true;
}

bool Graph::TryConnectSockets(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId, std::string* errorMessage) {
    std::string resolvedToSocketId = toSocketId;
    if (!CanConnectSockets(fromNodeId, fromSocketId, toNodeId, toSocketId, &resolvedToSocketId, errorMessage)) {
        return false;
    }

    const Node* from = FindNode(fromNodeId);
    const Node* to = FindNode(toNodeId);
    SocketDefinition fromSocket;
    SocketDefinition toSocket;
    if (!from || !to || !FindSocket(fromNodeId, fromSocketId, &fromSocket) || !FindSocket(toNodeId, resolvedToSocketId, &toSocket)) {
        if (errorMessage) *errorMessage = "Invalid socket connection.";
        return false;
    }

    Link preparedLink;
    try {
        preparedLink =
            MakeSocketLink(
                fromNodeId,
                fromSocketId,
                toNodeId,
                resolvedToSocketId);
        if (m_Links.size() == m_Links.max_size()) {
            throw std::length_error("node graph link capacity exhausted");
        }
        // Every branch below removes an existing input before publishing this
        // replacement. Secure both the Link strings and vector capacity first
        // so allocation failure cannot leave the graph partially disconnected.
        m_Links.reserve(m_Links.size() + 1u);
    } catch (const std::bad_alloc&) {
        SetConnectionErrorNoThrow(
            errorMessage,
            "The connection could not be created because memory is exhausted.");
        return false;
    } catch (const std::length_error&) {
        SetConnectionErrorNoThrow(
            errorMessage,
            "The connection could not be created because the graph link limit was reached.");
        return false;
    }

    const bool toScope = to->kind == NodeKind::Scope && resolvedToSocketId == kScopeInputSocketId;
    if (toScope) {
        RemoveScopeLinksForNodeInput(toNodeId, resolvedToSocketId);
        m_Links.push_back(std::move(preparedLink));
        TouchStructure();
        return true;
    }

    const bool toPreview = to->kind == NodeKind::Preview && resolvedToSocketId == kPreviewInputSocketId;
    if (toPreview) {
        RemoveScopeLinksForNodeInput(toNodeId, resolvedToSocketId);
        m_Links.push_back(std::move(preparedLink));
        TouchStructure();
        return true;
    }

    if (fromSocket.type == SocketType::Raw || toSocket.type == SocketType::Raw) {
        RemoveRenderLinksForNodeInput(toNodeId, resolvedToSocketId);
        m_Links.push_back(std::move(preparedLink));
        TouchStructure();
        return true;
    }

    if (fromSocket.type == SocketType::Mask || toSocket.type == SocketType::Mask ||
        fromSocket.type == SocketType::ScalarField || toSocket.type == SocketType::ScalarField ||
        IsUniformOrResourceSocketType(fromSocket.type) || IsUniformOrResourceSocketType(toSocket.type)) {
        if (to->kind == NodeKind::Lut && IsChannelSocketId(resolvedToSocketId)) {
            RemoveLinksForNodeInput(toNodeId, kImageInputSocketId);
        } else if (to->kind == NodeKind::Lut &&
                   resolvedToSocketId == kImageInputSocketId) {
            RemoveLinksForNodeInput(toNodeId, "r");
            RemoveLinksForNodeInput(toNodeId, "g");
            RemoveLinksForNodeInput(toNodeId, "b");
            RemoveLinksForNodeInput(toNodeId, "a");
        }
        RemoveLinksForNodeInput(toNodeId, resolvedToSocketId);
        m_Links.push_back(std::move(preparedLink));
        TouchStructure();
        return true;
    }

    if (to->kind == NodeKind::Lut &&
        IsChannelSocketId(resolvedToSocketId)) {
        RemoveLinksForNodeInput(toNodeId, kImageInputSocketId);
    }
    if (to->kind == NodeKind::Lut && resolvedToSocketId == kImageInputSocketId) {
        RemoveLinksForNodeInput(toNodeId, "r");
        RemoveLinksForNodeInput(toNodeId, "g");
        RemoveLinksForNodeInput(toNodeId, "b");
        RemoveLinksForNodeInput(toNodeId, "a");
    }
    RemoveRenderLinksForNodeInput(toNodeId, resolvedToSocketId);
    if (from->kind == NodeKind::Image) {
        ActivateImageNode(fromNodeId);
    }
    m_Links.push_back(std::move(preparedLink));
    TouchStructure();
    return true;
}

bool Graph::RemoveNode(int nodeId) {
    Node* node = FindNode(nodeId);
    if (!node) {
        return false;
    }

    if (node->role == Stack::GraphModel::NodeRole::OriginalImage ||
        node->role == Stack::GraphModel::NodeRole::CurrentImage ||
        node->role == Stack::GraphModel::NodeRole::LayerResult) return false;

    if (node->kind == NodeKind::Layer) {
        return false;
    }

    const bool wasActiveImage = node->kind == NodeKind::Image && node->id == m_ActiveImageNodeId;
    m_Nodes.erase(
        std::remove_if(m_Nodes.begin(), m_Nodes.end(), [nodeId](const Node& item) {
            return item.id == nodeId;
        }),
        m_Nodes.end());
    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [nodeId](const Link& link) {
            return link.fromNodeId == nodeId || link.toNodeId == nodeId;
        }),
        m_Links.end());
    m_SelectedNodeIds.erase(
        std::remove(m_SelectedNodeIds.begin(), m_SelectedNodeIds.end(), nodeId),
        m_SelectedNodeIds.end());
    m_SelectedNodeId = m_SelectedNodeIds.empty() ? -1 : m_SelectedNodeIds.back();
    if (wasActiveImage) {
        m_ActiveImageNodeId = -1;
    }
    RefreshPrimaryOutputNode();
    TouchStructure();
    return true;
}

bool Graph::SetOutputNodeEnabled(int nodeId, bool enabled) {
    Node* node = FindNode(nodeId);
    if (!node || node->kind != NodeKind::Output) {
        return false;
    }
    if (node->outputEnabled == enabled) {
        return true;
    }
    node->outputEnabled = enabled;
    TouchStructure();
    return true;
}

bool Graph::RemoveLink(int fromNodeId, int toNodeId) {
    const Node* from = FindNode(fromNodeId);
    const Node* to = FindNode(toNodeId);
    return RemoveLink(
        fromNodeId,
        from ? DefaultOutputSocket(*from) : std::string(),
        toNodeId,
        to ? DefaultInputSocket(*to) : std::string());
}

bool Graph::RemoveLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) {
    const auto oldSize = m_Links.size();
    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [&](const Link& link) {
            return link.fromNodeId == fromNodeId &&
                link.fromSocketId == fromSocketId &&
                link.toNodeId == toNodeId &&
                link.toSocketId == toSocketId;
        }),
        m_Links.end());
    if (m_HasSelectedLink &&
        m_SelectedLink.fromNodeId == fromNodeId &&
        m_SelectedLink.fromSocketId == fromSocketId &&
        m_SelectedLink.toNodeId == toNodeId &&
        m_SelectedLink.toSocketId == toSocketId) {
        m_HasSelectedLink = false;
    }
    if (fromNodeId == m_ActiveImageNodeId && FindOutputLink(fromNodeId, kImageOutputSocketId) == nullptr) {
        m_ActiveImageNodeId = -1;
    }
    if (oldSize != m_Links.size()) {
        TouchStructure();
        return true;
    }
    return false;
}

bool Graph::RemoveSelectedLink() {
    const Link* selected = GetSelectedLink();
    if (!selected) {
        return false;
    }
    const Link copy = *selected;
    return RemoveLink(copy.fromNodeId, copy.fromSocketId, copy.toNodeId, copy.toSocketId);
}

bool Graph::HasLink(int fromNodeId, int toNodeId) const {
    const Node* from = FindNode(fromNodeId);
    const Node* to = FindNode(toNodeId);
    return HasLink(
        fromNodeId,
        from ? DefaultOutputSocket(*from) : std::string(),
        toNodeId,
        to ? DefaultInputSocket(*to) : std::string());
}

bool Graph::HasLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) const {
    bool found = false;
    ForEachOutgoingLink(fromNodeId, [&](const Link& link) {
        if (!found &&
            link.fromSocketId == fromSocketId &&
            link.toNodeId == toNodeId &&
            link.toSocketId == toSocketId) {
            found = true;
        }
    });
    return found;
}

bool Graph::WouldCreateCycle(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) const {
    // Walk the dependencies of the proposed source output. A cycle exists
    // precisely when that output already depends on the destination input.
    std::unordered_set<std::string> visited;
    std::vector<std::pair<int, std::string>> pending{{fromNodeId, fromSocketId}};
    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        if (!visited.insert(std::to_string(current.first) + "/" + current.second).second) continue;
        const auto* node = FindNode(current.first);
        if (!node) continue;
        if (current.first == toNodeId &&
            EditorNodeGraphDefinitions::OutputDependsOnInput(*this, *node, current.second, toSocketId)) return true;
        for (const auto& link : m_Links) {
            if (link.toNodeId != current.first) continue;
            // Replacing a single-input binding removes its old edge.
            if (link.toNodeId == toNodeId && link.toSocketId == toSocketId) continue;
            if (EditorNodeGraphDefinitions::OutputDependsOnInput(*this, *node, current.second, link.toSocketId))
                pending.emplace_back(link.fromNodeId, link.fromSocketId);
        }
    }
    return false;
}

void Graph::RemoveRenderLinksForNodeInput(int nodeId, const std::string& socketId) {
    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [this, nodeId, &socketId](const Link& link) {
            return link.toNodeId == nodeId && link.toSocketId == socketId && IsRenderLink(link);
        }),
        m_Links.end());
}

void Graph::RemoveRenderLinksForNodeOutput(int nodeId, const std::string& socketId) {
    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [this, nodeId, &socketId](const Link& link) {
            return link.fromNodeId == nodeId && link.fromSocketId == socketId && IsRenderLink(link);
        }),
        m_Links.end());
}

void Graph::RemoveScopeLinksForNodeInput(int nodeId, const std::string& socketId) {
    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [this, nodeId, &socketId](const Link& link) {
            return link.toNodeId == nodeId && link.toSocketId == socketId && GetLinkRole(link) == LinkRole::Scope;
        }),
        m_Links.end());
}

void Graph::RemoveLinksForNodeInput(int nodeId, const std::string& socketId) {
    m_Links.erase(
        std::remove_if(m_Links.begin(), m_Links.end(), [nodeId, &socketId](const Link& link) {
            return link.toNodeId == nodeId && link.toSocketId == socketId;
        }),
        m_Links.end());
}

void Graph::ActivateImageNode(int nodeId) {
    m_ActiveImageNodeId = nodeId;
}

void Graph::RefreshPrimaryOutputNode() {
    if (m_OutputNodeId > 0 && FindNode(m_OutputNodeId) && FindNode(m_OutputNodeId)->kind == NodeKind::Output) {
        return;
    }
    m_OutputNodeId = -1;
    for (const Node& node : m_Nodes) {
        if (node.kind == NodeKind::Output) {
            m_OutputNodeId = node.id;
            return;
        }
    }
}

} // namespace EditorNodeGraph
