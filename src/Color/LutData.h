#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace ColorLut {

enum class LutImportFormat {
    Unknown = 0,
    Cube,
    Format3dl,
    Spi1d,
    Spi3d
};

enum class LutUseMode {
    PostViewTransform = 0,
    PreViewTransform
};

enum class LutTransferFunction {
    None = 0,
    SrgbEncode,
    Gamma22Encode,
    SrgbDecode,
    Gamma22Decode
};

struct Lut1DStage {
    int size = 0;
    std::vector<float> values;
    std::array<float, 3> domainMin { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> domainMax { 1.0f, 1.0f, 1.0f };
};

struct Lut3DStage {
    int size = 0;
    std::vector<float> values;
    std::array<float, 3> domainMin { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> domainMax { 1.0f, 1.0f, 1.0f };
};

struct LutPayload {
    std::string sourcePath;
    std::string label;
    std::string importedTitle;
    std::string importError;
    LutImportFormat importFormat = LutImportFormat::Unknown;
    LutUseMode useMode = LutUseMode::PostViewTransform;
    LutTransferFunction inputTransform = LutTransferFunction::None;
    LutTransferFunction outputTransform = LutTransferFunction::None;
    Lut1DStage lut1D;
    Lut1DStage shaper1D;
    Lut3DStage lut3D;
};

inline bool IsFiniteFloat(float value) {
    return std::isfinite(value) != 0;
}

inline bool HasLut1D(const LutPayload& payload) {
    const std::size_t expected = payload.lut1D.size > 0
        ? static_cast<std::size_t>(payload.lut1D.size) * 3u
        : 0u;
    return payload.lut1D.size > 0 &&
        payload.lut1D.values.size() == expected;
}

inline bool HasLut3D(const LutPayload& payload) {
    const std::size_t edge = payload.lut3D.size > 0
        ? static_cast<std::size_t>(payload.lut3D.size)
        : 0u;
    const std::size_t expected = edge > 0
        ? edge * edge * edge * 3u
        : 0u;
    return payload.lut3D.size > 0 &&
        payload.lut3D.values.size() == expected;
}

inline bool HasShaper1D(const LutPayload& payload) {
    const std::size_t expected = payload.shaper1D.size > 0
        ? static_cast<std::size_t>(payload.shaper1D.size) * 3u
        : 0u;
    return payload.shaper1D.size > 0 &&
        payload.shaper1D.values.size() == expected;
}

inline bool HasAnyLutData(const LutPayload& payload) {
    return HasLut1D(payload) || HasShaper1D(payload) || HasLut3D(payload);
}

inline const char* LutTypeSummary(const LutPayload& payload) {
    if (HasShaper1D(payload) && HasLut3D(payload)) {
        return "1D + 3D";
    }
    if (HasLut3D(payload)) {
        return "3D";
    }
    if (HasLut1D(payload)) {
        return "1D";
    }
    return "None";
}

inline const char* LutImportFormatLabel(LutImportFormat format) {
    switch (format) {
        case LutImportFormat::Cube: return ".cube";
        case LutImportFormat::Format3dl: return ".3dl";
        case LutImportFormat::Spi1d: return ".spi1d";
        case LutImportFormat::Spi3d: return ".spi3d";
        case LutImportFormat::Unknown:
        default:
            return "Unknown";
    }
}

inline const char* LutUseModeLabel(LutUseMode mode) {
    switch (mode) {
        case LutUseMode::PreViewTransform: return "Pre View Transform";
        case LutUseMode::PostViewTransform:
        default:
            return "Post View Transform";
    }
}

inline const char* LutTransferFunctionLabel(LutTransferFunction transform) {
    switch (transform) {
        case LutTransferFunction::SrgbEncode: return "sRGB Encode";
        case LutTransferFunction::Gamma22Encode: return "Gamma 2.2 Encode";
        case LutTransferFunction::SrgbDecode: return "sRGB Decode";
        case LutTransferFunction::Gamma22Decode: return "Gamma 2.2 Decode";
        case LutTransferFunction::None:
        default:
            return "None";
    }
}

inline LutTransferFunction DefaultInputTransformForMode(LutUseMode mode) {
    return mode == LutUseMode::PreViewTransform
        ? LutTransferFunction::SrgbEncode
        : LutTransferFunction::None;
}

inline LutTransferFunction DefaultOutputTransformForMode(LutUseMode mode) {
    return mode == LutUseMode::PreViewTransform
        ? LutTransferFunction::SrgbDecode
        : LutTransferFunction::None;
}

inline bool HasModeDefaultTransforms(const LutPayload& payload, LutUseMode mode) {
    return payload.inputTransform == DefaultInputTransformForMode(mode) &&
        payload.outputTransform == DefaultOutputTransformForMode(mode);
}

inline void ApplyModeDefaultsIfUnmodified(LutPayload& payload, LutUseMode newMode) {
    if (HasModeDefaultTransforms(payload, payload.useMode)) {
        payload.inputTransform = DefaultInputTransformForMode(newMode);
        payload.outputTransform = DefaultOutputTransformForMode(newMode);
    }
    payload.useMode = newMode;
}

inline void ClearCanonicalLutData(LutPayload& payload) {
    payload.lut1D = {};
    payload.shaper1D = {};
    payload.lut3D = {};
}

} // namespace ColorLut
