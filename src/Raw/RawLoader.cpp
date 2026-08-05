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

    const LibRawRuntimeStatus& runtimeStatus = GetLibRawRuntimeStatus();
    if (!runtimeStatus.runtimeAvailable) {
        outData.metadata.error = runtimeStatus.message;
        return false;
    }
    if (shouldCancel && shouldCancel()) {
        outData.metadata.error = "RAW load canceled.";
        return false;
    }

    return DecodeWithLibRaw(path, outData, shouldCancel);
}

} // namespace Raw
