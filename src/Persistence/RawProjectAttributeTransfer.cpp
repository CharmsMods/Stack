#include "Persistence/RawProjectAttributeTransfer.h"

#include "Persistence/ProjectStore.h"
#include "Persistence/RawProjectEditPipeline.h"

#include <algorithm>
#include <limits>

namespace Stack::Project {
namespace {

const nlohmann::json* FindManagedGraphViewTransformLayer(
    const RawProjectSnapshot& snapshot,
    const std::string& sourceSetId) {
    const MultiFrameSourceSet* sourceSet =
        FindSourceSet(snapshot, sourceSetId);
    if (!sourceSet || !snapshot.pipelineData.is_object()) return nullptr;
    const std::string nodeUuid = sourceSet->settings.value(
        "graphViewTransformNodeUuid", std::string());
    if (nodeUuid.empty()) return nullptr;
    auto graph = snapshot.pipelineData.find("nodeGraph");
    if (graph == snapshot.pipelineData.end()) {
        graph = snapshot.pipelineData.find("graph");
    }
    const auto layers = snapshot.pipelineData.find("layers");
    if (graph == snapshot.pipelineData.end() || !graph->is_object() ||
        layers == snapshot.pipelineData.end() || !layers->is_array()) {
        return nullptr;
    }
    const auto nodes = graph->find("nodes");
    if (nodes == graph->end() || !nodes->is_array()) return nullptr;
    for (const nlohmann::json& node : *nodes) {
        if (!node.is_object() ||
            node.value("instanceUuid", std::string()) != nodeUuid) {
            continue;
        }
        const int layerIndex = node.value("layerIndex", -1);
        if (layerIndex < 0 ||
            layerIndex >= static_cast<int>(layers->size()) ||
            !(*layers)[static_cast<std::size_t>(layerIndex)].is_object()) {
            return nullptr;
        }
        return &(*layers)[static_cast<std::size_t>(layerIndex)];
    }
    return nullptr;
}

nlohmann::json* FindManagedGraphViewTransformLayer(
    RawProjectSnapshot& snapshot,
    const std::string& sourceSetId) {
    return const_cast<nlohmann::json*>(
        FindManagedGraphViewTransformLayer(
            static_cast<const RawProjectSnapshot&>(snapshot),
            sourceSetId));
}

} // namespace

bool CaptureRawEditAttributesFromProject(
    const std::filesystem::path& projectPath,
    const std::string& sourceSetId,
    Stack::RawRecipe::RawEditAttributeBundle& bundle,
    std::string* errorMessage) {
    const ProjectStoreOpenResult opened = OpenProjectStore(projectPath);
    if (!opened) {
        if (errorMessage) {
            *errorMessage = opened.message.empty()
                ? "The RAW project could not be opened."
                : opened.message;
        }
        return false;
    }
    RawProjectEditRecipeBinding binding;
    std::string resolveError;
    if (!ResolveRawProjectEditRecipe(
            opened.snapshot, sourceSetId, binding, &resolveError)) {
        if (errorMessage) *errorMessage = std::move(resolveError);
        return false;
    }
    if (binding.multiFrameResult &&
        binding.viewTransformPlacement == "graph") {
        if (const nlohmann::json* layer =
                FindManagedGraphViewTransformLayer(
                    opened.snapshot, binding.sourceSetId)) {
            binding.recipe.viewTransform.layerJson = *layer;
            binding.recipe.viewTransform.layerJson["enabled"] = false;
        }
    }
    bundle = Stack::RawRecipe::CaptureRawEditAttributeBundle(
        binding.recipe,
        binding.sourceLabel,
        binding.sourceKind,
        binding.viewTransformPlacement);
    if (errorMessage) errorMessage->clear();
    return true;
}

RawProjectAttributeTransferResult PasteRawEditAttributesIntoProject(
    const std::filesystem::path& projectPath,
    const std::string& sourceSetId,
    const Stack::RawRecipe::RawEditAttributeBundle& bundle,
    const std::vector<std::string>& selectedKeys) {
    RawProjectAttributeTransferResult result;
    ProjectStoreOpenResult opened = OpenProjectStore(projectPath);
    if (!opened) {
        result.errorMessage = opened.message.empty()
            ? "The target RAW project could not be opened."
            : opened.message;
        return result;
    }
    if (opened.store->IsReadOnlyRecovery()) {
        result.errorMessage =
            "The target is a recovered read-only project. Save a repaired copy first.";
        return result;
    }

    RawProjectEditRecipeBinding binding;
    if (!ResolveRawProjectEditRecipe(
            opened.snapshot,
            sourceSetId,
            binding,
            &result.errorMessage)) {
        return result;
    }

    Stack::RawRecipe::RawEditAttributeTargetCompatibility compatibility;
    compatibility.postCfaMerge = binding.multiFrameResult;
    compatibility.hdrMerge = binding.hdrResult;
    compatibility.graphViewTransform =
        binding.viewTransformPlacement == "graph";
    const nlohmann::json* graphViewLayer =
        binding.viewTransformPlacement == "graph"
        ? FindManagedGraphViewTransformLayer(
            opened.snapshot, binding.sourceSetId)
        : nullptr;
    compatibility.canUpdateGraphViewTransform = graphViewLayer != nullptr;
    if (graphViewLayer) {
        binding.recipe.viewTransform.layerJson = *graphViewLayer;
        binding.recipe.viewTransform.layerJson["enabled"] = false;
    }
    const Stack::RawRecipe::RawEditAttributeSelectionPlan plan =
        Stack::RawRecipe::PlanRawEditAttributeSelectionForTarget(
            bundle, selectedKeys, compatibility);
    result.warnings = plan.warnings;
    if (plan.applicableKeys.empty()) {
        result.success = true;
        result.skipped = true;
        result.committedStorageRevision =
            opened.snapshot.persistedStorageRevision;
        return result;
    }

    std::string targetPlacement = binding.viewTransformPlacement;
    const Stack::RawRecipe::RawEditAttributeApplyResult applied =
        Stack::RawRecipe::ApplyRawEditAttributeBundle(
            bundle,
            plan.applicableKeys,
            binding.recipe,
            &targetPlacement);
    result.appliedKeys = applied.appliedKeys;
    result.warnings.insert(
        result.warnings.end(),
        applied.warnings.begin(),
        applied.warnings.end());
    if (!applied.success) {
        result.errorMessage = applied.errorMessage;
        return result;
    }
    if (!applied.changed) {
        result.success = true;
        result.changed = false;
        result.committedStorageRevision =
            opened.snapshot.persistedStorageRevision;
        return result;
    }

    // Offline batch transfer preserves graph topology. Keep the compact/post
    // recipe's enabled flag consistent with the already-authoritative target
    // placement even if the copied source used the other placement.
    binding.recipe.viewTransform.layerJson["enabled"] =
        binding.viewTransformPlacement == "internal";
    RawProjectSnapshot candidate = opened.snapshot;
    bool graphViewLayerChanged = false;
    if (binding.viewTransformPlacement == "graph" &&
        compatibility.canUpdateGraphViewTransform) {
        if (nlohmann::json* layer =
                FindManagedGraphViewTransformLayer(
                    candidate, binding.sourceSetId)) {
            nlohmann::json updatedLayer =
                binding.recipe.viewTransform.layerJson;
            updatedLayer.erase("enabled");
            graphViewLayerChanged = *layer != updatedLayer;
            *layer = std::move(updatedLayer);
        }
    }
    const RawProjectEditRecipeUpdate stored = StoreRawProjectEditRecipe(
        candidate, binding, binding.recipe);
    if (!stored.success) {
        result.errorMessage = stored.errorMessage;
        return result;
    }
    if (!stored.changed && !graphViewLayerChanged) {
        result.success = true;
        result.changed = false;
        result.committedStorageRevision =
            opened.snapshot.persistedStorageRevision;
        return result;
    }
    if (candidate.dirtyRevision == std::numeric_limits<std::uint64_t>::max()) {
        result.errorMessage = "The target project's edit revision is exhausted.";
        return result;
    }
    ++candidate.dirtyRevision;

    const ProjectStoreTransaction transaction = opened.store->BeginTransaction(
        opened.snapshot.persistedStorageRevision);
    if (!transaction) {
        result.errorMessage =
            "The target project changed before the paste transaction began.";
        return result;
    }
    const ProjectStoreCommitResult committed =
        opened.store->Commit(transaction, candidate);
    if (!committed) {
        opened.store->Abort(transaction);
        result.errorMessage = committed.message.empty()
            ? "The target project could not save the pasted attributes."
            : committed.message;
        return result;
    }
    result.success = true;
    result.changed = true;
    result.committedStorageRevision = committed.committedStorageRevision;
    return result;
}

} // namespace Stack::Project
