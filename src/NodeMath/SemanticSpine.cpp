#include "NodeMath/SemanticSpine.h"

#include "NodeMath/DescriptorSerialization.h"
#include "NodeMath/OutputInspection.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace Stack::NodeMath {
namespace {

Diagnostic MakeGraphDiagnostic(
    const char* rule,
    DiagnosticSeverity severity,
    const std::string& authored,
    const std::string& affected,
    std::string message,
    std::string repair = {}) {
    Diagnostic diagnostic;
    diagnostic.ruleId = rule;
    diagnostic.stage = DiagnosticStage::Semantic;
    diagnostic.severity = severity;
    diagnostic.authoredSourceIdentity = authored;
    diagnostic.affectedIdentity = affected;
    diagnostic.semanticFingerprint = Sha256ContentIdentity(
        std::string(rule) + "\n" + authored + "\n" + affected + "\n" + message);
    diagnostic.message = std::move(message);
    diagnostic.suggestedRepair = std::move(repair);
    return diagnostic;
}

bool IsHard(DiagnosticSeverity severity) {
    return severity == DiagnosticSeverity::HardError ||
        severity == DiagnosticSeverity::RuntimeFault;
}

std::string GraphFingerprint(
    const std::vector<SemanticNodeOutput>& outputs,
    const std::vector<SemanticEdgeState>& edges,
    const std::vector<Diagnostic>& diagnostics) {
    std::ostringstream content;
    content << "stack.semantic-image-graph.v1\n";
    for (const SemanticNodeOutput& output : outputs) {
        content << "node\t" << output.nodeIdentity << '\t'
            << output.descriptorIdentity << '\t' << output.executable << '\n';
    }
    for (const SemanticEdgeState& edge : edges) {
        content << "edge\t" << edge.edge.identity << '\t'
            << edge.edge.sourceNodeIdentity << '\t'
            << edge.edge.destinationNodeIdentity << '\t'
            << edge.edge.destinationPort << '\t'
            << edge.descriptorIdentity << '\n';
    }
    for (const Diagnostic& diagnostic : diagnostics) {
        content << "diagnostic\t" << diagnostic.semanticFingerprint << '\n';
    }
    return Sha256ContentIdentity(content.str());
}

ValueDescriptor FailureDescriptor() {
    return MakeUnknownDescriptor(LogicalValueType::Failure);
}

void RebindDiagnostic(
    Diagnostic& diagnostic,
    const std::string& nodeIdentity) {
    diagnostic.authoredSourceIdentity = nodeIdentity;
    diagnostic.affectedIdentity = nodeIdentity;
    diagnostic.semanticFingerprint = Sha256ContentIdentity(
        diagnostic.ruleId + "\n" + nodeIdentity + "\n" + diagnostic.message);
}

} // namespace

