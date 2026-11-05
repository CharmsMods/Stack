#include "Project/GraphSnapshotBrightness.h"
#include "Editor/NodeGraph/BrightnessRecipe.h"
#include "Editor/NodeGraph/MathRecipes.h"
#include <algorithm>

namespace Stack::Project::GraphSnapshotInternal {
bool LowerNativeRecipes(const EditorNodeGraph::Graph& sourceGraph, const GraphSnapshotInputs& inputs,
                            const Timeline::FrameEvaluationContext& frameContext,
                            EditorNodeGraph::Graph& recipeGraph,
                            EditorNodeGraph::CompoundExpansionResult& expansion,
                            EditorNodeGraph::GraphOutputDescriptions& authoredWireIssues,
                            EditorNodeGraph::GraphOutputDescriptions& blockedResults, int& resolvedStageId) {
    const auto nativeRecipe = [](const auto& node) {
        return EditorNodeGraphDefinitions::IsBrightnessRecipe(node) || EditorNodeGraphDefinitions::IsMathRecipe(node);
    };
    if (std::none_of(sourceGraph.GetNodes().begin(), sourceGraph.GetNodes().end(),
                     nativeRecipe))
        return false;
    bool lowered = false;
    const auto descriptions = EditorNodeGraph::DescribeGraphOutputs(
        sourceGraph,
        inputs.outputContextOverrides ? *inputs.outputContextOverrides : EditorNodeGraph::GraphOutputContext{});
    auto recipes = sourceGraph.CloneForAnalysis();
    const auto blockRecipe = [&](const EditorNodeGraph::Node& node, const std::string& reason) {
        const auto key = EditorNodeGraph::GraphOutputIdentity(node.id, EditorNodeGraph::kImageOutputSocketId);
        if (const auto first = authoredWireIssues.find(key); first != authoredWireIssues.end()) {
            blockedResults[key] = first->second;
            return;
        }
        EditorNodeGraph::GraphOutputDescription failure;
        failure.descriptor = NodeMath::MakeUnknownDescriptor(NodeMath::LogicalValueType::Invalid);
        failure.usesArithmeticContract = true;
        NodeMath::Diagnostic issue;
        issue.ruleId = "graph.recipe";
        issue.stage = NodeMath::DiagnosticStage::Lowering;
        issue.severity = NodeMath::DiagnosticSeverity::HardError;
        issue.authoredSourceIdentity = node.instanceUuid;
        issue.affectedIdentity = key;
        issue.message = reason;
        issue.semanticFingerprint = key + reason;
        failure.diagnostics.push_back(std::move(issue));
        blockedResults[key] = std::move(failure);
    };
    for (const auto& leaf : sourceGraph.GetNodes()) {
        if (!nativeRecipe(leaf))
            continue;
        for (const auto& socket : sourceGraph.GetSockets(leaf, false)) {
            const auto key = EditorNodeGraph::GraphOutputIdentity(leaf.id, socket.id);
            const auto found = descriptions.find(key);
            if (found != descriptions.end() && found->second.invalidWire)
                authoredWireIssues.emplace(key, found->second);
        }
        if (EditorNodeGraphDefinitions::IsMathRecipe(leaf)) {
            std::string reason;
            if (!EditorNodeGraphDefinitions::BindMathRecipe(recipes, leaf.id, reason))
                blockRecipe(leaf, reason);
            continue;
        }
        const auto* wire = sourceGraph.FindInputLink(leaf.id, "imageIn");
        const auto source =
            wire ? descriptions.find(EditorNodeGraph::GraphOutputIdentity(wire->fromNodeId, wire->fromSocketId))
                 : descriptions.end();
        if (!wire || source == descriptions.end()) {
            blockRecipe(leaf, "Connect a Channel, RGB or RGBA input to Brightness.");
            continue;
        }
        auto settings = leaf.layerParameters;
        Stack::Timeline::ApplyFrameEvaluationContextToLayerJson(frameContext, leaf.id, leaf.layerType, settings,
                                                                inputs.graphId, leaf.instanceUuid);
        std::string reason;
        if (!EditorNodeGraphDefinitions::BindBrightnessRecipe(recipes, leaf.id, source->second.descriptor,
                                                              settings, reason))
            blockRecipe(leaf, reason);
    }
    EditorNodeGraph::CompoundExpansionResult loweredRecipes;
    if (!recipes.ExpandAllCompoundNodes(recipeGraph, &loweredRecipes)) {
        for (const auto& leaf : sourceGraph.GetNodes())
            if (nativeRecipe(leaf))
                blockRecipe(leaf, loweredRecipes.error);
    } else {
        lowered = true;
    }
    for (const auto& binding : loweredRecipes.outputBindings) {
        const auto native =
            EditorNodeGraph::GraphOutputIdentity(binding.authoredNodeId, binding.authoredSocketId);
        if (const auto issue = authoredWireIssues.find(native); issue != authoredWireIssues.end())
            blockedResults.emplace(
                EditorNodeGraph::GraphOutputIdentity(binding.expandedNodeId, binding.expandedSocketId),
                issue->second);
        for (auto& existing : expansion.outputBindings)
            if (existing.expandedNodeId == binding.authoredNodeId &&
                existing.expandedSocketId == binding.authoredSocketId) {
                existing.expandedNodeId = binding.expandedNodeId;
                existing.expandedSocketId = binding.expandedSocketId;
            }
        if (binding.authoredNodeId == inputs.stageOutputNodeId && binding.authoredSocketId == "imageOut")
            resolvedStageId = binding.expandedNodeId;
    }
    expansion.outputBindings.insert(expansion.outputBindings.end(), loweredRecipes.outputBindings.begin(),
                                    loweredRecipes.outputBindings.end());
    for (const auto& binding : expansion.outputBindings)
        if (binding.authoredNodeId == inputs.stageOutputNodeId && binding.authoredSocketId == "imageOut")
            resolvedStageId = binding.expandedNodeId;
    return lowered;
}
}
