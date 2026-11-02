#include "Persistence/BracketMaster.h"
#include "Raw/DepthMaskManifest.h"
#include "Raw/ReconstructionDepth.h"
#include "Persistence/BracketSourceReferences.h"
#include "Persistence/BracketDngMaster.h"
#include "Persistence/BracketRgbMaster.h"
#include "Persistence/ManagedAssetPaths.h"
#include "Raw/RawMetadataSerialization.h"
#include "NodeMath/ContractTypes.h"
#include <algorithm>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Stack::Project {
std::filesystem::path PublishBracketMaster(const ProjectStoreHandle& store, const RawProjectSnapshot& project,
    const MultiFrameSourceSet& set, const Raw::Bracketing::BracketingResult& result) {
    const auto& raw=*result.raw;
    const auto selected=BracketCaptureIds(set.settings.at("bracketing"));
    nlohmann::json sources=nlohmann::json::array();
    const EmbeddedAssetRecord* first=nullptr;
    std::int64_t earliest=0;
    for (const auto& frame:set.frames) {
        if (!selected.count(frame.frameId)) continue;
        const auto* asset=FindEmbeddedAsset(project,frame.assetId);
        if (!asset) throw std::runtime_error("Bracket capture identity is missing.");
        const auto time=asset->captureMetadataSummary.value("captureTimestamp",std::int64_t{});
        if (!first || (time>0 && (earliest<=0 || time<earliest))) { first=asset; earliest=time; }
        sources.push_back({{"assetId",asset->assetId},{"frameId",frame.frameId},{"path",asset->workspaceRelativeSourcePath},
            {"filename",asset->originalFilename},{"sha256",asset->sha256},{"byteLength",asset->byteLength}});
    }
    if (!first) throw std::runtime_error("The master has no source capture location.");
    auto capture=store->ResolveAssetFile(first->assetId);
    if (capture.empty()) capture=ExpectedSourcePath(store->StoragePath(),*first);
    if (capture.empty() || !std::filesystem::is_directory(capture.parent_path()))
        throw std::runtime_error("Locate the first capture folder to publish the bracket master.");
    auto& id=result.identity;
    nlohmann::json marker={{"schema","stack.bracket.master.v1"},{"resultId",id},
        {"recipeId",NodeMath::Sha256ContentIdentity(set.settings.at("bracketing").dump())},
        {"sources",std::move(sources)},{"orientation",raw.metadata.orientation},
        {"contentHash",raw.contentIdentityHash},{"mosaicHash",raw.normalizedMosaicContentHash},
        {"decoderVersion",raw.decoderIdentityVersion},{"cameraRgb",raw.reconstructedCameraRgb},{"metadata",Raw::Serialization::SerializeMetadata(raw.metadata)}};
    const std::vector<Raw::DepthMask> emptyMasks;
    const auto& masks=raw.reconstructionDepth?raw.reconstructionDepth->masks:emptyMasks;
    if(!masks.empty())marker["depthMasks"]=Raw::DepthMaskManifest(masks);
    const auto stem=capture.stem().u8string()+" Stack Bracket";
    const auto extension=raw.reconstructedCameraRgb?".exr":".dng";
    if (!raw.reconstructedCameraRgb && !raw.normalizedMosaicBuffer) throw std::runtime_error("The Bayer result has no measurements.");
    const auto& samples=raw.reconstructedCameraRgb?raw.linearFloatBuffer:*raw.normalizedMosaicBuffer;
    if (samples.empty() || id.empty()) throw std::runtime_error("The bracket measurement is incomplete.");
    const auto sameMaster = [&](const std::filesystem::path& path) {
        const auto stored=BracketMasterMetadata(path);
        if (!stored.is_object() || stored.value("resultId",std::string{})!=id ||
            stored.value("recipeId",std::string{})!=marker["recipeId"]) return false;
        // A matching embedded marker alone cannot certify a modified image.
        const int w=raw.metadata.rawWidth,h=raw.metadata.rawHeight;
        try {
            const auto decoded=raw.reconstructedCameraRgb ? RgbMaster::Read(path,w,h,id) : DngMaster::Read(path,w,h,id);
            if(!masks.empty() || stored.contains("depthMasks")) {
                if(stored.value("depthMasks",nlohmann::json::array())!=Raw::DepthMaskManifest(masks))return false;
                const auto saved=raw.reconstructedCameraRgb?RgbMaster::ReadMasks(path,stored):DngMaster::ReadMasks(path);
                if(saved.size()!=masks.size())return false;
                for(std::size_t i=0;i<saved.size();++i)if(saved[i].id!=masks[i].id || *saved[i].Read()!=*masks[i].Read())return false;
            }
            return decoded.size()==samples.size() && std::memcmp(decoded.data(),samples.data(),samples.size()*sizeof(float))==0;
        } catch (const std::exception&) { return false; }
    };
    struct Temp { std::filesystem::path path; ~Temp(){if (!path.empty()) {std::error_code ec;std::filesystem::remove(path,ec);}} } temp;
    for (unsigned n=1;n<=10000;++n) {
        const auto path=capture.parent_path()/std::filesystem::u8path(stem+(n==1?"":" ("+std::to_string(n)+")")+extension);
        std::error_code ec;
        if (std::filesystem::exists(path,ec)) { if (sameMaster(path)) return path; continue; }
        if (ec) throw std::runtime_error(ec.message());
        if (temp.path.empty()) {
            temp.path=capture.parent_path()/(".stack-master-"+GenerateStableUuid()+".tmp");
            if (raw.reconstructedCameraRgb) RgbMaster::Write(temp.path,raw.metadata.rawWidth,raw.metadata.rawHeight,samples,marker,masks);
            else DngMaster::Write(temp.path,raw,marker);
        }
#ifdef _WIN32
        if (MoveFileExW(temp.path.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)) { temp.path.clear();return path; }
        const auto error=GetLastError();
        if (error!=ERROR_FILE_EXISTS && error!=ERROR_ALREADY_EXISTS) throw std::runtime_error("Could not publish bracket master.");
#else
        std::filesystem::create_hard_link(temp.path,path,ec);
        if (!ec) return path;
        if (ec!=std::errc::file_exists) throw std::runtime_error(ec.message());
#endif
        if (sameMaster(path)) return path;
    }
    throw std::runtime_error("Too many files have this bracket name.");
}
}
