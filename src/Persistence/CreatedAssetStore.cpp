#include "Persistence/CreatedAssetStore.h"
#include <stdexcept>

namespace Stack::Project {
namespace {
nlohmann::json* Records(nlohmann::json& pipeline) {
    if (!pipeline.is_object() || !pipeline.contains("rawLayerStack")) return nullptr;
    auto& stack=pipeline.at("rawLayerStack");
    if (!stack.contains("createdAssets")) return nullptr;
    auto& library=stack.at("createdAssets");
    if (library.at("version")!=1 || !library.at("records").is_array()) throw std::runtime_error("Invalid created asset library.");
    return &library.at("records");
}
}
bool ExternalizeCreatedAssets(nlohmann::json& pipeline,const CreatedAssetWriter& write,
    std::vector<std::string>& referenced) {
    try {
        auto* records=Records(pipeline); if (!records) return true;
        for (auto& asset:*records) {
            auto managedId=asset.value("managedAssetId",std::string{});
            if (asset.contains("data") && asset.contains("samples")) {
                const auto bytes=nlohmann::json::to_cbor({{"version",1},{"data",asset.at("data")},{"samples",asset.at("samples")}});
                if (!write(asset.at("id"),asset.at("name"),bytes,managedId)) return false;
                asset["managedAssetId"]=managedId;
                asset.erase("data"); asset.erase("samples");
            }
            if (managedId.empty()) return false;
            referenced.push_back(std::move(managedId));
        }
        return true;
    } catch (...) { return false; }
}
bool RehydrateCreatedAssets(nlohmann::json& pipeline,const CreatedAssetReader& read) {
    try {
        auto* records=Records(pipeline);if (!records) return true;
        for (auto& asset:*records) {
            const auto id=asset.value("managedAssetId",std::string{});
            if (id.empty()) {
                if (!asset.contains("data") || !asset.contains("samples")) return false;
                continue;
            }
            std::vector<unsigned char> bytes;
            if (!read(id,bytes)) return false;
            auto value=nlohmann::json::from_cbor(bytes);
            if (value.at("version")!=1) return false;
            asset["data"]=std::move(value.at("data"));asset["samples"]=std::move(value.at("samples"));
        }
        return true;
    } catch (...) { return false; }
}
} // namespace Stack::Project
