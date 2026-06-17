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
        return ext == ".arw" || ext == ".srf" || ext == ".sr2" || ext == ".raw" || ext == ".dng";
    } catch (...) {
        return false;
    }
#endif
}

bool RawLoader::LoadMetadata(const std::string& path, RawMetadata& outMetadata) {
    RawImageData data;
    if (!LoadFile(path, data)) {
        outMetadata = data.metadata;
        return false;
    }
    outMetadata = data.metadata;
    return outMetadata.error.empty();
}

bool RawLoader::LoadFile(const std::string& path, RawImageData& outData) {
    outData = {};
    outData.metadata.sourcePath = path;
    if (path.empty()) {
        outData.metadata.error = "No RAW source path.";
        return false;
    }

    const LibRawRuntimeStatus& runtimeStatus = GetLibRawRuntimeStatus();
    if (!runtimeStatus.runtimeAvailable) {
        outData.metadata.error = runtimeStatus.message;
        return false;
    }

    return DecodeWithLibRaw(path, outData);
}

} // namespace Raw
