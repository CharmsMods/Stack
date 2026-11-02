#include "ResultSerialization.h"
#include "Raw/DepthMaskManifest.h"
#include "Raw/Bracketing/DepthMaskStorage.h"
#include "Raw/RawMetadataSerialization.h"
#include "Raw/Bracketing/Panorama/Panorama.h"
#include "NodeMath/ContractTypes.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <random>
#include <type_traits>
#include <stdexcept>

namespace Raw::Bracketing::Serialization {
using json = nlohmann::json;
namespace {
std::string StorageId() {
    std::random_device random;
    constexpr char digits[]="0123456789abcdef";
    std::string value(32,'0');
    for(auto& character:value)character=digits[random()&15];
    return value;
}
constexpr std::array<char,8> magic{'S','T','K','B','R','0','1','\0'};
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
}
bool WriteResultFile(const std::filesystem::path& path,const Raw::Bracketing::BracketingResult& result, bool auxiliaryOnly) {
    if(!result.raw)throw std::runtime_error("The bracket result contains no image measurement.");
    const auto& raw=*result.raw;
    const auto& preview=result.preview;
    const auto sidecars=raw.multiFrameMeasurementSidecars;
    json header={
        {"version",raw.reconstructionDepth?5:result.panorama?2:1},{"storageInstance",auxiliaryOnly ? result.identity : StorageId()},{"externalPixels",auxiliaryOnly},{"identity",result.identity},{"contentIdentity",raw.contentIdentity},
        {"contentHash",raw.contentIdentityHash},{"mosaicHash",raw.normalizedMosaicContentHash},
        {"decoderVersion",raw.decoderIdentityVersion},{"cameraRgb",raw.reconstructedCameraRgb},
        {"metadata",Raw::Serialization::SerializeMetadata(raw.metadata)},
        {"contract",Raw::NormalizedMosaicInputContractName(raw.normalizedMosaicInputContract)},
        {"frames",sidecars?sidecars->originalFrameIds:std::vector<std::string>{}},
        {"evidence",sidecars?sidecars->evidenceIdentitySha256:std::string()},
        {"previewWidth",preview.width},{"previewHeight",preview.height},
        {"previewMetadata",Raw::Serialization::SerializeMetadata(preview.metadata)},
        {"previewSamples",!preview.samples.empty()},{"previewFallbackClipping",true},
        {"originX",preview.sensorOriginX},{"originY",preview.sensorOriginY},
        {"stepX",preview.sensorStepX},{"stepY",preview.sensorStepY},
        {"fallback",result.fallbackSamples},{"unrecoverable",result.unrecoverableSamples},{"invalid",result.invalidSamples}
    };
    if (auxiliaryOnly) header["previewMetadata"]["sourcePath"]="bracketing://preview-reference";
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
    auto depth=raw.reconstructionDepth;
    std::vector<DepthMask::Bytes> encodedMasks;
    if(depth) {
        const auto& d=*depth;
        try {
            if(!auxiliaryOnly) for(const auto& mask:d.masks) encodedMasks.push_back(CompressCoverage(mask));
            header["depth"]={{"width",d.width},{"height",d.height},{"sensorWidth",d.sensorWidth},{"sensorHeight",d.sensorHeight},
            {"masks",Raw::DepthMaskManifest(d.masks)},{"pairs",d.acceptedPairs},{"measuredFraction",d.measuredFraction},{"message",d.message}};
        } catch(const std::exception& e) {
            depth=Raw::UnavailableDepth(d.sensorWidth,d.sensorHeight,e.what());
            encodedMasks.clear();
            header["depth"]={{"width",0},{"height",0},{"sensorWidth",depth->sensorWidth},{"sensorHeight",depth->sensorHeight},
                {"masks",json::array()},{"pairs",0},{"measuredFraction",0},{"message",depth->message}};
        }
    }
    const auto text=header.dump();
    std::ofstream stream(path,std::ios::binary|std::ios::trunc);
    stream.write(magic.data(),magic.size());Write(stream,static_cast<std::uint64_t>(text.size()));
    stream.write(text.data(),text.size());
    if (!auxiliaryOnly) {
        WriteBlock(stream,raw.normalizedMosaicBuffer);
        WriteBlock(stream,raw.linearFloatBuffer);
    }
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
                (sample.localRejected?4:0)|(sample.fixedReference?8:0)|(sample.fallbackClipped?16:0)|(unsigned(sample.clippedReference)<<5)));
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
                checksum!=Stack::NodeMath::Sha256ContentIdentity(std::vector<std::string_view>{
                    {reinterpret_cast<const char*>(values.data()),values.size()*sizeof(float)}}))
                throw std::runtime_error("The super resolution inspection measurements could not be saved. Refresh the bracket.");
            WriteBlock(stream,values);
        }
    }
    if(result.panorama) {
        WriteBlock(stream,raw.outputCoverage);WriteBlock(stream,preview.coverage);
        WriteBlock(stream,result.panorama->ownership);
    }
    if(depth) {
        WriteBlock(stream,depth->inverseDepth);WriteBlock(stream,depth->confidence);
        for(const auto& mask:encodedMasks)WriteBlock(stream,mask);
    }
    stream.close();return stream.good();
}
void ReadResult(std::istream& stream,std::uint64_t bytes,std::size_t groups,Raw::Bracketing::BracketingResult& result,const std::filesystem::path& inspectionDirectory, const std::function<void(RawImageData&)>& loadMaster) {
    Reader reader(stream,bytes);std::array<char,8> found{};reader.Bytes(found.data(),found.size());
    if(found!=magic)throw std::runtime_error("The bracket result format is unsupported.");
    const auto headerBytes=reader.Value<std::uint64_t>();
    if(headerBytes>16u*1024*1024)throw std::runtime_error("The bracket result header is too large.");
    std::string text(static_cast<std::size_t>(headerBytes),'\0');reader.Bytes(text.data(),headerBytes);
    const auto header=json::parse(text);
    const int version=header.at("version");
    if(version<1||version>5)throw std::runtime_error("The bracket result version is unsupported.");
    const bool panorama=version>=2&&header.contains("panorama");
    auto raw=std::make_shared<Raw::RawImageData>();
    raw->metadata=Raw::Serialization::DeserializeMetadata(header.at("metadata"));
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
    if (header.value("externalPixels",false)) {
        if (!loadMaster) throw std::runtime_error("Locate the bracket master to restore the result.");
        loadMaster(*raw);
    } else {
        raw->normalizedMosaicBuffer=reader.Map<float>(pixels);
        raw->linearFloatBuffer=reader.Block<float>(pixels*3);
    }
    if(raw->reconstructedCameraRgb?raw->linearFloatBuffer.size()!=pixels*3:!raw->normalizedMosaicBuffer)
        throw std::runtime_error("The saved bracket pixels are incomplete.");
    auto maps=std::make_shared<Raw::RawImageData::MultiFrameMeasurementSidecars>();
    maps->variance=reader.Map<float>(pixels);maps->fusionUncertaintyVariance=reader.Map<float>(pixels);
    maps->effectiveSupport=reader.Map<float>(pixels);maps->validity=reader.Map<std::uint8_t>(pixels);
    maps->clipping=reader.Map<std::uint8_t>(pixels);maps->localRejection=reader.Map<std::uint8_t>(pixels);
    maps->fallbackReason=reader.Map<std::uint8_t>(pixels);
    maps->originalFrameIds=header.at("frames").get<std::vector<std::string>>();maps->evidenceIdentitySha256=header.at("evidence");
    raw->multiFrameMeasurementSidecars=maps;
    if(version>=3&&!maps->variance&&!maps->fusionUncertaintyVariance&&!maps->effectiveSupport&&
        !maps->validity&&!maps->clipping&&!maps->localRejection&&!maps->fallbackReason)
        raw->multiFrameMeasurementSidecars.reset();
    auto& preview=result.preview;preview.width=header.at("previewWidth");preview.height=header.at("previewHeight");
    const auto previewPixels=std::uint64_t(preview.width)*preview.height;
    if(previewPixels>pixels)throw std::runtime_error("Invalid bracket preview dimensions.");
    preview.metadata=Raw::Serialization::DeserializeMetadata(header.at("previewMetadata"));
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
            sample.clippedReference=static_cast<Raw::Bracketing::ClippedReference>((flags[i]>>5)&3);
        }
    }
    if(header.contains("inspection")) {
        const auto& saved=header.at("inspection");
        auto inspection=std::make_shared<Raw::Bracketing::BracketingResult::ReconstructionInspection>();
        inspection->scale=saved.at("scale");inspection->tilePixels=saved.at("tilePixels");
        inspection->sensorWidth=saved.at("sensorWidth");inspection->sensorHeight=saved.at("sensorHeight");
        inspection->groups=saved.at("groups");std::string error;
        if(!raw->reconstructedCameraRgb||!std::isfinite(inspection->scale)||inspection->scale<1||inspection->scale>4||
            !inspection->tilePixels||inspection->tilePixels>4096||inspection->groups!=groups||
            std::llround(inspection->sensorWidth*inspection->scale)!=raw->metadata.visibleWidth||
            std::llround(inspection->sensorHeight*inspection->scale)!=raw->metadata.visibleHeight||
            !Raw::Bracketing::Deserialize(saved.at("recipe"),inspection->recipe,error))
            throw std::runtime_error("Invalid saved reconstruction measurements.");
        const unsigned columns=(raw->metadata.visibleWidth-1)/inspection->tilePixels+1;
        const unsigned rows=(raw->metadata.visibleHeight-1)/inspection->tilePixels+1;
        if(saved.at("tiles").size()!=std::uint64_t(columns)*rows)
            throw std::runtime_error("Incomplete saved reconstruction measurements.");
        inspection->directory=inspectionDirectory/StorageId();
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
            if(values.size()!=count||checksum!=Stack::NodeMath::Sha256ContentIdentity(std::vector<std::string_view>{
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
    if(version>=3&&header.contains("depth")) {
        // Depth is the final, optional section. All image data has already passed validation.
        try {
            const auto& saved=header.at("depth");auto d=std::make_shared<Raw::ReconstructionDepth>();
            d->width=saved.at("width");d->height=saved.at("height");
            d->sensorWidth=saved.at("sensorWidth");d->sensorHeight=saved.at("sensorHeight");
            d->acceptedPairs=saved.at("pairs");d->measuredFraction=saved.at("measuredFraction");d->message=saved.at("message");
            if((!d->width!=!d->height)||(d->acceptedPairs&&!d->width)||d->width>512||d->height>512||!d->sensorWidth||!d->sensorHeight||d->acceptedPairs>8||
                !std::isfinite(d->measuredFraction)||d->measuredFraction<0||d->measuredFraction>1||
                (result.reconstructionInspection?
                    d->sensorWidth!=result.reconstructionInspection->sensorWidth||d->sensorHeight!=result.reconstructionInspection->sensorHeight:
                    std::uint64_t(d->sensorWidth)*raw->metadata.visibleHeight!=std::uint64_t(d->sensorHeight)*raw->metadata.visibleWidth))
                throw std::runtime_error("Invalid saved depth geometry.");
            const auto count=std::uint64_t(d->width)*d->height;
            d->inverseDepth=reader.Block<float>(count);d->confidence=reader.Block<float>(count);
            if(d->inverseDepth.size()!=d->confidence.size()||(!d->inverseDepth.empty()&&d->inverseDepth.size()!=count)||
                (d->acceptedPairs&&!d->Available()))throw std::runtime_error("Incomplete saved depth evidence.");
            // Older GPU completion passes could round a normalized weighted mean
            // just past an endpoint. Keep rejecting materially invalid evidence.
            constexpr float depthRoundoff=8*std::numeric_limits<float>::epsilon();
            for(float& v:d->inverseDepth) {
                if(v < -depthRoundoff||v > 1.f+depthRoundoff)throw std::runtime_error("Invalid saved relative depth.");
                v=std::clamp(v,0.f,1.f);
            }
            for(float v:d->confidence)if(v<0||v>1)throw std::runtime_error("Invalid saved depth confidence.");
            if(version>=4) {
                d->masks=ReadDepthMaskManifest(saved.at("masks"));
                if (version==5 && header.value("externalPixels",false)) {
                    const auto master=raw->reconstructionDepth;
                    if(master && !master->Available() && master->masks.empty() && !master->message.empty()) {
                        d->masks.clear();
                        d->message=master->message;
                    } else if ((!master && !d->masks.empty()) ||
                        (master && DepthMaskManifest(master->masks)!=saved.at("masks")))
                        throw std::runtime_error("The bracket master mask directory does not match the project.");
                    if (master) d->masks=master->masks;
                } else for(auto& mask:d->masks) {
                    const auto size=std::uint64_t(mask.w)*mask.h;
                    if (version==5) {
                        auto encoded=reader.Block<std::uint8_t>(compressBound(static_cast<uLong>(size)));
                        if (encoded.empty()) throw std::runtime_error("Missing compressed depth mask.");
                        SetCompressedCoverage(mask,std::move(encoded));
                        continue;
                    }
                    auto bytes=reader.Block<std::uint8_t>(size);
                    if(bytes.size()!=size)throw std::runtime_error("Truncated depth mask.");
                    mask.pixels=std::make_shared<const DepthMask::Bytes>(std::move(bytes));
                }
            }
            raw->reconstructionDepth=std::move(d);
            if(!reader.Finished())throw std::runtime_error("Unexpected trailing depth data.");
        } catch(const std::exception& e) {
            raw->reconstructionDepth=Raw::UnavailableDepth(raw->metadata.visibleWidth,raw->metadata.visibleHeight,e.what());
        }
    } else if(!reader.Finished())throw std::runtime_error("Unexpected data in saved bracket result.");
    result.raw=std::move(raw);result.identity=header.at("identity");result.fallbackSamples=header.at("fallback");
    result.unrecoverableSamples=header.at("unrecoverable");result.invalidSamples=header.at("invalid");
    result.status=Raw::Bracketing::BracketingResult::Status::Completed;result.message="Saved bracket ready.";
}
} // namespace Raw::Bracketing::Serialization
