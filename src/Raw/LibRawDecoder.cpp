#include "LibRawDecoder.h"
#include "Raw/DngMetadataSupplement.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Raw/RawOrientation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#ifdef STACK_ENABLE_LIBRAW
#include <libraw/libraw.h>
#endif

namespace Raw {
namespace {

constexpr std::size_t kCancellationCheckInterval = 65536;

std::filesystem::path NativeRawPath(const std::string& path) {
    try {
        const auto utf8 = std::filesystem::u8path(path);
        std::error_code error;
        if (std::filesystem::exists(utf8, error) && !error) return utf8;
    } catch (const std::exception&) {
    }
    return std::filesystem::path(path);
}

bool IsCancelled(const std::function<bool()>& shouldCancel) {
    return shouldCancel && shouldCancel();
}

void MarkCancelled(RawImageData& outData) {
    auto sourcePath = std::move(outData.metadata.sourcePath);
    outData = {};
    outData.metadata.sourcePath = std::move(sourcePath);
    outData.metadata.error = "RAW load canceled.";
}

int EstimateBitDepth(float whiteLevel) {
    if (whiteLevel <= 0.0f) {
        return 0;
    }
    return static_cast<int>(std::ceil(std::log2(whiteLevel + 1.0f)));
}

float SafePositive(float value, float fallback) {
    return value > 0.0f ? value : fallback;
}

#ifdef STACK_ENABLE_LIBRAW
struct LibRawCancellation {
    const std::function<bool()>& shouldCancel;
    bool cancelled = false;

    static int Progress(void* context, LibRaw_progress, int, int) noexcept {
        auto& state = *static_cast<LibRawCancellation*>(context);
        // A user callback must not unwind through LibRaw's C callback boundary.
        try {
            state.cancelled = state.cancelled || IsCancelled(state.shouldCancel);
        } catch (...) {
            state.cancelled = true;
        }
        return state.cancelled ? 1 : 0;
    }
};

bool HasMatrix3x3(const std::array<float, 9>& matrix) {
    for (float value : matrix) {
        if (std::abs(value) > 0.000001f) {
            return true;
        }
    }
    return false;
}

char ColorChar(const libraw_data_t& image, int colorIndex) {
    if (colorIndex < 0 || colorIndex >= 4) {
        return '?';
    }
    const char value = image.idata.cdesc[colorIndex];
    return value ? value : '?';
}

CfaPattern PatternFromString(const std::string& pattern) {
    if (pattern == "RGGB") return CfaPattern::RGGB;
    if (pattern == "BGGR") return CfaPattern::BGGR;
    if (pattern == "GBRG") return CfaPattern::GBRG;
    if (pattern == "GRBG") return CfaPattern::GRBG;
    return CfaPattern::Unknown;
}

CfaPattern ExtractCfaPattern(LibRaw& processor, const libraw_data_t& image) {
    if (image.idata.colors < 3 || image.idata.filters == 0) {
        return CfaPattern::Unknown;
    }

    const int top = std::max(0, static_cast<int>(image.sizes.top_margin));
    const int left = std::max(0, static_cast<int>(image.sizes.left_margin));
    std::string pattern;
    pattern.reserve(4);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            pattern.push_back(ColorChar(image, processor.COLOR(top + y, left + x)));
        }
    }
    return PatternFromString(pattern);
}

void SetCfaBlackForVisiblePattern(
    LibRaw& processor,
    const libraw_data_t& image,
    const float patternBlack[4],
    RawMetadata& metadata) {
    bool wroteGreen1 = false;
    bool wroteGreen2 = false;
    const int top = std::max(0, static_cast<int>(image.sizes.top_margin));
    const int left = std::max(0, static_cast<int>(image.sizes.left_margin));
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            const float black = patternBlack[y * 2 + x];
            const int color = processor.COLOR(top + y, left + x);
            if (color == 0) {
                metadata.perChannelBlack[0] = black;
            } else if (color == 2) {
                metadata.perChannelBlack[2] = black;
            } else if (!wroteGreen1) {
                metadata.perChannelBlack[1] = black;
                wroteGreen1 = true;
            } else if (!wroteGreen2) {
                metadata.perChannelBlack[3] = black;
                wroteGreen2 = true;
            }
        }
    }
    if (metadata.perChannelBlack[3] <= 0.0f) {
        metadata.perChannelBlack[3] = metadata.perChannelBlack[1];
    }
}

