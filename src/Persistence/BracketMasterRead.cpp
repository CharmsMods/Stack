#include "Persistence/BracketMaster.h"
#include "Persistence/BracketDngMaster.h"
#include "Persistence/BracketRgbMaster.h"
#include <algorithm>
#include "Raw/ReconstructionDepth.h"
#include "Raw/RawLoader.h"

namespace Stack::Project {
nlohmann::json BracketMasterMetadata(const std::filesystem::path& path) {
    auto ext = path.extension().u8string();
    std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){ return char(std::tolower(c)); });
    if (ext == ".dng") return DngMaster::ReadMetadata(path);
    if (ext == ".exr") return RgbMaster::ReadMetadata(path);
    return {};
}
// The Gallery stages RAW pixels lazily. Inspect just the mask directory when a
// master opens, so a disconnected node never causes a full image decode.
std::shared_ptr<const Raw::RawImageData> ReadBracketMasterMaskSource(const std::filesystem::path& path) {
    auto ext = path.extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return char(std::tolower(c)); });
    int orientation = 1;
    std::vector<Raw::DepthMask> masks;
    if (ext == ".dng") {
        masks = DngMaster::ReadMasks(path, &orientation);
    } else if (ext == ".exr") {
        const auto marker = RgbMaster::ReadMetadata(path);
        if (!marker.contains("depthMasks")) return {};
        masks = RgbMaster::ReadMasks(path, marker);
        orientation = marker.value("orientation", 1);
    }
    if (masks.empty()) return {};
    auto depth = std::make_shared<Raw::ReconstructionDepth>();
    depth->masks = std::move(masks);
    auto source = std::make_shared<Raw::RawImageData>();
    if (!Raw::RawLoader::LoadMetadata(path.u8string(),source->metadata))
        throw std::runtime_error(source->metadata.error);
    if (!source->metadata.orientation) source->metadata.orientation=orientation;
    source->reconstructionDepth = std::move(depth);
    return source;
}
void ReadBracketMasterPixels(const std::filesystem::path& path, const std::string& resultId, Raw::RawImageData& raw) {
    const int w=raw.metadata.rawWidth, h=raw.metadata.rawHeight;
    if (raw.reconstructedCameraRgb)
        raw.linearFloatBuffer=RgbMaster::Read(path,w,h,resultId);
    else
        raw.normalizedMosaicBuffer=std::make_shared<const std::vector<float>>(DngMaster::Read(path,w,h,resultId));
    const auto marker=raw.reconstructedCameraRgb?RgbMaster::ReadMetadata(path):DngMaster::ReadMetadata(path);
    if(marker.contains("depthMasks")) {
        auto depth=std::make_shared<Raw::ReconstructionDepth>();
        try {
            depth->masks=raw.reconstructedCameraRgb?RgbMaster::ReadMasks(path,marker):DngMaster::ReadMasks(path);
            if(depth->masks.size()!=marker.at("depthMasks").size())throw std::runtime_error("Incomplete bracket master masks.");
        } catch(const std::exception& e) {
            depth=Raw::UnavailableDepth(w,h,e.what());
        }
        raw.reconstructionDepth=std::move(depth);
    }
}
}
