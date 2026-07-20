#include "Editor/NodeGraph/EditorNodeGraph.h"

#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace EditorNodeGraph {
namespace {

Stack::NodeMath::LogicalValueType LogicalTypeForSocket(SocketType type) {
    using Logical = Stack::NodeMath::LogicalValueType;
    switch (type) {
        case SocketType::Mask: return Logical::Mask;
        case SocketType::ScalarField: return Logical::ScalarField;
        case SocketType::Boolean: return Logical::Boolean;
        case SocketType::Integer: return Logical::Integer;
        case SocketType::Scalar: return Logical::Scalar;
        case SocketType::Vector2: return Logical::Vector2;
        case SocketType::Vector3: return Logical::Vector3;
        case SocketType::Vector4: return Logical::Vector4;
        case SocketType::Matrix3: return Logical::Matrix3;
        case SocketType::Matrix4: return Logical::Matrix4;
        case SocketType::Curve: return Logical::Curve1D;
        case SocketType::Coordinate: return Logical::Coordinate2;
        case SocketType::Histogram: return Logical::Histogram;
        case SocketType::Statistics: return Logical::Statistics;
        case SocketType::Metadata: return Logical::Metadata;
        case SocketType::Handle: return Logical::SpecializedHandle;
        case SocketType::Analysis: return Logical::Analysis;
        case SocketType::Raw: return Logical::Raw;
        case SocketType::Value: return Logical::Scalar;
        case SocketType::Image:
        default:
            return Logical::ColorImage;
    }
}

bool ApplyPromotedParameter(
    Node& node,
    const std::string& parameterId,
    const Stack::NodeMath::ParameterValue& value) {
    const double* scalar = std::get_if<double>(&value);
    if (!scalar) return false;
    const float converted = static_cast<float>(*scalar);
    if (parameterId == "dataMathSettings.constantA") {
        node.dataMathSettings.constantA = converted;
    } else if (parameterId == "dataMathSettings.constantB") {
        node.dataMathSettings.constantB = converted;
    } else if (parameterId == "dataMathSettings.minValue") {
        node.dataMathSettings.minValue = converted;
    } else if (parameterId == "dataMathSettings.maxValue") {
        node.dataMathSettings.maxValue = converted;
    } else if (parameterId == "dataMathSettings.outMin") {
        node.dataMathSettings.outMin = converted;
    } else if (parameterId == "dataMathSettings.outMax") {
        node.dataMathSettings.outMax = converted;
    } else if (parameterId == "technicalImageSettings.exposureValue") {
        node.technicalImageSettings.exposureValue = converted;
    } else {
        return false;
    }
    return true;
}

bool HasPort(
    const Stack::NodeMath::CompoundDefinition& definition,
    const std::string& portId,
    Stack::NodeMath::PortDirection direction) {
    return std::any_of(definition.ports.begin(), definition.ports.end(),
        [&](const Stack::NodeMath::CompoundPortDefinition& port) {
            return port.id == portId && port.direction == direction;
        });
}

Node* FindNodeByInstanceUuid(Graph& graph, const std::string& instanceUuid) {
    for (Node& node : graph.GetNodes()) {
        if (node.instanceUuid == instanceUuid) return &node;
    }
    return nullptr;
}

const Node* FindNodeByInstanceUuid(const Graph& graph, const std::string& instanceUuid) {
    for (const Node& node : graph.GetNodes()) {
        if (node.instanceUuid == instanceUuid) return &node;
    }
    return nullptr;
}

Stack::NodeMath::CompoundParameterDefinition ScalarParameter(
    std::string id,
    std::string label,
    double defaultValue,
    double minimum,
    double maximum,
    const Node& node,
    std::string internalParameterId,
    std::string units = "unitless") {
    Stack::NodeMath::CompoundParameterDefinition parameter;
    parameter.id = std::move(id);
    parameter.label = std::move(label);
    parameter.type = Stack::NodeMath::ParameterType::Scalar;
    parameter.defaultValue = defaultValue;
    parameter.hardDomain = { true, minimum, maximum, true, true };
    parameter.uiHint = "slider";
    parameter.units = std::move(units);
    parameter.internalInstanceUuid = node.instanceUuid;
    parameter.internalParameterId = std::move(internalParameterId);
    return parameter;
}

void PromoteSupportedParameters(
    const std::vector<Node>& nodes,
    std::vector<Stack::NodeMath::CompoundParameterDefinition>& parameters) {
    int ordinal = 1;
    for (const Node& node : nodes) {
        const std::string prefix = "node-" + std::to_string(ordinal++);
        if (node.kind == NodeKind::DataMath) {
            if (node.dataMathMode == DataMathMode::Clamp) {
                parameters.push_back(ScalarParameter(
                    prefix + "-minimum", node.title + " Minimum",
                    node.dataMathSettings.minValue, -65504.0, 65504.0, node,
                    "dataMathSettings.minValue"));
                parameters.push_back(ScalarParameter(
                    prefix + "-maximum", node.title + " Maximum",
                    node.dataMathSettings.maxValue, -65504.0, 65504.0, node,
                    "dataMathSettings.maxValue"));
            } else if (node.dataMathMode != DataMathMode::Average &&
                       node.dataMathMode != DataMathMode::ImageAverage &&
                       node.dataMathMode != DataMathMode::Remap) {
                parameters.push_back(ScalarParameter(
                    prefix + "-value", node.title + " Value",
                    node.dataMathSettings.constantB, -65504.0, 65504.0, node,
                    "dataMathSettings.constantB"));
            }
        } else if (node.kind == NodeKind::TechnicalImage &&
                   node.technicalImageSettings.operation ==
                       Stack::NodeMath::TechnicalImageOperation::Exposure) {
            parameters.push_back(ScalarParameter(
                prefix + "-exposure-ev", node.title,
                node.technicalImageSettings.exposureValue, -16.0, 16.0, node,
                "technicalImageSettings.exposureValue", "ev"));
        }
    }
}

Node CloneImageNodeForCompoundExpansion(const Node& source) {
    Node clone;
    clone.id = source.id;
    clone.instanceUuid = source.instanceUuid;
    clone.kind = source.kind;
    clone.typeId = source.typeId;
    clone.title = source.title;
    clone.position = source.position;
    clone.expanded = source.expanded;
    clone.outputEnabled = source.outputEnabled;
    clone.definitionId = source.definitionId;
    clone.definitionVersion = source.definitionVersion;
    clone.definitionHash = source.definitionHash;
    clone.definitionResolved = source.definitionResolved;
    clone.definitionResolutionError = source.definitionResolutionError;

    clone.image.label = source.image.label;
    clone.image.sourcePath = source.image.sourcePath;
    clone.image.width = source.image.width;
    clone.image.height = source.image.height;
    clone.image.channels = source.image.channels;
    clone.image.originalChannels = source.image.originalChannels;
    clone.image.sourceColorMetadata = source.image.sourceColorMetadata;
    clone.image.isLoading = source.image.isLoading;
    clone.image.isEmbedding = source.image.isEmbedding;
    clone.image.importRequestId = source.image.importRequestId;
    clone.image.embeddingRequestId = source.image.embeddingRequestId;
    if (!source.image.sharedPixels && !source.image.pixels.empty()) {
        source.image.sharedPixels =
            std::make_shared<std::vector<unsigned char>>(source.image.pixels);
    }
    clone.image.sharedPixels = source.image.sharedPixels;
    clone.image.pixelsFingerprint = source.image.pixelsFingerprint;
    return clone;
}

bool ValidateCanonicalCompoundBindings(
    const Stack::NodeMath::CompoundDefinition& definition,
    std::string* error) {
    if (definition.definitionClass ==
        Stack::NodeMath::CompoundDefinitionClass::OpaqueSpecialized) {
        return true;
    }
    Graph canonical;
    DeserializeGraphPayload(definition.canonicalGraph, canonical, 0, {}, 0, 0, 0);
    if (canonical.GetNodes().empty()) {
        if (error) *error = "Compound canonical graph contains no executable nodes.";
        return false;
    }
    for (const Stack::NodeMath::CompoundPortDefinition& port : definition.ports) {
        const Node* node = FindNodeByInstanceUuid(canonical, port.internalInstanceUuid);
        SocketDefinition socket;
        if (!node || !canonical.FindSocket(node->id, port.internalSocketId, &socket)) {
            if (error) *error = "Compound port does not bind to a canonical internal socket.";
            return false;
        }
        const SocketDirection expectedDirection =
            port.direction == Stack::NodeMath::PortDirection::Input
                ? SocketDirection::Input : SocketDirection::Output;
        if (socket.direction != expectedDirection ||
            LogicalTypeForSocket(socket.type) != port.logicalType) {
            if (error) *error = "Compound port type or direction disagrees with its canonical internal socket.";
            return false;
        }
    }
    for (const Stack::NodeMath::CompoundParameterDefinition& parameter :
            definition.parameters) {
        Node* node = FindNodeByInstanceUuid(canonical, parameter.internalInstanceUuid);
        if (!node || !ApplyPromotedParameter(
                *node, parameter.internalParameterId, parameter.defaultValue)) {
            if (error) *error = "Promoted compound parameter has no supported canonical binding.";
            return false;
        }
    }
    return true;
}

} // namespace

