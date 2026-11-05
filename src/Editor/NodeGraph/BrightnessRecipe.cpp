#include "Editor/NodeGraph/BrightnessRecipe.h"
#include "Editor/Timeline/TimelineAnimation.h"
#include "Editor/NodeGraph/CompoundInstantiation.h"
#include "Editor/NodeGraph/CompoundRecipeBuilder.h"
#include "NodeMath/ArithmeticContract.h"
#include "Editor/NodeGraph/GraphOutputSemantics.h"
#include "Editor/NodeGraph/MixContributions.h"
#include <array>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace EditorNodeGraphDefinitions {
using namespace EditorNodeGraph;
using namespace Stack::NodeMath;
using RecipeBuilding::Recipe;

namespace {
bool HasBrightnessInput(const ValueDescriptor& input) {
    const auto shape = ShapeOf(&input);
    return shape == ArithmeticShape::Channel || shape == ArithmeticShape::RGB || shape == ArithmeticShape::RGBA;
}

struct BrightnessVariant {
    bool rgba, covered, driven, disabled;
};

BrightnessVariant Variant(const Graph &graph, const Node &node, const ValueDescriptor &input,
                          const nlohmann::json &settings) {
    const bool rgba = ShapeOf(&input) == ArithmeticShape::RGBA;
    return {rgba, graph.FindAnyInputLink(node.id, "maskIn") != nullptr,
            graph.FindAnyInputLink(node.id, "param:layer.brightness") != nullptr,
            !settings.value("visible", true)};
}

CompoundDefinition MakeBrightness(BrightnessVariant v, int key) {
    const auto name = "brightness-" + std::to_string(key);
    Recipe r(200 + key, name.c_str(), "Brightness");
    int amount;
    if (v.driven) {
        amount = r.Math(DataMathMode::Add, 0);
        r.Port("param:layer.brightness", "Brightness", amount, "imageA", LogicalValueType::Scalar, false, true);
        r.Param("brightness", "Brightness", amount, "dataMathSettings.constantA", 0, -1, 1);
    } else {
        amount = r.Keep(r.graph.AddValueNode(MakeUniformScalar(0), {}));
        r.Param("brightness", "Brightness", amount, "value.payload", 0, -1, 1);
    }
    const char *amountSocket = v.driven ? "imageOut" : "valueOut";
    if (v.covered) {
        const int mask = r.Math(DataMathMode::Multiply, 1);
        r.At(mask).dataMathSettings.constantA = 1;
        const int coverage = r.Math(DataMathMode::Clamp);
        r.Port("maskIn", "Coverage", mask, "imageA", LogicalValueType::Channel, false, true);
        r.Wire(mask, coverage);
        const int weighted = r.Math(DataMathMode::Multiply);
        r.Wire(amount, amountSocket, weighted, "imageA");
        r.Wire(coverage, weighted, "imageB");
        amount = weighted;
        amountSocket = "imageOut";
    }
    if (v.disabled) {
        const int muted = r.Math(DataMathMode::Multiply, 0);
        r.Wire(amount, amountSocket, muted, "imageA");
        amount = muted;
        amountSocket = "imageOut";
    }
    int first, last;
    if (v.rgba) {
        first = r.Keep(r.graph.AddChannelSplitNode({}));
        last = r.Combine();
        for (const char *component : {"r", "g", "b"}) {
            const int add = r.Math(DataMathMode::Add, 0);
            r.Wire(first, component, add, "imageA");
            r.Wire(amount, amountSocket, add, "imageB");
            r.Wire(add, "imageOut", last, component);
        }
        r.Wire(first, "a", last, "a");
    } else {
        first = last = r.Math(DataMathMode::Add, 0);
        r.Wire(amount, amountSocket, last, "imageB");
    }
    r.Port("imageIn", "Image", first, v.rgba ? "imageIn" : "imageA", LogicalValueType::ColorImage, false, false,
           true);
    r.Port("imageOut", "Result", last, "imageOut", LogicalValueType::ColorImage, true, false, true);
    r.definition.description = "Add the brightness offset to each color component and retain source alpha.";
    return r.Finish();
}

const CompoundDefinition &BrightnessDefinition(BrightnessVariant v) {
    const int key = int(v.rgba) + 2 * int(v.covered) + 4 * int(v.driven) + 8 * int(v.disabled);
    static std::array<CompoundDefinition, 16> definitions;
    static std::array<std::once_flag, 16> ready;
    std::call_once(ready[key], [&] { definitions[key] = MakeBrightness(v, key); });
    return definitions[key];
}
}

