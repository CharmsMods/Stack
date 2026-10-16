#include "BracketingResultStore.h"
#include "App/AppPaths.h"
#include "Raw/Bracketing/ResultSerialization.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Raw/Bracketing/Panorama/Panorama.h"
#include "NodeMath/ContractTypes.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <type_traits>
#include <stdexcept>

namespace Stack::Project {
namespace {
struct Temporary {
    std::filesystem::path path;
    ~Temporary() { if(!path.empty()) {std::error_code ec;std::filesystem::remove(path,ec);} }
};
std::string RecipeIdentity(const MultiFrameSourceSet& set) {
    const auto found=set.settings.find("bracketing");
    return found==set.settings.end()?std::string():found->dump();
}
}

bool StageBracketingResult(const ProjectStoreHandle& store,const ProjectStoreTransaction& transaction,RawProjectSnapshot& snapshot,
    const std::string& setId,const Raw::Bracketing::BracketingResult& result,
    const std::vector<unsigned char>& cover,std::string& error) {
    auto* set=FindSourceSet(snapshot,setId);
    if(!store||!set||!result.raw||result.status!=Raw::Bracketing::BracketingResult::Status::Completed) {
        error="No completed bracket to save.";return false;
    }
    try {
        if(HasSavedBracketingResult(snapshot,*set) && set->settings["bracketingResult"].value("identity",std::string())==result.identity) {
            const auto* asset=FindEmbeddedAsset(snapshot,set->settings["bracketingResult"].at("assetId").get<std::string>());
            auto stream=store->OpenAssetStream(asset->assetId);
            // Saved bytes may have disappeared since this snapshot was opened.
            // Only reuse a result whose complete content still matches.
            if(stream) {
                RawEvidence::SourceIdentity identity;
                if(store->StorageKind()==ProjectStorageKind::DirectoryBundle)
                    identity=RawEvidence::ComputeSourceIdentity(store->ResolveAssetFile(asset->assetId));
                else {
                    const auto directory=AppPaths::GetCacheDirectory()/"BracketTransfer";
                    std::filesystem::create_directories(directory);
                    Temporary copy{directory/(GenerateStableUuid()+".bracket")};
                    if(store->CopyAssetToFile(asset->assetId,copy.path))identity=RawEvidence::ComputeSourceIdentity(copy.path);
                }
                if(identity.valid&&identity.sha256==asset->sha256&&identity.byteSize==asset->byteLength) {
                    if(!cover.empty())snapshot.coverThumbnailBytes=cover;
                    return true;
                }
            }
        }
        const auto directory=AppPaths::GetCacheDirectory()/"BracketTransfer";
        std::filesystem::create_directories(directory);
        Temporary temporary{directory/(GenerateStableUuid()+".bracket")};
        if(!Raw::Bracketing::Serialization::WriteResultFile(temporary.path,result))throw std::runtime_error("Could not write the completed bracket.");
        if(!transaction)throw std::runtime_error("Could not begin saving the bracket result.");
        auto updated=snapshot;auto* target=FindSourceSet(updated,setId);
        EmbeddedAssetRecord asset;
        if(!store->StageAssetFile(transaction,temporary.path,MultiFrameInputFamily::Raw,json::object(),asset,&error))
            throw std::runtime_error(error);
        asset.managedRole="bracketing-result";asset.displayName="Bracket result";
        asset.originalSourcePath.clear();asset.workspaceRelativeSourcePath.clear();
        if(!FindEmbeddedAsset(updated,asset.assetId))updated.embeddedAssets.push_back(asset);
        target->settings["bracketingResult"]={{"version",1},{"assetId",asset.assetId},
            {"recipe",RecipeIdentity(*set)},{"inputRevision",snapshot.hdrInputRevision},{"identity",result.identity}};
        updated.embeddedAssets.erase(std::remove_if(updated.embeddedAssets.begin(),updated.embeddedAssets.end(),
            [&](const auto& previous) {
                if(previous.managedRole!="bracketing-result"||previous.assetId==asset.assetId)return false;
                return std::none_of(updated.sourceSets.begin(),updated.sourceSets.end(),[&](const auto& candidate) {
                    const auto saved=candidate.settings.find("bracketingResult");
                    return saved!=candidate.settings.end()&&saved->value("assetId",std::string())==previous.assetId;
                });
            }),updated.embeddedAssets.end());
        if(!cover.empty())updated.coverThumbnailBytes=cover;
        updated.sourceWidth=result.raw->metadata.visibleWidth;updated.sourceHeight=result.raw->metadata.visibleHeight;
        snapshot=std::move(updated);return true;
    }catch(const std::exception& exception) {
        error=exception.what();return false;
    }
}

bool SaveBracketingResult(const ProjectStoreHandle& store,RawProjectSnapshot& snapshot,
    const std::string& setId,const Raw::Bracketing::BracketingResult& result,
    const std::vector<unsigned char>& cover,std::string& error) {
    if(!store){error="The project store is unavailable.";return false;}
    const auto transaction=store->BeginTransaction(snapshot.persistedStorageRevision);
    auto updated=snapshot;
    if(!transaction || !StageBracketingResult(store,transaction,updated,setId,result,cover,error)) {
        if(transaction)store->Abort(transaction);return false;
    }
    if (auto* set=FindSourceSet(updated,setId); set && set->settings.contains("autoBracket"))
        set->settings["autoBracket"]["storageRevision"]=snapshot.persistedStorageRevision+1;
    ++updated.dirtyRevision;
    const auto committed=store->Commit(transaction,updated);
    if(!committed){store->Abort(transaction);error=committed.message;return false;}
    updated.persistedStorageRevision=committed.committedStorageRevision;snapshot=std::move(updated);return true;
}

bool RestoreBracketingResult(const ProjectStoreHandle& store,const RawProjectSnapshot& snapshot,
    const std::string& setId,Raw::Bracketing::BracketingResult& result,std::string& error,
    const std::function<bool()>& shouldCancel) {
    result={};const auto* set=FindSourceSet(snapshot,setId);
    if(!store||!set||!HasSavedBracketingResult(snapshot,*set))return false;
    try {
        const auto& saved=set->settings.at("bracketingResult");
        const auto* asset=FindEmbeddedAsset(snapshot,saved.at("assetId").get<std::string>());
        Temporary temporary;std::filesystem::path path;
        if(store->StorageKind()==ProjectStorageKind::DirectoryBundle)path=store->ResolveAssetFile(asset->assetId);
        else {
            const auto directory=AppPaths::GetCacheDirectory()/"BracketTransfer";std::filesystem::create_directories(directory);
            temporary.path=directory/(GenerateStableUuid()+".bracket");path=temporary.path;
            if(!store->CopyAssetToFile(asset->assetId,path,&error))return false;
        }
        if(shouldCancel&&shouldCancel())return false;
        const auto identity=RawEvidence::ComputeSourceIdentity(path,shouldCancel);
        if(!identity.valid||identity.sha256!=asset->sha256||identity.byteSize!=asset->byteLength)
            throw std::runtime_error("The saved bracket result failed content verification.");
        if(shouldCancel&&shouldCancel())return false;
        std::ifstream stream(path,std::ios::binary);
        Raw::Bracketing::Serialization::ReadResult(stream,identity.byteSize,set->settings.at("bracketing").at("groups").size(),result,AppPaths::GetCacheDirectory()/"BracketInspection");
        if(result.identity!=saved.at("identity").get<std::string>())throw std::runtime_error("The saved result belongs to another bracket revision.");
        return true;
    }catch(const std::exception& exception){result={};error=exception.what();return false;}
}
} // namespace Stack::Project
