#pragma once

#include "Raw/RawImageData.h"
#include "Utils/HashUtils.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Stack::Mfsr {

enum class MfsrInputFamily {
    Unknown = 0,
    RawBurst,
    RasterBurst,
    MixedUnsupported
};

enum class MfsrFrameClass {
    Unknown = 0,
    RawMosaic,
    RawLinear,
    RasterLinear
};

enum class MfsrScalePreset {
    Auto = 0,
    Scale125,
    Scale150,
    Scale200
};

enum class MfsrQualityPreset {
    Automatic = 0,
    Conservative,
    Balanced,
    Aggressive
};

enum class MfsrValidationSeverity {
    Info = 0,
    Warning,
    Error
};

enum class MfsrValidationCode {
    Ok = 0,
    EmptyInputSet,
    MissingReferenceInput,
    MultipleReferenceInputs,
    UnsupportedInputKind,
    MixedInputFamilies,
    InvalidDimensions,
    IncompatibleDimensions,
    IncompatibleRawMetadata
};

inline const char* MfsrInputFamilyStableString(MfsrInputFamily family) {
    switch (family) {
        case MfsrInputFamily::RawBurst: return "RawBurst";
        case MfsrInputFamily::RasterBurst: return "RasterBurst";
        case MfsrInputFamily::MixedUnsupported: return "MixedUnsupported";
        case MfsrInputFamily::Unknown:
        default:
            return "Unknown";
    }
}

inline const char* MfsrFrameClassStableString(MfsrFrameClass frameClass) {
    switch (frameClass) {
        case MfsrFrameClass::RawMosaic: return "RAW_MOSAIC";
        case MfsrFrameClass::RawLinear: return "RAW_LINEAR";
        case MfsrFrameClass::RasterLinear: return "RASTER_LINEAR";
        case MfsrFrameClass::Unknown:
        default:
            return "Unknown";
    }
}

inline const char* MfsrScalePresetStableString(MfsrScalePreset preset) {
    switch (preset) {
        case MfsrScalePreset::Scale125: return "Scale125";
        case MfsrScalePreset::Scale150: return "Scale150";
        case MfsrScalePreset::Scale200: return "Scale200";
        case MfsrScalePreset::Auto:
        default:
            return "Auto";
    }
}

inline MfsrScalePreset MfsrScalePresetFromStableString(const std::string& value) {
    if (value == "Scale125" || value == "1.25x") return MfsrScalePreset::Scale125;
    if (value == "Scale150" || value == "1.5x") return MfsrScalePreset::Scale150;
    if (value == "Scale200" || value == "2.0x") return MfsrScalePreset::Scale200;
    return MfsrScalePreset::Auto;
}

inline const char* MfsrQualityPresetStableString(MfsrQualityPreset preset) {
    switch (preset) {
        case MfsrQualityPreset::Conservative: return "Conservative";
        case MfsrQualityPreset::Balanced: return "Balanced";
        case MfsrQualityPreset::Aggressive: return "Aggressive";
        case MfsrQualityPreset::Automatic:
        default:
            return "Automatic";
    }
}

inline MfsrQualityPreset MfsrQualityPresetFromStableString(const std::string& value) {
    if (value == "Conservative" || value == "Preview") return MfsrQualityPreset::Conservative;
    if (value == "Balanced" || value == "HighQuality" || value == "High Quality") return MfsrQualityPreset::Balanced;
    if (value == "Aggressive" || value == "Experimental") return MfsrQualityPreset::Aggressive;
    return MfsrQualityPreset::Automatic;
}

inline MfsrInputFamily MfsrInputFamilyForFrameClass(MfsrFrameClass frameClass) {
    switch (frameClass) {
        case MfsrFrameClass::RawMosaic:
        case MfsrFrameClass::RawLinear:
            return MfsrInputFamily::RawBurst;
        case MfsrFrameClass::RasterLinear:
            return MfsrInputFamily::RasterBurst;
        case MfsrFrameClass::Unknown:
        default:
            return MfsrInputFamily::Unknown;
    }
}

