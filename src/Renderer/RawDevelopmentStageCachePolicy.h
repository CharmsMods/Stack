#pragma once
#include "Raw/RawZoneArea.h"

#include "Raw/RawDevelopmentRecipe.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>

namespace Stack::Renderer::RawDevelopmentCache {

enum class Stage {
    RawBase,
    NeutralPlacement,
    RawPlacement,
    PostLocalRange,
    PostFinishTone,
    PostColorWarp,
    PostViewTransform,
    PostOutputCrop
};

inline void MixJsonHash(std::size_t& fingerprint, std::size_t value) {
    fingerprint ^= value +
        static_cast<std::size_t>(0x9e3779b97f4a7c15ull) +
        (fingerprint << 6u) +
        (fingerprint >> 2u);
}

inline void HashJsonValue(
    std::size_t& fingerprint,
    const nlohmann::json& value) {
    MixJsonHash(fingerprint, static_cast<std::size_t>(value.type()));
    if (value.is_null() || value.is_discarded()) {
        return;
    }
    if (value.is_boolean()) {
        MixJsonHash(fingerprint, std::hash<bool>{}(value.get<bool>()));
        return;
    }
    if (value.is_number_unsigned()) {
        MixJsonHash(
            fingerprint,
            std::hash<std::uint64_t>{}(value.get<std::uint64_t>()));
        return;
    }
    if (value.is_number_integer()) {
        MixJsonHash(
            fingerprint,
            std::hash<std::int64_t>{}(value.get<std::int64_t>()));
        return;
    }
    if (value.is_number_float()) {
        MixJsonHash(fingerprint, std::hash<double>{}(value.get<double>()));
        return;
    }
    if (value.is_string()) {
        const auto& text =
            value.get_ref<const nlohmann::json::string_t&>();
        MixJsonHash(fingerprint, text.size());
        MixJsonHash(fingerprint, std::hash<std::string>{}(text));
        return;
    }
    if (value.is_array()) {
        MixJsonHash(fingerprint, value.size());
        for (const auto& item : value) {
            HashJsonValue(fingerprint, item);
        }
        return;
    }
    if (value.is_object()) {
        MixJsonHash(fingerprint, value.size());
        for (auto item = value.cbegin(); item != value.cend(); ++item) {
            MixJsonHash(fingerprint, item.key().size());
            MixJsonHash(
                fingerprint,
                std::hash<std::string>{}(item.key()));
            HashJsonValue(fingerprint, item.value());
        }
        return;
    }

    // Recipe fingerprints do not currently contain binary JSON. Keep the
    // fallback deterministic if that contract grows later.
    MixJsonHash(fingerprint, std::hash<std::string>{}(value.dump()));
}

inline std::size_t NonzeroHash(const nlohmann::json& value) {
    std::size_t fingerprint =
        static_cast<std::size_t>(1469598103934665603ull);
    HashJsonValue(fingerprint, value);
    return fingerprint == 0 ? 1 : fingerprint;
}

inline std::string BuildSourceDataIdentity(
    const Stack::RawRecipe::RawSourceReference& source) {
    std::string identity = "raw-source-v2:";
    const auto appendText = [&](const std::string& value) {
        identity += std::to_string(value.size());
        identity.push_back(':');
        identity += value;
        identity.push_back('|');
    };
    appendText(source.sourcePath);
    appendText(source.fingerprint);
    identity += std::to_string(source.fileSizeBytes);
    identity.push_back('|');
    identity += std::to_string(source.modifiedTimeTicks);
    return identity;
}

template <typename T>
inline void HashTypedValue(std::size_t& fingerprint, const T& value) {
    if constexpr (std::is_enum_v<T>) {
        using Underlying = std::underlying_type_t<T>;
        MixJsonHash(
            fingerprint,
            std::hash<Underlying>{}(static_cast<Underlying>(value)));
    } else {
        MixJsonHash(fingerprint, std::hash<T>{}(value));
    }
}

inline void HashTypedJson(
    std::size_t& fingerprint,
    const nlohmann::json& value) {
    HashJsonValue(fingerprint, value);
}

inline void HashWhiteBalance(
    std::size_t& fingerprint,
    const Stack::RawRecipe::RawWhiteBalanceRecipe& value) {
    HashTypedValue(fingerprint, value.mode);
    HashTypedValue(fingerprint, value.hasMultipliers);
    for (float multiplier : value.multipliers) {
        HashTypedValue(fingerprint, multiplier);
    }
}

inline void HashRgbDenoise(
    std::size_t& fingerprint,
    const Stack::RawRecipe::RawRgbDenoiseRecipe& value) {
    const bool active = Stack::RawRecipe::IsRgbDenoiseActive(value) ||
        value.diagnosticMode !=
            Stack::RawRecipe::RawDenoiseDiagnosticMode::None;
    HashTypedValue(fingerprint, active);
    if (!active) return;
    HashTypedValue(fingerprint, value.method);
    HashTypedValue(fingerprint, value.mapping);
    HashTypedValue(fingerprint, value.packageId);
    HashTypedValue(fingerprint, value.packageVersion);
    HashTypedValue(fingerprint, value.modelSha256);
    HashTypedValue(fingerprint, value.adapterVersion);
    HashTypedValue(fingerprint, value.colorNoise);
    HashTypedValue(fingerprint, value.luminanceNoise);
    HashTypedValue(fingerprint, value.detailProtection);
    HashTypedValue(fingerprint, value.edgeSensitivity);
    HashTypedValue(fingerprint, value.maximumStructureSize);
    const auto hashMap = [&](const Stack::RawRecipe::RawDenoiseControlMap& map) {
        HashTypedValue(fingerprint, map.baseMultiplier);
        HashTypedValue(fingerprint, map.minimumEv);
        HashTypedValue(fingerprint, map.maximumEv);
        HashTypedValue(fingerprint, map.points.size());
        for (const auto& point : map.points) {
            HashTypedValue(fingerprint, point.id);
            HashTypedValue(fingerprint, point.linkGroup);
            HashTypedValue(fingerprint, point.enabled);
            HashTypedValue(fingerprint, point.frequency);
            HashTypedValue(fingerprint, point.sceneEv);
            HashTypedValue(fingerprint, point.frequencyRadius);
            HashTypedValue(fingerprint, point.luminanceRadiusEv);
            HashTypedValue(fingerprint, point.multiplierDelta);
        }
    };
    hashMap(value.lumaMap);
    hashMap(value.chromaMap);
    HashTypedValue(fingerprint, value.diagnosticMode);
    HashTypedValue(fingerprint, value.diagnosticLayer);
    HashTypedValue(fingerprint, value.diagnosticPointId);
}

inline void HashBezierHandle(
    std::size_t& fingerprint,
    const Stack::RawRecipe::RawBezierHandleState& value) {
    HashTypedValue(fingerprint, value.strength);
    HashTypedValue(fingerprint, value.manual);
    HashTypedValue(fingerprint, value.offsetX);
    HashTypedValue(fingerprint, value.offsetY);
}

inline void HashLocalRange(
    std::size_t& fingerprint,
    const Stack::RawRecipe::RawLocalRangeRecipe& value) {
    HashTypedValue(fingerprint, value.enabled);
    if (!Stack::RawRecipe::IsLocalRangeEnabled(value)) return;
    HashTypedValue(fingerprint, value.strength);
    for (const auto& area : value.areas) HashTypedValue(fingerprint, Stack::RawRecipe::ZoneAreaGainFingerprint(area));
    HashTypedValue(fingerprint, value.middleGrey);
    HashTypedValue(fingerprint, value.minEv);
    HashTypedValue(fingerprint, value.maxEv);
    HashTypedValue(fingerprint, value.points.size());
    for (const auto& point : value.points) {
        HashTypedValue(fingerprint, point.ev);
        HashTypedValue(fingerprint, point.deltaEv);
        HashBezierHandle(fingerprint, point.incoming);
        HashBezierHandle(fingerprint, point.outgoing);
    }
    HashTypedValue(fingerprint, value.smoothness);
    HashTypedValue(fingerprint, value.edgeProtection);
    HashTypedValue(fingerprint, value.detailProtection);
    HashTypedValue(fingerprint, value.highlightProtection);
    HashTypedValue(fingerprint, value.regionMaskEnabled);
    HashTypedValue(fingerprint, value.regionMaskMode);
    HashTypedValue(fingerprint, value.regionMaskInvert);
    HashTypedValue(fingerprint, value.regionMaskCenterX);
    HashTypedValue(fingerprint, value.regionMaskCenterY);
    HashTypedValue(fingerprint, value.regionMaskAngleDegrees);
    HashTypedValue(fingerprint, value.regionMaskSize);
    HashTypedValue(fingerprint, value.regionMaskFeather);
    HashTypedValue(fingerprint, value.regionMaskLowEv);
    HashTypedValue(fingerprint, value.regionMaskHighEv);
    HashTypedValue(fingerprint, value.colorMaskEnabled);
    HashTypedValue(fingerprint, value.colorMaskTargetR);
    HashTypedValue(fingerprint, value.colorMaskTargetG);
    HashTypedValue(fingerprint, value.colorMaskTargetB);
    HashTypedValue(fingerprint, value.colorMaskHueWidth);
    HashTypedValue(fingerprint, value.colorMaskFeather);
    HashTypedValue(fingerprint, value.colorMaskMinChroma);
    HashTypedValue(fingerprint, value.targetZoneCombineMode);
    HashTypedValue(fingerprint, value.targetZones.size());
    for (const auto& zone : value.targetZones) {
        HashTypedValue(fingerprint, zone.id);
        HashTypedValue(fingerprint, zone.enabled);
        HashTypedValue(fingerprint, zone.centerEv);
        HashTypedValue(fingerprint, zone.coreHalfWidthEv);
        HashTypedValue(fingerprint, zone.featherEv);
        HashTypedValue(fingerprint, zone.deltaEv);
        HashTypedValue(fingerprint, zone.scope);
        HashTypedValue(fingerprint, zone.colorEnabled);
        HashTypedValue(fingerprint, zone.targetUPrime);
        HashTypedValue(fingerprint, zone.targetVPrime);
        HashTypedValue(fingerprint, zone.targetChroma);
        HashTypedValue(fingerprint, zone.colorRadius);
        HashTypedValue(fingerprint, zone.colorFeather);
        HashTypedValue(fingerprint, zone.seeds.size());
        for (const auto& seed : zone.seeds) {
            HashTypedValue(fingerprint, seed.sourceU);
            HashTypedValue(fingerprint, seed.sourceV);
        }
    }
}

inline void HashColorWarp(
    std::size_t& fingerprint,
    const Stack::RawRecipe::RawColorWarpRecipe& value) {
    HashTypedValue(fingerprint, value.version);
    HashTypedValue(fingerprint, value.enabled);
    if (!value.enabled) return;
    HashTypedValue(fingerprint, value.strength);
    HashTypedValue(fingerprint, value.pins.size());
    for (const auto& pin : value.pins) {
        HashTypedValue(fingerprint, pin.id);
        HashTypedValue(fingerprint, pin.enabled);
        HashTypedValue(fingerprint, pin.protectColor);
        HashTypedValue(fingerprint, pin.sourceA);
        HashTypedValue(fingerprint, pin.sourceB);
        HashTypedValue(fingerprint, pin.targetA);
        HashTypedValue(fingerprint, pin.targetB);
        HashTypedValue(fingerprint, pin.radius);
        HashTypedValue(fingerprint, pin.softness);
        HashTypedValue(fingerprint, pin.qualifierDirectionality);
        HashTypedValue(fingerprint, pin.qualifierOrientationRadians);
        HashTypedValue(fingerprint, pin.qualifierAperture);
        HashTypedValue(fingerprint, pin.strength);
        HashTypedValue(fingerprint, pin.regionId);
        HashTypedValue(fingerprint, pin.evCurve.samples.size());
        for (const float sample : pin.evCurve.samples) {
            HashTypedValue(fingerprint, sample);
        }
        HashTypedValue(fingerprint, pin.lightnessDeltaEv);
    }
    HashTypedValue(fingerprint, value.regions.size());
    for (const auto& region : value.regions) {
        HashTypedValue(fingerprint, region.id);
        HashTypedValue(fingerprint, region.spatialMode);
        HashTypedValue(fingerprint, region.reachPixels);
        HashTypedValue(fingerprint, region.spatialSupport);
        HashTypedValue(fingerprint, region.edgeStop);
        HashTypedValue(fingerprint, region.featherPixels);
        HashTypedValue(fingerprint, region.featherDirection);
        HashTypedValue(fingerprint, region.circles.size());
        for (const auto& circle : region.circles) {
            HashTypedValue(fingerprint, circle.id);
            HashTypedValue(fingerprint, circle.centerU);
            HashTypedValue(fingerprint, circle.centerV);
            HashTypedValue(fingerprint, circle.radiusU);
            HashTypedValue(fingerprint, circle.radiusV);
            HashTypedValue(fingerprint, circle.interpretation);
            HashTypedValue(fingerprint, circle.polarity);
        }
    }
    // Link groups are UI-side organization. Final rendering depends only on
    // the resolved pin sources and targets, so group membership is not part of
    // the Post-Color-Warp fingerprint.
}

struct StageFingerprints {
    std::size_t rawBase = 0;
    std::size_t neutralPlacement = 0;
    // Calibrated pre-exposure guide identity, without another image cache.
    std::size_t calibratedNeutral = 0;
    std::size_t rawPlacement = 0;
    std::size_t postLocalRange = 0;
    std::size_t postFinishTone = 0;
    std::size_t postColorWarp = 0;
    std::size_t postViewTransform = 0;
    std::size_t postOutputCrop = 0;

