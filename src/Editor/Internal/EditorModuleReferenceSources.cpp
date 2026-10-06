#include "Editor/EditorModule.h"
#include "Project/GraphImagePayload.h"

#include "Raw/RawImageData.h"
#include "Utils/HashUtils.h"
#include "Utils/PixelBufferUtils.h"
#include "Utils/SharedPixelBuffer.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace {

const EditorNodeGraph::Node* FindUpstreamRawSourceNode(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& rawDomainNode) {
    const EditorNodeGraph::Link* rawInput = graph.FindInputLink(rawDomainNode.id, EditorNodeGraph::kRawInputSocketId);
    std::unordered_set<int> visited;
    while (rawInput) {
        if (!visited.insert(rawInput->fromNodeId).second) {
            return nullptr;
        }

        const EditorNodeGraph::Node* upstream = graph.FindNode(rawInput->fromNodeId);
        if (!upstream) {
            return nullptr;
        }
        if (upstream->kind == EditorNodeGraph::NodeKind::RawSource) {
            return upstream;
        }
        if (upstream->kind != EditorNodeGraph::NodeKind::RawNeuralDenoise) {
            return nullptr;
        }
        rawInput = graph.FindInputLink(upstream->id, EditorNodeGraph::kRawInputSocketId);
    }
    return nullptr;
}

int ResolveRawDevelopOrientation(const Raw::RawMetadata& metadata, const Raw::RawDevelopSettings& settings) {
    int exifRotationSteps = 0;
    if (metadata.orientation == 3) exifRotationSteps = 2;
    else if (metadata.orientation == 5) exifRotationSteps = 1;
    else if (metadata.orientation == 6) exifRotationSteps = 1;
    else if (metadata.orientation == 8) exifRotationSteps = 3;

    int manualRotationSteps = 0;
    if (settings.rotationDegrees == 90) manualRotationSteps = 3;
    else if (settings.rotationDegrees == 180) manualRotationSteps = 2;
    else if (settings.rotationDegrees == 270) manualRotationSteps = 1;
    const int totalRotationSteps = (exifRotationSteps + manualRotationSteps) % 4;

    int effectiveOrientation = metadata.orientation;
    if (metadata.orientation <= 1 || metadata.orientation == 3 || metadata.orientation == 6 || metadata.orientation == 8) {
        if (totalRotationSteps == 0) effectiveOrientation = 1;
        else if (totalRotationSteps == 1) effectiveOrientation = 6;
        else if (totalRotationSteps == 2) effectiveOrientation = 3;
        else if (totalRotationSteps == 3) effectiveOrientation = 8;
    } else if (metadata.orientation == 2 || metadata.orientation == 4 || metadata.orientation == 5 || metadata.orientation == 7) {
        int baseSteps = 0;
        if (metadata.orientation == 2) baseSteps = 0;
        else if (metadata.orientation == 7) baseSteps = 1;
        else if (metadata.orientation == 4) baseSteps = 2;
        else if (metadata.orientation == 5) baseSteps = 3;

        const int finalSteps = (baseSteps + manualRotationSteps) % 4;
        if (finalSteps == 0) effectiveOrientation = 2;
        else if (finalSteps == 1) effectiveOrientation = 7;
        else if (finalSteps == 2) effectiveOrientation = 4;
        else if (finalSteps == 3) effectiveOrientation = 5;
    }
    return effectiveOrientation;
}

} // namespace

SharedPixelBuffer EditorModule::EnsureSharedImagePixels(const EditorNodeGraph::ImagePayload& payload) const {
    return Stack::Project::EnsureSharedImagePixels(payload);
}

RenderGraphImagePayload EditorModule::BuildRenderImagePayload(const EditorNodeGraph::ImagePayload& payload) const {
    return Stack::Project::BuildRenderImagePayload(payload);
}
SharedPixelBuffer EditorModule::MakeSharedSourcePixelBufferCopy(const std::vector<unsigned char>& pixels) const {
    return MakeSharedPixelBufferCopy(pixels);
}