struct MfsrActiveArea {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct MfsrFrameSourceDescriptor {
    int graphNodeId = 0;
    std::string sourcePath;
    std::string assetId;
    std::uint64_t modifiedTimeUtc = 0;
    std::size_t sourceFingerprint = 0;
};

struct MfsrRawFrameSummary {
    bool present = false;
    std::string cameraMake;
    std::string cameraModel;
    int rawWidth = 0;
    int rawHeight = 0;
    int visibleWidth = 0;
    int visibleHeight = 0;
    int leftMargin = 0;
    int topMargin = 0;
    int orientation = 0;
    int bitDepth = 0;
    Raw::CfaPattern cfaPattern = Raw::CfaPattern::Unknown;
    Raw::RawPixelLayout pixelLayout = Raw::RawPixelLayout::Unknown;
    Raw::RawSampleFormat sampleFormat = Raw::RawSampleFormat::Unknown;
    bool mosaiced = false;
    float blackLevel = 0.0f;
    float whiteLevel = 0.0f;
    bool hasExposureMetadata = false;
    double exposureSeconds = 0.0;
    float iso = 0.0f;
    float aperture = 0.0f;
};

struct MfsrRasterFrameSummary {
    bool present = false;
    int width = 0;
    int height = 0;
    int channels = 0;
    int bitDepth = 0;
    bool linearLight = false;
    bool colorSpaceKnown = false;
    std::string colorSpaceName;
    std::size_t pixelsFingerprint = 0;
};

struct MfsrFramePacketSummary {
    MfsrFrameSourceDescriptor source;
    MfsrFrameClass frameClass = MfsrFrameClass::Unknown;
    bool isReference = false;
    int width = 0;
    int height = 0;
    MfsrActiveArea activeArea;
    int orientation = 0;
    int bitDepth = 0;
    MfsrRawFrameSummary raw;
    MfsrRasterFrameSummary raster;
};

struct MfsrSettings {
    int schemaVersion = 1;
    int algorithmVersion = 1;
    MfsrScalePreset scalePreset = MfsrScalePreset::Auto;
    MfsrQualityPreset qualityPreset = MfsrQualityPreset::Balanced;
    bool preferRawMosaicPath = true;
    int maxInputFrames = 0;
};

struct MfsrValidationMessage {
    MfsrValidationSeverity severity = MfsrValidationSeverity::Info;
    MfsrValidationCode code = MfsrValidationCode::Ok;
    int frameIndex = -1;
    std::string message;
};

struct MfsrValidationResult {
    bool valid = false;
    MfsrInputFamily inputFamily = MfsrInputFamily::Unknown;
    int referenceFrameIndex = -1;
    int frameCount = 0;
    std::vector<MfsrValidationMessage> messages;

    bool HasErrors() const {
        for (const MfsrValidationMessage& message : messages) {
            if (message.severity == MfsrValidationSeverity::Error) {
                return true;
            }
        }
        return false;
    }

    bool HasMessage(MfsrValidationCode code, MfsrValidationSeverity severity) const {
        for (const MfsrValidationMessage& message : messages) {
            if (message.code == code && message.severity == severity) {
                return true;
            }
        }
        return false;
    }

    bool HasError(MfsrValidationCode code) const {
        return HasMessage(code, MfsrValidationSeverity::Error);
    }

