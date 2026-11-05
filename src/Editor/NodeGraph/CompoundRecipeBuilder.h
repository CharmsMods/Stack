#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Graph/LayerOperationParameters.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace EditorNodeGraphDefinitions::RecipeBuilding {
using namespace EditorNodeGraph;
using namespace Stack::NodeMath;

struct Recipe {
    Graph graph;
    CompoundDefinition definition;
    int identity;
    explicit Recipe(int identity, const char *id, const char *label) : identity(identity) {
        graph.Clear();
        graph.SetAllowNoOutput(true);
        definition.definitionUuid = Uuid(0);
        definition.identity.id = std::string("stack:compound/") + id;
        definition.identity.version = {1, 0, 0};
        definition.label = label;
        definition.description = "Editable canonical graph of standalone operations.";
        definition.source = CompoundDefinitionSource::ShippedTemplate;
        definition.lifecycleNotes = "Decompile one level; repack the edited graph. No native specialization.";
    }
    std::string Uuid(int n) const {
        char text[64];
        std::snprintf(text, sizeof(text), "%08d-1019-4000-8000-%012d", identity, n);
        return text;
    }
    int Keep(Node *node) {
        if (!node)
            throw std::runtime_error("Could not construct compound recipe node.");
        node->instanceUuid = Uuid(node->id);
        node->position = {float((node->id - 1) % 6) * 280.f, float((node->id - 1) / 6) * 240.f};
        if (node->kind == NodeKind::Compound)
            node->compound.instance.instanceUuid = node->instanceUuid;
        return node->id;
    }
    Node &At(int id) {
        return *graph.FindNode(id);
    }
    // Purpose notes are authored template data, copied with the node on decompile.
    void Note(int id, const std::string &text) {
        At(id).notes = text;
    }
    int Math(DataMathMode mode, float b = 1.f) {
        const int id = Keep(graph.AddDataMathNode(mode, {}));
        At(id).dataMathSettings.constantB = b;
        return id;
    }
    int Identity() {
        return Math(DataMathMode::Multiply);
    }
    int Split(int source) {
        const int id = Keep(graph.AddChannelSplitNode({}));
        Wire(source, "imageOut", id, "imageIn");
        return id;
    }
    int Combine() {
        return Keep(graph.AddChannelCombineNode({}));
    }
    void Wire(int a, const char *out, int b, const char *in) {
        // Public inputs are unbound in a template. Validate structural endpoints
        // on registration and resolve shapes in the instantiated graph.
        graph.EditLinks().push_back({a, out, b, in});
    }
    void Wire(int a, int b, const char *in = "imageA") {
        Wire(a, "imageOut", b, in);
    }
    void Port(const char *id, const char *label, int node, const char *socket, LogicalValueType type,
              bool output = false, bool optional = false, bool numeric = false) {
        definition.ports.push_back({id, label, output ? PortDirection::Output : PortDirection::Input, type,
                                    optional, At(node).instanceUuid, socket, numeric});
    }
    void Input(int node, const char *socket = "imageA", bool numeric = true) {
        Port("image-in", "Image / data", node, socket, LogicalValueType::ColorImage, false, false, numeric);
    }
    void Output(int node, LogicalValueType type = LogicalValueType::ColorImage, const char *socket = "imageOut",
                bool numeric = true) {
        Port("image-out", "Result", node, socket, type, true, false, numeric);
    }
    void Param(const char *id, const char *label, int node, const char *path, double value, double minimum,
               double maximum, const char *units = "unitless") {
        CompoundParameterDefinition p;
        p.id = id;
        p.label = label;
        p.defaultValue = value;
        p.hardDomain = {true, minimum, maximum, true, true};
        p.units = units;
        p.internalInstanceUuid = At(node).instanceUuid;
        p.internalParameterId = path;
        definition.parameters.push_back(std::move(p));
    }
    int Control(const char *id, const char *label, double value, double low, double high,
                const char *units = "unitless") {
        const int n = Math(DataMathMode::Add, 0);
        At(n).dataMathSettings.constantA = float(value);
        Param(id, label, n, "dataMathSettings.constantA", value, low, high, units);
        const std::string port = ParameterInputSocketId(id);
        Port(port.c_str(), label, n, "imageA", LogicalValueType::Scalar, false, true);
        return n;
    }
    int Child(const CompoundDefinition &child) {
        Graph embedded;
        DeserializeGraphPayload(child.canonicalGraph, embedded, 0,
                                EditorNodeGraphDefinitions::LiveGraphRole::Compound);
        for (const auto &d : embedded.GetCompoundDefinitions())
            AddDependency(d);
        AddDependency(child);
        return Keep(graph.AddCompoundNode(child.identity, {}));
    }
    void AddDependency(const CompoundDefinition &child) {
        std::string error;
        if (!graph.AddCompoundDefinition(child, &error))
            throw std::runtime_error(definition.label + ": " + error);
        if (std::none_of(definition.dependencies.begin(), definition.dependencies.end(),
                         [&](const auto &r) { return SameDefinitionReference(r, child.identity); }))
            definition.dependencies.push_back(child.identity);
    }
    void Override(int n, const char *id, double value) {
        SetCompoundParameterOverride(At(n).compound.instance, id, value);
    }
    CompoundDefinition Finish() {
        Stack::GraphModel::ReindexLayerParameterRecords(graph);
        definition.canonicalGraph = SerializeGraphPayload(nlohmann::json::array(), graph);
        RefreshCompoundDefinitionContentHash(definition);
        return definition;
    }
};
}