bool Graph::AddCompoundDefinition(
    Stack::NodeMath::CompoundDefinition definition,
    std::string* error) {
    if (!Stack::NodeMath::ValidateCompoundDefinition(definition).empty()) {
        if (error) *error = Stack::NodeMath::ValidateCompoundDefinition(definition).front().message;
        return false;
    }
    if (!ValidateCanonicalCompoundBindings(definition, error)) return false;
    for (const Stack::NodeMath::CompoundDefinition& existing : m_CompoundDefinitions) {
        if (Stack::NodeMath::SameDefinitionReference(existing.identity, definition.identity)) return true;
        if (existing.identity.id == definition.identity.id &&
            existing.identity.version == definition.identity.version &&
            existing.identity.contentHash != definition.identity.contentHash) {
            if (error) *error = "A different compound hash already uses this definition ID and version.";
            return false;
        }
    }
    m_CompoundDefinitions.push_back(std::move(definition));
    const std::vector<Stack::NodeMath::ContractIssue> catalogIssues =
        Stack::NodeMath::ValidateCompoundCatalog(m_CompoundDefinitions);
    if (!catalogIssues.empty()) {
        if (error) *error = catalogIssues.front().message;
        m_CompoundDefinitions.pop_back();
        return false;
    }
    TouchStructure();
    return true;
}

