#include "Raw/RawGraphOperation.h"
#include "Persistence/RawProjectEditPipeline.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace Stack::Project {
namespace {

bool IsStoredRecipe(const nlohmann::json& value) {
    return value.is_object() && value.contains("rawRecipeVersion");
}

enum class GraphMergedResultKind {
    None,
    Burst,
    Hdr
};

struct GraphMergedResult {
    GraphMergedResultKind kind = GraphMergedResultKind::None;
    std::unordered_set<std::string> sourceSetIds;
};

GraphMergedResult ResolveGraphMergedResult(
    const RawProjectSnapshot& snapshot) {
    GraphMergedResult result;
    const MultiFrameGraphDocument& graph = snapshot.multiFrameGraph;
    if (graph.outputNodeId.empty() || graph.nodes.empty()) {
        return result;
    }
    const MultiFrameGraphNode* output =
        FindMultiFrameGraphNode(graph, graph.outputNodeId);
    if (output == nullptr || output->kind != MultiFrameGraphNodeKind::Output) {
        return result;
    }

    const MultiFrameGraphLink* outputLink = nullptr;
    for (const MultiFrameGraphLink& link : graph.links) {
        if (link.toNodeId != output->nodeId) {
            continue;
        }
        if (outputLink != nullptr) {
            return {};
        }
        outputLink = &link;
    }
    if (outputLink == nullptr) {
        return result;
    }
    const MultiFrameGraphNode* producer =
        FindMultiFrameGraphNode(graph, outputLink->fromNodeId);
    if (producer == nullptr) {
        return result;
    }
    if (producer->kind == MultiFrameGraphNodeKind::BurstDenoise) {
        result.kind = GraphMergedResultKind::Burst;
    } else if (producer->kind == MultiFrameGraphNodeKind::HdrMerge) {
        result.kind = GraphMergedResultKind::Hdr;
    } else {
        return result;
    }

    std::vector<std::string> pending { producer->nodeId };
    std::unordered_set<std::string> visited;
    while (!pending.empty()) {
        const std::string nodeId = std::move(pending.back());
        pending.pop_back();
        if (!visited.insert(nodeId).second) {
            continue;
        }
        const MultiFrameGraphNode* node =
            FindMultiFrameGraphNode(graph, nodeId);
        if (node != nullptr && !node->sourceSetId.empty()) {
            result.sourceSetIds.insert(node->sourceSetId);
        }
        for (const MultiFrameGraphLink& link : graph.links) {
            if (link.toNodeId == nodeId) {
                pending.push_back(link.fromNodeId);
            }
        }
    }
    return result;
}

Stack::RawRecipe::RawDevelopmentRecipe DefaultMultiFramePostRecipe(
    const RawProjectSnapshot& snapshot,
    const MultiFrameSourceSet& sourceSet,
    bool hdr) {
    return Stack::RawRecipe::MakeDefaultRecipe(
        std::string(hdr ? "hdr://" : "mfd://") + snapshot.projectId + "/" +
            sourceSet.sourceSetId,
        sourceSet.name + (hdr ? " HDR result" : " developed result"));
}

} // namespace

