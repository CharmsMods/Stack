#pragma once
#include "Persistence/ProjectStore.h"
#include "Persistence/ManagedAssetPaths.h"

namespace Stack::Project {
// RAW recipes occur in more than one graph container. Bind each source by
// its own content identity; a workspace's primary RAW is not every node's RAW.
inline bool CollectRawSourceAssets(json& value, const ProjectStoreHandle& store,
    const ProjectStoreTransaction& transaction, RawProjectSnapshot& snapshot) {
    if (value.is_binary() || (value.is_object() && value.contains("$rawLayerBytes"))) return true;
    if (value.is_object()) {
        const bool rawNode = value.contains("kind") && value.at("kind") == "RawSource";
        if (rawNode && !value.contains("sourceRef"))
            value["sourceRef"] = {{"sourcePath", value.value("sourcePath", std::string{})}};
        auto source = value.find("sourceRef");
        if (source != value.end() && source->is_object() && (rawNode || value.contains("rawRecipeVersion"))) {
            auto& ref = *source;
            const auto path = ref.value("sourcePath", std::string{});
            // Result recipes bind to a source set, not a filesystem original.
            if (path.find("://") != std::string::npos) return true;
            const auto fingerprint = ref.value("fingerprint", std::string{});
            const EmbeddedAssetRecord* match = nullptr;
            for (const auto& asset : snapshot.embeddedAssets) {
                if (asset.inputFamily != MultiFrameInputFamily::Raw) continue;
                if (!fingerprint.empty() ? asset.sha256 == fingerprint :
                    (!path.empty() && (asset.originalSourcePath == path ||
                        store->ResolveAssetFile(asset.assetId).u8string() == path))) {
                    match = &asset;
                    break;
                }
            }
            if (!match && !path.empty()) {
                EmbeddedAssetRecord asset;
                if (!store->StageAssetFile(transaction, std::filesystem::u8path(path),
                        MultiFrameInputFamily::Raw, json::object(), asset)) return false;
                if (!fingerprint.empty() && fingerprint != asset.sha256) return false;
                if (!FindEmbeddedAsset(snapshot, asset.assetId)) snapshot.embeddedAssets.push_back(asset);
                match = FindEmbeddedAsset(snapshot, asset.assetId);
            }
            if (match) {
                // Recipe source references already carry canonical content
                // identity. Extra fields would invalidate their strict schema.
                if (rawNode) ref["managedAssetId"] = match->assetId;
                ref["fingerprint"] = match->sha256;
                if (store->StorageKind() == ProjectStorageKind::DirectoryBundle)
                    ref["sourcePath"] = ManagedAssetFilePath(store->StoragePath(), *match).u8string();
                if (rawNode) {
                    value["sourcePath"] = ref.at("sourcePath");
                    value["rawMetadata"]["sourcePath"] = ref.at("sourcePath");
                }
            } else if (!path.empty()) return false;
        }
    }
    if (value.is_structured())
        for (auto& child : value) if (!CollectRawSourceAssets(child, store, transaction, snapshot)) return false;
    return true;
}

inline bool RestoreRawSourceAssets(json& value, const ProjectStoreHandle& store,
    const RawProjectSnapshot& snapshot) {
    if (value.is_binary() || (value.is_object() && value.contains("$rawLayerBytes"))) return true;
    if (value.is_object()) {
        auto source = value.find("sourceRef");
        if (source != value.end() && source->is_object()) {
            auto& ref = *source;
            auto id = ref.value("managedAssetId", std::string{});
            if (id.empty()) {
                const auto fingerprint = ref.value("fingerprint", std::string{});
                for (const auto& asset : snapshot.embeddedAssets)
                    if (!fingerprint.empty() && asset.sha256 == fingerprint && asset.inputFamily == MultiFrameInputFamily::Raw) {
                        id = asset.assetId;
                        break;
                    }
            }
            if (!id.empty()) {
                const auto* asset = FindEmbeddedAsset(snapshot, id);
                if (!asset) return false;
                auto path = store->ResolveAssetFile(id);
                if (path.empty()) {
                    path = std::filesystem::temp_directory_path() / "Stack" / "ProjectSources" / ContentAssetName(*asset);
                    if (!store->CopyAssetToFile(id, path)) return false;
                }
                ref["sourcePath"] = path.u8string();
                if (value.contains("kind") && value.at("kind") == "RawSource") {
                    value["sourcePath"] = path.u8string();
                    value["rawMetadata"]["sourcePath"] = path.u8string();
                }
            }
        }
    }
    if (value.is_structured())
        for (auto& child : value) if (!RestoreRawSourceAssets(child, store, snapshot)) return false;
    return true;
}
} // namespace Stack::Project