const Stack::NodeMath::CompoundDefinition* Graph::FindCompoundDefinition(
    const Stack::NodeMath::DefinitionReference& reference) const {
    return Stack::NodeMath::FindExactCompoundDefinition(m_CompoundDefinitions, reference);
}

Stack::NodeMath::CompoundDefinition* Graph::FindCompoundDefinition(
    const Stack::NodeMath::DefinitionReference& reference) {
    for (Stack::NodeMath::CompoundDefinition& definition : m_CompoundDefinitions) {
        if (Stack::NodeMath::SameDefinitionReference(definition.identity, reference)) return &definition;
    }
    return nullptr;
}

bool Graph::ResolveCompoundNode(int nodeId) {
    Node* node = FindNode(nodeId);
    if (!node || node->kind != NodeKind::Compound) return false;
    node->compound.instance.instanceUuid = node->instanceUuid;
    Stack::NodeMath::ResolveCompoundInstance(node->compound.instance, m_CompoundDefinitions);
    if (node->compound.instance.resolution == Stack::NodeMath::CompoundResolutionStatus::Exact) {
        std::string closureError;
        const auto closure = Stack::NodeMath::CollectCompoundDependencyClosure(
            m_CompoundDefinitions, { node->compound.instance.definition }, &closureError);
        if (closure.empty()) {
            node->compound.instance.resolution =
                closureError.find("Recursive") != std::string::npos
                    ? Stack::NodeMath::CompoundResolutionStatus::RecursiveDependency
                    : Stack::NodeMath::CompoundResolutionStatus::Missing;
            node->compound.instance.resolutionError = closureError;
        }
    }
    node->definitionId = node->compound.instance.definition.id;
    node->definitionVersion = Stack::NodeMath::ToString(node->compound.instance.definition.version);
    node->definitionHash = node->compound.instance.definition.contentHash;
    node->definitionResolved =
        node->compound.instance.resolution == Stack::NodeMath::CompoundResolutionStatus::Exact;
    node->definitionResolutionError = node->compound.instance.resolutionError;
    if (const Stack::NodeMath::CompoundDefinition* definition =
            FindCompoundDefinition(node->compound.instance.definition)) {
        node->title = definition->label;
    } else if (node->title.empty()) {
        node->title = "Unresolved Compound";
    }
    return node->definitionResolved;
}

bool Graph::MakeCompoundNodeUnique(int nodeId, std::string* error) {
    Node* node = FindNode(nodeId);
    if (!node || node->kind != NodeKind::Compound) {
        if (error) *error = "Selected node is not a compound instance.";
        return false;
    }
    const Stack::NodeMath::CompoundDefinition* source =
        FindCompoundDefinition(node->compound.instance.definition);
    if (!source) {
        if (error) *error = "Exact compound definition is missing.";
        return false;
    }
    Stack::NodeMath::CompoundDefinition unique;
    if (!Stack::NodeMath::CreateUniqueCompoundDefinition(
            *source, source->label + " Copy", unique, error)) return false;
    const Stack::NodeMath::DefinitionReference reference = unique.identity;
    if (!AddCompoundDefinition(std::move(unique), error)) return false;
    return UpdateCompoundNodeDefinition(nodeId, reference, error);
}

