#pragma once

#include "NodeMath/TechnicalImageMath.h"

#include <string>
#include <vector>

namespace Stack::NodeMath {

enum class SemanticImageNodeKind {
    Source,
    Identity,
    Geometry,
    TechnicalOperation,
    DeclaredColorOutput,
    StraightSourceOver,
    PremultipliedSourceOver,
    DirectOutput
};

struct SemanticImageNode {
    std::string identity;
    SemanticImageNodeKind kind = SemanticImageNodeKind::Identity;
    ValueDescriptor sourceDescriptor;
    TechnicalImageOperation technicalOperation = TechnicalImageOperation::Exposure;
    float exposureValue = 0.0f;
    SpatialDescriptor geometryOutputSpatial;
    ColorIdentity declaredColor;
    TransferDescriptor declaredTransfer;
    ReferenceState declaredReference = ReferenceState::Display;
    std::string declaredOperationIdentity;
};

struct SemanticImageEdge {
    std::string identity;
    std::string sourceNodeIdentity;
    std::string destinationNodeIdentity;
    std::string destinationPort; // "image", "source", or "backdrop"
};

struct SemanticNodeOutput {
    std::string nodeIdentity;
    ValueDescriptor descriptor;
    std::string descriptorIdentity;
    bool executable = true;
};

struct SemanticEdgeState {
    SemanticImageEdge edge;
    ValueDescriptor descriptor;
    std::string descriptorIdentity;
};

struct SemanticAnalysisResult {
    std::vector<SemanticNodeOutput> nodeOutputs;
    std::vector<SemanticEdgeState> edges;
    std::vector<Diagnostic> diagnostics;
    std::string semanticFingerprint;
    bool executable = true;
};

// Pure analysis: neither the authored graph nor image pixels are mutated.
SemanticAnalysisResult AnalyzeSemanticImageGraph(
    const std::vector<SemanticImageNode>& nodes,
    const std::vector<SemanticImageEdge>& edges);

const SemanticNodeOutput* FindSemanticNodeOutput(
    const SemanticAnalysisResult& result,
    const std::string& nodeIdentity);
const SemanticEdgeState* FindSemanticEdgeState(
    const SemanticAnalysisResult& result,
    const std::string& edgeIdentity);

} // namespace Stack::NodeMath
