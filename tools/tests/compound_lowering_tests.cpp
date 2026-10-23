#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"

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
