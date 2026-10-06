#include "BracketingResultStore.h"
#include "App/AppPaths.h"
#include "Editor/NodeGraph/Serialization/EditorNodeGraphRawSerialization.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Raw/Bracketing/Panorama/Panorama.h"
#include "NodeMath/ContractTypes.h"

#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <type_traits>
#include <stdexcept>

namespace Stack::Project {
namespace {
constexpr std::array<char,8> magic{'S','T','K','B','R','0','1','\0'};
struct Temporary {
    std::filesystem::path path;
    ~Temporary() { if(!path.empty()) {std::error_code ec;std::filesystem::remove(path,ec);} }
};
template<class T> void Write(std::ostream& stream,const T& value) {
    stream.write(reinterpret_cast<const char*>(&value),sizeof(value));
}
template<class T> void WriteBlock(std::ostream& stream,const std::vector<T>& value) {
    Write(stream,static_cast<std::uint64_t>(value.size()));
    if(!value.empty())stream.write(reinterpret_cast<const char*>(value.data()),value.size()*sizeof(T));
}
template<class T> void WriteBlock(std::ostream& stream,const std::shared_ptr<const std::vector<T>>& value) {
    if(value)WriteBlock(stream,*value);else Write(stream,std::uint64_t{0});
}
class Reader {
public:
    Reader(std::istream& stream,std::uint64_t remaining):stream_(stream),remaining_(remaining) {}
    void Bytes(void* data,std::uint64_t bytes) {
        if(bytes>remaining_ || bytes>static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
            throw std::runtime_error("The saved bracket result is truncated.");
        if(bytes&&!stream_.read(static_cast<char*>(data),static_cast<std::streamsize>(bytes)))
            throw std::runtime_error("The saved bracket result could not be read.");
        remaining_-=bytes;
    }
    template<class T> T Value() {T value{};Bytes(&value,sizeof(value));return value;}
    template<class T> std::vector<T> Block(std::uint64_t maximum,bool allowNonFinite=false) {
        const auto count=Value<std::uint64_t>();
        if(count>maximum || count>remaining_/sizeof(T) || count>std::numeric_limits<std::size_t>::max()/sizeof(T))
            throw std::runtime_error("The saved bracket result has invalid dimensions.");
        std::vector<T> out(static_cast<std::size_t>(count));Bytes(out.data(),count*sizeof(T));
        if constexpr(std::is_floating_point_v<T>) for(const auto value:out)
            if(!allowNonFinite&&!std::isfinite(value))throw std::runtime_error("The saved bracket result contains invalid measurements.");
        return out;
    }
    template<class T> std::shared_ptr<const std::vector<T>> Map(std::uint64_t pixels) {
        auto data=Block<T>(pixels);
        if(data.empty())return {};
        if(data.size()!=pixels)throw std::runtime_error("The saved bracket measurement map is incomplete.");
        return std::make_shared<const std::vector<T>>(std::move(data));
    }
    bool Finished() const {return remaining_==0;}
private:
    std::istream& stream_;
    std::uint64_t remaining_;
};
std::string RecipeIdentity(const MultiFrameSourceSet& set) {
    const auto found=set.settings.find("bracketing");
    return found==set.settings.end()?std::string():found->dump();
}
bool WriteResult(const std::filesystem::path& path,const Raw::Bracketing::BracketingResult& result) {
    const auto& raw=*result.raw;
    const auto& preview=result.preview;
    const auto sidecars=raw.multiFrameMeasurementSidecars;
    json header={
        {"version",result.panorama?2:1},{"storageInstance",GenerateStableUuid()},{"identity",result.identity},{"contentIdentity",raw.contentIdentity},
        {"contentHash",raw.contentIdentityHash},{"mosaicHash",raw.normalizedMosaicContentHash},
        {"decoderVersion",raw.decoderIdentityVersion},{"cameraRgb",raw.reconstructedCameraRgb},
        {"metadata",EditorNodeGraph::SerializeRawMetadata(raw.metadata)},
        {"contract",Raw::NormalizedMosaicInputContractName(raw.normalizedMosaicInputContract)},
        {"frames",sidecars?sidecars->originalFrameIds:std::vector<std::string>{}},
        {"evidence",sidecars?sidecars->evidenceIdentitySha256:std::string()},
        {"previewWidth",preview.width},{"previewHeight",preview.height},
        {"previewMetadata",EditorNodeGraph::SerializeRawMetadata(preview.metadata)},
        {"previewSamples",!preview.samples.empty()},{"previewFallbackClipping",true},
        {"originX",preview.sensorOriginX},{"originY",preview.sensorOriginY},
        {"stepX",preview.sensorStepX},{"stepY",preview.sensorStepY},
        {"fallback",result.fallbackSamples},{"unrecoverable",result.unrecoverableSamples},{"invalid",result.invalidSamples}
    };
    if(result.reconstructionInspection) {
        const auto& inspection=*result.reconstructionInspection;
        json tiles=json::array();
        for(const auto& [key,checksum]:inspection.checksums)
            tiles.push_back({{"x",key.first},{"y",key.second},{"sha256",checksum}});
        header["inspection"]={{"scale",inspection.scale},{"tilePixels",inspection.tilePixels},
            {"sensorWidth",inspection.sensorWidth},{"sensorHeight",inspection.sensorHeight},
            {"groups",inspection.groups},{"recipe",Raw::Bracketing::Serialize(inspection.recipe)},
            {"tiles",std::move(tiles)}};
    }
    if(result.panorama)header["panorama"]=Raw::Bracketing::Panorama::SerializeLayout(*result.panorama);
    const auto text=header.dump();
    std::ofstream stream(path,std::ios::binary|std::ios::trunc);
    stream.write(magic.data(),magic.size());Write(stream,static_cast<std::uint64_t>(text.size()));
    stream.write(text.data(),text.size());
    WriteBlock(stream,raw.normalizedMosaicBuffer);WriteBlock(stream,raw.linearFloatBuffer);
    const Raw::RawImageData::MultiFrameMeasurementSidecars empty;
    const auto& maps=sidecars?*sidecars:empty;
    WriteBlock(stream,maps.variance);WriteBlock(stream,maps.fusionUncertaintyVariance);
    WriteBlock(stream,maps.effectiveSupport);WriteBlock(stream,maps.validity);
    WriteBlock(stream,maps.clipping);WriteBlock(stream,maps.localRejection);WriteBlock(stream,maps.fallbackReason);
    WriteBlock(stream,preview.resultRgb);WriteBlock(stream,preview.sourceRgb);
    WriteBlock(stream,preview.contributions);WriteBlock(stream,preview.requested);
    WriteBlock(stream,preview.guideEv);WriteBlock(stream,preview.diagnostics);
    if(!preview.samples.empty()) {
        std::vector<float> values;values.reserve(preview.samples.size()*8);
        std::vector<std::uint8_t> flags;flags.reserve(preview.samples.size());
        for(const auto& sample:preview.samples) {
            values.insert(values.end(),{sample.value,sample.variance,sample.headroom,sample.support,
                sample.fallback,sample.exposure,sample.measurementVariance,sample.uncertaintyVariance});
            flags.push_back(static_cast<std::uint8_t>((sample.finite?1:0)|(sample.clipped?2:0)|
                (sample.localRejected?4:0)|(sample.fixedReference?8:0)|(sample.fallbackClipped?16:0)));
        }
        WriteBlock(stream,values);WriteBlock(stream,flags);
    }
    if(result.reconstructionInspection) {
        const auto& inspection=*result.reconstructionInspection;
        for(const auto& [key,checksum]:inspection.checksums) {
            const auto tileWidth=std::min(inspection.tilePixels,unsigned(raw.metadata.visibleWidth)-key.first*inspection.tilePixels);
            const auto tileHeight=std::min(inspection.tilePixels,unsigned(raw.metadata.visibleHeight)-key.second*inspection.tilePixels);
            std::vector<float> values(std::size_t(tileWidth)*tileHeight*inspection.groups);
            std::ifstream tile(inspection.directory/(std::to_string(key.first)+"-"+std::to_string(key.second)+".bin"),std::ios::binary);
            if(!tile.read(reinterpret_cast<char*>(values.data()),values.size()*sizeof(float))||
                checksum!=NodeMath::Sha256ContentIdentity(std::vector<std::string_view>{
                    {reinterpret_cast<const char*>(values.data()),values.size()*sizeof(float)}}))
                throw std::runtime_error("The super resolution inspection measurements could not be saved. Refresh the bracket.");
            WriteBlock(stream,values);
        }
    }
    if(result.panorama) {
        WriteBlock(stream,raw.outputCoverage);WriteBlock(stream,preview.coverage);
        WriteBlock(stream,result.panorama->ownership);
    }
    stream.close();return stream.good();
}
void ReadResult(std::istream& stream,std::uint64_t bytes,std::size_t groups,Raw::Bracketing::BracketingResult& result) {
    Reader reader(stream,bytes);std::array<char,8> found{};reader.Bytes(found.data(),found.size());
    if(found!=magic)throw std::runtime_error("The bracket result format is unsupported.");
    const auto headerBytes=reader.Value<std::uint64_t>();
    if(headerBytes>16u*1024*1024)throw std::runtime_error("The bracket result header is too large.");
    std::string text(static_cast<std::size_t>(headerBytes),'\0');reader.Bytes(text.data(),headerBytes);
    const auto header=json::parse(text);
    const int version=header.at("version");
    if(version!=1&&version!=2)throw std::runtime_error("The bracket result version is unsupported.");
    const bool panorama=version==2&&header.contains("panorama");
    auto raw=std::make_shared<Raw::RawImageData>();
    raw->metadata=EditorNodeGraph::DeserializeRawMetadata(header.at("metadata"));
    if(raw->metadata.rawWidth<=0||raw->metadata.rawHeight<=0)throw std::runtime_error("Invalid bracket dimensions.");
    const auto pixels=std::uint64_t(raw->metadata.rawWidth)*raw->metadata.rawHeight;
    raw->reconstructedCameraRgb=header.at("cameraRgb").get<bool>();
    const auto contract=header.at("contract").get<std::string>();
    if(contract!="bracketing-pre-gain-v1"&&!(raw->reconstructedCameraRgb&&contract=="none"))
        throw std::runtime_error("The saved image is not a bracket measurement.");
    raw->normalizedMosaicInputContract=contract=="bracketing-pre-gain-v1"?
        Raw::NormalizedMosaicInputContract::BracketingPreGain:Raw::NormalizedMosaicInputContract::None;
    raw->contentIdentity=header.at("contentIdentity").get<std::string>();raw->contentIdentityHash=header.at("contentHash");
    raw->normalizedMosaicContentHash=header.at("mosaicHash");raw->decoderIdentityVersion=header.at("decoderVersion");
    raw->normalizedMosaicBuffer=reader.Map<float>(pixels);
    raw->linearFloatBuffer=reader.Block<float>(pixels*3);
    if(raw->reconstructedCameraRgb?raw->linearFloatBuffer.size()!=pixels*3:!raw->normalizedMosaicBuffer)
        throw std::runtime_error("The saved bracket pixels are incomplete.");
    auto maps=std::make_shared<Raw::RawImageData::MultiFrameMeasurementSidecars>();
    maps->variance=reader.Map<float>(pixels);maps->fusionUncertaintyVariance=reader.Map<float>(pixels);
    maps->effectiveSupport=reader.Map<float>(pixels);maps->validity=reader.Map<std::uint8_t>(pixels);
    maps->clipping=reader.Map<std::uint8_t>(pixels);maps->localRejection=reader.Map<std::uint8_t>(pixels);
    maps->fallbackReason=reader.Map<std::uint8_t>(pixels);
    maps->originalFrameIds=header.at("frames").get<std::vector<std::string>>();maps->evidenceIdentitySha256=header.at("evidence");
    raw->multiFrameMeasurementSidecars=maps;
    auto& preview=result.preview;preview.width=header.at("previewWidth");preview.height=header.at("previewHeight");
    const auto previewPixels=std::uint64_t(preview.width)*preview.height;
    if(previewPixels>pixels)throw std::runtime_error("Invalid bracket preview dimensions.");
    preview.metadata=EditorNodeGraph::DeserializeRawMetadata(header.at("previewMetadata"));
    preview.sensorOriginX=header.at("originX");preview.sensorOriginY=header.at("originY");
    preview.sensorStepX=header.at("stepX");preview.sensorStepY=header.at("stepY");
    preview.resultRgb=reader.Block<float>(previewPixels*3);preview.sourceRgb=reader.Block<float>(previewPixels*groups*3);
    preview.contributions=reader.Block<float>(previewPixels*groups);preview.requested=reader.Block<float>(previewPixels*groups);
    preview.guideEv=reader.Block<float>(previewPixels);preview.diagnostics=reader.Block<std::uint8_t>(previewPixels);
    if (!previewPixels || preview.resultRgb.size()!=previewPixels*3 || (!panorama&&(
        preview.sourceRgb.size()!=previewPixels*groups*3 ||
        preview.contributions.size()!=previewPixels*groups || preview.requested.size()!=previewPixels*groups ||
        preview.guideEv.size()!=previewPixels || preview.diagnostics.size()!=previewPixels)))
        throw std::runtime_error("The saved bracket preview is incomplete.");
    if(header.value("previewSamples",false)) {
        const auto count=previewPixels*4*groups;
        const auto values=reader.Block<float>(count*8,true);
        const auto flags=reader.Block<std::uint8_t>(count);
        if(values.size()!=count*8||flags.size()!=count)
            throw std::runtime_error("The saved bracket observations are incomplete.");
        preview.samples.resize(static_cast<std::size_t>(count));
        for(std::size_t i=0;i<count;++i) {
            const auto offset=i*8;auto& sample=preview.samples[i];
            sample.value=values[offset];sample.variance=values[offset+1];sample.headroom=values[offset+2];
            sample.support=values[offset+3];sample.fallback=values[offset+4];sample.exposure=values[offset+5];
            sample.measurementVariance=values[offset+6];sample.uncertaintyVariance=values[offset+7];
            sample.finite=(flags[i]&1)!=0;sample.clipped=(flags[i]&2)!=0;
            sample.localRejected=(flags[i]&4)!=0;sample.fixedReference=(flags[i]&8)!=0;
            // Old previews did not retain this fact separately. Do not assume
            // an accepted group mean proves its fallback capture was usable.
            sample.fallbackClipped=header.value("previewFallbackClipping",false)?
                (flags[i]&16)!=0:sample.clipped||sample.support>0;
        }
    }
    if(header.contains("inspection")) {
        const auto& saved=header.at("inspection");
        auto inspection=std::make_shared<Raw::Bracketing::BracketingResult::ReconstructionInspection>();
        inspection->scale=saved.at("scale");inspection->tilePixels=saved.at("tilePixels");
        inspection->sensorWidth=saved.at("sensorWidth");inspection->sensorHeight=saved.at("sensorHeight");
        inspection->groups=saved.at("groups");std::string error;
        if(!raw->reconstructedCameraRgb||inspection->scale<1||inspection->scale>4||
            !inspection->tilePixels||inspection->tilePixels>4096||inspection->groups!=groups||
            std::uint64_t(inspection->sensorWidth)*inspection->scale!=raw->metadata.visibleWidth||
            std::uint64_t(inspection->sensorHeight)*inspection->scale!=raw->metadata.visibleHeight||
            !Raw::Bracketing::Deserialize(saved.at("recipe"),inspection->recipe,error))
            throw std::runtime_error("Invalid saved reconstruction measurements.");
        const unsigned columns=(raw->metadata.visibleWidth-1)/inspection->tilePixels+1;
        const unsigned rows=(raw->metadata.visibleHeight-1)/inspection->tilePixels+1;
        if(saved.at("tiles").size()!=std::uint64_t(columns)*rows)
            throw std::runtime_error("Incomplete saved reconstruction measurements.");
        inspection->directory=AppPaths::GetCacheDirectory()/"BracketInspection"/GenerateStableUuid();
        std::filesystem::create_directories(inspection->directory);
        for(const auto& tile:saved.at("tiles")) {
            const unsigned x=tile.at("x"),y=tile.at("y");
            const auto checksum=tile.at("sha256").get<std::string>();
            if(x>=columns||y>=rows||!inspection->checksums.emplace(std::make_pair(x,y),checksum).second)
                throw std::runtime_error("Invalid saved reconstruction tile.");
            const auto tileWidth=std::min(inspection->tilePixels,unsigned(raw->metadata.visibleWidth)-x*inspection->tilePixels);
            const auto tileHeight=std::min(inspection->tilePixels,unsigned(raw->metadata.visibleHeight)-y*inspection->tilePixels);
            const auto count=std::uint64_t(tileWidth)*tileHeight*groups;
            const auto values=reader.Block<float>(count);
            if(values.size()!=count||checksum!=NodeMath::Sha256ContentIdentity(std::vector<std::string_view>{
                {reinterpret_cast<const char*>(values.data()),values.size()*sizeof(float)}}))
                throw std::runtime_error("Saved reconstruction measurements failed verification.");
            std::ofstream file(inspection->directory/(std::to_string(x)+"-"+std::to_string(y)+".bin"),std::ios::binary);
            file.write(reinterpret_cast<const char*>(values.data()),values.size()*sizeof(float));file.close();
            if(!file)throw std::runtime_error("Could not restore reconstruction inspection data.");
        }
        result.reconstructionInspection=std::move(inspection);
    }
    if(panorama) {
        auto layout=std::make_shared<Raw::Bracketing::Panorama::Layout>(*Raw::Bracketing::Panorama::DeserializeLayout(header.at("panorama")));
        if(!raw->reconstructedCameraRgb||layout->width!=raw->metadata.visibleWidth||layout->height!=raw->metadata.visibleHeight)
            throw std::runtime_error("The saved panorama dimensions do not match its pixels.");
        raw->outputCoverage=reader.Map<float>(pixels);preview.coverage=reader.Block<float>(previewPixels);
        layout->ownership=reader.Map<std::uint16_t>(pixels);
        if(!raw->outputCoverage||!layout->ownership||preview.coverage.size()!=previewPixels)
            throw std::runtime_error("The saved panorama coverage is incomplete.");
        for(std::size_t p=0;p<pixels;++p)if((*raw->outputCoverage)[p]<0||(*raw->outputCoverage)[p]>1||
            ((*raw->outputCoverage)[p]>0&&(*layout->ownership)[p]>=layout->cameras.size()))throw std::runtime_error("Invalid saved panorama coverage.");
        for(float a:preview.coverage)if(a<0||a>1)throw std::runtime_error("Invalid panorama preview coverage.");
        result.panorama=std::move(layout);
    }
    if(!reader.Finished())throw std::runtime_error("Unexpected data in saved bracket result.");
    result.raw=std::move(raw);result.identity=header.at("identity");result.fallbackSamples=header.at("fallback");
    result.unrecoverableSamples=header.at("unrecoverable");result.invalidSamples=header.at("invalid");
    result.status=Raw::Bracketing::BracketingResult::Status::Completed;result.message="Saved bracket ready.";
}
}

bool HasSavedBracketingResult(const RawProjectSnapshot& snapshot,const MultiFrameSourceSet& set) {
    try {
    const auto it=set.settings.find("bracketingResult");
    if(it==set.settings.end()||!it->is_object())return false;
    const auto* asset=FindEmbeddedAsset(snapshot,it->value("assetId",std::string()));
    return it->value("version",0u)==1&&it->value("recipe",std::string())==RecipeIdentity(set)&&
        it->value("inputRevision",std::uint64_t{0})==snapshot.hdrInputRevision&&
        asset&&asset->managedRole=="bracketing-result";
    } catch (const json::exception&) {return false;}
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
                    identity=RawEvidence::ComputeSourceIdentity(store->StoragePath()/std::filesystem::u8path(asset->projectAssetPath));
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
        if(!WriteResult(temporary.path,result))throw std::runtime_error("Could not write the completed bracket.");
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
        if(store->StorageKind()==ProjectStorageKind::DirectoryBundle)path=store->StoragePath()/std::filesystem::u8path(asset->projectAssetPath);
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
        ReadResult(stream,identity.byteSize,set->settings.at("bracketing").at("groups").size(),result);
        if(result.identity!=saved.at("identity").get<std::string>())throw std::runtime_error("The saved result belongs to another bracket revision.");
        return true;
    }catch(const std::exception& exception){result={};error=exception.what();return false;}
}
} // namespace Stack::Project