bool ResolveRawProjectEditRecipe(
    const RawProjectSnapshot& snapshot,
    const std::string& requestedSourceSetId,
    RawProjectEditRecipeBinding& binding,
    std::string* errorMessage) {
    binding = {};
    const nlohmann::json storedRecipe = snapshot.rawWorkspaceData.value(
        "rawRecipe", nlohmann::json::object());
    const bool hasSingleRawRecipe = IsStoredRecipe(storedRecipe);
    const auto resolveSingleRecipe = [&]() {
        if (!IsStoredRecipe(storedRecipe)) {
            if (errorMessage) {
                *errorMessage =
                    "The single-image project has no editable RAW recipe.";
            }
            return false;
        }
        binding.recipe = Stack::RawRecipe::DeserializeRecipe(storedRecipe);
        binding.sourceLabel = snapshot.projectName.empty()
            ? Stack::RawRecipe::RecipeDisplayName(binding.recipe)
            : snapshot.projectName;
        binding.sourceKind = "single-raw";
        binding.viewTransformPlacement =
            Stack::RawRecipe::IsViewTransformEnabled(binding.recipe)
                ? "internal"
                : "graph";
        if (errorMessage) errorMessage->clear();
        return true;
    };
    if (snapshot.sourceSets.empty()) {
        if (!requestedSourceSetId.empty()) {
            if (errorMessage) *errorMessage = "The requested source set no longer exists in the project.";
            return false;
        }
        return resolveSingleRecipe();
    }

    std::string sourceSetId = requestedSourceSetId.empty()
        ? snapshot.activeSourceSetId
        : requestedSourceSetId;
    const GraphMergedResult graphResult = ResolveGraphMergedResult(snapshot);
    const auto graphUsesSourceSet = [&](const std::string& candidateId) {
        return graphResult.kind != GraphMergedResultKind::None &&
            !candidateId.empty() &&
            (graphResult.sourceSetIds.empty() ||
             graphResult.sourceSetIds.find(candidateId) !=
                 graphResult.sourceSetIds.end());
    };
    if (sourceSetId.empty() && graphResult.sourceSetIds.size() == 1u) {
        sourceSetId = *graphResult.sourceSetIds.begin();
    }
    const MultiFrameSourceSet* sourceSet = FindSourceSet(snapshot, sourceSetId);
    if (!requestedSourceSetId.empty() && !sourceSet) {
        if (errorMessage) *errorMessage = "The requested source set no longer exists in the project.";
        return false;
    }
    const auto isEditableMergedResult = [](const MultiFrameSourceSet& candidate) {
        return candidate.operationIntent ==
                MultiFrameOperationIntent::RawBurstDenoise ||
            candidate.operationIntent ==
                MultiFrameOperationIntent::RawBurstHdr;
    };
    if (sourceSet != nullptr && !isEditableMergedResult(*sourceSet) &&
        !graphUsesSourceSet(sourceSet->sourceSetId)) {
        if (!requestedSourceSetId.empty()) {
            if (errorMessage) {
                *errorMessage =
                    "The selected source set has no finished CFA merge to edit.";
            }
            return false;
        }
        sourceSet = nullptr;
    }
    if (sourceSet == nullptr) {
        const MultiFrameSourceSet* onlyEditable = nullptr;
        for (const MultiFrameSourceSet& candidate : snapshot.sourceSets) {
            if (!isEditableMergedResult(candidate) &&
                !graphUsesSourceSet(candidate.sourceSetId)) {
                continue;
            }
            if (onlyEditable != nullptr) {
                onlyEditable = nullptr;
                break;
            }
            onlyEditable = &candidate;
        }
        sourceSet = onlyEditable;
    }
    if (sourceSet == nullptr && hasSingleRawRecipe) {
        return resolveSingleRecipe();
    }
    if (sourceSet == nullptr) {
        if (errorMessage) {
            *errorMessage =
                "The project does not identify which multi-frame result to copy.";
        }
        return false;
    }

    const bool hdr = graphUsesSourceSet(sourceSet->sourceSetId)
        ? graphResult.kind == GraphMergedResultKind::Hdr
        : sourceSet->operationIntent ==
            MultiFrameOperationIntent::RawBurstHdr;
    const char* postRecipeKey = hdr
        ? "sharedPostHdrRecipe"
        : "sharedPostMfdRecipe";
    const nlohmann::json storedPost = sourceSet->settings.value(
        postRecipeKey, nlohmann::json::object());
    binding.recipe = IsStoredRecipe(storedPost)
        ? Stack::RawRecipe::DeserializeRecipe(storedPost)
        : DefaultMultiFramePostRecipe(snapshot, *sourceSet, hdr);

    if (!hdr) {
        const nlohmann::json storedPre = sourceSet->settings.value(
            "sharedPreMfdRecipe", nlohmann::json::object());
        if (IsStoredRecipe(storedPre)) {
            const Stack::RawRecipe::RawDevelopmentRecipe pre =
                Stack::RawRecipe::DeserializeRecipe(storedPre);
            binding.recipe.technical.mosaicDenoise =
                pre.technical.mosaicDenoise;
            binding.recipe.cropRotation = pre.cropRotation;
        }
    }
    binding.recipe.technical.processingVersion =
        Raw::RawProcessingVersion::TruthfulV2;
    binding.sourceSetId = sourceSet->sourceSetId;
    binding.sourceLabel = snapshot.projectName.empty()
        ? sourceSet->name
        : snapshot.projectName;
    binding.sourceKind = "post-cfa-merge";
    binding.viewTransformPlacement =
        sourceSet->settings.value(
            "viewTransformPlacement", std::string("internal")) == "graph"
            ? "graph"
            : "internal";
    binding.multiFrameResult = true;
    binding.hdrResult = hdr;
    if (errorMessage) errorMessage->clear();
    return true;
}

