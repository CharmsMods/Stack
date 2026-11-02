#include "Raw/RawGalleryInputPolicy.h"
#include "Editor/RawAttributeTargetProject.h"

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Persistence/ProjectStore.h"
#include "Persistence/ManagedAssetPaths.h"
#include "Raw/RawLoader.h"

#include "Project/RawLayerStack.h"
#include "Persistence/ProjectPayloadAssets.h"
#include "Raw/RawTechnicalEvidence.h"

#include <algorithm>
#include <set>
#include <map>
#include <system_error>

namespace Stack::EditorRawAttributes {
namespace {

bool BuildCompactRawGraph(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    nlohmann::json& pipeline,
    std::string& error) {
    EditorNodeGraph::Graph graph;
    EditorNodeGraph::RawDevelopmentPayload payload;
    payload.recipe = recipe;
    payload.projectStatus = "Edited";
    payload.edited = true;
    payload.autosaved = false;
    EditorNodeGraph::Node* raw = graph.AddRawDevelopmentNode(
        std::move(payload), EditorNodeGraph::Vec2{ 20.0f, 120.0f });
    if (!raw) {
        error = "Failed to create the target RAW Development node.";
        return false;
    }
    const auto rawId = raw->id;
    const auto sourceUuid = raw->instanceUuid;
    EditorNodeGraph::Node* output = graph.AddOutputNode(
        EditorNodeGraph::Vec2{ 320.0f, 120.0f }, true);
    if (!output) {
        error = "Failed to create the target Output node.";
        return false;
    }
    if (!graph.TryConnectSockets(
            rawId,
            EditorNodeGraph::kImageOutputSocketId,
            output->id,
            EditorNodeGraph::kImageInputSocketId,
            &error)) {
        if (error.empty()) {
            error = "Failed to connect the target RAW project graph.";
        }
        return false;
    }
    graph.SetOutputNodeId(output->id);
    graph.SelectNode(rawId);
    pipeline = EditorNodeGraph::SerializeGraphPayload(
        nlohmann::json::array(), graph);
    pipeline["rawLayerSourceNodeUuid"] = sourceUuid;
    return true;
}

// Rebind the graph's single photo input and bypass unchecked image effects.
bool ApplyComposition(const RawRecipe::AuthoredLayerEdits& edits, const std::set<std::string>& selected,
    nlohmann::json& pipeline, const Project::EmbeddedAssetRecord& target, bool raster, std::string& error) {
    try {
        auto graph=edits.stack;
        graph.erase("rawLayerStack");
        auto& nodes=graph.at("nodeGraph").at("nodes");
        auto& links=graph.at("nodeGraph").at("links");
        const auto& neutral=pipeline.at("nodeGraph").at("nodes").front();
        std::vector<int> omit;
        unsigned inputs=0;
        std::set<std::string> optionalEffects;
        for (const auto& choice:edits.choices)
            if (choice.layerKey=="layer/@project") optionalEffects.insert(choice.key);
        for (auto& node : nodes) {
            const auto kind=node.at("kind").get<std::string>();
            const int id=node.at("id");
            const auto uuid=node.at("instanceUuid").get<std::string>();
            if (kind=="RawProjectFrame" || kind=="RawSource" || kind=="RawDecode" || kind=="HdrMerge" || kind=="MFSR")
                throw std::runtime_error("The selected graph requires camera frames or sensor data from its original project.");
            if (kind=="Image" || kind=="RawDevelopment" || kind=="MultiFrameDenoise" || kind=="MultiFrameHdr") {
                if (++inputs!=1) throw std::runtime_error("The selected graph requires more than one source image.");
                const auto x=node.value("x",0.f),y=node.value("y",0.f);
                if (raster) {
                    EditorNodeGraph::Graph inputGraph;
                    EditorNodeGraph::ImagePayload image;
                    image.sourcePath=target.originalSourcePath;image.managedAssetId=target.assetId;
                    image.label=target.originalFilename;
                    inputGraph.AddImageNode(std::move(image),{x,y});
                    node=EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(),inputGraph).at("nodeGraph").at("nodes").front();
                } else node=neutral;
                node["id"]=id;node["x"]=x;node["y"]=y;node["instanceUuid"]=uuid;
            } else {
                const auto key="layer/@project/"+uuid;
                if (optionalEffects.count(key) && !selected.count(key)) omit.push_back(id);
            }
        }
        for (const int id : omit) {
            auto upstream=std::find_if(links.begin(),links.end(),[&](const auto& l){return l.at("toNodeId")==id && l.at("toSocket")=="imageIn";});
            for (auto& link : links) if (link.at("fromNodeId")==id) {
                if (upstream==links.end() || link.at("fromSocket")!="imageOut")
                    throw std::runtime_error("A selected connection needs the omitted operation.");
                link["fromNodeId"]=upstream->at("fromNodeId"); link["fromSocket"]=upstream->at("fromSocket");
            }
            links.erase(std::remove_if(links.begin(),links.end(),[&](const auto& l){return l.at("toNodeId")==id;}),links.end());
            nodes.erase(std::remove_if(nodes.begin(),nodes.end(),[&](const auto& n){return n.at("id")==id;}),nodes.end());
        }
        std::vector<nlohmann::json*> effects;
        for (auto& node:nodes) if (node.at("kind")=="Layer") effects.push_back(&node);
        std::sort(effects.begin(),effects.end(),[](const auto* a,const auto* b) {
            return a->at("layerIndex")<b->at("layerIndex");
        });
        for (std::size_t i=0;i<effects.size();++i) (*effects[i])["layerIndex"]=i;
        Project::ProjectStoreOpenResult source;
        if (!Project::RehydratePayloadBytes(graph,[&](const auto& id,auto& bytes) {
                if (!source && !edits.sourceProject.empty()) source=Project::OpenProjectStore(std::filesystem::u8path(edits.sourceProject));
                if (!source) return false;
                const auto* asset=Project::FindEmbeddedAsset(source.snapshot,id);
                if (!asset || asset->linkedSource || asset->managedRole!="graph-payload") return false;
                auto stream=source.store->OpenAssetStream(id);
                if (!stream) return false;
                bytes.assign(std::istreambuf_iterator<char>(*stream.stream),{});
                return RawEvidence::ComputeSourceIdentity(bytes).sha256==asset->sha256;
            })) throw std::runtime_error("A selected operation needs an unavailable authored asset.");
        EditorNodeGraph::Graph checked;
        EditorNodeGraph::DeserializeGraphPayload(graph,checked,static_cast<int>(EditorNodeGraph::ExtractLayerArray(graph).size()));
        if (checked.GetNodes().size()!=nodes.size() || checked.GetLinks().size()!=links.size())
            throw std::runtime_error("A selected connection cannot run on the target image.");
        pipeline=std::move(graph);
        return true;
    } catch(const std::exception& e) { error=e.what();return false; }
}

bool ApplyAuthoredLayers(const RawRecipe::AuthoredLayerEdits& edits, const std::set<std::string>& selected,
    nlohmann::json& pipeline, std::string& error) {
    using namespace Project;
    try {
        auto stack = edits.stack.contains("rawLayerStack") ? edits.stack.at("rawLayerStack") : edits.stack;
        // Find dependencies in the authored instructions before opening any
        // raster payload. An unchecked layer may have an offline raster.
        std::map<std::string,const nlohmann::json*> byId;
        auto remember=[&](const auto& layer){byId.emplace(layer.at("id").template get<std::string>(),&layer);};
        remember(stack.at("background"));
        for (const auto& layer:stack.at("layers")) remember(layer);
        std::set<std::string> included;
        std::vector<const nlohmann::json*> pending;
        auto include=[&](const std::string& id) {
            if (id.empty() || id=="$raw-source" || included.count(id)) return;
            const auto it=byId.find(id);
            if (it==byId.end()) throw std::runtime_error("A selected layer requires a missing graph input: "+id);
            included.insert(id);pending.push_back(it->second);
        };
        for (const auto& item:byId) if (selected.count("layer/"+item.first)) include(item.first);
        while (!pending.empty()) {
            const auto& layer=*pending.back();pending.pop_back();
            if (!layer.at("layerMask").is_null()) include(layer.at("layerMask").at("layer").get<std::string>());
            if (layer.contains("imageInput") && !layer.at("imageInput").is_null())
                include(layer.at("imageInput").at("graph").get<std::string>());
            for (const auto& node:layer.at("graph").at("nodeGraph").at("nodes"))
                if (node.value("role",0)==static_cast<int>(GraphModel::NodeRole::Reference))
                    include(node.at("reference").at("graph").get<std::string>());
        }
        auto& layers=stack.at("layers");
        layers.erase(std::remove_if(layers.begin(),layers.end(),[&](const auto& layer){
            return !included.count(layer.at("id").template get<std::string>());
        }),layers.end());
        if (!included.count(stack.at("background").at("id").get<std::string>()))
            stack["background"]=SerializeRawLayerStack(RawLayerStackState{},false).at("background");
        if (stack.value("explicitRouting",false) && stack.at("imageOutput").is_object() &&
            !included.count(stack.at("imageOutput").at("graph").get<std::string>())) {
            const auto& last=layers.empty()?stack.at("background"):layers.back();
            const auto& nodes=last.at("graph").at("nodeGraph").at("nodes");
            auto output=std::find_if(nodes.begin(),nodes.end(),[](const auto& n){
                return n.value("role",0)==static_cast<int>(GraphModel::NodeRole::LayerResult);});
            if (output==nodes.end()) throw std::runtime_error("The selected layer has no image output.");
            stack["imageOutput"]={{"graph",last.at("id")},{"node",output->at("instanceUuid")},{"port","imageOut"}};
        }
        // Inserting a library asset authors its raster into the selected mask
        // node. Independent items in the source's asset browser stay there.
        stack.erase("createdAssets");
        nlohmann::json materialized = {{"rawLayerStack", std::move(stack)}};
        ProjectStoreOpenResult source;
        const auto read = [&](const std::string& id, std::vector<unsigned char>& bytes) {
            if (!source && !edits.sourceProject.empty()) source = OpenProjectStore(std::filesystem::u8path(edits.sourceProject));
            if (!source) return false;
            const auto* record = FindEmbeddedAsset(source.snapshot,id);
            if (!record || record->linkedSource ||
                (record->managedRole != "graph-payload" && record->managedRole != "created-asset")) return false;
            auto stream = source.store->OpenAssetStream(id);
            if (!stream || stream.byteLength != record->byteLength) return false;
            bytes.assign(std::istreambuf_iterator<char>(*stream.stream), {});
            const auto identity = RawEvidence::ComputeSourceIdentity(bytes);
            return identity.valid && identity.sha256 == record->sha256 && bytes.size() == record->byteLength;
        };
        if (!RehydratePayloadBytes(materialized,read))
            throw std::runtime_error("A mask or other authored asset required by the chosen edits is missing.");
        RawLayerStackState state;
        if (!DeserializeRawLayerStack(materialized.at("rawLayerStack"),state,error)) return false;
        for (auto* immutable : RawLayersInOrder(state)) {
            auto* layer = FindRawAdjustmentLayer(state,immutable->id);
            const auto key = "layer/" + layer->id;
            if (!selected.count(key)) continue; // Keep graph dependencies of selected edits intact.
            std::vector<int> omit;
            for (const auto& node : layer->graph.GetNodes())
                if (node.kind == EditorNodeGraph::NodeKind::RawOperation && !selected.count(key + "/" + node.instanceUuid))
                    omit.push_back(node.id);
            for (int id : omit) {
                auto& graph = layer->graph;
                const auto* input = graph.FindAnyInputLink(id,"imageIn");
                if (!input) throw std::runtime_error("An omitted operation has no image input to pass through.");
                const auto upstream = *input;
                std::vector<EditorNodeGraph::Link> consumers;
                graph.ForEachOutgoingLink(id,[&](const auto& link){ consumers.push_back(link); });
                if (std::any_of(consumers.begin(),consumers.end(),[](const auto& link){return link.fromSocketId != "imageOut";}))
                    throw std::runtime_error("A selected edit needs another output of an omitted operation.");
                graph.RemoveNode(id);
                for (const auto& link : consumers)
                    if (!graph.TryConnectSockets(upstream.fromNodeId,upstream.fromSocketId,link.toNodeId,link.toSocketId,&error)) return false;
            }
        }
        if (!ValidateRawLayerStack(state,error)) return false;
        pipeline["rawLayerStack"] = SerializeRawLayerStack(state);
        RawLayerStackState verified;
        return ReadRawLayersFromPipeline(pipeline, verified, error, true);
    } catch (const std::exception& e) { error=e.what(); return false; }
}

std::filesystem::path BuildUniqueEditedCopyPath(
    const std::filesystem::path& sourcePath,
    Stack::Project::ProjectStorageKind storageKind) {
    std::filesystem::path normalized = sourcePath.lexically_normal();
    const auto parent = normalized.parent_path();
    const auto sourceName = normalized.stem().u8string();
    const std::string extension = ".stack";
    const std::string baseName = (sourceName.empty()
        ? std::string("RAW Project")
        : sourceName) + " - Pasted Edits";
    for (unsigned int suffix = 1u; suffix < 10000u; ++suffix) {
        const std::string candidateName = baseName +
            (suffix == 1u ? std::string()
                          : " " + std::to_string(suffix));
        const std::filesystem::path candidate =
            parent / std::filesystem::u8path(candidateName + extension);
        std::error_code existsError;
        if (!std::filesystem::exists(candidate, existsError) &&
            !existsError) {
            return candidate;
        }
    }
    return {};
}

} // namespace

