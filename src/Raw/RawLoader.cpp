#ifdef STACK_READ_BRACKET_MASTERS
#include "Persistence/BracketMaster.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Persistence/BracketDngMaster.h"
#include "Raw/ReconstructionDepth.h"
#include "Raw/RawGalleryInputPolicy.h"
#ifdef STACK_HDR_FILE_INPUT
#include "Hdr/RasterSource.h"
#endif
#include "ThirdParty/stb_image.h"
#include <cmath>
#include <fstream>
#include "Raw/RawMetadataSerialization.h"
#endif
#include "RawLoader.h"

#include "LibRawRuntime.h"
#include "LibRawDecoder.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace Raw {
namespace {

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

#ifdef STACK_READ_BRACKET_MASTERS
bool ReadGalleryRaster(const std::filesystem::path& path, RawMetadata& metadata, RawImageData* output) {
    std::ifstream input(path,std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),{});
    int w=0,h=0,channels=0;
    if (bytes.size()>INT_MAX || !stbi_info_from_memory(bytes.data(),int(bytes.size()),&w,&h,&channels)) {
        metadata.error="Could not read the raster original."; return false;
    }
    metadata.rawWidth=metadata.visibleWidth=w; metadata.rawHeight=metadata.visibleHeight=h;
    metadata.pixelLayout=RawPixelLayout::LinearRgb; metadata.mosaiced=false; metadata.linearChannels=3;
    metadata.cameraWhiteBalance={1,1,1,1}; metadata.whiteLevel=1.f;
    metadata.bitDepth=stbi_is_16_bit_from_memory(bytes.data(),int(bytes.size())) ? 16 : 8;
    if (!output) return true;
    const auto sourceIdentity=Stack::RawEvidence::ComputeSourceIdentity(bytes);
    if(!sourceIdentity.valid) { metadata.error="Could not verify raster source identity."; return false; }
    metadata.sourceContentSha256=sourceIdentity.sha256;
    metadata.sourceByteSize=sourceIdentity.byteSize;
#ifdef STACK_HDR_FILE_INPUT
    // Only explicit RAW-recipe operations arrive here. Ordinary Open creates
    // a display-referred graph Image and keeps both the HDR and SDR renditions.
    Stack::Hdr::Image managed;
    if(Stack::Hdr::DecodeDisplaySource(bytes,managed)) {
        if(managed.width!=w || managed.height!=h) { metadata.error="Unexpected raster dimensions."; return false; }
        output->linearFloatBuffer.resize(size_t(w)*h*3);
        for(size_t i=0;i<managed.rgba.size()/4;++i) {
            if(managed.rgba[i*4+3]!=1.f) {
                metadata.error="These RAW edits need opaque pixels. Open the image in the graph editor to preserve its alpha channel.";
                return false;
            }
            std::copy_n(managed.rgba.data()+i*4,3,output->linearFloatBuffer.data()+i*3);
        }
        return true;
    }
#endif
    stbi_set_flip_vertically_on_load_thread(0);
    std::unique_ptr<stbi_us,decltype(&stbi_image_free)> rgba(stbi_load_16_from_memory(bytes.data(),int(bytes.size()),&w,&h,&channels,4),stbi_image_free);
    if (!rgba) { metadata.error="Could not decode the raster original.";return false; }
    output->linearFloatBuffer.resize(std::size_t(w)*h*3);
    for (std::size_t i=0;i<std::size_t(w)*h;++i) {
        if (rgba.get()[4*i+3]!=65535) {
            metadata.error="These RAW edits need opaque pixels. Open this image in the graph editor to preserve its alpha channel.";
            return false;
        }
        for (int c=0;c<3;++c) {
            const float x=rgba.get()[4*i+c]/65535.f;
            output->linearFloatBuffer[3*i+c]=x<=.04045f ? x/12.92f : std::pow((x+.055f)/1.055f,2.4f);
        }
    }
    return true;
}
#endif
} // namespace

bool RawLoader::IsRawPath(const std::string& path) {
#ifndef STACK_ENABLE_LIBRAW
    (void)path;
    return false;
#else
    try {
        const std::string ext = ToLower(std::filesystem::path(path).extension().string());
        static const char* kRawExtensions[] = {
            ".3fr", ".arw", ".cr2", ".cr3", ".dng", ".fff", ".iiq",
            ".nef", ".nrw", ".orf", ".pef", ".raf", ".raw", ".rw2",
            ".rwl", ".sr2", ".srf"
        };
        return std::find(
            std::begin(kRawExtensions),
            std::end(kRawExtensions),
            ext) != std::end(kRawExtensions);
    } catch (...) {
        return false;
    }
#endif
}