bool Graph::UpdateCompoundNodeDefinition(
    int nodeId,
    const Stack::NodeMath::DefinitionReference& definition,
    std::string* error) {
    Node* node = FindNode(nodeId);
    const Stack::NodeMath::CompoundDefinition* resolved = FindCompoundDefinition(definition);
    if (!node || node->kind != NodeKind::Compound || !resolved) {
        if (error) *error = "The requested exact compound definition is unavailable.";
        return false;
    }
    const Stack::NodeMath::CompoundDefinition* current =
        FindCompoundDefinition(node->compound.instance.definition);
    for (const Link& link : m_Links) {
        if (link.toNodeId == nodeId &&
            !HasPort(*resolved, link.toSocketId, Stack::NodeMath::PortDirection::Input)) {
            if (error) *error = "Updated definition does not contain a connected input port.";
            return false;
        }
        if (link.toNodeId == nodeId && current) {
            const auto oldPort = std::find_if(current->ports.begin(), current->ports.end(),
                [&](const Stack::NodeMath::CompoundPortDefinition& item) {
                    return item.direction == Stack::NodeMath::PortDirection::Input &&
                        item.id == link.toSocketId;
                });
            const auto newPort = std::find_if(resolved->ports.begin(), resolved->ports.end(),
                [&](const Stack::NodeMath::CompoundPortDefinition& item) {
                    return item.direction == Stack::NodeMath::PortDirection::Input &&
                        item.id == link.toSocketId;
                });
            if (oldPort != current->ports.end() && newPort != resolved->ports.end() &&
                oldPort->logicalType != newPort->logicalType) {
                if (error) *error = "Updated definition changes the type of a connected input port.";
                return false;
            }
        }
        if (link.fromNodeId == nodeId &&
            !HasPort(*resolved, link.fromSocketId, Stack::NodeMath::PortDirection::Output)) {
            if (error) *error = "Updated definition does not contain a connected output port.";
            return false;
        }
        if (link.fromNodeId == nodeId && current) {
            const auto oldPort = std::find_if(current->ports.begin(), current->ports.end(),
                [&](const Stack::NodeMath::CompoundPortDefinition& item) {
                    return item.direction == Stack::NodeMath::PortDirection::Output &&
                        item.id == link.fromSocketId;
                });
            const auto newPort = std::find_if(resolved->ports.begin(), resolved->ports.end(),
                [&](const Stack::NodeMath::CompoundPortDefinition& item) {
                    return item.direction == Stack::NodeMath::PortDirection::Output &&
                        item.id == link.fromSocketId;
                });
            if (oldPort != current->ports.end() && newPort != resolved->ports.end() &&
                oldPort->logicalType != newPort->logicalType) {
                if (error) *error = "Updated definition changes the type of a connected output port.";
                return false;
            }
        }
    }
    for (const Stack::NodeMath::ParameterOverride& overrideValue :
            node->compound.instance.parameterOverrides) {
        const auto parameter = std::find_if(
            resolved->parameters.begin(), resolved->parameters.end(),
            [&](const Stack::NodeMath::CompoundParameterDefinition& item) {
                return item.id == overrideValue.parameterId;
            });
        if (parameter == resolved->parameters.end()) {
            if (error) *error = "Updated definition removes a parameter overridden by this instance.";
            return false;
        }
        if (current) {
            const auto oldParameter = std::find_if(
                current->parameters.begin(), current->parameters.end(),
                [&](const Stack::NodeMath::CompoundParameterDefinition& item) {
                    return item.id == overrideValue.parameterId;
                });
            if (oldParameter != current->parameters.end() &&
                oldParameter->type != parameter->type) {
                if (error) *error = "Updated definition changes the type of an overridden parameter.";
                return false;
            }
        }
    }
    node->compound.instance.definition = definition;
    node->compound.instance.interfaceSnapshot = resolved->ports;
    ResolveCompoundNode(nodeId);
    TouchStructure();
    return true;
}