bool RebaseSingleRawRecipeToManagedAsset(
    RawProjectSnapshot& snapshot,
    const std::filesystem::path& projectRoot,
    std::string* errorMessage) {
    if (IsMultiFrameProjectDocument(snapshot) ||
        !snapshot.rawWorkspaceData.is_object()) {
        if (errorMessage) errorMessage->clear();
        return true;
    }
    const nlohmann::json storedRecipe = snapshot.rawWorkspaceData.value(
        "rawRecipe", nlohmann::json::object());
    if (!IsStoredRecipe(storedRecipe)) {
        if (errorMessage) errorMessage->clear();
        return true;
    }

    std::string assetId = snapshot.rawWorkspaceData.value(
        "managedAssetId", std::string());
    if (assetId.empty() && snapshot.embeddedAssets.size() == 1u) {
        assetId = snapshot.embeddedAssets.front().assetId;
    }
    const EmbeddedAssetRecord* asset = FindEmbeddedAsset(snapshot, assetId);
    if (asset == nullptr || asset->projectAssetPath.empty()) {
        if (errorMessage) {
            *errorMessage =
                "The current single-image RAW project has no managed source asset.";
        }
        return false;
    }

    RawProjectEditRecipeBinding binding;
    std::string resolveError;
    if (!ResolveRawProjectEditRecipe(snapshot, {}, binding, &resolveError) ||
        binding.multiFrameResult) {
        if (errorMessage) {
            *errorMessage = resolveError.empty()
                ? "The single-image RAW recipe could not be rebound to its managed asset."
                : std::move(resolveError);
        }
        return false;
    }

    Stack::RawRecipe::RawDevelopmentRecipe recipe = binding.recipe;
    recipe.source.sourcePath = (
        projectRoot / asset->projectAssetPath).lexically_normal().string();
    const std::uint64_t preservedPostRecipeRevision =
        snapshot.postRecipeRevision;
    const RawProjectEditRecipeUpdate update = StoreRawProjectEditRecipe(
        snapshot, binding, recipe);
    snapshot.postRecipeRevision = preservedPostRecipeRevision;
    if (!update.success) {
        if (errorMessage) {
            *errorMessage = update.errorMessage.empty()
                ? "The managed RAW path could not be stored in the copied project."
                : update.errorMessage;
        }
        return false;
    }
    if (errorMessage) errorMessage->clear();
    return true;
}