void ExtractDngLevels(LibRaw& processor, RawMetadata& metadata) {
    const libraw_data_t& image = processor.imgdata;
    metadata.isDng = image.idata.dng_version != 0;
    if (!metadata.isDng) {
        metadata.blackLevelSource = "LibRaw color.black";
        metadata.whiteLevelSource = "LibRaw color.maximum";
        metadata.whiteBalanceSource = "LibRaw cam_mul";
        metadata.cameraMatrixSource = "LibRaw rgb_cam";
        return;
    }

    const libraw_dng_levels_t& levels = image.color.dng_levels;
    const bool hasDngBlack = (levels.parsedfields & LIBRAW_DNGFM_BLACK) != 0;
    const bool hasDngWhite = (levels.parsedfields & LIBRAW_DNGFM_WHITE) != 0;
    const bool hasAsShotNeutral = (levels.parsedfields & LIBRAW_DNGFM_ASSHOTNEUTRAL) != 0;

    if (hasDngWhite && levels.dng_whitelevel[0] > 0) {
        metadata.whiteLevel = static_cast<float>(levels.dng_whitelevel[0]);
        metadata.whiteLevelSource = "DNG WhiteLevel";
    } else {
        metadata.whiteLevelSource = "LibRaw color.maximum";
    }

    if (hasDngBlack) {
        float patternBlack[4] {
            levels.dng_fcblack[6] > 0.0f ? levels.dng_fcblack[6] : static_cast<float>(levels.dng_cblack[6]),
            levels.dng_fcblack[7] > 0.0f ? levels.dng_fcblack[7] : static_cast<float>(levels.dng_cblack[7]),
            levels.dng_fcblack[8] > 0.0f ? levels.dng_fcblack[8] : static_cast<float>(levels.dng_cblack[8]),
            levels.dng_fcblack[9] > 0.0f ? levels.dng_fcblack[9] : static_cast<float>(levels.dng_cblack[9])
        };
        const bool hasPattern = patternBlack[0] > 0.0f || patternBlack[1] > 0.0f || patternBlack[2] > 0.0f || patternBlack[3] > 0.0f;
        if (hasPattern && metadata.pixelLayout == RawPixelLayout::MosaicBayer) {
            SetCfaBlackForVisiblePattern(processor, image, patternBlack, metadata);
            metadata.blackLevel = (patternBlack[0] + patternBlack[1] + patternBlack[2] + patternBlack[3]) * 0.25f;
            metadata.blackLevelSource = "DNG BlackLevel pattern";
        } else if (levels.dng_fblack > 0.0f || levels.dng_black > 0) {
            metadata.blackLevel = levels.dng_fblack > 0.0f ? levels.dng_fblack : static_cast<float>(levels.dng_black);
            metadata.perChannelBlack = { metadata.blackLevel, metadata.blackLevel, metadata.blackLevel, metadata.blackLevel };
            metadata.blackLevelSource = "DNG BlackLevel";
        } else {
            metadata.blackLevelSource = "LibRaw color.black";
        }
    } else {
        metadata.blackLevelSource = "LibRaw color.black";
    }

    if (hasAsShotNeutral) {
        for (int i = 0; i < 3; ++i) {
            metadata.dngAsShotNeutral[static_cast<std::size_t>(i)] = levels.asshotneutral[i];
        }
        metadata.hasDngAsShotNeutral = metadata.dngAsShotNeutral[0] > 0.0001f &&
            metadata.dngAsShotNeutral[1] > 0.0001f &&
            metadata.dngAsShotNeutral[2] > 0.0001f;
        if (metadata.hasDngAsShotNeutral) {
            metadata.cameraWhiteBalance[0] = 1.0f / metadata.dngAsShotNeutral[0];
            metadata.cameraWhiteBalance[1] = 1.0f / metadata.dngAsShotNeutral[1];
            metadata.cameraWhiteBalance[2] = 1.0f / metadata.dngAsShotNeutral[2];
            metadata.cameraWhiteBalance[3] = metadata.cameraWhiteBalance[1];
            metadata.whiteBalanceSource = "DNG AsShotNeutral";
        }
    }
    if (metadata.whiteBalanceSource.empty()) {
        metadata.whiteBalanceSource = "LibRaw cam_mul";
    }
}

