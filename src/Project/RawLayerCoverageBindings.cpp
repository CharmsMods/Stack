#include "Project/RawLayerStack.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace Stack::Project {
namespace {
const EditorNodeGraph::Node* Operation(const RawAdjustmentLayer& layer, const std::string& uuid) {
    for (const auto& node : layer.graph.GetNodes())
        if (node.instanceUuid == uuid && node.kind == EditorNodeGraph::NodeKind::RawOperation) return &node;
    return nullptr;
}
}
const EditorNodeGraph::Node* FindRawCoverageOwner(const RawAdjustmentLayer& layer, int operationId,
    const std::string& port, const std::string& coverageId) {
    const auto* input = layer.graph.FindInputLink(operationId,port);
    if (!input) return nullptr;
    std::vector<int> pending{input->fromNodeId};
    std::unordered_set<int> visited;
    const EditorNodeGraph::Node* owner = nullptr;
    while (!pending.empty()) {
        const int id = pending.back(); pending.pop_back();
        if (!visited.insert(id).second) continue;
        const auto* node = layer.graph.FindNode(id);
        if (!node) continue;
        if (!node->rawCoverage.empty() && node->rawCoverage.value("id",std::string{}) == coverageId) {
            if (owner && owner != node) return nullptr;
            owner = node;
        }
        for (const auto& link : layer.graph.GetLinks())
            if (link.toNodeId == id && link.toSocketId != EditorNodeGraph::kMatchExtentInputSocketId)
                pending.push_back(link.fromNodeId);
    }
    return owner;
}

RawRecipe::RawDevelopmentRecipe ReadRawLayerOperation(const RawAdjustmentLayer& layer, const std::string& uuid) {
    const auto* operation = Operation(layer, uuid);
    if (!operation) throw std::runtime_error("The selected photo operation is unavailable.");
    auto settings = operation->rawOperation;
    if (settings.kind == RawRecipe::GraphOperationKind::LocalEv && settings.parameters.value("graphCoverage", false)) {
        const auto coverage = [&](const std::string& port) -> const nlohmann::json* {
            const auto* generator = FindRawCoverageOwner(layer,operation->id,port,port.substr(port.find(':')+1));
            return generator && !generator->rawCoverage.empty() ? &generator->rawCoverage : nullptr;
        };
        for (auto& gradient : settings.parameters["evGradients"]) {
            const auto id = gradient.at("mask").value("id", std::string{});
            if (const auto* mask = coverage("gradient:" + id)) gradient["mask"] = *mask;
        }
        for (auto& area : settings.parameters["localRange"]["areas"]) {
            if (const auto* mask = coverage("area:" + area.value("id", std::string{}))) {
                area["strokes"] = mask->value("strokes", nlohmann::json::array());
                area["sourceAspect"] = mask->value("sourceAspect", 1.f);
            }
        }
    }
    return RawRecipe::ReadGraphOperation(settings);
}

bool WriteRawLayerOperation(RawAdjustmentLayer& layer, const std::string& uuid,
    const RawRecipe::RawDevelopmentRecipe& recipe, std::string& error) {
    const auto* operation = Operation(layer, uuid);
    if (!operation) { error = "The selected photo operation is unavailable."; return false; }
    const int operationId = operation->id;
    std::unordered_set<std::string> previousPorts;
    if (operation->rawOperation.parameters.value("graphCoverage", false))
        for (const auto& socket : layer.graph.GetSockets(*operation))
            previousPorts.insert(socket.id);
    RawRecipe::WriteGraphOperation(layer.graph.FindNode(operationId)->rawOperation, recipe);
    if (operation->rawOperation.kind != RawRecipe::GraphOperationKind::LocalEv) return true;
    const auto serialized = RawRecipe::SerializeRecipe(recipe);
    const auto bind = [&](const std::string& port, EditorNodeGraph::MaskGeneratorKind kind,
        const nlohmann::json& coverage, const std::string& name) {
        const auto* input = layer.graph.FindInputLink(operationId, port);
        const auto* bound = FindRawCoverageOwner(layer,operationId,port,coverage.value("id",std::string{}));
        auto* generator = bound ? layer.graph.FindNode(bound->id) : input ? layer.graph.FindNode(input->fromNodeId) : nullptr;
        if (generator && (generator->kind != EditorNodeGraph::NodeKind::MaskGenerator || generator->maskKind != kind)) {
            // A graph-driven mask has another authored owner. Editing the
            // operation's curve must not replace that connection or its data.
            return true;
        }
        if (!generator && previousPorts.count(port)) return true;
        if (!generator) {
            generator = layer.graph.AddMaskGeneratorNode(kind, {0, 300});
            generator->title = name;
            const int generatorId = generator->id;
            if (!layer.graph.TryConnectSockets(operationId, kind == EditorNodeGraph::MaskGeneratorKind::PaintedArea ? "measurementImageOut" : "inputImageOut", generatorId,
                    EditorNodeGraph::kMatchExtentInputSocketId, &error) ||
                !layer.graph.TryConnectSockets(generatorId, "maskOut", operationId, port, &error)) return false;
            generator = layer.graph.FindNode(generatorId);
        }
        generator->rawCoverage = coverage;
        return true;
    };
    for (const auto& gradient : serialized.at("evGradients")) {
        const auto& mask = gradient.at("mask");
        const auto id = mask.at("id").get<std::string>();
        if (!bind("gradient:" + id, EditorNodeGraph::MaskGeneratorKind::RawGradient, mask, "Local EV gradient")) return false;
    }
    for (const auto& area : serialized.at("localRange").value("areas", nlohmann::json::array())) {
        const nlohmann::json coverage{{"id", area.at("id")}, {"strokes", area.at("strokes")},
            {"sourceAspect", area.value("sourceAspect", 1.f)}};
        if (!bind("area:" + area.at("id").get<std::string>(), EditorNodeGraph::MaskGeneratorKind::PaintedArea,
                coverage, area.value("name", std::string("Painted area")))) return false;
    }
    auto& parameters = layer.graph.FindNode(operationId)->rawOperation.parameters;
    parameters["graphCoverage"] = true;
    for (auto& gradient : parameters["evGradients"]) gradient["mask"] = {{"id", gradient.at("mask").at("id")}};
    for (auto& area : parameters["localRange"]["areas"]) { area.erase("strokes"); area.erase("sourceAspect"); }
    // Removing an area removes its socket. Its independent generator remains.
    auto& links = layer.graph.EditLinks();
    links.erase(std::remove_if(links.begin(), links.end(), [&](const auto& link) {
        if (link.toNodeId != operationId || (link.toSocketId.rfind("gradient:", 0) != 0 && link.toSocketId.rfind("area:", 0) != 0)) return false;
        return !layer.graph.FindSocket(operationId, link.toSocketId);
    }), links.end());
    return true;
}
} // namespace Stack::Project