    std::size_t For(Stage stage) const {
        switch (stage) {
            case Stage::RawBase: return rawBase;
            case Stage::NeutralPlacement: return neutralPlacement;
            case Stage::RawPlacement: return rawPlacement;
            case Stage::PostLocalRange: return postLocalRange;
            case Stage::PostFinishTone: return postFinishTone;
            case Stage::PostColorWarp: return postColorWarp;
            case Stage::PostViewTransform: return postViewTransform;
            case Stage::PostOutputCrop: return postOutputCrop;
        }
        return 1;
    }
};

inline StageFingerprints BuildStageFingerprints(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    int previewMaxDimension) {
    constexpr int kTypedFingerprintContractVersion = 8;
    std::size_t fingerprint =
        static_cast<std::size_t>(1469598103934665603ull);
    HashTypedValue(fingerprint, kTypedFingerprintContractVersion);
    HashTypedValue(fingerprint, std::max(0, previewMaxDimension));
    HashTypedValue(fingerprint, recipe.rawRecipeVersion);
    HashTypedValue(fingerprint, recipe.source.sourcePath);
    HashTypedValue(fingerprint, recipe.source.fingerprint);
    HashTypedValue(fingerprint, recipe.source.fileSizeBytes);
    HashTypedValue(fingerprint, recipe.source.modifiedTimeTicks);
    HashTypedValue(fingerprint, recipe.technical.processingVersion);
    HashTypedValue(fingerprint, recipe.technical.demosaicMethod);
    HashTypedValue(fingerprint, recipe.technical.workingSpace);
    HashTypedValue(fingerprint, recipe.technical.applyBaselineExposure);
    const auto& mosaic = recipe.technical.mosaicDenoise;
    HashTypedValue(fingerprint, mosaic.enabled);
    HashTypedValue(fingerprint, mosaic.mode);
    HashTypedValue(fingerprint, mosaic.hotPixelSuppression);
    HashTypedValue(fingerprint, mosaic.hotPixelThreshold);
    HashTypedValue(fingerprint, mosaic.lumaStrength);
    HashTypedValue(fingerprint, mosaic.chromaStrength);
    HashTypedValue(fingerprint, mosaic.radius);
    HashTypedValue(fingerprint, mosaic.edgeProtection);
    HashTypedValue(fingerprint, mosaic.iterations);
    HashWhiteBalance(fingerprint, recipe.whiteBalance);
    HashTypedValue(fingerprint, recipe.cropRotation.rotationDegrees);
    HashTypedValue(fingerprint, recipe.cropRotation.flipHorizontally);
    HashTypedValue(fingerprint, recipe.cropRotation.flipVertically);

    StageFingerprints stages;
    stages.rawBase = fingerprint == 0 ? 1 : fingerprint;

    HashRgbDenoise(fingerprint, recipe.rgbDenoise);
    stages.neutralPlacement = fingerprint == 0 ? 1 : fingerprint;

    if (Stack::RawRecipe::IsColorCalibrationActive(recipe.colorCalibration)) {
        const auto calibration = Stack::RawRecipe::SanitizeColorCalibration(recipe.colorCalibration);
        HashTypedValue(fingerprint, 0x43414C49);
        HashTypedValue(fingerprint, calibration.version);
        for (const auto& primary : calibration.primaries) {
            HashTypedValue(fingerprint, primary.hue);
            HashTypedValue(fingerprint, primary.saturation);
        }
    }
    stages.calibratedNeutral = fingerprint == 0 ? 1 : fingerprint;

    if (!recipe.localRange.areas.empty()) HashTypedValue(fingerprint, 0x41524541);
    HashTypedValue(fingerprint, recipe.preToneExposureEv);
    stages.rawPlacement = fingerprint == 0 ? 1 : fingerprint;

    HashLocalRange(fingerprint, recipe.localRange);
    for (const auto& adjustment : recipe.evGradients) {
        const auto& mask = adjustment.mask;
        HashTypedValue(fingerprint, mask.id);
        HashTypedValue(fingerprint, mask.geometryVersion);
        HashTypedValue(fingerprint, mask.shape);
        HashTypedValue(fingerprint, mask.enabled);
        HashTypedValue(fingerprint, mask.inverted);
        HashTypedValue(fingerprint, mask.centerU);
        HashTypedValue(fingerprint, mask.centerV);
        HashTypedValue(fingerprint, mask.angleRadians);
        HashTypedValue(fingerprint, mask.lowBoundary);
        HashTypedValue(fingerprint, mask.highBoundary);
        HashTypedValue(fingerprint, mask.radiusX);
        HashTypedValue(fingerprint, mask.radiusY);
        HashTypedValue(fingerprint, mask.innerScale);
        HashLocalRange(fingerprint, adjustment.curve);
    }
    stages.postLocalRange = fingerprint == 0 ? 1 : fingerprint;

    HashTypedJson(fingerprint, recipe.finishTone.layerJson);
    for (const auto& adjustment : recipe.toneGradients) {
        const auto& mask = adjustment.mask;
        HashTypedValue(fingerprint, mask.id);
        HashTypedValue(fingerprint, mask.geometryVersion);
        HashTypedValue(fingerprint, mask.shape);
        HashTypedValue(fingerprint, mask.enabled);
        HashTypedValue(fingerprint, mask.inverted);
        HashTypedValue(fingerprint, mask.centerU);
        HashTypedValue(fingerprint, mask.centerV);
        HashTypedValue(fingerprint, mask.angleRadians);
        HashTypedValue(fingerprint, mask.lowBoundary);
        HashTypedValue(fingerprint, mask.highBoundary);
        HashTypedValue(fingerprint, mask.radiusX);
        HashTypedValue(fingerprint, mask.radiusY);
        HashTypedValue(fingerprint, mask.innerScale);
        HashTypedJson(fingerprint, adjustment.curveJson);
    }
    stages.postFinishTone = fingerprint == 0 ? 1 : fingerprint;

    HashColorWarp(fingerprint, recipe.colorWarp);
    // Color and detail share a cache boundary but execute in that order.
    HashTypedJson(fingerprint, Stack::RawRecipe::SerializeDetailContrast(recipe.detailContrast));
    stages.postColorWarp = fingerprint == 0 ? 1 : fingerprint;

    HashTypedJson(fingerprint, recipe.viewTransform.layerJson);
    HashTypedValue(fingerprint, recipe.technical.encodeSrgbOutput);
    stages.postViewTransform = fingerprint == 0 ? 1 : fingerprint;

    HashTypedValue(fingerprint, recipe.cropRotation.cropEnabled);
    HashTypedValue(fingerprint, recipe.cropRotation.cropX);
    HashTypedValue(fingerprint, recipe.cropRotation.cropY);
    HashTypedValue(fingerprint, recipe.cropRotation.cropWidth);
    HashTypedValue(fingerprint, recipe.cropRotation.cropHeight);
    stages.postOutputCrop = fingerprint == 0 ? 1 : fingerprint;
    return stages;
}

inline std::size_t BuildStageFingerprint(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    int previewMaxDimension,
    Stage stage) {
    return BuildStageFingerprints(recipe, previewMaxDimension).For(stage);
}

template <typename T>
inline void HashSelectionValue(std::size_t& fingerprint, const T& value) {
    fingerprint ^= std::hash<T>{}(value) +
        static_cast<std::size_t>(0x9e3779b97f4a7c15ull) +
        (fingerprint << 6u) +
        (fingerprint >> 2u);
}

inline std::size_t BuildLocalRangeSelectionFingerprint(
    const Stack::RawRecipe::RawLocalRangeRecipe& localRange,
    Raw::RawWorkingSpace workingSpace,
    std::size_t inputStageFingerprint,
    int maximumSelectionDimension) {
    std::size_t fingerprint =
        static_cast<std::size_t>(1469598103934665603ull);
    HashSelectionValue(fingerprint, inputStageFingerprint);
    HashSelectionValue(fingerprint, static_cast<int>(workingSpace));
    HashSelectionValue(
        fingerprint,
        std::clamp(maximumSelectionDimension, 64, 1536));
    HashSelectionValue(fingerprint, localRange.middleGrey);
    for (const Stack::RawRecipe::RawLocalRangeTargetZone& zone :
         localRange.targetZones) {
        HashSelectionValue(fingerprint, zone.id);
        HashSelectionValue(fingerprint, zone.enabled);
        HashSelectionValue(fingerprint, zone.centerEv);
        HashSelectionValue(fingerprint, zone.coreHalfWidthEv);
        HashSelectionValue(fingerprint, zone.featherEv);
        HashSelectionValue(fingerprint, static_cast<int>(zone.scope));
        HashSelectionValue(fingerprint, zone.colorEnabled);
        HashSelectionValue(fingerprint, zone.targetUPrime);
        HashSelectionValue(fingerprint, zone.targetVPrime);
        HashSelectionValue(fingerprint, zone.targetChroma);
        HashSelectionValue(fingerprint, zone.colorRadius);
        HashSelectionValue(fingerprint, zone.colorFeather);
        for (const Stack::RawRecipe::RawLocalRangeTargetSeed& seed :
             zone.seeds) {
            HashSelectionValue(fingerprint, seed.sourceU);
            HashSelectionValue(fingerprint, seed.sourceV);
        }
    }
    return fingerprint == 0 ? 1 : fingerprint;
}

} // namespace Stack::Renderer::RawDevelopmentCache