void ExtractDngColorMetadata(const libraw_data_t& image, RawMetadata& metadata) {
    if (!metadata.isDng) {
        return;
    }
    for (int set = 0; set < 2; ++set) {
        const libraw_dng_color_t& color = image.color.dng_color[set];
        const bool hasColorMatrix = (color.parsedfields & LIBRAW_DNGFM_COLORMATRIX) != 0;
        const bool hasForwardMatrix = (color.parsedfields & LIBRAW_DNGFM_FORWARDMATRIX) != 0;
        std::array<float, 9> colorMatrix {};
        std::array<float, 9> forwardMatrix {};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                colorMatrix[static_cast<std::size_t>(r * 3 + c)] = color.colormatrix[r][c];
                forwardMatrix[static_cast<std::size_t>(r * 3 + c)] = color.forwardmatrix[r][c];
            }
        }
        if (set == 0) {
            metadata.dngIlluminant1 = static_cast<int>(color.illuminant);
            metadata.dngColorMatrix1 = colorMatrix;
            metadata.dngForwardMatrix1 = forwardMatrix;
            metadata.hasDngColorMatrix1 = hasColorMatrix && HasMatrix3x3(colorMatrix);
            metadata.hasDngForwardMatrix1 = hasForwardMatrix && HasMatrix3x3(forwardMatrix);
        } else {
            metadata.dngIlluminant2 = static_cast<int>(color.illuminant);
            metadata.dngColorMatrix2 = colorMatrix;
            metadata.dngForwardMatrix2 = forwardMatrix;
            metadata.hasDngColorMatrix2 = hasColorMatrix && HasMatrix3x3(colorMatrix);
            metadata.hasDngForwardMatrix2 = hasForwardMatrix && HasMatrix3x3(forwardMatrix);
        }
    }
    if (metadata.hasDngForwardMatrix1) {
        metadata.cameraMatrixSource = "DNG ForwardMatrix 1";
    } else if (metadata.hasDngForwardMatrix2) {
        metadata.cameraMatrixSource = "DNG ForwardMatrix 2";
    } else if (metadata.hasDngColorMatrix1 || metadata.hasDngColorMatrix2) {
        metadata.cameraMatrixSource = "DNG ColorMatrix";
    } else {
        metadata.cameraMatrixSource = "LibRaw rgb_cam";
    }
}