SemanticAnalysisResult AnalyzeSemanticImageGraph(
    const std::vector<SemanticImageNode>& nodes,
    const std::vector<SemanticImageEdge>& edges) {
    SemanticAnalysisResult result;
    std::map<std::string, const SemanticImageNode*> nodesById;
    std::map<std::string, std::vector<const SemanticImageEdge*>> incoming;
    std::map<std::string, std::vector<std::string>> outgoing;
    std::map<std::string, int> indegree;
    std::set<std::string> edgeIds;

    for (const SemanticImageNode& node : nodes) {
        if (node.identity.empty() || !nodesById.emplace(node.identity, &node).second) {
            result.diagnostics.push_back(MakeGraphDiagnostic(
                "nmr.connection.missing-definition", DiagnosticSeverity::HardError,
                node.identity, node.identity,
                "Semantic node identities must be non-empty and unique."));
            continue;
        }
        indegree[node.identity] = 0;
    }
    for (const SemanticImageEdge& edge : edges) {
        if (edge.identity.empty() || !edgeIds.insert(edge.identity).second) {
            result.diagnostics.push_back(MakeGraphDiagnostic(
                "nmr.connection.unknown-port", DiagnosticSeverity::HardError,
                edge.identity, edge.identity,
                "Semantic edge identities must be non-empty and unique."));
            continue;
        }
        if (!nodesById.count(edge.sourceNodeIdentity) ||
            !nodesById.count(edge.destinationNodeIdentity)) {
            result.diagnostics.push_back(MakeGraphDiagnostic(
                "nmr.connection.missing-definition", DiagnosticSeverity::HardError,
                edge.identity, edge.identity,
                "A semantic edge refers to a node that is not present."));
            continue;
        }
        incoming[edge.destinationNodeIdentity].push_back(&edge);
        outgoing[edge.sourceNodeIdentity].push_back(edge.destinationNodeIdentity);
        ++indegree[edge.destinationNodeIdentity];
    }

    std::set<std::string> ready;
    for (const auto& [identity, count] : indegree) if (count == 0) ready.insert(identity);
    std::map<std::string, SemanticNodeOutput> outputs;
    std::size_t visited = 0;
    while (!ready.empty()) {
        const std::string nodeId = *ready.begin();
        ready.erase(ready.begin());
        ++visited;
        const SemanticImageNode& node = *nodesById.at(nodeId);
        std::map<std::string, const SemanticNodeOutput*> inputByPort;
        for (const SemanticImageEdge* edge : incoming[nodeId]) {
            const auto found = outputs.find(edge->sourceNodeIdentity);
            if (found != outputs.end()) inputByPort[edge->destinationPort] = &found->second;
        }

        SemanticNodeOutput output;
        output.nodeIdentity = nodeId;
        output.descriptor = FailureDescriptor();
        auto requireInput = [&](const char* port) -> const SemanticNodeOutput* {
            const auto found = inputByPort.find(port);
            if (found != inputByPort.end()) return found->second;
            result.diagnostics.push_back(MakeGraphDiagnostic(
                "nmr.connection.missing-input", DiagnosticSeverity::HardError,
                nodeId, nodeId, std::string("Required semantic input '") + port + "' is disconnected."));
            output.executable = false;
            return nullptr;
        };

        switch (node.kind) {
        case SemanticImageNodeKind::Source:
            output.descriptor = node.sourceDescriptor;
            if (!ValidateDescriptor(output.descriptor).empty()) {
                output.executable = false;
                result.diagnostics.push_back(MakeGraphDiagnostic(
                    "nmr.semantic.metadata-missing", DiagnosticSeverity::HardError,
                    nodeId, nodeId, "Source descriptor is structurally invalid."));
            } else if (output.descriptor.color.state == KnowledgeState::Unknown) {
                result.diagnostics.push_back(MakeGraphDiagnostic(
                    "nmr.semantic.source-color-unknown", DiagnosticSeverity::Information,
                    nodeId, nodeId,
                    "The source is untagged, so its color identity and transfer remain Unknown."));
            }
            break;
        case SemanticImageNodeKind::Identity:
        case SemanticImageNodeKind::DirectOutput: {
            const SemanticNodeOutput* input = requireInput("image");
            if (input) {
                output.descriptor = input->descriptor;
                output.executable = input->executable;
                if (node.kind == SemanticImageNodeKind::DirectOutput) {
                    OutputInspectionPolicy policy =
                        EvaluateOutputInspectionPolicy(output.descriptor);
                    output.executable = output.executable && policy.executable;
                    for (Diagnostic& diagnostic : policy.diagnostics) {
                        RebindDiagnostic(diagnostic, nodeId);
                        result.diagnostics.push_back(std::move(diagnostic));
                    }
                }
            }
            break;
        }
        case SemanticImageNodeKind::Geometry: {
            const SemanticNodeOutput* input = requireInput("image");
            if (input) {
                output.descriptor = input->descriptor;
                output.executable = input->executable;
                if (node.geometryOutputSpatial.kind != SpatialExtentKind::Finite ||
                    node.geometryOutputSpatial.fullWindow.width <= 0 ||
                    node.geometryOutputSpatial.fullWindow.height <= 0) {
                    output.executable = false;
                    result.diagnostics.push_back(MakeGraphDiagnostic(
                        "nmr.semantic.extent-policy-missing", DiagnosticSeverity::HardError,
                        nodeId, nodeId,
                        "Geometry output requires an explicit finite full/data window."));
                } else {
                    SpatialDescriptor outputSpatial = node.geometryOutputSpatial;
                    if (input->descriptor.spatial.state == KnowledgeState::Known &&
                        input->descriptor.spatial.value.kind == SpatialExtentKind::Finite) {
                        const SpatialDescriptor& inputSpatial = input->descriptor.spatial.value;
                        outputSpatial.fullWindow.x = inputSpatial.fullWindow.x;
                        outputSpatial.fullWindow.y = inputSpatial.fullWindow.y;
                        outputSpatial.dataWindow = outputSpatial.fullWindow;
                        outputSpatial.rasterOrigin = inputSpatial.rasterOrigin;
                        outputSpatial.pixelAspect = inputSpatial.pixelAspect;
                    }
                    output.descriptor.spatial =
                        SemanticField<SpatialDescriptor>::Known(outputSpatial);
                    output.descriptor.provenance =
                        SemanticField<ProvenanceDescriptor>::Known({
                            ProvenanceKind::Derived, input->descriptorIdentity,
                            "geometry.reformat.v1"
                        });
                }
            }
            break;
        }
        case SemanticImageNodeKind::TechnicalOperation: {
            const SemanticNodeOutput* input = requireInput("image");
            if (input) {
                std::vector<Diagnostic> diagnostics;
                output.descriptor = DescribeTechnicalImageOutput(
                    node.technicalOperation, input->descriptor, node.exposureValue, diagnostics);
                output.executable = input->executable;
                for (Diagnostic& diagnostic : diagnostics) {
                    RebindDiagnostic(diagnostic, nodeId);
                    if (IsHard(diagnostic.severity)) output.executable = false;
                    result.diagnostics.push_back(std::move(diagnostic));
                }
            }
            break;
        }
        case SemanticImageNodeKind::DeclaredColorOutput: {
            const SemanticNodeOutput* input = requireInput("image");
            if (input) {
                output.descriptor = input->descriptor;
                output.executable = input->executable;
                output.descriptor.color =
                    SemanticField<ColorIdentity>::Known(node.declaredColor);
                output.descriptor.transfer =
                    SemanticField<TransferDescriptor>::Known(node.declaredTransfer);
                output.descriptor.reference =
                    SemanticField<ReferenceState>::Known(node.declaredReference);
                output.descriptor.range = SemanticField<NumericRange>::Known({
                    0.0, 1.0, false, false, NonFinitePolicy::Forbidden
                });
                output.descriptor.provenance =
                    SemanticField<ProvenanceDescriptor>::Known({
                        ProvenanceKind::Converted,
                        input->descriptorIdentity,
                        node.declaredOperationIdentity.empty()
                            ? "color.declared-output.v1"
                            : node.declaredOperationIdentity
                    });
            }
            break;
        }
        case SemanticImageNodeKind::StraightSourceOver:
        case SemanticImageNodeKind::PremultipliedSourceOver: {
            const SemanticNodeOutput* source = requireInput("source");
            const SemanticNodeOutput* backdrop = requireInput("backdrop");
            if (source && backdrop) {
                output.descriptor = backdrop->descriptor;
                output.executable = source->executable && backdrop->executable;
                const AlphaMode expected = node.kind == SemanticImageNodeKind::StraightSourceOver
                    ? AlphaMode::Straight : AlphaMode::Premultiplied;
                const auto alphaMatches = [expected](const ValueDescriptor& value) {
                    return value.alpha.state == KnowledgeState::Known && value.alpha.value == expected;
                };
                if (!alphaMatches(source->descriptor) || !alphaMatches(backdrop->descriptor)) {
                    result.diagnostics.push_back(MakeGraphDiagnostic(
                        "nmr.semantic.alpha-formula-mismatch", DiagnosticSeverity::Warning,
                        nodeId, nodeId,
                        "Source Over will execute its explicitly selected alpha formula, but one or both inputs declare a different alpha representation.",
                        expected == AlphaMode::Straight
                            ? "Add Unpremultiply where needed, or select Premultiplied Source Over."
                            : "Add Premultiply where needed, or select Straight Source Over."));
                }
                if (source->descriptor.color != backdrop->descriptor.color ||
                    source->descriptor.transfer != backdrop->descriptor.transfer) {
                    result.diagnostics.push_back(MakeGraphDiagnostic(
                        "nmr.semantic.color-mismatch", DiagnosticSeverity::Warning,
                        nodeId, nodeId,
                        "Source Over will execute numerically even though its inputs declare different or Unknown color states.",
                        "Add explicit color/transfer conversions if matching color math is intended."));
                }
                output.descriptor.alpha = SemanticField<AlphaMode>::Known(expected);
                const std::string sourceIdentity = output.descriptor.provenance.state == KnowledgeState::Known
                    ? output.descriptor.provenance.value.sourceIdentity : std::string();
                output.descriptor.provenance = SemanticField<ProvenanceDescriptor>::Known({
                    ProvenanceKind::Derived, sourceIdentity,
                    expected == AlphaMode::Straight
                        ? "composite.source-over.straight.v1"
                        : "composite.source-over.premultiplied.v1"
                });
            }
            break;
        }
        }
        output.descriptorIdentity = DescriptorContentIdentity(output.descriptor);
        outputs[nodeId] = output;

        for (const std::string& destination : outgoing[nodeId]) {
            auto found = indegree.find(destination);
            if (found != indegree.end() && --found->second == 0) ready.insert(destination);
        }
    }

    if (visited != nodesById.size()) {
        result.diagnostics.push_back(MakeGraphDiagnostic(
            "nmr.connection.cycle", DiagnosticSeverity::HardError,
            "graph", "graph", "The semantic image graph contains a cycle."));
    }

    for (const auto& [identity, output] : outputs) result.nodeOutputs.push_back(output);
    for (const SemanticImageEdge& edge : edges) {
        const auto found = outputs.find(edge.sourceNodeIdentity);
        if (found == outputs.end()) continue;
        result.edges.push_back({ edge, found->second.descriptor, found->second.descriptorIdentity });
    }
    std::sort(result.edges.begin(), result.edges.end(), [](const auto& left, const auto& right) {
        return left.edge.identity < right.edge.identity;
    });
    std::sort(result.diagnostics.begin(), result.diagnostics.end(), [](const Diagnostic& left, const Diagnostic& right) {
        if (left.affectedIdentity != right.affectedIdentity) return left.affectedIdentity < right.affectedIdentity;
        if (left.ruleId != right.ruleId) return left.ruleId < right.ruleId;
        return left.semanticFingerprint < right.semanticFingerprint;
    });
    result.executable = visited == nodesById.size() &&
        std::none_of(result.diagnostics.begin(), result.diagnostics.end(), [](const Diagnostic& diagnostic) {
            return IsHard(diagnostic.severity);
        });
    result.semanticFingerprint = GraphFingerprint(result.nodeOutputs, result.edges, result.diagnostics);
    return result;
}

const SemanticNodeOutput* FindSemanticNodeOutput(
    const SemanticAnalysisResult& result,
    const std::string& nodeIdentity) {
    const auto found = std::find_if(result.nodeOutputs.begin(), result.nodeOutputs.end(),
        [&](const SemanticNodeOutput& output) { return output.nodeIdentity == nodeIdentity; });
    return found == result.nodeOutputs.end() ? nullptr : &*found;
}

const SemanticEdgeState* FindSemanticEdgeState(
    const SemanticAnalysisResult& result,
    const std::string& edgeIdentity) {
    const auto found = std::find_if(result.edges.begin(), result.edges.end(),
        [&](const SemanticEdgeState& edge) { return edge.edge.identity == edgeIdentity; });
    return found == result.edges.end() ? nullptr : &*found;
}

} // namespace Stack::NodeMath
