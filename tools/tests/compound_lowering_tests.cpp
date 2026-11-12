#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"
#include "Editor/NodeGraph/CompoundRecipeBuilder.h"
#include "Graph/GraphDocumentRules.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void RequireLowering(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
}

void TestCompoundExecutionLowering() {
    using namespace EditorNodeGraph;
    using namespace Stack::NodeMath;
    {
        EditorNodeGraphDefinitions::RecipeBuilding::Recipe recipe(914001, "test-independent-insertion", "Independent branches");
        const int first = recipe.Identity(), second = recipe.Identity();
        recipe.Port("first-in", "First input", first, "imageA", LogicalValueType::ColorImage);
        recipe.Port("second-in", "Second input", second, "imageA", LogicalValueType::ColorImage);
        // Deliberately reverse the public output order relative to inputs.
        recipe.Port("second-out", "Second output", second, "imageOut", LogicalValueType::ColorImage, true, false, true);
        recipe.Port("first-out", "First output", first, "imageOut", LogicalValueType::ColorImage, true, false, true);
        const auto independent = recipe.Finish();
        Graph graph; graph.Clear(); graph.SetAllowNoOutput(true);
        std::string error;
        const bool independentAccepted = graph.AddCompoundDefinition(independent, &error);
        RequireLowering(independentAccepted, "Independent branch definition: " + error);
        const int source = graph.AddImageGeneratorNode(ImageGeneratorKind::SolidColor, {})->id;
        const int output = graph.AddOutputNode({}, true)->id;
        const int compound = graph.AddCompoundNode(independent.identity, {})->id;
        RequireLowering(graph.TryConnectSockets(source, "imageOut", output, "imageIn", &error), error);
        const Link original = *graph.FindAnyInputLink(output, "imageIn");
        const auto revision = graph.GetStructureRevision();
        auto insertion = Stack::GraphModel::ProposeInsertion(graph, revision, compound, original);
        RequireLowering(insertion.analysis.valid, "Independent-branch insertion must find a through-path.");
        const auto* insertedInput = insertion.candidate.FindAnyInputLink(compound, "first-in");
        const auto* insertedOutput = insertion.candidate.FindAnyInputLink(output, "imageIn");
        RequireLowering(insertedInput && insertedInput->fromNodeId == source && insertedOutput &&
            insertedOutput->fromNodeId == compound && insertedOutput->fromSocketId == "first-out",
            "Insertion must pair a compound input with its dependent output, not the first compatible output.");
        RequireLowering(graph.GetStructureRevision() == revision && graph.GetLinks().size() == 1 &&
            graph.FindAnyInputLink(output, "imageIn")->fromNodeId == source,
            "Preparing insertion must leave the original graph and connection unchanged.");
        auto rejected = Stack::GraphModel::ProposeInsertion(graph, revision, source, original);
        RequireLowering(!rejected.analysis.valid && graph.GetLinks().size() == 1 &&
            graph.GetStructureRevision() == revision,
            "Rejected insertion must preserve the original connection and revision.");
    }
    {
        EditorNodeGraphDefinitions::RecipeBuilding::Recipe recipe(914002, "test-native-output-dependency", "Native output dependencies");
        const int operation = recipe.Keep(recipe.graph.AddRawOperationNode(Stack::RawRecipe::GraphOperationKind::Exposure, {}));
        recipe.Port("image", "Image", operation, "imageIn", LogicalValueType::ColorImage, false, false, true);
        recipe.Port("mask", "Mask", operation, "maskIn", LogicalValueType::Mask, false, true);
        recipe.Port("before", "Before", operation, "inputImageOut", LogicalValueType::ColorImage, true);
        recipe.Port("after", "After", operation, "imageOut", LogicalValueType::ColorImage, true);
        Graph graph; graph.Clear(); graph.SetAllowNoOutput(true);
        const auto definition = recipe.Finish();
        std::string error;
        const bool nativeAccepted = graph.AddCompoundDefinition(definition, &error);
        RequireLowering(nativeAccepted, "Native dependency definition: " + error);
        const int compound = graph.AddCompoundNode(definition.identity, {})->id;
        std::vector<std::string> dependencies;
        RequireLowering(graph.ResolveCompoundOutputInputDependencies(compound, "before", dependencies, &error), error);
        RequireLowering(dependencies == std::vector<std::string>{"image"},
            "Native before-image compound output must not depend on the operation mask.");
        RequireLowering(graph.ResolveCompoundOutputInputDependencies(compound, "after", dependencies, &error), error);
        RequireLowering(std::find(dependencies.begin(), dependencies.end(), "mask") != dependencies.end(),
            "Native processed compound output must retain its mask dependency.");
    }
    const auto& definition = EditorNodeGraphDefinitions::GetShippedCompoundTemplates().front();
    Graph authored;
    authored.SetAllowNoOutput(true);
    std::string error;
    RequireLowering(authored.AddCompoundDefinition(definition, &error), error);
    int first = 0, last = 0;
    for (int i = 0; i < 257; ++i) {
        const auto* node = authored.AddCompoundNode(definition.identity, {float(i), 0});
        RequireLowering(node != nullptr, "Flat compound fixture could not create an instance.");
        if (!first) first = node->id;
        last = node->id;
    }
    SetCompoundParameterOverride(authored.FindNode(first)->compound.instance, "add", 0.25);
    Graph lowered;
    CompoundExpansionResult expansion;
    RequireLowering(authored.ExpandAllCompoundNodes(lowered, &expansion), expansion.error);
    RequireLowering(expansion.success && expansion.authoredCompoundNodeIds.size() == 257 &&
        lowered.GetNodes().size() == 514 && lowered.GetLinks().size() == 257 &&
        std::none_of(lowered.GetNodes().begin(), lowered.GetNodes().end(), [](const auto& node) {
            return node.kind == NodeKind::Compound;
        }), "257 shallow compounds must lower; sibling count is not nesting depth.");
    const auto addFor = [&](const Graph& graph, const CompoundExpansionResult& mappings, int authoredId) -> const Node* {
        const auto output = std::find_if(mappings.outputBindings.begin(), mappings.outputBindings.end(),
            [&](const auto& binding) { return binding.authoredNodeId == authoredId && binding.authoredSocketId == "image-out"; });
        if (output == mappings.outputBindings.end()) return nullptr;
        const auto* link = graph.FindAnyInputLink(output->expandedNodeId, kMixInputASocketId);
        return link ? graph.FindNode(link->fromNodeId) : nullptr;
    };
    const auto* firstAdd = addFor(lowered, expansion, first);
    const auto* lastAdd = addFor(lowered, expansion, last);
    RequireLowering(firstAdd && lastAdd && std::abs(firstAdd->dataMathSettings.constantB - .25f) < 1e-6f &&
        lastAdd->dataMathSettings.constantB == 0,
        "Shared exact-definition plans must preserve independent instance overrides.");
    const auto stableUuid = firstAdd->instanceUuid;
    SetCompoundParameterOverride(authored.FindNode(first)->compound.instance, "add", 0.5);
    Graph rebound;
    CompoundExpansionResult reboundMappings;
    RequireLowering(authored.ExpandAllCompoundNodes(rebound, &reboundMappings), reboundMappings.error);
    const auto* reboundAdd = addFor(rebound, reboundMappings, first);
    RequireLowering(reboundAdd && reboundAdd->instanceUuid == stableUuid &&
        std::abs(reboundAdd->dataMathSettings.constantB - .5f) < 1e-6f &&
        firstAdd->dataMathSettings.constantB == .25f && authored.GetNodes().size() == 257,
        "Rebinding cached plans must preserve stable execution identities and immutable prior results.");

    Graph recursive;
    recursive.SetAllowNoOutput(true);
    RequireLowering(recursive.AddCompoundDefinition(definition, &error), error);
    recursive.AddCompoundNode(definition.identity, {});
    recursive.GetCompoundDefinitions().front().dependencies.push_back(definition.identity);
    const auto sentinelNodes = rebound.GetNodes().size();
    RequireLowering(!recursive.ExpandAllCompoundNodes(rebound, &reboundMappings) &&
        reboundMappings.error.find("Recursive") != std::string::npos && rebound.GetNodes().size() == sentinelNodes,
        "Recursive catalog rejection must preserve the caller's previously compiled graph.");

    Graph changed = authored;
    changed.GetCompoundDefinitions().front().label += " modified without new identity";
    RequireLowering(!changed.ExpandAllCompoundNodes(rebound, &reboundMappings) &&
        reboundMappings.error.find("exact identity") != std::string::npos && rebound.GetNodes().size() == sentinelNodes,
        "A warm immutable plan must not conceal content modified without a new exact definition identity.");

    Graph exhausted = authored;
    exhausted.SetNextNodeId(std::numeric_limits<int>::max());
    RequireLowering(!exhausted.ExpandAllCompoundNodes(rebound, &reboundMappings) &&
        reboundMappings.error.find("node IDs") != std::string::npos && rebound.GetNodes().size() == sentinelNodes,
        "Execution identity exhaustion must reject atomically.");
}