CreatedTargetProjectResult CreateProjectAndPasteAttributes(
    const std::filesystem::path& workspaceRoot,
    const Stack::RawWorkspace::SourceRecord& source,
    const Stack::RawRecipe::RawEditAttributeBundle& bundle,
    const std::vector<std::string>& selectedKeys,
    const std::filesystem::path& projectsDirectoryOverride) {
    namespace Project = Stack::Project;
    CreatedTargetProjectResult result;
    if (workspaceRoot.empty() || source.absolutePath.empty()) {
        result.transfer.errorMessage =
            "The selected RAW source is not available in a local workspace.";
        return result;
    }
    std::vector<std::string> recipeKeys;
    std::set<std::string> layerKeys;
    for (const auto& key : selectedKeys) {
        if (key.compare(0,6,"layer/") == 0) layerKeys.insert(key);
        else recipeKeys.push_back(key);
    }
    const bool graphPreset=bundle.authoredLayers && bundle.authoredLayers->stack.contains("nodeGraph") && layerKeys.count("layer/@project");
    const auto identity=RawEvidence::ComputeSourceIdentity(source.absolutePath);
    if (!identity.valid || (!source.fingerprint.empty() && identity.sha256!=source.fingerprint)) {
        result.transfer.errorMessage="Locate the target original before applying edits.";
        return result;
    }
    const bool applyLayers = bundle.authoredLayers && std::any_of(bundle.authoredLayers->choices.begin(),
        bundle.authoredLayers->choices.end(),[&](const auto& c){return c.layerKey.empty() && layerKeys.count(c.key);});
    const bool rawLayers=applyLayers && std::any_of(bundle.authoredLayers->choices.begin(),
        bundle.authoredLayers->choices.end(),[&](const auto& c){
            return c.layerKey.empty() && c.key!="layer/@project" && layerKeys.count(c.key);
        });
    const bool nativeComposition=graphPreset && !rawLayers;
    Stack::RawRecipe::RawEditAttributeTargetCompatibility compatibility;
    const Stack::RawRecipe::RawEditAttributeSelectionPlan plan =
        Stack::RawRecipe::PlanRawEditAttributeSelectionForTarget(
            bundle, recipeKeys, compatibility);
    result.transfer.warnings = plan.warnings;
    if (plan.applicableKeys.empty() && !applyLayers) {
        result.transfer.success = true;
        result.transfer.skipped = true;
        return result;
    }

    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe(
            source.absolutePath.u8string(), source.fileName);
    recipe.source.relativePathKey = source.relativePathKey;
    recipe.source.fingerprint = source.fingerprint;
    recipe.source.fileSizeBytes =
        static_cast<std::uint64_t>(source.fileSizeBytes);
    recipe.source.modifiedTimeTicks = source.modifiedTimeTicks;
    recipe.source.displayName = source.fileName;
    std::string targetPlacement = "internal";
    const Stack::RawRecipe::RawEditAttributeApplyResult applied =
        Stack::RawRecipe::ApplyRawEditAttributeBundle(
            bundle, plan.applicableKeys, recipe, &targetPlacement);
    result.transfer.appliedKeys = applied.appliedKeys;
    result.transfer.warnings.insert(
        result.transfer.warnings.end(),
        applied.warnings.begin(),
        applied.warnings.end());
    if (!plan.applicableKeys.empty() && !applied.success) {
        result.transfer.errorMessage = applied.errorMessage;
        return result;
    }
    // New projects always start with the compact internal transform. The look
    // is copied, while graph placement remains a separate structural action.
    recipe.viewTransform.layerJson["enabled"] = true;

    Stack::RawWorkspace::ManagedLayout layout =
        Stack::RawWorkspace::BuildManagedLayout(workspaceRoot);
    if ((projectsDirectoryOverride.empty() || projectsDirectoryOverride.lexically_normal() == layout.projectsDirectory) &&
        !Stack::RawWorkspace::IsMarkedCloset(layout.dataDirectory) &&
        !Stack::RawWorkspace::EnsureManagedFolders(workspaceRoot, &result.transfer.errorMessage)) return result;
    if (!projectsDirectoryOverride.empty()) {
        layout.projectsDirectory = projectsDirectoryOverride.lexically_normal();
    }
    std::filesystem::path projectRoot =
        layout.projectsDirectory /
        Stack::RawWorkspace::BuildProjectRelativePathForSource(source);
    std::error_code existsError;
    if (std::filesystem::exists(projectRoot, existsError) && !existsError) {
        const std::filesystem::path base = projectRoot;
        bool found = false;
        for (unsigned int suffix = 2u; suffix < 10000u; ++suffix) {
            const auto candidate = base.parent_path() / (base.stem().u8string() + "-" + std::to_string(suffix) + ".stack");
            existsError.clear();
            if (!std::filesystem::exists(candidate, existsError) &&
                !existsError) {
                projectRoot = std::move(candidate);
                found = true;
                break;
            }
        }
        if (!found) {
            result.transfer.errorMessage =
                "Stack could not choose a unique project folder for this image.";
            return result;
        }
    }
    result.projectPath = projectRoot;

    const std::string projectName = source.stem.empty()
        ? source.fileName
        : source.stem;
    Project::RawProjectSnapshot bootstrap;
    bootstrap.projectId = Project::GenerateStableUuid();
    bootstrap.projectName = projectName.empty()
        ? "Untitled RAW Project"
        : projectName;
    bootstrap.projectKindHint = nativeComposition ? StackBinaryFormat::kEditorProjectKind : StackBinaryFormat::kRawProjectKind;
    bootstrap.lifecycle.creationOrigin =
        Project::ProjectCreationOrigin::MultiSelection;
    bootstrap.lifecycle.cleanupWhenUntouched = false;
    bootstrap.lifecycle.explicitlyRetained = true;

    Project::ProjectStoreOpenResult created = Project::CreateProjectStore(
        projectRoot,
        Project::ProjectStorageKind::WorkingFile,
        bootstrap);
    if (!created) {
        result.transfer.errorMessage = created.message.empty()
            ? "Stack could not create the target project folder."
            : created.message;
        return result;
    }
    const auto removeFailedNewProject = [&]() {
        created.store.reset();
        std::error_code cleanup;
        const std::filesystem::path normalizedRoot =
            projectRoot.lexically_normal();
        const std::filesystem::path normalizedProjects =
            layout.projectsDirectory.lexically_normal();
        const auto relative = normalizedRoot.lexically_relative(
            normalizedProjects);
        const bool safelyContained =
            !normalizedRoot.empty() &&
            !normalizedProjects.empty() &&
            normalizedRoot != normalizedProjects &&
            !relative.empty() &&
            !relative.is_absolute() &&
            *relative.begin() != std::filesystem::path("..");
        if (safelyContained) {
            std::filesystem::remove(normalizedRoot, cleanup);
        }
    };

    const Project::ProjectStoreTransaction transaction =
        created.store->BeginTransaction(
            created.snapshot.persistedStorageRevision);
    if (!transaction) {
        removeFailedNewProject();
        result.transfer.errorMessage =
            "Stack could not begin the target project import.";
        return result;
    }

    nlohmann::json captureSummary = nlohmann::json::object();
    Raw::RawMetadata metadata;
    if (Raw::RawLoader::LoadMetadata(source.absolutePath.u8string(), metadata)) {
        captureSummary = Project::SerializeRawCaptureCompatibilitySummary(
            Project::BuildRawCaptureCompatibilitySummary(metadata));
    }
    const bool raster=RawWorkspace::IsGalleryRasterPath(source.absolutePath);
    if (raster && !nativeComposition) {
        Raw::RawImageData decoded;
        if (!Raw::RawLoader::LoadFile(source.absolutePath.u8string(),decoded)) {
            created.store->Abort(transaction);removeFailedNewProject();
            result.transfer.errorMessage=decoded.metadata.error;
            return result;
        }
    }
    Project::EmbeddedAssetRecord asset;
    std::string stageError;
    if (!created.store->StageAssetFile(
            transaction,
            source.absolutePath,
            raster ? Project::MultiFrameInputFamily::Raster : Project::MultiFrameInputFamily::Raw,
            captureSummary,
            asset,
            &stageError)) {
        created.store->Abort(transaction);
        removeFailedNewProject();
        result.transfer.errorMessage = stageError.empty()
            ? "Stack could not link the RAW source to the new project."
            : stageError;
        return result;
    }

    if (!source.fingerprint.empty() && asset.sha256 != source.fingerprint) {
        created.store->Abort(transaction);
        removeFailedNewProject();
        result.transfer.errorMessage = "The original at this path has changed. Locate the target original before applying edits.";
        return result;
    }

    recipe.source.sourcePath =
        Stack::Project::ManagedAssetFilePath(projectRoot, asset).lexically_normal().u8string();
    recipe.source.fingerprint = asset.originalFileFingerprint;
    recipe.source.fileSizeBytes = asset.byteLength;
    nlohmann::json pipeline;
    std::string graphError;
    if (!BuildCompactRawGraph(recipe, pipeline, graphError) ||
        (graphPreset && !ApplyComposition(*bundle.authoredLayers,layerKeys,pipeline,asset,raster && nativeComposition,graphError)) ||
        (rawLayers && !ApplyAuthoredLayers(*bundle.authoredLayers,layerKeys,pipeline,graphError))) {
        created.store->Abort(transaction);
        removeFailedNewProject();
        result.transfer.errorMessage = std::move(graphError);
        return result;
    }

    Project::RawProjectSnapshot snapshot = created.snapshot;
    snapshot.embeddedAssets.push_back(asset);
    snapshot.lifecycle.initialAssetIds.push_back(asset.assetId);
    snapshot.dirtyRevision = 1u;
    snapshot.postRecipeRevision = 1u;
    snapshot.pipelineData = std::move(pipeline);
    if (!nativeComposition) {
        StackBinaryFormat::ProjectDocument rawDocument;
        Stack::RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(
            source,
            recipe,
            snapshot.pipelineData,
            rawDocument,
            Stack::RawWorkspace::RawProjectMode::UnifiedLayers,
            false);
        snapshot.rawWorkspaceData = std::move(rawDocument.rawWorkspaceData);
        snapshot.rawWorkspaceData["managedAssetId"] = asset.assetId;
        snapshot.rawWorkspaceData["originalSourcePath"] =
            asset.originalSourcePath;
        snapshot.rawWorkspaceData["originalFileFingerprint"] =
            asset.originalFileFingerprint;

    }
    if (!StackBinaryFormat::ExternalizeManagedProjectAssets(created.store,transaction,snapshot)) {
        created.store->Abort(transaction);
        removeFailedNewProject();
        result.transfer.errorMessage = "Could not save the selected edit dependencies.";
        return result;
    }
    const Project::ProjectStoreCommitResult committed =
        created.store->Commit(transaction, snapshot);
    if (!committed) {
        created.store->Abort(transaction);
        removeFailedNewProject();
        result.transfer.errorMessage = committed.message.empty()
            ? "Stack could not save the new target project."
            : committed.message;
        return result;
    }
    result.transfer.success = true;
    result.transfer.changed = true;
    result.transfer.committedStorageRevision =
        committed.committedStorageRevision;
    return result;
}