void ExtractMetadata(
    LibRaw& processor,
    const std::string& path,
    RawMetadata& metadata,
    bool includeDngSupplement = true,
    const std::function<bool()>& shouldCancel = {}) {
    const libraw_data_t& image = processor.imgdata;
    const libraw_imgother_t& capture = image.other;
    metadata.cameraMake = image.idata.make ? image.idata.make : "";
    metadata.cameraModel = image.idata.model ? image.idata.model : "";
    metadata.rawWidth = image.sizes.raw_width;
    metadata.rawHeight = image.sizes.raw_height;
    metadata.visibleWidth = image.sizes.width > 0 ? image.sizes.width : image.sizes.iwidth;
    metadata.visibleHeight = image.sizes.height > 0 ? image.sizes.height : image.sizes.iheight;
    metadata.leftMargin = image.sizes.left_margin;
    metadata.topMargin = image.sizes.top_margin;
    metadata.orientation = ExifOrientationFromLibRawFlip(image.sizes.flip);
    metadata.isDng = image.idata.dng_version != 0;
    metadata.whiteLevel = SafePositive(static_cast<float>(image.color.maximum), 65535.0f);
    metadata.blackLevel = std::max(0.0f, static_cast<float>(image.color.black));
    metadata.bitDepth = EstimateBitDepth(metadata.whiteLevel);
    metadata.exposureTimeSeconds = std::max(0.0f, capture.shutter);
    metadata.isoSpeed = std::max(0.0f, capture.iso_speed);
    metadata.apertureFNumber = std::max(0.0f, capture.aperture);
    metadata.focalLengthMm = std::max(0.0f, capture.focal_len);
    metadata.lensModel = image.lens.Lens ? image.lens.Lens : "";
    metadata.captureTimestamp = capture.timestamp > 0
        ? static_cast<std::int64_t>(capture.timestamp)
        : 0;
    metadata.hasExposureTime = metadata.exposureTimeSeconds > 0.0f;
    metadata.hasIsoSpeed = metadata.isoSpeed > 0.0f;
    metadata.hasApertureFNumber = metadata.apertureFNumber > 0.0f;
    metadata.hasFocalLength = metadata.focalLengthMm > 0.0f;
    metadata.hasCaptureTimestamp = metadata.captureTimestamp > 0;
    metadata.mosaiced = image.idata.filters != 0 && image.idata.colors >= 3;
    metadata.pixelLayout = metadata.mosaiced ? RawPixelLayout::MosaicBayer : RawPixelLayout::Unknown;
    metadata.cfaPattern = metadata.mosaiced ? ExtractCfaPattern(processor, image) : CfaPattern::Unknown;
    if (!metadata.mosaiced && image.idata.colors >= 3) {
        metadata.pixelLayout = RawPixelLayout::LinearRgb;
        metadata.linearChannels = std::clamp(image.idata.colors, 3, 4);
        metadata.dngTypeStatus = metadata.isDng ? "DNG type: Linear RGB / demosaic skipped" : "";
        metadata.uploadFormat = "RGBA16F";
    } else if (metadata.pixelLayout == RawPixelLayout::MosaicBayer) {
        metadata.dngTypeStatus = metadata.isDng ? "DNG type: Mosaic RAW" : "";
        metadata.uploadFormat = "R16UI";
    }

    for (int i = 0; i < 4; ++i) {
        metadata.perChannelBlack[i] = static_cast<float>(image.color.cblack[i]);
        metadata.cameraWhiteBalance[i] = SafePositive(image.color.cam_mul[i], 1.0f);
        metadata.daylightWhiteBalance[i] = SafePositive(image.color.pre_mul[i], 1.0f);
    }
    ExtractDngLevels(processor, metadata);
    // ARW2 encodes an eleven-bit value and decodes it through this curve.
    // Its largest reachable value can be below color.maximum. Comparing
    // against the nominal white level then treats the clipped plateau as
    // valid HDR evidence. Use the codec endpoint, never an observed image max.
    const auto* decoder=processor.unpack_function_name();
    if(!metadata.isDng&&decoder&&std::string(decoder)=="sony_arw2_load_raw()") {
        const float decodedWhite=static_cast<float>(image.color.curve[0x7ffu<<1u]);
        if(decodedWhite>metadata.blackLevel&&decodedWhite<metadata.whiteLevel) {
            metadata.whiteLevel=decodedWhite;
            metadata.whiteLevelSource="LibRaw ARW2 decoded curve endpoint";
            metadata.bitDepth=EstimateBitDepth(metadata.whiteLevel);
        }
    }

    bool hasMatrix = false;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            const float value = image.color.rgb_cam[r][c];
            metadata.cameraToSrgb[static_cast<std::size_t>(r * 3 + c)] = value;
            hasMatrix = hasMatrix || std::abs(value) > 0.000001f;
        }
    }
    metadata.hasCameraMatrix = hasMatrix;
    ExtractDngColorMetadata(image, metadata);
    if (includeDngSupplement) {
        if (!ApplyDngSupplement(NativeRawPath(path), metadata, shouldCancel)) {
            metadata.error = "RAW load canceled.";
            return;
        }
    }

    if (metadata.visibleWidth <= 0 || metadata.visibleHeight <= 0) {
        metadata.visibleWidth = metadata.rawWidth;
        metadata.visibleHeight = metadata.rawHeight;
    }
    if (metadata.pixelLayout != RawPixelLayout::MosaicBayer && metadata.pixelLayout != RawPixelLayout::LinearRgb) {
        metadata.dngTypeStatus = "DNG type: Unsupported/unknown";
        metadata.error = "Unsupported RAW pixel layout.";
    } else if (metadata.pixelLayout == RawPixelLayout::MosaicBayer && metadata.cfaPattern == CfaPattern::Unknown) {
        metadata.dngTypeStatus = "DNG type: Unsupported/unknown";
        metadata.error = "Unsupported RAW CFA pattern.";
    }
    if (metadata.pixelLayout == RawPixelLayout::MosaicBayer &&
        metadata.dngCfaRepeatPatternDim[0] > 0 &&
        !(metadata.dngCfaRepeatPatternDim[0] == 2 && metadata.dngCfaRepeatPatternDim[1] == 2)) {
        metadata.error = "Unsupported DNG CFA layout. Only 2x2 Bayer mosaics are supported.";
    }
}