bool Graph::UnpackCompoundNode(
    int nodeId,
    std::vector<int>* unpackedNodeIds,
    std::string* error) {
    Node* instanceNode = FindNode(nodeId);
    if (!instanceNode || instanceNode->kind != NodeKind::Compound) {
        if (error) *error = "Selected node is not a compound instance.";
        return false;
    }
    const Stack::NodeMath::CompoundDefinition* definition =
        FindCompoundDefinition(instanceNode->compound.instance.definition);
    if (!definition || instanceNode->compound.instance.resolution !=
            Stack::NodeMath::CompoundResolutionStatus::Exact) {
        if (error) *error = "Exact compound definition is unresolved.";
        return false;
    }
    if (!definition->unpackable ||
        definition->definitionClass == Stack::NodeMath::CompoundDefinitionClass::OpaqueSpecialized) {
        if (error) *error = "Opaque specialized definitions cannot be unpacked.";
        return false;
    }

    Graph internal;
    DeserializeGraphPayload(definition->canonicalGraph, internal, 0, {}, 0, 0, 0);
    for (const Stack::NodeMath::CompoundDefinition& dependency : internal.GetCompoundDefinitions()) {
        bool present = false;
        for (const Stack::NodeMath::CompoundDefinition& existing : m_CompoundDefinitions) {
            present = present || Stack::NodeMath::SameDefinitionReference(
                existing.identity, dependency.identity);
        }
        if (!present) {
            if (error) *error = "Canonical graph is missing an embedded dependency in the project catalog.";
            return false;
        }
    }

    for (const Stack::NodeMath::CompoundParameterDefinition& parameter : definition->parameters) {
        Node* target = FindNodeByInstanceUuid(internal, parameter.internalInstanceUuid);
        if (!target || !ApplyPromotedParameter(
                *target,
                parameter.internalParameterId,
                Stack::NodeMath::ResolveCompoundParameterValue(instanceNode->compound.instance, parameter))) {
            if (error) *error = "Promoted compound parameter could not bind to its internal node.";
            return false;
        }
    }

    const Vec2 instancePosition = instanceNode->position;
    float minX = 0.0f;
    float minY = 0.0f;
    if (!internal.GetNodes().empty()) {
        minX = internal.GetNodes().front().position.x;
        minY = internal.GetNodes().front().position.y;
        for (const Node& node : internal.GetNodes()) {
            minX = std::min(minX, node.position.x);
            minY = std::min(minY, node.position.y);
        }
    }

    const std::vector<Link> externalLinks = m_Links;
    std::unordered_map<int, int> oldToNew;
    std::unordered_map<std::string, int> uuidToNew;
    std::vector<Node> clones;
    for (const Node& source : internal.GetNodes()) {
        Node clone = source;
        clone.id = m_NextNodeId++;
        clone.instanceUuid = Stack::NodeMath::GenerateCanonicalUuid();
        if (clone.kind == NodeKind::Compound) {
            clone.compound.instance.instanceUuid = clone.instanceUuid;
        }
        clone.position.x = instancePosition.x + (source.position.x - minX);
        clone.position.y = instancePosition.y + (source.position.y - minY);
        oldToNew[source.id] = clone.id;
        uuidToNew[source.instanceUuid] = clone.id;
        clones.push_back(std::move(clone));
    }

    for (Node& clone : clones) m_Nodes.push_back(std::move(clone));
    for (const Link& link : internal.GetLinks()) {
        const auto from = oldToNew.find(link.fromNodeId);
        const auto to = oldToNew.find(link.toNodeId);
        if (from != oldToNew.end() && to != oldToNew.end()) {
            m_Links.push_back({ from->second, link.fromSocketId, to->second, link.toSocketId });
        }
    }

    std::vector<Link> rewired;
    for (const Link& link : externalLinks) {
        if (link.toNodeId == nodeId) {
            const auto port = std::find_if(definition->ports.begin(), definition->ports.end(),
                [&](const Stack::NodeMath::CompoundPortDefinition& item) {
                    return item.direction == Stack::NodeMath::PortDirection::Input && item.id == link.toSocketId;
                });
            if (port != definition->ports.end() && uuidToNew.count(port->internalInstanceUuid)) {
                rewired.push_back({ link.fromNodeId, link.fromSocketId,
                    uuidToNew[port->internalInstanceUuid], port->internalSocketId });
            }
        } else if (link.fromNodeId == nodeId) {
            const auto port = std::find_if(definition->ports.begin(), definition->ports.end(),
                [&](const Stack::NodeMath::CompoundPortDefinition& item) {
                    return item.direction == Stack::NodeMath::PortDirection::Output && item.id == link.fromSocketId;
                });
            if (port != definition->ports.end() && uuidToNew.count(port->internalInstanceUuid)) {
                rewired.push_back({ uuidToNew[port->internalInstanceUuid], port->internalSocketId,
                    link.toNodeId, link.toSocketId });
            }
        }
    }

    RemoveNode(nodeId);
    for (const Link& link : rewired) {
        std::string linkError;
        if (!TryConnectSockets(
                link.fromNodeId, link.fromSocketId,
                link.toNodeId, link.toSocketId, &linkError)) {
            if (error) *error = "Unpack could not restore an external connection: " + linkError;
            return false;
        }
    }
    ClearSelection();
    if (unpackedNodeIds) unpackedNodeIds->clear();
    for (const auto& item : oldToNew) {
        SelectNode(item.second, true);
        if (unpackedNodeIds) unpackedNodeIds->push_back(item.second);
    }
    TouchStructure();
    return true;
}