bool RebindExpandedBrightnessAnimation(const Graph& graph, const NodeGroup& group, const Node& original,
    Stack::Timeline::TimelineAnimationState& animation, std::string& error) {
    const auto ownsTrack = [&](const auto& track) {
        return track.target.nodeUuid == original.instanceUuid ||
            (track.target.nodeUuid.empty() && track.target.nodeId == original.id);
    };
    if (std::none_of(animation.tracks.begin(), animation.tracks.end(), ownsTrack)) return true;
    for (const auto& track : animation.tracks) {
        if (ownsTrack(track) && track.target.parameterId != "layer.brightness") {
            error = "This Brightness animation track has no expanded parameter binding.";
            return false;
        }
    }
    for (const auto& parameter : group.compoundBoundary.at("parameters")) {
        if (parameter.value("id", std::string{}) != "brightness") continue;
        const auto target = parameter.at("internalInstanceUuid").get<std::string>();
        for (int id : group.compoundMembers) {
            const auto* child = graph.FindNode(id);
            if (!child || child->instanceUuid != target) continue;
            for (auto& track : animation.tracks) if (ownsTrack(track)) {
                track.target.nodeId = id;
                track.target.nodeUuid = target;
                track.target.parameterId = child->kind == NodeKind::Value ? "value" : "constant-a";
            }
            return true;
        }
    }
    error = "Brightness animation has no expanded Number binding.";
    return false;
}

bool IsBrightnessRecipe(const Node &node) {
    return node.kind == NodeKind::Layer && node.layerType == LayerType::Brightness;
}

bool BindBrightnessRecipe(Graph &graph, int nodeId, const ValueDescriptor &input, const nlohmann::json &settings,
                          std::string &error) {
    auto *node = graph.FindNode(nodeId);
    if (!node || !IsBrightnessRecipe(*node)) {
        error = "Brightness instance missing.";
        return false;
    }
    if (!HasBrightnessInput(input)) {
        error = "Connect a resolved Channel, RGB or RGBA input to select a Brightness recipe.";
        return false;
    }
    const auto &definition = BrightnessDefinition(Variant(graph, *node, input, settings));
    const double amount = settings.value("brightness", 0.0);
    if (!graph.FindCompoundDefinition(definition.identity) && !graph.AddCompoundDefinition(definition, &error))
        return false;
    node = graph.FindNode(nodeId);
    node->kind = NodeKind::Compound;
    node->compound.instance.definition = definition.identity;
    node->compound.instance.interfaceSnapshot = definition.ports;
    node->compound.instance.instanceUuid = node->instanceUuid;
    SetCompoundParameterOverride(node->compound.instance, "brightness", amount);
    if (!graph.ResolveCompoundNode(nodeId)) {
        error = "Brightness recipe could not be resolved.";
        return false;
    }
    graph.EditNodes();
    return true;
}