CreatedTargetProjectResult DuplicateProjectAndPasteAttributes(
    const std::filesystem::path& sourceProjectPath,
    const std::string& sourceSetId,
    const Stack::RawRecipe::RawEditAttributeBundle& bundle,
    const std::vector<std::string>& selectedKeys,
    const Stack::Project::ProjectStoreHandle& sourceStoreOverride,
    const Stack::Project::RawProjectSnapshot* sourceSnapshotOverride) {
    namespace Project = Stack::Project;
    CreatedTargetProjectResult result;

    Project::ProjectStoreHandle sourceStore = sourceStoreOverride;
    Project::RawProjectSnapshot sourceSnapshot;
    if (sourceStore && sourceSnapshotOverride != nullptr) {
        sourceSnapshot = *sourceSnapshotOverride;
    } else {
        const Project::ProjectStoreOpenResult opened =
            Project::OpenProjectStore(sourceProjectPath);
        if (!opened) {
            result.transfer.errorMessage = opened.message.empty()
                ? "The target RAW project could not be opened for copying."
                : opened.message;
            return result;
        }
        sourceStore = opened.store;
        sourceSnapshot = opened.snapshot;
    }
    if (!sourceStore) {
        result.transfer.errorMessage =
            "The target RAW project store is unavailable.";
        return result;
    }

    const Project::ProjectStorageKind storageKind =
        sourceStore->StorageKind();
    const std::filesystem::path destination = BuildUniqueEditedCopyPath(
        sourceProjectPath.empty()
            ? sourceStore->StoragePath()
            : sourceProjectPath,
        storageKind);
    if (destination.empty()) {
        result.transfer.errorMessage =
            "Stack could not choose a unique name for the pasted-edit copy.";
        return result;
    }

    sourceSnapshot.projectId = Project::GenerateStableUuid();
    sourceSnapshot.projectName =
        (sourceSnapshot.projectName.empty()
            ? std::string("RAW Project")
            : sourceSnapshot.projectName) + " - Pasted Edits";
    sourceSnapshot.lifecycle.creationOrigin =
        Project::ProjectCreationOrigin::MultiSelection;
    sourceSnapshot.lifecycle.cleanupWhenUntouched = false;
    sourceSnapshot.lifecycle.explicitlyRetained = true;

    Project::ProjectStoreOpenResult copied = Project::ConvertProjectStore(
        sourceStore,
        sourceSnapshot,
        destination,
        storageKind);
    if (!copied) {
        result.transfer.errorMessage = copied.message.empty()
            ? "Stack could not create the independent target project copy."
            : copied.message;
        return result;
    }
    copied.store.reset();
    result.projectPath = destination;
    result.transfer = Project::PasteRawEditAttributesIntoProject(
        destination,
        sourceSetId,
        bundle,
        selectedKeys);
    if (!result.transfer.success || result.transfer.skipped) {
        std::error_code cleanupError;
        if (storageKind == Project::ProjectStorageKind::WorkingFile) {
            std::filesystem::remove(destination, cleanupError);
        } else {
            std::filesystem::remove(destination, cleanupError);
            std::filesystem::path staging = destination;
            staging += ".staging";
            cleanupError.clear();
            std::filesystem::remove_all(staging, cleanupError);
        }
        result.projectPath.clear();
    }
    if (!result.transfer.success) {
        result.transfer.errorMessage = result.transfer.errorMessage.empty()
            ? "The independent project copy could not apply the selected RAW attributes."
            : result.transfer.errorMessage;
    }
    return result;
}

} // namespace Stack::EditorRawAttributes
