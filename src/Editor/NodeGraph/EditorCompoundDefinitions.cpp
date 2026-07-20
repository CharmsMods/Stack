#include "Editor/NodeGraph/EditorCompoundDefinitions.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"

namespace EditorNodeGraphDefinitions {
namespace {

Stack::NodeMath::CompoundParameterDefinition ScalarParameter(
    const char* id,
    const char* label,
    double defaultValue,
    double minimum,
    double maximum,
    const char* internalUuid,
    const char* internalParameterId,
    const char* units = "unitless") {
    Stack::NodeMath::CompoundParameterDefinition parameter;
    parameter.id = id;
    parameter.label = label;
    parameter.type = Stack::NodeMath::ParameterType::Scalar;
    parameter.defaultValue = defaultValue;
    parameter.hardDomain = { true, minimum, maximum, true, true };
    parameter.uiHint = "slider";
    parameter.units = units;
    parameter.internalInstanceUuid = internalUuid;
    parameter.internalParameterId = internalParameterId;
    return parameter;
}

Stack::NodeMath::CompoundDefinition BuildAddMultiplyTemplate() {
    constexpr const char* addUuid = "11111111-1111-4111-8111-111111111111";
    constexpr const char* multiplyUuid = "22222222-2222-4222-8222-222222222222";

    EditorNodeGraph::Graph graph;
    graph.Clear();
    graph.SetAllowNoOutput(true);
    EditorNodeGraph::Node* add = graph.AddDataMathNode(
        EditorNodeGraph::DataMathMode::Add, { 0.0f, 0.0f });
    const int addId = add->id;
    add->instanceUuid = addUuid;
    add->dataMathSettings.constantB = 0.0f;
    EditorNodeGraph::Node* multiply = graph.AddDataMathNode(
        EditorNodeGraph::DataMathMode::Multiply, { 300.0f, 0.0f });
    const int multiplyId = multiply->id;
    multiply->instanceUuid = multiplyUuid;
    multiply->dataMathSettings.constantB = 1.0f;
    graph.TryConnectSockets(
        addId, EditorNodeGraph::kImageOutputSocketId,
        multiplyId, EditorNodeGraph::kMixInputASocketId);

    Stack::NodeMath::CompoundDefinition definition;
    definition.definitionUuid = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    definition.identity.id = "stack:compound/add-then-multiply";
    definition.identity.version = { 1, 0, 0 };
    definition.label = "Add, Then Multiply";
    definition.description = "Adds a uniform value, then multiplies by a uniform value in the exact authored order.";
    definition.definitionClass = Stack::NodeMath::CompoundDefinitionClass::TransparentGraph;
    definition.source = Stack::NodeMath::CompoundDefinitionSource::ShippedTemplate;
    definition.ports = {
        { "image-in", "Image", Stack::NodeMath::PortDirection::Input,
            Stack::NodeMath::LogicalValueType::ColorImage, false, addUuid,
            EditorNodeGraph::kMixInputASocketId },
        { "image-out", "Image", Stack::NodeMath::PortDirection::Output,
            Stack::NodeMath::LogicalValueType::ColorImage, false, multiplyUuid,
            EditorNodeGraph::kImageOutputSocketId }
    };
    definition.parameters = {
        ScalarParameter("add", "Add", 0.0, -16.0, 16.0, addUuid,
            "dataMathSettings.constantB"),
        ScalarParameter("multiply", "Multiply", 1.0, -16.0, 16.0, multiplyUuid,
            "dataMathSettings.constantB")
    };
    definition.canonicalGraph = EditorNodeGraph::SerializeGraphPayload(
        nlohmann::json::array(), graph);
    definition.unpackable = true;
    definition.lifecycleNotes = "Phase 5 transparent pointwise reference compound.";
    Stack::NodeMath::RefreshCompoundDefinitionContentHash(definition);
    return definition;
}

Stack::NodeMath::CompoundDefinition BuildExposurePremultiplyTemplate() {
    constexpr const char* exposureUuid = "33333333-3333-4333-8333-333333333333";
    constexpr const char* premultiplyUuid = "44444444-4444-4444-8444-444444444444";

    EditorNodeGraph::Graph graph;
    graph.Clear();
    graph.SetAllowNoOutput(true);
    EditorNodeGraph::Node* exposure = graph.AddTechnicalImageNode(
        Stack::NodeMath::TechnicalImageOperation::Exposure, { 0.0f, 0.0f });
    const int exposureId = exposure->id;
    exposure->instanceUuid = exposureUuid;
    exposure->technicalImageSettings.exposureValue = 0.0f;
    EditorNodeGraph::Node* premultiply = graph.AddTechnicalImageNode(
        Stack::NodeMath::TechnicalImageOperation::Premultiply, { 300.0f, 0.0f });
    const int premultiplyId = premultiply->id;
    premultiply->instanceUuid = premultiplyUuid;
    graph.TryConnectSockets(
        exposureId, EditorNodeGraph::kImageOutputSocketId,
        premultiplyId, EditorNodeGraph::kImageInputSocketId);

    Stack::NodeMath::CompoundDefinition definition;
    definition.definitionUuid = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
    definition.identity.id = "stack:compound/exposure-then-premultiply";
    definition.identity.version = { 1, 0, 0 };
    definition.label = "Exposure, Then Premultiply";
    definition.description = "Canonical Exposure EV followed by explicit premultiplication; Phase 4 may execute the exact chain as one fused shader.";
    definition.definitionClass =
        Stack::NodeMath::CompoundDefinitionClass::GraphDefinedOptimizedEquivalent;
    definition.source = Stack::NodeMath::CompoundDefinitionSource::ShippedTemplate;
    definition.ports = {
        { "image-in", "Image", Stack::NodeMath::PortDirection::Input,
            Stack::NodeMath::LogicalValueType::ColorImage, false, exposureUuid,
            EditorNodeGraph::kImageInputSocketId },
        { "image-out", "Image", Stack::NodeMath::PortDirection::Output,
            Stack::NodeMath::LogicalValueType::ColorImage, false, premultiplyUuid,
            EditorNodeGraph::kImageOutputSocketId }
    };
    definition.parameters = {
        ScalarParameter("exposure-ev", "Exposure", 0.0, -16.0, 16.0,
            exposureUuid, "technicalImageSettings.exposureValue", "ev")
    };
    definition.canonicalGraph = EditorNodeGraph::SerializeGraphPayload(
        nlohmann::json::array(), graph);
    definition.unpackable = true;
    definition.optimizedImplementationId = "phase4.pointwise-fusion.v1";
    definition.absoluteTolerance = 2.5e-3;
    definition.relativeTolerance = 0.0;
    definition.equivalenceEvidenceIds = {
        "phase5.compound.exposure-premultiply.cpu",
        "phase5.compound.exposure-premultiply.live-gpu"
    };
    definition.lifecycleNotes = "Phase 5 optimized-equivalent pointwise reference compound.";
    Stack::NodeMath::RefreshCompoundDefinitionContentHash(definition);
    return definition;
}

} // namespace

const std::vector<Stack::NodeMath::CompoundDefinition>& GetShippedCompoundTemplates() {
    static const std::vector<Stack::NodeMath::CompoundDefinition> definitions = {
        BuildAddMultiplyTemplate(),
        BuildExposurePremultiplyTemplate()
    };
    return definitions;
}

const Stack::NodeMath::CompoundDefinition* FindShippedCompoundTemplate(std::size_t index) {
    const auto& definitions = GetShippedCompoundTemplates();
    return index < definitions.size() ? &definitions[index] : nullptr;
}

} // namespace EditorNodeGraphDefinitions