bool Graph::ExpandAllCompoundNodes(Graph& expanded, CompoundExpansionResult* result) const {
    expanded.m_Nodes.clear();
    expanded.m_Nodes.reserve(m_Nodes.size());
    for (const Node& source : m_Nodes) {
        expanded.m_Nodes.push_back(source.kind == NodeKind::Image
            ? CloneImageNodeForCompoundExpansion(source)
            : source);
    }
    expanded.m_Links = m_Links;
    expanded.m_Groups = m_Groups;
    expanded.m_CompoundDefinitions = m_CompoundDefinitions;
    expanded.m_NextNodeId = m_NextNodeId;
    expanded.m_NextGroupId = m_NextGroupId;
    expanded.m_SelectedNodeId = m_SelectedNodeId;
    expanded.m_SelectedNodeIds = m_SelectedNodeIds;
    expanded.m_SelectedLink = m_SelectedLink;
    expanded.m_HasSelectedLink = m_HasSelectedLink;
    expanded.m_ActiveImageNodeId = m_ActiveImageNodeId;
    expanded.m_OutputNodeId = m_OutputNodeId;
    expanded.m_ForceOutputFourPins = m_ForceOutputFourPins;
    expanded.m_AllowNoOutput = m_AllowNoOutput;
    expanded.m_SocketPreviewNodeId = m_SocketPreviewNodeId;
    expanded.m_SocketPreviewIntent = m_SocketPreviewIntent;
    expanded.m_StructureRevision = m_StructureRevision;
    expanded.m_CompletedChainsCacheRevision = 0;
    expanded.m_CompletedChainsCache.clear();
    expanded.m_OutputConnectionDiagnosticCache.clear();
    CompoundExpansionResult local;
    for (int iteration = 0; iteration < 256; ++iteration) {
        int compoundNodeId = -1;
        for (const Node& node : expanded.GetNodes()) {
            if (node.kind == NodeKind::Compound) {
                compoundNodeId = node.id;
                break;
            }
        }
        if (compoundNodeId < 0) {
            local.success = true;
            if (result) *result = std::move(local);
            return true;
        }
        std::vector<int> unpacked;
        std::string unpackError;
        local.authoredCompoundNodeIds.push_back(compoundNodeId);
        if (!expanded.UnpackCompoundNode(compoundNodeId, &unpacked, &unpackError)) {
            local.error = unpackError;
            if (result) *result = std::move(local);
            return false;
        }
        local.expandedNodeIds.insert(local.expandedNodeIds.end(), unpacked.begin(), unpacked.end());
    }
    local.error = "Compound nesting exceeds the Phase 5 expansion limit.";
    if (result) *result = std::move(local);
    return false;
}

bool Graph::ResolveCompoundOutputInputDependencies(
    int nodeId,
    const std::string& outputSocketId,
    std::vector<std::string>& inputSocketIds,
    std::string* errorMessage) const {
    inputSocketIds.clear();
    const Node* instance = FindNode(nodeId);
    if (!instance || instance->kind != NodeKind::Compound) {
        if (errorMessage) *errorMessage = "The requested node is not a compound instance.";
        return false;
    }
    if (instance->compound.instance.resolution !=
        Stack::NodeMath::CompoundResolutionStatus::Exact) {
        if (errorMessage) {
            *errorMessage = instance->compound.instance.resolutionError.empty()
                ? "The compound's exact definition is unresolved."
                : instance->compound.instance.resolutionError;
        }
        return false;
    }

    const Stack::NodeMath::CompoundDefinition* definition =
        FindCompoundDefinition(instance->compound.instance.definition);
    if (!definition) {
        if (errorMessage) *errorMessage = "The compound's exact definition is missing.";
        return false;
    }
    if (definition->definitionClass ==
        Stack::NodeMath::CompoundDefinitionClass::OpaqueSpecialized) {
        if (errorMessage) *errorMessage = "Opaque specialized compounds do not expose canonical graph dependencies.";
        return false;
    }

    const auto publicOutput = std::find_if(
        definition->ports.begin(), definition->ports.end(),
        [&](const Stack::NodeMath::CompoundPortDefinition& port) {
            return port.direction == Stack::NodeMath::PortDirection::Output &&
                port.id == outputSocketId;
        });
    if (publicOutput == definition->ports.end()) {
        if (errorMessage) *errorMessage = "The connected compound output is not declared by the exact definition.";
        return false;
    }

    Graph canonical;
    DeserializeGraphPayload(definition->canonicalGraph, canonical, 0, {}, 0, 0, 0);
    const Node* outputNode = FindNodeByInstanceUuid(canonical, publicOutput->internalInstanceUuid);
    SocketDefinition outputSocket;
    if (!outputNode ||
        !canonical.FindSocket(outputNode->id, publicOutput->internalSocketId, &outputSocket) ||
        outputSocket.direction != SocketDirection::Output) {
        if (errorMessage) *errorMessage = "The connected compound output has no valid canonical binding.";
        return false;
    }

    std::map<std::pair<std::string, std::string>, std::string> publicInputsByBinding;
    for (const Stack::NodeMath::CompoundPortDefinition& port : definition->ports) {
        if (port.direction == Stack::NodeMath::PortDirection::Input) {
            publicInputsByBinding[{ port.internalInstanceUuid, port.internalSocketId }] = port.id;
        }
    }

    std::set<std::pair<int, std::string>> visiting;
    std::set<std::pair<int, std::string>> visited;
    std::set<std::string> dependencies;
    std::function<bool(int, const std::string&)> walkOutput;
    std::function<bool(int, const SocketDefinition&)> walkInput;

    walkInput = [&](int internalNodeId, const SocketDefinition& inputSocket) -> bool {
        const Node* internalNode = canonical.FindNode(internalNodeId);
        if (!internalNode) return false;
        const auto publicBinding = publicInputsByBinding.find(
            { internalNode->instanceUuid, inputSocket.id });
        if (publicBinding != publicInputsByBinding.end()) {
            dependencies.insert(publicBinding->second);
            return true;
        }
        if (const Link* upstream = canonical.FindInputLink(internalNodeId, inputSocket.id)) {
            return walkOutput(upstream->fromNodeId, upstream->fromSocketId);
        }
        // Optional inputs have a canonical default owned by their internal node.
        // Hidden required alternatives (for example packed image versus separate
        // channels) are inactive unless linked or deliberately bound.
        return inputSocket.optional || !inputSocket.visible;
    };

    walkOutput = [&](int internalNodeId, const std::string& internalOutputSocketId) -> bool {
        const std::pair<int, std::string> key { internalNodeId, internalOutputSocketId };
        if (visited.count(key) != 0) return true;
        if (!visiting.insert(key).second) return false;
        const Node* internalNode = canonical.FindNode(internalNodeId);
        SocketDefinition internalOutput;
        if (!internalNode ||
            !canonical.FindSocket(internalNodeId, internalOutputSocketId, &internalOutput) ||
            internalOutput.direction != SocketDirection::Output) {
            visiting.erase(key);
            return false;
        }

        bool valid = true;
        if (internalNode->kind == NodeKind::Compound) {
            std::vector<std::string> nestedDependencies;
            std::string nestedError;
            valid = canonical.ResolveCompoundOutputInputDependencies(
                internalNodeId, internalOutputSocketId, nestedDependencies, &nestedError);
            if (valid) {
                for (const std::string& nestedInputId : nestedDependencies) {
                    SocketDefinition nestedInput;
                    if (!canonical.FindSocket(internalNodeId, nestedInputId, &nestedInput) ||
                        !walkInput(internalNodeId, nestedInput)) {
                        valid = false;
                        break;
                    }
                }
            }
        } else {
            for (const SocketDefinition& socket : canonical.GetSockets(*internalNode, false)) {
                if (socket.direction == SocketDirection::Input &&
                    !walkInput(internalNodeId, socket)) {
                    valid = false;
                    break;
                }
            }
        }

        visiting.erase(key);
        if (valid) visited.insert(key);
        return valid;
    };

    if (!walkOutput(outputNode->id, publicOutput->internalSocketId)) {
        if (errorMessage) *errorMessage = "The compound output's canonical dependency path is invalid or incomplete.";
        return false;
    }

    for (const Stack::NodeMath::CompoundPortDefinition& port : definition->ports) {
        if (port.direction == Stack::NodeMath::PortDirection::Input &&
            dependencies.count(port.id) != 0) {
            inputSocketIds.push_back(port.id);
        }
    }
    if (errorMessage) errorMessage->clear();
    return true;
}