bool ExtractLinearStats(RawImageData& data, const std::function<bool()>& shouldCancel) {
    if (!data.linearUInt16Buffer.empty()) {
        std::uint16_t minValue = data.linearUInt16Buffer.front();
        std::uint16_t maxValue = data.linearUInt16Buffer.front();
        for (std::size_t i = 0; i < data.linearUInt16Buffer.size(); ++i) {
            if ((i % kCancellationCheckInterval) == 0 && IsCancelled(shouldCancel)) {
                return false;
            }
            minValue = std::min(minValue, data.linearUInt16Buffer[i]);
            maxValue = std::max(maxValue, data.linearUInt16Buffer[i]);
        }
        data.metadata.rawMinimum = static_cast<float>(minValue);
        data.metadata.rawMaximum = static_cast<float>(maxValue);
    } else if (!data.linearFloatBuffer.empty()) {
        float minValue = data.linearFloatBuffer.front();
        float maxValue = data.linearFloatBuffer.front();
        for (std::size_t i = 0; i < data.linearFloatBuffer.size(); ++i) {
            if ((i % kCancellationCheckInterval) == 0 && IsCancelled(shouldCancel)) {
                return false;
            }
            minValue = std::min(minValue, data.linearFloatBuffer[i]);
            maxValue = std::max(maxValue, data.linearFloatBuffer[i]);
        }
        data.metadata.rawMinimum = minValue;
        data.metadata.rawMaximum = maxValue;
    }
    data.metadata.defaultWhiteClipPercent = 0.0f;
    return !IsCancelled(shouldCancel);
}

template <typename T, typename Sample>
bool CopyColorImage(
    const T* source,
    int width,
    int height,
    int left,
    int top,
    int stridePixels,
    int sourceChannels,
    int outputChannels,
    std::vector<Sample>& output,
    const std::function<bool()>& shouldCancel) {
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    output.assign(pixelCount * static_cast<std::size_t>(outputChannels), 0);
    const int safeStride = stridePixels > 0 ? stridePixels : width;
    for (int y = 0; y < height; ++y) {
        if (IsCancelled(shouldCancel)) {
            output.clear();
            return false;
        }
        for (int x = 0; x < width; ++x) {
            const std::size_t outPixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            const std::size_t inPixel = static_cast<std::size_t>(top + y) *
                static_cast<std::size_t>(safeStride) + static_cast<std::size_t>(left + x);
            for (int c = 0; c < outputChannels; ++c) {
                const int srcC = std::min(c, std::max(0, sourceChannels - 1));
                output[outPixel * static_cast<std::size_t>(outputChannels) + static_cast<std::size_t>(c)] =
                    static_cast<Sample>(source[inPixel][srcC]);
            }
        }
    }
    return !IsCancelled(shouldCancel);
}

