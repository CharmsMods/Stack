#include "Persistence/BracketingResultStore.h"

#include <algorithm>
#include <cctype>

namespace Stack::Project {

bool HasSavedBracketingResult(const RawProjectSnapshot& snapshot,const MultiFrameSourceSet& set) {
    try {
        const auto recipe=set.settings.find("bracketing");
        if(recipe==set.settings.end()||!recipe->is_object()||recipe->empty())return false;
        const auto it=set.settings.find("bracketingResult");
        if(it==set.settings.end()||!it->is_object())return false;
        const auto version=it->find("version"),revision=it->find("inputRevision");
        if(version==it->end()||!version->is_number_integer()||*version!=1||revision==it->end()||
            (!revision->is_number_unsigned()&&(!revision->is_number_integer()||revision->get<std::int64_t>()<0)))return false;
        const auto* asset=FindEmbeddedAsset(snapshot,it->value("assetId",std::string()));
        // The stored format starts with its eight-byte magic and header length.
        constexpr std::uint64_t preambleBytes=8+sizeof(std::uint64_t);
        return it->value("recipe",std::string())==recipe->dump()&&
            revision->get<std::uint64_t>()==snapshot.hdrInputRevision&&
            !it->value("identity",std::string()).empty()&&asset&&asset->managedRole=="bracketing-result"&&
            asset->byteLength>preambleBytes&&asset->sha256.size()==64&&
            std::all_of(asset->sha256.begin(),asset->sha256.end(),
                [](unsigned char value){return std::isxdigit(value)!=0;})&&
            asset->assetId==MakeAssetId(asset->sha256,asset->byteLength);
    } catch (const json::exception&) {return false;}
}

bool HasSavedBracketingResult(const ProjectStoreHandle& store,const RawProjectSnapshot& snapshot,
    const MultiFrameSourceSet& set) {
    if(!store||!HasSavedBracketingResult(snapshot,set))return false;
    const auto* asset=FindEmbeddedAsset(snapshot,set.settings.at("bracketingResult").at("assetId").get<std::string>());
    const auto stream=store->OpenAssetStream(asset->assetId);
    if(!stream||stream.byteLength!=asset->byteLength)return false;
    if(store->StorageKind()==ProjectStorageKind::DirectoryBundle) {
        // Immutable shared assets can disappear independently of the manifest.
        // Catalog scans check presence/length; Restore verifies complete content.
        std::error_code error;
        const auto bytes=std::filesystem::file_size(store->ResolveAssetFile(asset->assetId),error);
        if(error||bytes!=asset->byteLength)return false;
    }
    return true;
}

} // namespace Stack::Project