bool Graph::CreateCompoundFromSelection(
    const std::vector<int>& nodeIds,
    const std::string& label,
    int* compoundNodeId,
    std::string* error) {
    std::unordered_set<int> selected(nodeIds.begin(), nodeIds.end());
    if (selected.empty()) {
        if (error) *error = "Select at least one pointwise graph node.";
        return false;
    }
    std::vector<Node> selectedNodes;
    float minX = 0.0f;
    float minY = 0.0f;
    bool first = true;
    for (int nodeId : nodeIds) {
        const Node* node = FindNode(nodeId);
        if (!node || (node->kind != NodeKind::DataMath &&
                      node->kind != NodeKind::TechnicalImage &&
                      node->kind != NodeKind::Compound)) {
            if (error) *error = "Phase 5 compound authoring accepts Data Math, Technical Image, and graph-defined compound nodes only.";
            return false;
        }
        if (node->kind == NodeKind::Compound) {
            const Stack::NodeMath::CompoundDefinition* nested =
                FindCompoundDefinition(node->compound.instance.definition);
            if (!nested || nested->definitionClass ==
                    Stack::NodeMath::CompoundDefinitionClass::OpaqueSpecialized) {
                if (error) *error = "Opaque or unresolved nodes cannot be nested in a graph-defined compound.";
                return false;
            }
        }
        selectedNodes.push_back(*node);
        if (first) {
            minX = node->position.x;
            minY = node->position.y;
            first = false;
        } else {
            minX = std::min(minX, node->position.x);
            minY = std::min(minY, node->position.y);
        }
    }

    struct BoundaryLink { Link link; std::string portId; };
    std::vector<BoundaryLink> inputs;
    std::vector<BoundaryLink> outputs;
    std::map<std::pair<int, std::string>, std::string> outputPorts;
    for (const Link& link : m_Links) {
        const bool fromSelected = selected.count(link.fromNodeId) != 0;
        const bool toSelected = selected.count(link.toNodeId) != 0;
        if (!fromSelected && toSelected) {
            inputs.push_back({ link, "input-" + std::to_string(inputs.size() + 1u) });
        } else if (fromSelected && !toSelected) {
            const std::pair<int, std::string> endpoint { link.fromNodeId, link.fromSocketId };
            auto inserted = outputPorts.emplace(
                endpoint, "output-" + std::to_string(outputPorts.size() + 1u));
            outputs.push_back({ link, inserted.first->second });
        }
    }
    if (inputs.empty() || outputs.empty()) {
        if (error) *error = "A compound selection needs at least one connected external input and output.";
        return false;
    }

    Graph internal;
    internal.Clear();
    internal.SetAllowNoOutput(true);
    internal.GetNodes() = selectedNodes;
    int maxInternalId = 0;
    for (const Node& node : selectedNodes) maxInternalId = std::max(maxInternalId, node.id);
    internal.SetNextNodeId(maxInternalId + 1);
    for (const Link& link : m_Links) {
        if (selected.count(link.fromNodeId) && selected.count(link.toNodeId)) {
            internal.GetLinks().push_back(link);
        }
    }

    std::vector<Stack::NodeMath::DefinitionReference> dependencies;
    for (const Node& node : selectedNodes) {
        if (node.kind == NodeKind::Compound) dependencies.push_back(node.compound.instance.definition);
    }
    if (!dependencies.empty()) {
        std::string closureError;
        const std::vector<Stack::NodeMath::DefinitionReference> closure =
            Stack::NodeMath::CollectCompoundDependencyClosure(
                m_CompoundDefinitions, dependencies, &closureError);
        if (closure.empty()) {
            if (error) *error = closureError;
            return false;
        }
        for (const Stack::NodeMath::DefinitionReference& reference : closure) {
            const Stack::NodeMath::CompoundDefinition* dependency = FindCompoundDefinition(reference);
            if (dependency) internal.GetCompoundDefinitions().push_back(*dependency);
        }
        dependencies = closure;
    }

    Stack::NodeMath::CompoundDefinition definition;
    definition.definitionUuid = Stack::NodeMath::GenerateCanonicalUuid();
    definition.identity.id = "project:compound/" + definition.definitionUuid;
    definition.identity.version = { 1, 0, 0 };
    definition.label = label.empty() ? "Custom Compound" : label;
    definition.description = "Graph-defined compound created from an authored selection.";
    definition.definitionClass = Stack::NodeMath::CompoundDefinitionClass::TransparentGraph;
    definition.source = Stack::NodeMath::CompoundDefinitionSource::Embedded;
    definition.dependencies = dependencies;
    definition.canonicalGraph = SerializeGraphPayload(nlohmann::json::array(), internal);
    definition.unpackable = true;
    definition.lifecycleNotes = "Created from a live graph selection.";

    for (const BoundaryLink& boundary : inputs) {
        const Node* target = FindNode(boundary.link.toNodeId);
        SocketDefinition socket;
        FindSocket(boundary.link.toNodeId, boundary.link.toSocketId, &socket);
        definition.ports.push_back({
            boundary.portId,
            socket.label.empty() ? boundary.portId : socket.label,
            Stack::NodeMath::PortDirection::Input,
            LogicalTypeForSocket(socket.type),
            socket.optional,
            target ? target->instanceUuid : std::string(),
            boundary.link.toSocketId
        });
    }
    for (const auto& item : outputPorts) {
        const Node* source = FindNode(item.first.first);
        SocketDefinition socket;
        FindSocket(item.first.first, item.first.second, &socket);
        definition.ports.push_back({
            item.second,
            socket.label.empty() ? item.second : socket.label,
            Stack::NodeMath::PortDirection::Output,
            LogicalTypeForSocket(socket.type),
            false,
            source ? source->instanceUuid : std::string(),
            item.first.second
        });
    }
    PromoteSupportedParameters(selectedNodes, definition.parameters);
    Stack::NodeMath::RefreshCompoundDefinitionContentHash(definition);
    const Stack::NodeMath::DefinitionReference reference = definition.identity;
    if (!AddCompoundDefinition(std::move(definition), error)) return false;

    for (int selectedId : nodeIds) RemoveNode(selectedId);
    Node* compound = AddCompoundNode(reference, { minX, minY });
    if (!compound) {
        if (error) *error = "Could not create the compound instance.";
        return false;
    }
    const int newCompoundId = compound->id;
    for (const BoundaryLink& boundary : inputs) {
        std::string linkError;
        if (!TryConnectSockets(
                boundary.link.fromNodeId, boundary.link.fromSocketId,
                newCompoundId, boundary.portId, &linkError)) {
            if (error) *error = linkError;
            return false;
        }
    }
    for (const BoundaryLink& boundary : outputs) {
        std::string linkError;
        if (!TryConnectSockets(
                newCompoundId, boundary.portId,
                boundary.link.toNodeId, boundary.link.toSocketId, &linkError)) {
            if (error) *error = linkError;
            return false;
        }
    }
    SelectNode(newCompoundId, false);
    if (compoundNodeId) *compoundNodeId = newCompoundId;
    TouchStructure();
    return true;
}

} // namespace EditorNodeGraph
