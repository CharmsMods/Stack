#include "Editor/NodeGraph/MathRecipes.h"
#include "Editor/NodeGraph/CompoundRecipeBuilder.h"
#include "Editor/NodeGraph/MixContributions.h"
#include <array>
#include <memory>
#include <mutex>

namespace EditorNodeGraphDefinitions {
using namespace EditorNodeGraph;
using namespace Stack::NodeMath;
namespace {
using RecipeBuilding::Recipe;

int Number(Recipe& recipe, const std::string& id, const std::string& label, bool weight = false) {
    const int number = recipe.Keep(recipe.graph.AddValueNode(MakeUniformScalar(0), {}));
    const double limit = std::numeric_limits<float>::max();
    recipe.Param(id.c_str(), label.c_str(), number, "value.payload", 0, weight ? 0 : -limit, weight ? 1 : limit);
    return number;
}

CompoundDefinition MakeMix(unsigned mask) {
    const auto id = "mix-" + std::to_string(mask);
    Recipe recipe(300 + mask, id.c_str(), "Mix");
    int sum = -1;
    for (int slot = 0; slot < kMaxDataMathInputCount; ++slot) {
        if (!(mask & (1u << slot))) continue;
        const std::string label(1, char('A' + slot));
        const int weight = Number(recipe, "weight-" + label, label + " contribution", true);
        const int term = recipe.Math(DataMathMode::Multiply);
        recipe.Port(DataMathInputSocketId(slot).c_str(), label.c_str(), term, "imageA",
                    LogicalValueType::ColorImage, false, false, true);
        recipe.Wire(weight, "valueOut", term, "imageB");
        if (sum < 0) sum = term;
        else {
            const int add = recipe.Math(DataMathMode::Add);
            recipe.Wire(sum, add);
            recipe.Wire(term, add, "imageB");
            sum = add;
        }
    }
    recipe.Port("imageOut", "Result", sum, "imageOut", LogicalValueType::ColorImage, true, false, true);
    return recipe.Finish();
}

const CompoundDefinition& MixDefinition(unsigned mask) {
    static std::array<std::unique_ptr<CompoundDefinition>, 1u << kMaxDataMathInputCount> definitions;
    static std::array<std::once_flag, definitions.size()> flags;
    std::call_once(flags[mask], [&] { definitions[mask] = std::make_unique<CompoundDefinition>(MakeMix(mask)); });
    return *definitions[mask];
}

CompoundDefinition MakeRemap() {
    Recipe r(600, "remap-range", "Remap Range");
    const int a = Number(r, "input-start", "Input start"), b = Number(r, "input-end", "Input end");
    const int c = Number(r, "output-start", "Output start"), d = Number(r, "output-end", "Output end");
    const int offset = r.Math(DataMathMode::Subtract), span = r.Math(DataMathMode::Subtract);
    const int fraction = r.Math(DataMathMode::Divide), scale = r.Math(DataMathMode::Subtract);
    const int product = r.Math(DataMathMode::Multiply), result = r.Math(DataMathMode::Add);
    r.Port("imageA", "Data", offset, "imageA", LogicalValueType::ColorImage, false, false, true);
    r.Wire(a, "valueOut", offset, "imageB");
    r.Wire(b, "valueOut", span, "imageA"); r.Wire(a, "valueOut", span, "imageB");
    r.Wire(offset, fraction); r.Wire(span, fraction, "imageB");
    r.Wire(d, "valueOut", scale, "imageA"); r.Wire(c, "valueOut", scale, "imageB");
    r.Wire(fraction, product); r.Wire(scale, product, "imageB");
    r.Wire(product, result); r.Wire(c, "valueOut", result, "imageB");
    r.Port("imageOut", "Result", result, "imageOut", LogicalValueType::ColorImage, true, false, true);
    return r.Finish();
}
}

bool BindMathRecipe(Graph& graph, int nodeId, std::string& error) {
    auto* node = graph.FindNode(nodeId);
    if (!node || !IsMathRecipe(*node)) return false;
    const CompoundDefinition* definition;
    std::vector<std::pair<std::string,double>> values;
    if (node->dataMathMode == DataMathMode::Mix) {
        const MixContributions mix(graph, *node);
        if (!mix.count) { error = "Connect at least one input to Mix."; return false; }
        unsigned mask = 0;
        for (int i = 0; i < mix.count; ++i) {
            const int slot = mix.slots[i];
            mask |= 1u << slot;
            values.emplace_back("weight-" + std::string(1, char('A' + slot)), mix.shares[i]);
        }
        definition = &MixDefinition(mask);
    } else {
        const auto& limits = node->dataMathSettings;
        if (auto reason = ArithmeticLimitsError(node->dataMathMode, limits)) { error = reason; return false; }
        if (!graph.FindAnyInputLink(nodeId, "imageA")) { error = "Connect a data input to Remap Range."; return false; }
        static const CompoundDefinition remap = MakeRemap();
        definition = &remap;
        values = {{"input-start", limits.minValue}, {"input-end", limits.maxValue},
                  {"output-start", limits.outMin}, {"output-end", limits.outMax}};
    }
    if (!graph.FindCompoundDefinition(definition->identity) && !graph.AddCompoundDefinition(*definition, &error))
        return false;
    node = graph.FindNode(nodeId);
    node->kind = NodeKind::Compound;
    auto& instance = node->compound.instance;
    instance.definition = definition->identity;
    instance.interfaceSnapshot = definition->ports;
    instance.instanceUuid = node->instanceUuid;
    for (const auto& [id, value] : values) SetCompoundParameterOverride(instance, id, value);
    if (!graph.ResolveCompoundNode(nodeId)) { error = "The math recipe could not be resolved."; return false; }
    graph.EditNodes();
    return true;
}
}
