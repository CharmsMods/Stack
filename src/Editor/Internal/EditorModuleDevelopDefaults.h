#pragma once

#include "Editor/Layers/ToneLayers.h"
#include "Raw/RawImageData.h"

namespace Stack::Editor::DevelopDefaults {

inline nlohmann::json BuildDefaultIntegratedToneLayerJson() {
    ToneCurveLayer toneCurve;
    return toneCurve.Serialize();
}

inline Raw::RawDevelopSettings BuildRawDevelopSettingsFromMetadata(const Raw::RawMetadata& metadata) {
    Raw::RawDevelopSettings settings;
    if (metadata.hasDngBaselineExposure) {
        settings.exposureStops = metadata.dngBaselineExposure;
    }
    settings.blackLevelOverride = metadata.blackLevel;
    settings.whiteLevelOverride = metadata.whiteLevel;
    const bool dngHasColorTags = metadata.hasDngForwardMatrix1 || metadata.hasDngForwardMatrix2 ||
        metadata.hasDngColorMatrix1 || metadata.hasDngColorMatrix2;
    const bool dngHasCalibrationTags = metadata.hasDngCameraCalibration1 || metadata.hasDngCameraCalibration2;
    const bool dngHasDualForwardMatrices = metadata.hasDngForwardMatrix1 && metadata.hasDngForwardMatrix2;
    // Stack's own DNG dual-illuminant blend is still approximate; prefer LibRaw
    // when it has a matrix and the DNG does not provide calibration metadata.
    const bool preferLibRawForUnderSpecifiedDng =
        metadata.isDng &&
        metadata.hasCameraMatrix &&
        dngHasDualForwardMatrices &&
        !dngHasCalibrationTags;
    if (!metadata.isDng || preferLibRawForUnderSpecifiedDng) {
        settings.cameraTransformSource = Raw::RawCameraTransformSource::LibRawRgbCam;
    } else if (dngHasColorTags) {
        settings.cameraTransformSource = Raw::RawCameraTransformSource::DngAuto;
    } else {
        settings.cameraTransformSource = Raw::RawCameraTransformSource::LibRawRgbCam;
    }
    return settings;
}

} // namespace Stack::Editor::DevelopDefaults