bool CopyRawImageToBufferAndStats(
    const std::uint16_t* source,
    int width,
    int height,
    int strideSamples,
    std::vector<std::uint16_t>& output,
    RawMetadata& metadata,
    const std::function<bool()>& shouldCancel) {
    const std::size_t pixelCount =
        static_cast<std::size_t>(std::max(0, width)) *
        static_cast<std::size_t>(std::max(0, height));
    if (source == nullptr || width <= 0 || height <= 0 || pixelCount == 0) {
        output.clear();
        return false;
    }
    output.assign(pixelCount, 0);
    const int safeStride = std::max(width, strideSamples);
    std::uint16_t minimum = source[0];
    std::uint16_t maximum = source[0];
    std::size_t clipped = 0;
    const float white = metadata.whiteLevel;
    for (int y = 0; y < height; ++y) {
        if (IsCancelled(shouldCancel)) {
            output.clear();
            return false;
        }
        const std::uint16_t* sourceRow =
            source + static_cast<std::ptrdiff_t>(y) * safeStride;
        std::uint16_t* destinationRow =
            output.data() + static_cast<std::ptrdiff_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            const std::uint16_t value = sourceRow[x];
            destinationRow[x] = value;
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
            if (white > 0.0f && static_cast<float>(value) >= white) {
                ++clipped;
            }
        }
    }
    metadata.rawMinimum = static_cast<float>(minimum);
    metadata.rawMaximum = static_cast<float>(maximum);
    metadata.defaultWhiteClipPercent = white > 0.0f
        ? 100.0f * static_cast<float>(clipped) /
            static_cast<float>(pixelCount)
        : 0.0f;
    return !IsCancelled(shouldCancel);
}
#endif

} // namespace

bool ProbeMetadataWithLibRaw(
    const std::string& path,
    RawMetadata& outMetadata) {
    outMetadata = {};
    outMetadata.sourcePath = path;
#ifndef STACK_ENABLE_LIBRAW
    outMetadata.error = "LibRaw support is disabled in this build.";
    return false;
#else
    if (path.empty()) {
        outMetadata.error = "No RAW source path.";
        return false;
    }
    LibRaw processor;
    const int status = processor.open_file(NativeRawPath(path).c_str());
    if (status != LIBRAW_SUCCESS) {
        outMetadata.error = std::string("LibRaw open_file failed: ") +
            libraw_strerror(status);
        return false;
    }
    // Do not call unpack() here. Header-provided LibRaw fields are sufficient
    // for structural burst compatibility. Deep DNG supplements and pixel
    // statistics remain part of the ordinary decode path.
    ExtractMetadata(processor, path, outMetadata, false);
    processor.recycle();
    return outMetadata.error.empty();
#endif
}