bool RawLoader::LoadMetadata(const std::string& path, RawMetadata& outMetadata) {
    outMetadata = {};
    outMetadata.sourcePath = path;
    if (path.empty()) {
        outMetadata.error = "No RAW source path.";
        return false;
    }
#ifdef STACK_READ_BRACKET_MASTERS
    try {
        const auto file=std::filesystem::u8path(path);
        if (Stack::RawWorkspace::IsGalleryRasterPath(file)) return ReadGalleryRaster(file,outMetadata,nullptr);
        const auto marker = Stack::Project::BracketMasterMetadata(file);
        if (marker.is_object()) {
            outMetadata = Serialization::DeserializeMetadata(marker.at("metadata"));
            outMetadata.sourcePath = path;
            return true;
        }
    } catch (const std::exception& e) { outMetadata.error=e.what();return false; }
#endif
    const LibRawRuntimeStatus& runtimeStatus = GetLibRawRuntimeStatus();
    if (!runtimeStatus.runtimeAvailable) {
        outMetadata.error = runtimeStatus.message;
        return false;
    }
    return ProbeMetadataWithLibRaw(path, outMetadata);
}

bool RawLoader::LoadFile(
    const std::string& path,
    RawImageData& outData,
    const std::function<bool()>& shouldCancel) {
    outData = {};
    outData.metadata.sourcePath = path;
    if (shouldCancel && shouldCancel()) {
        outData.metadata.error = "RAW load canceled.";
        return false;
    }
    if (path.empty()) {
        outData.metadata.error = "No RAW source path.";
        return false;
    }

#ifdef STACK_READ_BRACKET_MASTERS
    try {
        const auto file = std::filesystem::u8path(path);
        if (Stack::RawWorkspace::IsGalleryRasterPath(file)) return ReadGalleryRaster(file,outData.metadata,&outData);
        const auto marker = Stack::Project::BracketMasterMetadata(file);
        if (marker.is_object()) {
            outData.metadata = Serialization::DeserializeMetadata(marker.at("metadata"));
            outData.metadata.sourcePath = path;
            outData.contentIdentity = marker.at("resultId").get<std::string>();
            outData.contentIdentityHash = marker.at("contentHash").get<std::uint64_t>();
            outData.normalizedMosaicContentHash = marker.at("mosaicHash").get<std::uint64_t>();
            outData.decoderIdentityVersion = marker.at("decoderVersion").get<std::string>();
            outData.reconstructedCameraRgb = marker.at("cameraRgb");
            if (!outData.reconstructedCameraRgb)
                outData.normalizedMosaicInputContract = NormalizedMosaicInputContract::BracketingPreGain;
            const auto identity = Stack::RawEvidence::ComputeSourceIdentity(file, shouldCancel);
            if (!identity.valid) throw std::runtime_error("Could not verify bracket master: " + identity.reason);
            outData.metadata.sourceContentSha256 = identity.sha256;
            outData.metadata.sourceByteSize = identity.byteSize;
            Stack::Project::ReadBracketMasterPixels(file,outData.contentIdentity,outData);
            return true;
        }
    } catch (const std::exception& e) { outData.metadata.error=e.what();return false; }
#endif
    const LibRawRuntimeStatus& runtimeStatus = GetLibRawRuntimeStatus();
    if (!runtimeStatus.runtimeAvailable) {
        outData.metadata.error = runtimeStatus.message;
        return false;
    }
    if (shouldCancel && shouldCancel()) {
        outData.metadata.error = "RAW load canceled.";
        return false;
    }

    if(!DecodeWithLibRaw(path,outData,shouldCancel))return false;
#ifdef STACK_READ_BRACKET_MASTERS
    // Standalone StackBurst DNG exports use a calibrated raster and have no
    // project-master marker. Their semantic mask directories are self-contained.
    if(outData.metadata.isDng)try {
        auto masks=Stack::Project::DngMaster::ReadMasks(std::filesystem::u8path(path));
        if(!masks.empty()) {
            auto depth=std::make_shared<ReconstructionDepth>();depth->masks=std::move(masks);
            outData.reconstructionDepth=std::move(depth);
        }
    }catch(const std::exception& e){outData.metadata.error=e.what();return false;}
#endif
    return true;
}

} // namespace Raw