RawProjectEditRecipeUpdate StoreRawProjectEditRecipe(
    RawProjectSnapshot& snapshot,
    const RawProjectEditRecipeBinding& binding,
    const Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe) {
    RawProjectEditRecipeUpdate result;
    const auto serializePost = [&](const auto& recipe) {
        return snapshot.pipelineData.contains("rawLayerStack")
            ? Stack::RawRecipe::SerializeWorkspaceSourceRecipe(recipe)
            : Stack::RawRecipe::SerializeRecipe(recipe);
    };
    if (!binding.multiFrameResult) {
        if (!snapshot.rawWorkspaceData.is_object()) {
            snapshot.rawWorkspaceData = nlohmann::json::object();
        }
        const nlohmann::json serialized =
            serializePost(editedRecipe);
        result.changed = snapshot.rawWorkspaceData.value(
            "rawRecipe", nlohmann::json::object()) != serialized;
        snapshot.rawWorkspaceData["rawRecipe"] = serialized;
        if (result.changed) {
            std::string pipelineError;
            if (!StoreSingleRawRecipeInPipeline(
                    snapshot.pipelineData, editedRecipe, &pipelineError)) {
                result.errorMessage = std::move(pipelineError);
                return result;
            }
            ++snapshot.postRecipeRevision;
        }
        result.postMergeChanged = result.changed;
        result.success = true;
        return result;
    }

    MultiFrameSourceSet* sourceSet =
        FindSourceSet(snapshot, binding.sourceSetId);
    if (sourceSet == nullptr) {
        result.errorMessage =
            "The multi-frame source set no longer exists in the project.";
        return result;
    }
    RawProjectEditRecipeBinding currentBinding;
    std::string currentBindingError;
    if (!ResolveRawProjectEditRecipe(
            snapshot,
            binding.sourceSetId,
            currentBinding,
            &currentBindingError) ||
        !currentBinding.multiFrameResult ||
        currentBinding.hdrResult != binding.hdrResult) {
        result.errorMessage =
            currentBindingError.empty()
                ? "The multi-frame processor changed while RAW edits were being applied."
                : std::move(currentBindingError);
        return result;
    }
    const bool hdr = binding.hdrResult;

    Stack::RawRecipe::RawDevelopmentRecipe postRecipe = editedRecipe;
    postRecipe.technical.processingVersion =
        Raw::RawProcessingVersion::TruthfulV2;
    postRecipe.technical.mosaicDenoise = Raw::RawMosaicDenoiseSettings {};

    if (hdr) {
        const nlohmann::json serializedPost =
            serializePost(postRecipe);
        result.postMergeChanged = sourceSet->settings.value(
            "sharedPostHdrRecipe", nlohmann::json::object()) != serializedPost;
        if (result.postMergeChanged) {
            sourceSet->settings["sharedPostHdrRecipe"] = serializedPost;
            ++snapshot.postRecipeRevision;
        }
        result.changed = result.postMergeChanged;
        result.success = true;
        return result;
    }

    postRecipe.cropRotation = {};
    Stack::RawRecipe::RawDevelopmentRecipe preRecipe =
        Stack::RawRecipe::MakeDefaultRecipe(
            "mfd-pre://" + snapshot.projectId + "/" + sourceSet->sourceSetId,
            sourceSet->name + " shared input processing");
    const nlohmann::json previousPre = sourceSet->settings.value(
        "sharedPreMfdRecipe", nlohmann::json::object());
    if (IsStoredRecipe(previousPre)) {
        preRecipe = Stack::RawRecipe::DeserializeRecipe(previousPre);
    }
    preRecipe.technical.processingVersion =
        Raw::RawProcessingVersion::TruthfulV2;
    preRecipe.technical.mosaicDenoise =
        editedRecipe.technical.mosaicDenoise;
    preRecipe.cropRotation = editedRecipe.cropRotation;

    const nlohmann::json serializedPre =
        Stack::RawRecipe::SerializeRecipe(preRecipe);
    const nlohmann::json serializedPost =
        serializePost(postRecipe);
    result.preMergeChanged = previousPre != serializedPre;
    result.postMergeChanged = sourceSet->settings.value(
        "sharedPostMfdRecipe", nlohmann::json::object()) != serializedPost;
    if (result.preMergeChanged || result.postMergeChanged) {
        sourceSet->operationSchemaVersion = kMfdOperationSchemaVersion;
        sourceSet->settings["schemaVersion"] = kMfdOperationSchemaVersion;
        sourceSet->settings["sharedPreMfdRecipe"] = serializedPre;
        sourceSet->settings["sharedPostMfdRecipe"] = serializedPost;
    }
    if (result.preMergeChanged) {
        ++snapshot.mfdInputRevision;
        nlohmann::json resultState = sourceSet->settings.value(
            "result", nlohmann::json::object());
        if (!resultState.is_object()) {
            resultState = nlohmann::json::object();
        }
        resultState["state"] = "stale";
        sourceSet->settings["result"] = std::move(resultState);
    }
    if (result.postMergeChanged) {
        ++snapshot.postRecipeRevision;
    }
    result.changed = result.preMergeChanged || result.postMergeChanged;
    result.success = true;
    return result;
}

bool StoreSingleRawRecipeInPipeline(
    nlohmann::json& pipelineData,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    std::string* errorMessage) {
    if (!pipelineData.is_object()) {
        if (errorMessage) {
            *errorMessage = "The single-image project graph is missing.";
        }
        return false;
    }
    auto graph = pipelineData.find("graph");
    if (graph == pipelineData.end()) {
        graph = pipelineData.find("nodeGraph");
    }
    if (graph == pipelineData.end() || !graph->is_object()) {
        if (errorMessage) {
            *errorMessage = "The single-image project graph is invalid.";
        }
        return false;
    }
    auto nodes = graph->find("nodes");
    if (nodes == graph->end() || !nodes->is_array()) {
        if (errorMessage) {
            *errorMessage = "The single-image project graph has no nodes.";
        }
        return false;
    }

    nlohmann::json* rawNode = nullptr;
    for (nlohmann::json& node : *nodes) {
        if (!node.is_object() ||
            node.value("kind", std::string()) != "RawDevelopment") {
            continue;
        }
        if (rawNode != nullptr) {
            if (errorMessage) {
                *errorMessage =
                    "The single-image project has more than one RAW Development owner.";
            }
            return false;
        }
        rawNode = &node;
    }
    if (rawNode == nullptr) {
        if (errorMessage) {
            *errorMessage =
                "The single-image project has no RAW Development owner.";
        }
        return false;
    }
    (*rawNode)["rawRecipe"] = pipelineData.contains("rawLayerStack") ? Stack::RawRecipe::SerializeWorkspaceSourceRecipe(recipe) : Stack::RawRecipe::SerializeRecipe(recipe);
    (*rawNode)["rawProjectStatus"] = "Edited";
    (*rawNode)["rawEdited"] = true;
    (*rawNode)["rawAutosaved"] = false;
    if (errorMessage) errorMessage->clear();
    return true;
}

} // namespace Stack::Project