    bool HasWarning(MfsrValidationCode code) const {
        return HasMessage(code, MfsrValidationSeverity::Warning);
    }
};

struct MfsrDiagnosticsSummary {
    MfsrInputFamily inputFamily = MfsrInputFamily::Unknown;
    int frameCount = 0;
    int acceptedFrameCount = 0;
    int rejectedFrameCount = 0;
    int warningCount = 0;
    int errorCount = 0;
    int referenceFrameIndex = -1;
    bool cacheHit = false;
    std::string status;
};

struct MfsrCacheKey {
    int schemaVersion = 1;
    int algorithmVersion = 1;
    MfsrInputFamily inputFamily = MfsrInputFamily::Unknown;
    std::size_t inputSetFingerprint = 0;
    std::size_t settingsFingerprint = 0;
};

inline void AddMfsrValidationMessage(
    MfsrValidationResult& result,
    MfsrValidationSeverity severity,
    MfsrValidationCode code,
    int frameIndex,
    const std::string& message) {
    result.messages.push_back({ severity, code, frameIndex, message });
}

inline bool MfsrActiveAreaIsSpecified(const MfsrActiveArea& area) {
    return area.x != 0 || area.y != 0 || area.width != 0 || area.height != 0;
}

inline bool MfsrActiveAreaFitsFrame(const MfsrActiveArea& area, int width, int height) {
    if (!MfsrActiveAreaIsSpecified(area)) {
        return true;
    }
    return area.x >= 0 &&
        area.y >= 0 &&
        area.width > 0 &&
        area.height > 0 &&
        area.x + area.width <= width &&
        area.y + area.height <= height;
}

inline MfsrValidationResult ValidateMfsrFrameSet(
    const std::vector<MfsrFramePacketSummary>& frames,
    const MfsrSettings& settings = {}) {
    (void)settings;

    MfsrValidationResult result;
    result.frameCount = static_cast<int>(frames.size());

    if (frames.empty()) {
        AddMfsrValidationMessage(
            result,
            MfsrValidationSeverity::Error,
            MfsrValidationCode::EmptyInputSet,
            -1,
            "MFSR requires at least one input frame.");
        result.valid = false;
        return result;
    }

    int referenceCount = 0;
    MfsrInputFamily firstSupportedFamily = MfsrInputFamily::Unknown;
    bool reportedMixedFamily = false;

    for (std::size_t index = 0; index < frames.size(); ++index) {
        const MfsrFramePacketSummary& frame = frames[index];
        const int frameIndex = static_cast<int>(index);
        const MfsrInputFamily frameFamily = MfsrInputFamilyForFrameClass(frame.frameClass);

        if (frame.isReference) {
            ++referenceCount;
            if (result.referenceFrameIndex < 0) {
                result.referenceFrameIndex = frameIndex;
            }
        }

        if (frameFamily == MfsrInputFamily::Unknown) {
            AddMfsrValidationMessage(
                result,
                MfsrValidationSeverity::Error,
                MfsrValidationCode::UnsupportedInputKind,
                frameIndex,
                "MFSR input frame kind is unknown or unsupported.");
        } else if (firstSupportedFamily == MfsrInputFamily::Unknown) {
            firstSupportedFamily = frameFamily;
            result.inputFamily = frameFamily;
        } else if (frameFamily != firstSupportedFamily) {
            result.inputFamily = MfsrInputFamily::MixedUnsupported;
            if (!reportedMixedFamily) {
                AddMfsrValidationMessage(
                    result,
                    MfsrValidationSeverity::Error,
                    MfsrValidationCode::MixedInputFamilies,
                    frameIndex,
                    "MFSR inputs cannot mix RAW burst and raster burst frames.");
                reportedMixedFamily = true;
            }
        }

        if (frame.width <= 0 || frame.height <= 0 ||
            !MfsrActiveAreaFitsFrame(frame.activeArea, frame.width, frame.height)) {
            AddMfsrValidationMessage(
                result,
                MfsrValidationSeverity::Error,
                MfsrValidationCode::InvalidDimensions,
                frameIndex,
                "MFSR input frame dimensions or active area are invalid.");
        }
    }

    if (referenceCount == 0) {
        AddMfsrValidationMessage(
            result,
            MfsrValidationSeverity::Error,
            MfsrValidationCode::MissingReferenceInput,
            -1,
            "MFSR requires exactly one reference input.");
    } else if (referenceCount > 1) {
        AddMfsrValidationMessage(
            result,
            MfsrValidationSeverity::Error,
            MfsrValidationCode::MultipleReferenceInputs,
            result.referenceFrameIndex,
            "MFSR requires exactly one reference input.");
    }

    if (result.referenceFrameIndex >= 0 &&
        result.referenceFrameIndex < static_cast<int>(frames.size())) {
        const MfsrFramePacketSummary& reference = frames[static_cast<std::size_t>(result.referenceFrameIndex)];
        for (std::size_t index = 0; index < frames.size(); ++index) {
            if (static_cast<int>(index) == result.referenceFrameIndex) {
                continue;
            }
            const MfsrFramePacketSummary& frame = frames[index];
            const int frameIndex = static_cast<int>(index);
            const MfsrInputFamily frameFamily = MfsrInputFamilyForFrameClass(frame.frameClass);
            if (frameFamily == MfsrInputFamily::Unknown ||
                frameFamily != MfsrInputFamilyForFrameClass(reference.frameClass)) {
                continue;
            }

            if (frame.width > 0 && frame.height > 0 &&
                reference.width > 0 && reference.height > 0 &&
                (frame.width != reference.width || frame.height != reference.height)) {
                AddMfsrValidationMessage(
                    result,
                    MfsrValidationSeverity::Warning,
                    MfsrValidationCode::IncompatibleDimensions,
                    frameIndex,
                    "MFSR frame dimensions differ from the reference; later phases must normalize or reject this burst.");
            }

            if (frameFamily == MfsrInputFamily::RawBurst) {
                const bool cfaKnown =
                    reference.raw.cfaPattern != Raw::CfaPattern::Unknown &&
                    frame.raw.cfaPattern != Raw::CfaPattern::Unknown;
                const bool rawLayoutKnown =
                    reference.raw.pixelLayout != Raw::RawPixelLayout::Unknown &&
                    frame.raw.pixelLayout != Raw::RawPixelLayout::Unknown;
                if ((cfaKnown && reference.raw.cfaPattern != frame.raw.cfaPattern) ||
                    (rawLayoutKnown && reference.raw.pixelLayout != frame.raw.pixelLayout)) {
                    AddMfsrValidationMessage(
                        result,
                        MfsrValidationSeverity::Warning,
                        MfsrValidationCode::IncompatibleRawMetadata,
                        frameIndex,
                        "MFSR RAW metadata differs from the reference; later phases must normalize or reject this burst.");
                }
            }
        }
    }

    result.valid = !result.HasErrors();
    return result;
}

inline std::size_t BuildMfsrFrameSourceFingerprint(const MfsrFrameSourceDescriptor& source) {
    std::size_t fingerprint = 0;
    StackHash::HashCombine(fingerprint, StackHash::HashValue(source.graphNodeId));
    StackHash::HashCombine(fingerprint, StackHash::HashValue(source.sourcePath));
    StackHash::HashCombine(fingerprint, StackHash::HashValue(source.assetId));
    StackHash::HashCombine(fingerprint, StackHash::HashValue(source.modifiedTimeUtc));
    StackHash::HashCombine(fingerprint, source.sourceFingerprint);
    return fingerprint;
}

inline std::size_t BuildMfsrSettingsFingerprint(const MfsrSettings& settings) {
    std::size_t fingerprint = 0;
    StackHash::HashCombine(fingerprint, StackHash::HashValue(settings.schemaVersion));
    StackHash::HashCombine(fingerprint, StackHash::HashValue(settings.algorithmVersion));
    StackHash::HashCombine(fingerprint, StackHash::HashValue(static_cast<int>(settings.scalePreset)));
    StackHash::HashCombine(fingerprint, StackHash::HashValue(static_cast<int>(settings.qualityPreset)));
    StackHash::HashCombine(fingerprint, StackHash::HashValue(settings.preferRawMosaicPath));
    StackHash::HashCombine(fingerprint, StackHash::HashValue(settings.maxInputFrames));
    return fingerprint;
}

inline std::size_t BuildMfsrCacheKeyFingerprint(const MfsrCacheKey& key) {
    std::size_t fingerprint = 0;
    StackHash::HashCombine(fingerprint, StackHash::HashValue(key.schemaVersion));
    StackHash::HashCombine(fingerprint, StackHash::HashValue(key.algorithmVersion));
    StackHash::HashCombine(fingerprint, StackHash::HashValue(static_cast<int>(key.inputFamily)));
    StackHash::HashCombine(fingerprint, key.inputSetFingerprint);
    StackHash::HashCombine(fingerprint, key.settingsFingerprint);
    return fingerprint;
}

} // namespace Stack::Mfsr