bool EditorModule::TryCopyImageNodeSharedPixels(
    int sourceNodeId,
    SharedPixelBuffer& outPixels,
    int& outW,
    int& outH,
    int& outChannels) const {
    const EditorNodeGraph::Node* sourceNode = m_Project->graph.FindNode(sourceNodeId);
    if (!sourceNode) {
        return false;
    }

    outPixels = {};

    if (sourceNode->kind == EditorNodeGraph::NodeKind::RawDecode ||
        sourceNode->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        const EditorNodeGraph::Node* rawSourceNode = FindUpstreamRawSourceNode(m_Project->graph, *sourceNode);
        const Raw::RawMetadata emptyMetadata;
        const Raw::RawMetadata& metadata = rawSourceNode ? rawSourceNode->rawSource.metadata : emptyMetadata;

        const int visibleWidth = metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth;
        const int visibleHeight = metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight;

        const Raw::RawDevelopSettings& rawSettings =
            sourceNode->kind == EditorNodeGraph::NodeKind::RawDecode
                ? sourceNode->rawDecode.settings
                : sourceNode->rawDevelop.settings;
        const int effectiveOrientation = ResolveRawDevelopOrientation(metadata, rawSettings);
        const bool swaps = (effectiveOrientation == 5 || effectiveOrientation == 6 || effectiveOrientation == 7 || effectiveOrientation == 8);
        if (rawSettings.rotateToFitFrame) {
            outW = visibleWidth;
            outH = visibleHeight;
        } else {
            outW = swaps ? visibleHeight : visibleWidth;
            outH = swaps ? visibleWidth : visibleHeight;
        }
        outChannels = 4;
        return outW > 0 && outH > 0;
    }

    if (sourceNode->kind == EditorNodeGraph::NodeKind::RawSource) {
        outW = Raw::DisplayWidth(sourceNode->rawSource.metadata);
        outH = Raw::DisplayHeight(sourceNode->rawSource.metadata);
        outChannels = 4;
        return outW > 0 && outH > 0;
    }

    if (sourceNode->kind != EditorNodeGraph::NodeKind::Image ||
        sourceNode->image.width <= 0 ||
        sourceNode->image.height <= 0 ||
        !Stack::PixelBuffer::IsSupportedInterleavedChannelCount(
            sourceNode->image.channels)) {
        return false;
    }

    outPixels = EnsureSharedImagePixels(sourceNode->image);
    outW = sourceNode->image.width;
    outH = sourceNode->image.height;
    outChannels = std::max(1, sourceNode->image.channels);
    return !outPixels.empty();
}

bool EditorModule::TryResolveReferenceSourceBuffer(
    int nodeId,
    const std::string& socketId,
    SharedPixelBuffer& outPixels,
    int& outW,
    int& outH,
    int& outChannels) const {
    const int referenceSourceNodeId = m_Project->graph.ResolveReferenceSourceNodeId(nodeId, socketId);
    return TryCopyImageNodeSharedPixels(referenceSourceNodeId, outPixels, outW, outH, outChannels);
}

bool EditorModule::TryResolveReferenceSourceBufferForOutput(
    int outputNodeId,
    SharedPixelBuffer& outPixels,
    int& outW,
    int& outH,
    int& outChannels) const {
    const int referenceSourceNodeId = m_Project->graph.ResolveReferenceSourceNodeIdForOutput(outputNodeId);
    return TryCopyImageNodeSharedPixels(referenceSourceNodeId, outPixels, outW, outH, outChannels);
}

bool EditorModule::TryCopyImageNodePixels(int sourceNodeId, std::vector<unsigned char>& outPixels, int& outW, int& outH, int& outChannels) const {
    const EditorNodeGraph::Node* sourceNode = m_Project->graph.FindNode(sourceNodeId);
    if (!sourceNode) {
        return false;
    }

    if (sourceNode->kind == EditorNodeGraph::NodeKind::RawDecode ||
        sourceNode->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        const EditorNodeGraph::Node* rawSourceNode = FindUpstreamRawSourceNode(m_Project->graph, *sourceNode);
        const Raw::RawMetadata emptyMetadata;
        const Raw::RawMetadata& metadata = rawSourceNode ? rawSourceNode->rawSource.metadata : emptyMetadata;

        const int visibleWidth = metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth;
        const int visibleHeight = metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight;

        const Raw::RawDevelopSettings& rawSettings =
            sourceNode->kind == EditorNodeGraph::NodeKind::RawDecode
                ? sourceNode->rawDecode.settings
                : sourceNode->rawDevelop.settings;
        const int effectiveOrientation = ResolveRawDevelopOrientation(metadata, rawSettings);
        const bool swaps = (effectiveOrientation == 5 || effectiveOrientation == 6 || effectiveOrientation == 7 || effectiveOrientation == 8);
        if (rawSettings.rotateToFitFrame) {
            outW = visibleWidth;
            outH = visibleHeight;
        } else {
            outW = swaps ? visibleHeight : visibleWidth;
            outH = swaps ? visibleWidth : visibleHeight;
        }
        outChannels = 4;
        outPixels.clear();
        return outW > 0 && outH > 0;
    }

    if (sourceNode->kind == EditorNodeGraph::NodeKind::RawSource) {
        outW = Raw::DisplayWidth(sourceNode->rawSource.metadata);
        outH = Raw::DisplayHeight(sourceNode->rawSource.metadata);
        outChannels = 4;
        outPixels.clear();
        return outW > 0 && outH > 0;
    }

    if (sourceNode->kind != EditorNodeGraph::NodeKind::Image ||
        !Stack::PixelBuffer::HasCompletePixelBuffer(
            sourceNode->image.pixels.size(),
            sourceNode->image.width,
            sourceNode->image.height,
            sourceNode->image.channels) ||
        sourceNode->image.width <= 0 ||
        sourceNode->image.height <= 0) {
        return false;
    }

    outPixels = sourceNode->image.pixels;
    outW = sourceNode->image.width;
    outH = sourceNode->image.height;
    outChannels = std::max(1, sourceNode->image.channels);
    return true;
}