bool DecodeWithLibRaw(
    const std::string& path,
    RawImageData& outData,
    const std::function<bool()>& shouldCancel) {
    outData = {};
    outData.metadata.sourcePath = path;

#ifndef STACK_ENABLE_LIBRAW
    outData.metadata.error = "LibRaw support is disabled in this build.";
    return false;
#else
    if (path.empty()) {
        outData.metadata.error = "No RAW source path.";
        return false;
    }
    if (IsCancelled(shouldCancel)) {
        MarkCancelled(outData);
        return false;
    }

    const Stack::RawEvidence::SourceIdentity sourceIdentity =
        Stack::RawEvidence::ComputeSourceIdentity(NativeRawPath(path), shouldCancel);
    if (!sourceIdentity.valid) {
        if (sourceIdentity.reason == "source-hash-canceled") {
            MarkCancelled(outData);
            return false;
        }
        outData.metadata.error = "RAW source identity could not be computed: " + sourceIdentity.reason;
        return false;
    }
    outData.metadata.sourceContentSha256 = sourceIdentity.sha256;
    outData.metadata.sourceByteSize = sourceIdentity.byteSize;

    LibRawCancellation cancellation { shouldCancel };
    LibRaw processor;
    processor.set_progress_handler(&LibRawCancellation::Progress, &cancellation);
    int status = processor.open_file(NativeRawPath(path).c_str());
    if (status == LIBRAW_CANCELLED_BY_CALLBACK || cancellation.cancelled || IsCancelled(shouldCancel)) {
        MarkCancelled(outData);
        return false;
    }
    if (status != LIBRAW_SUCCESS) {
        outData.metadata.error = std::string("LibRaw open_file failed: ") + libraw_strerror(status);
        return false;
    }
    if (IsCancelled(shouldCancel)) {
        MarkCancelled(outData);
        processor.recycle();
        return false;
    }

    status = processor.unpack();
    if (status == LIBRAW_CANCELLED_BY_CALLBACK || cancellation.cancelled || IsCancelled(shouldCancel)) {
        MarkCancelled(outData);
        return false;
    }
    if (status != LIBRAW_SUCCESS) {
        outData.metadata.error = std::string("LibRaw unpack failed: ") + libraw_strerror(status);
        processor.recycle();
        return false;
    }
    if (IsCancelled(shouldCancel)) {
        MarkCancelled(outData);
        processor.recycle();
        return false;
    }

    ExtractMetadata(processor, path, outData.metadata, true, shouldCancel);
    if (outData.metadata.error == "RAW load canceled.") {
        MarkCancelled(outData);
        return false;
    }
    if (!outData.metadata.error.empty()) {
        processor.recycle();
        return false;
    }
    if (IsCancelled(shouldCancel)) {
        MarkCancelled(outData);
        processor.recycle();
        return false;
    }
    const Stack::RawEvidence::DecodeIdentity decodeIdentity =
        Stack::RawEvidence::BuildDecodeIdentity(
            outData.metadata,
            Stack::RawEvidence::DecodeIdentityOptions {});
    outData.decoderIdentityVersion =
        Stack::RawEvidence::kRawDecoderIdentityVersion;
    outData.contentIdentity = sourceIdentity.sha256 + ":" +
        decodeIdentity.sha256;
    outData.contentIdentityHash = static_cast<std::uint64_t>(
        std::hash<std::string>{}(outData.contentIdentity));

    const int rawWidth = outData.metadata.rawWidth;
    const int rawHeight = outData.metadata.rawHeight;
    const std::size_t pixelCount = static_cast<std::size_t>(std::max(0, rawWidth)) * static_cast<std::size_t>(std::max(0, rawHeight));
    if (outData.metadata.pixelLayout == RawPixelLayout::LinearRgb) {
        const int channels = std::clamp(outData.metadata.linearChannels > 0 ? outData.metadata.linearChannels : processor.imgdata.idata.colors, 3, 4);
        outData.metadata.linearChannels = channels;
        const int visibleWidth = outData.metadata.visibleWidth > 0 ? outData.metadata.visibleWidth : rawWidth;
        const int visibleHeight = outData.metadata.visibleHeight > 0 ? outData.metadata.visibleHeight : rawHeight;
        const int left = std::max(0, outData.metadata.leftMargin);
        const int top = std::max(0, outData.metadata.topMargin);
        if (rawWidth <= 0 || rawHeight <= 0 || visibleWidth <= 0 || visibleHeight <= 0) {
            outData.metadata.error = "Linear DNG has invalid dimensions.";
            processor.recycle();
            return false;
        }
        const auto& rawdata = processor.imgdata.rawdata;
        if (rawdata.color3_image) {
            const int stride = processor.imgdata.sizes.raw_pitch > 0 ? processor.imgdata.sizes.raw_pitch / static_cast<int>(3 * sizeof(std::uint16_t)) : rawWidth;
            if (!CopyColorImage(
                    rawdata.color3_image,
                    visibleWidth,
                    visibleHeight,
                    left,
                    top,
                    stride,
                    3,
                    channels,
                    outData.linearUInt16Buffer,
                    shouldCancel)) {
                MarkCancelled(outData);
                processor.recycle();
                return false;
            }
            outData.metadata.linearSampleFormat = RawSampleFormat::UInt16;
        } else if (rawdata.color4_image) {
            const int stride = processor.imgdata.sizes.raw_pitch > 0 ? processor.imgdata.sizes.raw_pitch / static_cast<int>(4 * sizeof(std::uint16_t)) : rawWidth;
            if (!CopyColorImage(
                    rawdata.color4_image,
                    visibleWidth,
                    visibleHeight,
                    left,
                    top,
                    stride,
                    4,
                    channels,
                    outData.linearUInt16Buffer,
                    shouldCancel)) {
                MarkCancelled(outData);
                processor.recycle();
                return false;
            }
            outData.metadata.linearSampleFormat = RawSampleFormat::UInt16;
        } else if (rawdata.float3_image) {
            const int stride = processor.imgdata.sizes.raw_pitch > 0 ? processor.imgdata.sizes.raw_pitch / static_cast<int>(3 * sizeof(float)) : rawWidth;
            if (!CopyColorImage(
                    rawdata.float3_image,
                    visibleWidth,
                    visibleHeight,
                    left,
                    top,
                    stride,
                    3,
                    channels,
                    outData.linearFloatBuffer,
                    shouldCancel)) {
                MarkCancelled(outData);
                processor.recycle();
                return false;
            }
            outData.metadata.linearSampleFormat = RawSampleFormat::Float32;
        } else if (rawdata.float4_image) {
            const int stride = processor.imgdata.sizes.raw_pitch > 0 ? processor.imgdata.sizes.raw_pitch / static_cast<int>(4 * sizeof(float)) : rawWidth;
            if (!CopyColorImage(
                    rawdata.float4_image,
                    visibleWidth,
                    visibleHeight,
                    left,
                    top,
                    stride,
                    4,
                    channels,
                    outData.linearFloatBuffer,
                    shouldCancel)) {
                MarkCancelled(outData);
                processor.recycle();
                return false;
            }
            outData.metadata.linearSampleFormat = RawSampleFormat::Float32;
        } else {
            outData.metadata.error = "Linear DNG detected but LibRaw did not expose a color3/color4 or float color buffer.";
            processor.recycle();
            return false;
        }
        if (!ExtractLinearStats(outData, shouldCancel)) {
            MarkCancelled(outData);
            processor.recycle();
            return false;
        }
        const int warnings = processor.imgdata.process_warnings;
        if (warnings != 0) {
            outData.metadata.warnings.push_back("LibRaw reported process warnings: " + std::to_string(warnings));
        }
        processor.recycle();
        return true;
    }

    if (!processor.imgdata.rawdata.raw_image || rawWidth <= 0 || rawHeight <= 0 || pixelCount == 0) {
        outData.metadata.error = "LibRaw did not expose a mosaiced raw_image buffer after unpack().";
        processor.recycle();
        return false;
    }

    const int rawStrideSamples = processor.imgdata.sizes.raw_pitch > 0
        ? processor.imgdata.sizes.raw_pitch /
            static_cast<int>(sizeof(std::uint16_t))
        : rawWidth;
    if (!CopyRawImageToBufferAndStats(
            processor.imgdata.rawdata.raw_image,
            rawWidth,
            rawHeight,
            rawStrideSamples,
            outData.rawBuffer,
            outData.metadata,
            shouldCancel)) {
        MarkCancelled(outData);
        processor.recycle();
        return false;
    }
    const int warnings = processor.imgdata.process_warnings;
    if (warnings != 0) {
        outData.metadata.warnings.push_back("LibRaw reported process warnings: " + std::to_string(warnings));
    }

    processor.recycle();
    return true;
#endif
}

} // namespace Raw