std::string InspectArithmeticRecipe(const Graph &graph, const Node &node, const ValueDescriptor &input) {
    auto literal = [](double value) {
        std::ostringstream out;
        out << std::setprecision(15) << value;
        return out.str();
    };
    const auto endpoint = [&](const std::string &port, double fallback) {
        const auto *link = graph.FindAnyInputLink(node.id, port);
        if (!link)
            return literal(fallback);
        const auto *source = graph.FindNode(link->fromNodeId);
        return (source ? source->title + " #" + std::to_string(source->id) : "Unavailable") + "." +
               link->fromSocketId;
    };
    if (node.kind == NodeKind::Value && std::holds_alternative<double>(node.value.value.payload))
        return "Result = " + literal(std::get<double>(node.value.value.payload)) + " (one Number)";
    if (node.kind == NodeKind::DataMath) {
        if (node.dataMathMode == DataMathMode::Mix) {
            const MixContributions mix(graph, node);
            if (!mix.count) return "Connect at least one input to Mix.";
            std::string result = "Result = ";
            for (int i = 0; i < mix.count; ++i) {
                if (i) result += " + ";
                result += endpoint(DataMathInputSocketId(mix.slots[i]), 0) + " * " + literal(mix.shares[i]);
            }
            return result;
        }
        if (UnaryArithmetic(node.dataMathMode)) {
            const auto& s = node.dataMathSettings;
            if (const char* issue = ArithmeticLimitsError(node.dataMathMode, s)) return issue;
            if (!graph.FindAnyInputLink(node.id, "imageA")) return "Connect a data input.";
            const auto x = endpoint("imageA", 0);
            if (node.dataMathMode == DataMathMode::Clamp)
                return "Result = clamp(" + x + ", " + literal(s.minValue) + ", " + literal(s.maxValue) + ")";
            return "t = (" + x + " - " + literal(s.minValue) + ") / (" + literal(s.maxValue) + " - " +
                literal(s.minValue) + ")\nResult = " + literal(s.outMin) + " + t * (" + literal(s.outMax) +
                " - " + literal(s.outMin) + ")";
        }
        const auto *op = Primitive(node.dataMathMode);
        if (!op)
            return {};
        const std::string expression = node.dataMathMode == DataMathMode::Difference ? "abs(A - B)" :
            node.dataMathMode == DataMathMode::Min ? "min(A, B)" : node.dataMathMode == DataMathMode::Max ?
            "max(A, B)" : std::string("A ") + op->symbol + " B";
        std::string text = "A = " + endpoint("imageA", node.dataMathSettings.constantA) +
            "\nB = " + endpoint("imageB", node.dataMathSettings.constantB) + "\nResult = " + expression;
        const auto describe = [&](const char *port) -> std::optional<GraphOutputDescription> {
            const auto *wire = graph.FindAnyInputLink(node.id, port);
            if (!wire)
                return std::nullopt;
            return DescribeGraphOutput(graph, wire->fromNodeId, wire->fromSocketId);
        };
        const auto a = describe("imageA"), b = describe("imageB");
        const auto rule = ResolveArithmetic(a ? &a->descriptor : nullptr, b ? &b->descriptor : nullptr);
        text += "\nA carries " + std::string(ShapeName(rule.a)) + ". B carries " + ShapeName(rule.b) + ".";
        if (!rule.error.empty())
            text += "\n" + rule.error;
        else if (rule.result == LogicalValueType::Scalar)
            text += "\nEvaluate once as a Number.";
        else {
            if (rule.a == ArithmeticShape::Number || rule.b == ArithmeticShape::Number)
                text += "\nUse the same Number at every pixel and component.";
            if (rule.a == ArithmeticShape::Channel || rule.b == ArithmeticShape::Channel)
                text += "\nRead the Channel at the corresponding pixel.";
            if (rule.a == ArithmeticShape::RGBA || rule.b == ArithmeticShape::RGBA)
                text += "\nApply the operation to R, G, B and alpha.";
            else if (rule.result == LogicalValueType::ColorImage)
                text += "\nPair named color components.";
        }
        if (node.dataMathMode == DataMathMode::Divide)
            text += "\nWhere B is zero, Result is zero.";
        return text;
    }
    if (!IsBrightnessRecipe(node))
        return {};
    if (!HasBrightnessInput(input))
        return "Connect a Channel, RGB or RGBA input to select the executable Brightness recipe.";
    const auto &definition = BrightnessDefinition(Variant(graph, node, input, node.layerParameters));
    Graph internal;
    DeserializeGraphPayload(definition.canonicalGraph, internal, 0, LiveGraphRole::Compound);
    const double brightness = node.layerParameters.value("brightness", 0.);
    for (const auto &param : definition.parameters)
        for (auto &child : internal.GetNodes())
            if (child.instanceUuid == param.internalInstanceUuid)
                CompoundInternal::BindPromotedParameter(child, param.internalParameterId, brightness);
    const auto binding = [&](const Node &child, const char *socket, double fallback) {
        if (const auto *edge = internal.FindAnyInputLink(child.id, socket))
            return std::string("n") + std::to_string(edge->fromNodeId) + "." + edge->fromSocketId;
        for (const auto &port : definition.ports)
            if (port.direction == PortDirection::Input && port.internalInstanceUuid == child.instanceUuid &&
                port.internalSocketId == socket)
                return graph.FindAnyInputLink(node.id, port.id) ? endpoint(port.id, fallback)
                                                                : std::string("unconnected ") + port.label;
        return literal(fallback);
    };
    std::ostringstream out;
    for (const auto &child : internal.GetNodes()) {
        out << "n" << child.id << " = ";
        if (child.kind == NodeKind::Value)
            out << literal(std::get<double>(child.value.value.payload));
        else if (child.kind == NodeKind::ChannelSplit)
            out << "Split(" << binding(child, "imageIn", 0) << ")";
        else if (child.kind == NodeKind::ChannelCombine)
            out << "RGBA(" << binding(child, "r", 0) << ", " << binding(child, "g", 0) << ", "
                << binding(child, "b", 0) << ", " << binding(child, "a", 1) << ")";
        else if (child.dataMathMode == DataMathMode::Clamp)
            out << "Clamp(" << binding(child, "imageA", 0) << ", 0, 1)";
        else if (const auto *op = Primitive(child.dataMathMode))
            out << binding(child, "imageA", child.dataMathSettings.constantA) << " " << op->symbol << " "
                << binding(child, "imageB", child.dataMathSettings.constantB);
        out << "\n";
    }
    for (const auto &port : definition.ports)
        if (port.direction == PortDirection::Output)
            for (const auto &child : internal.GetNodes())
                if (child.instanceUuid == port.internalInstanceUuid)
                    out << "Result = n" << child.id << "." << port.internalSocketId;
    return out.str();
}

}
