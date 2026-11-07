#include "BurstEngine/ScratchDirectory.h"
#include "Storage/ScratchBudget.h"
#include "BracketingResultStore.h"
#include "Persistence/BracketMaster.h"
#include "Raw/ReconstructionDepth.h"
#include "Persistence/ManagedAssetPaths.h"
#include "Persistence/BracketAuxiliaryStorage.h"
#include "AutoBracketProvenance.h"
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
#include <thread>

namespace Stack::Project {
namespace {
std::shared_ptr<BurstEngine::ScratchDirectory> TemporaryOwner() {
    return {new BurstEngine::ScratchDirectory(AppPaths::GetCacheDirectory(),BurstEngine::ScratchKind::Sources),
        [](auto* owner) {
            try {std::thread([owner]{delete owner;}).detach();}
            catch(...) {delete owner;}
        }};
}
struct Temporary {
    std::shared_ptr<BurstEngine::ScratchDirectory> owner = TemporaryOwner();
    std::filesystem::path path;
    Temporary() = default;
    explicit Temporary(const std::filesystem::path& requested):path(owner->Path()/requested.filename()) {}

    // The scratch owner retires both the transfer and its sibling metadata.
};
std::string RecipeIdentity(const MultiFrameSourceSet& set) {
    const auto found=set.settings.find("bracketing");
    return found==set.settings.end()?std::string():found->dump();
}
}

bool StageBracketingResult(const ProjectStoreHandle& store,const ProjectStoreTransaction& transaction,RawProjectSnapshot& snapshot,
    const std::string& setId,const Raw::Bracketing::BracketingResult& result,
    const std::vector<unsigned char>& cover,std::string& error,bool dependenciesAlreadyStaged) {
    auto* set=FindSourceSet(snapshot,setId);
    if(!store||!set||!result.raw||result.status!=Raw::Bracketing::BracketingResult::Status::Completed) {
        error="No completed bracket to save.";return false;
    }
    try {
        // ConvertProjectStore verifies and stages dependencies before invoking
        // its preparation callback. They cannot be read through the new store
        // until commit. Reuse this measurement in every Save As / Pack path.
        if (dependenciesAlreadyStaged && HasSavedBracketingResult(snapshot,*set)) {
            const auto& saved=set->settings.at("bracketingResult");
            if (saved.value("identity",std::string{})==result.identity) {
                if (store->StorageKind()==ProjectStorageKind::PortableFile) return true;
                if (const auto* master=FindEmbeddedAsset(snapshot,saved.value("masterAssetId",std::string{}))) {
                    const auto id=RawEvidence::ComputeSourceIdentity(ExpectedSourcePath(store->StoragePath(),*master));
                    if (id.valid && id.byteSize==master->byteLength && id.sha256==master->sha256) return true;
                }
            }
        }
        if(HasSavedBracketingResult(store,snapshot,*set) && set->settings["bracketingResult"].value("identity",std::string())==result.identity) {
            const auto* asset=FindEmbeddedAsset(snapshot,set->settings["bracketingResult"].at("assetId").get<std::string>());
            auto stream=store->OpenAssetStream(asset->assetId);
            // Saved bytes may have disappeared since this snapshot was opened.
            // Only reuse a result whose complete content still matches.
            if(stream) {
                RawEvidence::SourceIdentity identity;
                if(store->StorageKind()==ProjectStorageKind::WorkingFile)
                    identity=RawEvidence::ComputeSourceIdentity(store->ResolveAssetFile(asset->assetId));
                else {
                    const auto directory=AppPaths::GetCacheDirectory()/"BracketTransfer";
                    std::filesystem::create_directories(directory);
                    Temporary copy{directory/(GenerateStableUuid()+".bracket")};
                    if(Storage::ScratchBudget::ReserveFile(copy.path,asset->byteLength) && store->CopyAssetToFile(asset->assetId,copy.path))identity=RawEvidence::ComputeSourceIdentity(copy.path);
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
        if(!transaction)throw std::runtime_error("Could not begin saving the bracket result.");
        auto updated=snapshot;auto* target=FindSourceSet(updated,setId);
        const bool working=store->StorageKind()==ProjectStorageKind::WorkingFile;
        std::string masterId;
        if (working) {
            const auto master=PublishBracketMaster(store,snapshot,*set,result);
            EmbeddedAssetRecord record;
            if (!store->StageAssetFile(transaction,master,MultiFrameInputFamily::Raw,json::object(),record,&error))
                throw std::runtime_error(error);
            record.managedRole="bracketing-master";
            masterId=record.assetId;
            if(!FindEmbeddedAsset(updated,masterId)) updated.embeddedAssets.push_back(std::move(record));
        }
        if(!Raw::Bracketing::Serialization::WriteResultFile(temporary.path,result,working))
            throw std::runtime_error("Could not write the completed bracket's measurements.");
        Temporary compressed;
        const auto plainBytes=std::filesystem::file_size(temporary.path);
        if (working) {
            compressed.path=directory/(GenerateStableUuid()+".gz");
            BracketAuxiliaryStorage::Compress(temporary.path,compressed.path);
        }
        EmbeddedAssetRecord asset;
        if(!store->StageAssetFile(transaction,working?compressed.path:temporary.path,MultiFrameInputFamily::Raw,json::object(),asset,&error,false))
            throw std::runtime_error(error);
        asset.managedRole="bracketing-result";asset.displayName="Bracket result";
        asset.originalSourcePath.clear();asset.workspaceRelativeSourcePath.clear();
        if(!FindEmbeddedAsset(updated,asset.assetId))updated.embeddedAssets.push_back(asset);
        target->settings["bracketingResult"]={{"version",1},{"assetId",asset.assetId},
            {"recipe",RecipeIdentity(*set)},{"inputRevision",snapshot.hdrInputRevision},{"identity",result.identity}};
        if (working) {
            auto& saved=target->settings["bracketingResult"];
            saved["masterAssetId"]=masterId;
            saved["auxiliaryCodec"]="gzip-v1";
            saved["auxiliaryBytes"]=plainBytes;
        }
        updated.embeddedAssets.erase(std::remove_if(updated.embeddedAssets.begin(),updated.embeddedAssets.end(),
            [&](const auto& previous) {
                if(previous.managedRole!="bracketing-result" && previous.managedRole!="bracketing-master") return false;
                return std::none_of(updated.sourceSets.begin(),updated.sourceSets.end(),[&](const auto& candidate) {
                    const auto saved=candidate.settings.find("bracketingResult");
                    return saved!=candidate.settings.end()&&(saved->value("assetId",std::string())==previous.assetId ||
                        saved->value("masterAssetId",std::string())==previous.assetId);
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
    ++updated.dirtyRevision;
    if (auto* set = FindSourceSet(updated,setId); set && MatchesAutomaticBracketRevision(
            *set, snapshot.projectId, snapshot.dirtyRevision, snapshot.persistedStorageRevision)) {
        auto& marker = set->settings["autoBracket"];
        marker["projectId"] = snapshot.projectId;
        marker["storageRevision"] = snapshot.persistedStorageRevision + 1;
        marker["editRevision"] = updated.dirtyRevision;
    }
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
        if(store->StorageKind()==ProjectStorageKind::WorkingFile)path=store->ResolveAssetFile(asset->assetId);
        else {
            const auto directory=AppPaths::GetCacheDirectory()/"BracketTransfer";std::filesystem::create_directories(directory);
            temporary.path=temporary.owner->Path()/(GenerateStableUuid()+".bracket");path=temporary.path;
            if(!Storage::ScratchBudget::ReserveFile(path,asset->byteLength))throw std::runtime_error(Storage::ScratchBudget::Failure);
            if(!store->CopyAssetToFile(asset->assetId,path,&error))return false;
        }
        if(shouldCancel&&shouldCancel())return false;
        const auto identity=RawEvidence::ComputeSourceIdentity(path,shouldCancel);
        if(!identity.valid||identity.sha256!=asset->sha256||identity.byteSize!=asset->byteLength)
            throw std::runtime_error("The saved bracket result failed content verification.");
        if(shouldCancel&&shouldCancel())return false;
        const auto decode=[&](std::istream& stream) {
            Raw::Bracketing::Serialization::ReadResult(stream,saved.value("auxiliaryBytes",identity.byteSize),set->settings.at("bracketing").at("groups").size(),
            result,temporary.owner->Path()/"inspection",[&](Raw::RawImageData& raw) {
                const auto master=saved.value("masterAssetId",std::string{});
                auto file=store->ResolveAssetFile(master);
                std::shared_ptr<Temporary> embedded;
                if (file.empty() && store->StorageKind()==ProjectStorageKind::PortableFile) {
                    embedded=std::make_shared<Temporary>();
                    embedded->path=embedded->owner->Path()/(GenerateStableUuid()+".master");
                    const auto* masterAsset=FindEmbeddedAsset(snapshot,master);
                    if(!masterAsset||!Storage::ScratchBudget::ReserveFile(embedded->path,masterAsset->byteLength))throw std::runtime_error(Storage::ScratchBudget::Failure);
                    if (!store->CopyAssetToFile(master,embedded->path,&error)) throw std::runtime_error(error);
                    file=embedded->path;
                }
                if (file.empty()) throw std::runtime_error("Locate or regenerate the missing bracket master.");
                ReadBracketMasterPixels(file,saved.at("identity").get<std::string>(),raw);
                if (embedded && raw.reconstructionDepth && !raw.reconstructionDepth->masks.empty()) {
                    auto depth=std::make_shared<Raw::ReconstructionDepth>(*raw.reconstructionDepth);
                    for (auto& mask : depth->masks) {
                        mask.readPixels=[embedded,read=std::move(mask.readPixels)]{ return read(); };
                    }
                    raw.reconstructionDepth=std::move(depth);
                }
            });
        };
        if (saved.value("auxiliaryCodec",std::string{})=="gzip-v1") BracketAuxiliaryStorage::Read(path,decode);
        else { std::ifstream stream(path,std::ios::binary); decode(stream); }
        if(result.reconstructionInspection)
            std::const_pointer_cast<Raw::Bracketing::BracketingResult::ReconstructionInspection>(result.reconstructionInspection)->storageOwner=temporary.owner;
        if(result.identity!=saved.at("identity").get<std::string>())throw std::runtime_error("The saved result belongs to another bracket revision.");
        return true;
    }catch(const std::exception& exception){result={};error=exception.what();return false;}
}
} // namespace Stack::Project