bool EditorModule::TryResolveReferenceSourcePixels(
    int nodeId,
    const std::string& socketId,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH,
    int& outChannels) const {
    const int referenceSourceNodeId = m_Project->graph.ResolveReferenceSourceNodeId(nodeId, socketId);
    return TryCopyImageNodePixels(referenceSourceNodeId, outPixels, outW, outH, outChannels);
}

bool EditorModule::TryResolveReferenceSourcePixelsForOutput(
    int outputNodeId,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH,
    int& outChannels) const {
    const int referenceSourceNodeId = m_Project->graph.ResolveReferenceSourceNodeIdForOutput(outputNodeId);
    return TryCopyImageNodePixels(referenceSourceNodeId, outPixels, outW, outH, outChannels);
}

bool EditorModule::TryResolveReferenceSourceDimensions(
    int nodeId,
    const std::string& socketId,
    int& outW,
    int& outH) const {
    outW = 0;
    outH = 0;

    const int referenceSourceNodeId = m_Project->graph.ResolveReferenceSourceNodeId(nodeId, socketId);
    const EditorNodeGraph::Node* sourceNode = m_Project->graph.FindNode(referenceSourceNodeId);
    if (!sourceNode) {
        return false;
    }

    switch (sourceNode->kind) {
        case EditorNodeGraph::NodeKind::Image:
            outW = sourceNode->image.width;
            outH = sourceNode->image.height;
            return outW > 0 && outH > 0;
        case EditorNodeGraph::NodeKind::RawSource:
            outW = Raw::DisplayWidth(sourceNode->rawSource.metadata);
            outH = Raw::DisplayHeight(sourceNode->rawSource.metadata);
            return outW > 0 && outH > 0;
        case EditorNodeGraph::NodeKind::RawDecode:
        case EditorNodeGraph::NodeKind::RawDevelop: {
            const EditorNodeGraph::Node* rawSourceNode = FindUpstreamRawSourceNode(m_Project->graph, *sourceNode);
            const Raw::RawMetadata emptyMetadata;
            const Raw::RawMetadata& metadata = rawSourceNode ? rawSourceNode->rawSource.metadata : emptyMetadata;
            const int visibleWidth = metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth;
            const int visibleHeight = metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight;
            const Raw::RawDevelopSettings& rawSettings =
                sourceNode->kind == EditorNodeGraph::NodeKind::RawDecode
                    ? sourceNode->rawDecode.settings
                    : sourceNode->rawDevelop.settings;
            const int effectiveOrientation = ResolveRawDevelopOrientation(metadata, rawSettings);
            const bool swaps = (effectiveOrientation == 5 || effectiveOrientation == 6 || effectiveOrientation == 7 || effectiveOrientation == 8);
            if (rawSettings.rotateToFitFrame) {
                outW = visibleWidth;
                outH = visibleHeight;
            } else {
                outW = swaps ? visibleHeight : visibleWidth;
                outH = swaps ? visibleWidth : visibleHeight;
            }
            return outW > 0 && outH > 0;
        }
        default: {
            std::vector<unsigned char> pixels;
            int channels = 4;
            return TryCopyImageNodePixels(referenceSourceNodeId, pixels, outW, outH, channels);
        }
    }
}
