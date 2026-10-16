#include "Raw/RawMetadataSerialization.h"
#include <string>
#include <utility>

namespace Raw::Serialization {
namespace {
std::string CfaPatternToString(Raw::CfaPattern pattern) {
    return Raw::CfaPatternName(pattern);
}

Raw::CfaPattern CfaPatternFromString(const std::string& value) {
    if (value == "RGGB") return Raw::CfaPattern::RGGB;
    if (value == "BGGR") return Raw::CfaPattern::BGGR;
    if (value == "GBRG") return Raw::CfaPattern::GBRG;
    if (value == "GRBG") return Raw::CfaPattern::GRBG;
    return Raw::CfaPattern::Unknown;
}

std::string RawPixelLayoutToString(Raw::RawPixelLayout layout) {
    switch (layout) {
        case Raw::RawPixelLayout::MosaicBayer: return "MosaicBayer";
        case Raw::RawPixelLayout::LinearRgb: return "LinearRgb";
        case Raw::RawPixelLayout::Unknown:
        default:
            return "Unknown";
    }
}

Raw::RawPixelLayout RawPixelLayoutFromString(const std::string& value) {
    if (value == "MosaicBayer") return Raw::RawPixelLayout::MosaicBayer;
    if (value == "LinearRgb") return Raw::RawPixelLayout::LinearRgb;
    return Raw::RawPixelLayout::Unknown;
}

std::string RawSampleFormatToString(Raw::RawSampleFormat format) {
    switch (format) {
        case Raw::RawSampleFormat::UInt16: return "UInt16";
        case Raw::RawSampleFormat::Float32: return "Float32";
        case Raw::RawSampleFormat::Unknown:
        default:
            return "Unknown";
    }
}

Raw::RawSampleFormat RawSampleFormatFromString(const std::string& value) {
    if (value == "UInt16") return Raw::RawSampleFormat::UInt16;
    if (value == "Float32") return Raw::RawSampleFormat::Float32;
    return Raw::RawSampleFormat::Unknown;
}
}

nlohmann::json SerializeMetadata(const Raw::RawMetadata& metadata) {
    nlohmann::json gainMaps = nlohmann::json::array();
    for (const Raw::DngGainMapOpcode& map : metadata.dngGainMaps) {
        gainMaps.push_back({
            { "top", map.top }, { "left", map.left },
            { "bottom", map.bottom }, { "right", map.right },
            { "plane", map.plane }, { "planes", map.planes },
            { "rowPitch", map.rowPitch }, { "colPitch", map.colPitch },
            { "mapPointsV", map.mapPointsV },
            { "mapPointsH", map.mapPointsH },
            { "mapPlanes", map.mapPlanes },
            { "mapSpacingV", map.mapSpacingV },
            { "mapSpacingH", map.mapSpacingH },
            { "mapOriginV", map.mapOriginV },
            { "mapOriginH", map.mapOriginH },
            { "gains", map.gains }
        });
    }
    return {
        { "sourcePath", metadata.sourcePath },
        { "cameraMake", metadata.cameraMake },
        { "cameraModel", metadata.cameraModel },
        { "dngUniqueCameraModel", metadata.dngUniqueCameraModel },
        { "rawWidth", metadata.rawWidth },
        { "rawHeight", metadata.rawHeight },
        { "visibleWidth", metadata.visibleWidth },
        { "visibleHeight", metadata.visibleHeight },
        { "leftMargin", metadata.leftMargin },
        { "topMargin", metadata.topMargin },
        { "orientation", metadata.orientation },
        { "bitDepth", metadata.bitDepth },
        { "focalLength35mm", metadata.focalLength35mm },
        { "cfaPattern", CfaPatternToString(metadata.cfaPattern) },
        { "pixelLayout", RawPixelLayoutToString(metadata.pixelLayout) },
        { "isDng", metadata.isDng },
        { "dngTypeStatus", metadata.dngTypeStatus },
        { "linearChannels", metadata.linearChannels },
        { "linearSampleFormat", RawSampleFormatToString(metadata.linearSampleFormat) },
        { "mosaiced", metadata.mosaiced },
        { "blackLevel", metadata.blackLevel },
        { "perChannelBlack", metadata.perChannelBlack },
        { "whiteLevel", metadata.whiteLevel },
        { "blackLevelSource", metadata.blackLevelSource },
        { "whiteLevelSource", metadata.whiteLevelSource },
        { "whiteBalanceSource", metadata.whiteBalanceSource },
        { "cameraMatrixSource", metadata.cameraMatrixSource },
        { "rawMinimum", metadata.rawMinimum },
        { "rawMaximum", metadata.rawMaximum },
        { "defaultWhiteClipPercent", metadata.defaultWhiteClipPercent },
        { "cameraWhiteBalance", metadata.cameraWhiteBalance },
        { "daylightWhiteBalance", metadata.daylightWhiteBalance },
        { "exposureTimeSeconds", metadata.exposureTimeSeconds },
        { "isoSpeed", metadata.isoSpeed },
        { "apertureFNumber", metadata.apertureFNumber },
        { "captureTimestamp", metadata.captureTimestamp },
        { "hasExposureTime", metadata.hasExposureTime },
        { "hasIsoSpeed", metadata.hasIsoSpeed },
        { "hasApertureFNumber", metadata.hasApertureFNumber },
        { "hasCaptureTimestamp", metadata.hasCaptureTimestamp },
        { "cameraToSrgb", metadata.cameraToSrgb },
        { "hasCameraMatrix", metadata.hasCameraMatrix },
        { "dngAsShotNeutral", metadata.dngAsShotNeutral },
        { "hasDngAsShotNeutral", metadata.hasDngAsShotNeutral },
        { "dngColorMatrix1", metadata.dngColorMatrix1 },
        { "dngColorMatrix2", metadata.dngColorMatrix2 },
        { "dngForwardMatrix1", metadata.dngForwardMatrix1 },
        { "dngForwardMatrix2", metadata.dngForwardMatrix2 },
        { "hasDngColorMatrix1", metadata.hasDngColorMatrix1 },
        { "hasDngColorMatrix2", metadata.hasDngColorMatrix2 },
        { "hasDngForwardMatrix1", metadata.hasDngForwardMatrix1 },
        { "hasDngForwardMatrix2", metadata.hasDngForwardMatrix2 },
        { "dngIlluminant1", metadata.dngIlluminant1 },
        { "dngIlluminant2", metadata.dngIlluminant2 },
        { "dngCompression", metadata.dngCompression },
        { "dngPhotometricInterpretation", metadata.dngPhotometricInterpretation },
        { "dngCfaLayout", metadata.dngCfaLayout },
        { "dngCfaRepeatPatternDim", metadata.dngCfaRepeatPatternDim },
        { "dngCfaPattern", metadata.dngCfaPattern },
        { "dngCfaPlaneColor", metadata.dngCfaPlaneColor },
        { "dngBlackLevelRepeatDim", metadata.dngBlackLevelRepeatDim },
        { "dngBlackLevelPattern", metadata.dngBlackLevelPattern },
        { "dngAnalogBalance", metadata.dngAnalogBalance },
        { "hasDngAnalogBalance", metadata.hasDngAnalogBalance },
        { "dngCameraCalibration1", metadata.dngCameraCalibration1 },
        { "dngCameraCalibration2", metadata.dngCameraCalibration2 },
        { "hasDngCameraCalibration1", metadata.hasDngCameraCalibration1 },
        { "hasDngCameraCalibration2", metadata.hasDngCameraCalibration2 },
        { "dngBaselineExposure", metadata.dngBaselineExposure },
        { "hasDngBaselineExposure", metadata.hasDngBaselineExposure },
        { "dngGainMapCount", metadata.dngGainMapCount },
        { "dngGainMaps", std::move(gainMaps) },
        { "dngUnsupportedOpcodeCount", metadata.dngUnsupportedOpcodeCount },
        { "uploadFormat", metadata.uploadFormat },
        { "warnings", metadata.warnings },
        { "error", metadata.error }
    };
}

Raw::RawMetadata DeserializeMetadata(const nlohmann::json& value) {
    Raw::RawMetadata metadata;
    if (!value.is_object()) return metadata;
    metadata.sourcePath = value.value("sourcePath", metadata.sourcePath);
    metadata.cameraMake = value.value("cameraMake", metadata.cameraMake);
    metadata.cameraModel = value.value("cameraModel", metadata.cameraModel);
    metadata.focalLength35mm = value.value("focalLength35mm", 0.0f);
    metadata.dngUniqueCameraModel = value.value("dngUniqueCameraModel", metadata.dngUniqueCameraModel);
    metadata.rawWidth = value.value("rawWidth", metadata.rawWidth);
    metadata.rawHeight = value.value("rawHeight", metadata.rawHeight);
    metadata.visibleWidth = value.value("visibleWidth", metadata.visibleWidth);
    metadata.visibleHeight = value.value("visibleHeight", metadata.visibleHeight);
    metadata.leftMargin = value.value("leftMargin", metadata.leftMargin);
    metadata.topMargin = value.value("topMargin", metadata.topMargin);
    metadata.orientation = value.value("orientation", metadata.orientation);
    metadata.bitDepth = value.value("bitDepth", metadata.bitDepth);
    metadata.cfaPattern = CfaPatternFromString(value.value("cfaPattern", std::string("Unknown")));
    metadata.pixelLayout = RawPixelLayoutFromString(value.value("pixelLayout", std::string("Unknown")));
    metadata.isDng = value.value("isDng", metadata.isDng);
    metadata.dngTypeStatus = value.value("dngTypeStatus", metadata.dngTypeStatus);
    metadata.linearChannels = value.value("linearChannels", metadata.linearChannels);
    metadata.linearSampleFormat = RawSampleFormatFromString(value.value("linearSampleFormat", std::string("Unknown")));
    metadata.mosaiced = value.value("mosaiced", metadata.mosaiced);
    if (metadata.pixelLayout == Raw::RawPixelLayout::Unknown && metadata.mosaiced) {
        metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    }
    metadata.blackLevel = value.value("blackLevel", metadata.blackLevel);
    metadata.whiteLevel = value.value("whiteLevel", metadata.whiteLevel);
    metadata.blackLevelSource = value.value("blackLevelSource", metadata.blackLevelSource);
    metadata.whiteLevelSource = value.value("whiteLevelSource", metadata.whiteLevelSource);
    metadata.whiteBalanceSource = value.value("whiteBalanceSource", metadata.whiteBalanceSource);
    metadata.cameraMatrixSource = value.value("cameraMatrixSource", metadata.cameraMatrixSource);
    metadata.rawMinimum = value.value("rawMinimum", metadata.rawMinimum);
    metadata.rawMaximum = value.value("rawMaximum", metadata.rawMaximum);
    metadata.defaultWhiteClipPercent = value.value("defaultWhiteClipPercent", metadata.defaultWhiteClipPercent);
    metadata.exposureTimeSeconds = value.value("exposureTimeSeconds", metadata.exposureTimeSeconds);
    metadata.isoSpeed = value.value("isoSpeed", metadata.isoSpeed);
    metadata.apertureFNumber = value.value("apertureFNumber", metadata.apertureFNumber);
    metadata.captureTimestamp = value.value("captureTimestamp", metadata.captureTimestamp);
    metadata.hasExposureTime = value.value("hasExposureTime", metadata.hasExposureTime);
    metadata.hasIsoSpeed = value.value("hasIsoSpeed", metadata.hasIsoSpeed);
    metadata.hasApertureFNumber = value.value("hasApertureFNumber", metadata.hasApertureFNumber);
    metadata.hasCaptureTimestamp = value.value("hasCaptureTimestamp", metadata.hasCaptureTimestamp);
    metadata.hasCameraMatrix = value.value("hasCameraMatrix", metadata.hasCameraMatrix);
    metadata.hasDngAsShotNeutral = value.value("hasDngAsShotNeutral", metadata.hasDngAsShotNeutral);
    metadata.hasDngColorMatrix1 = value.value("hasDngColorMatrix1", metadata.hasDngColorMatrix1);
    metadata.hasDngColorMatrix2 = value.value("hasDngColorMatrix2", metadata.hasDngColorMatrix2);
    metadata.hasDngForwardMatrix1 = value.value("hasDngForwardMatrix1", metadata.hasDngForwardMatrix1);
    metadata.hasDngForwardMatrix2 = value.value("hasDngForwardMatrix2", metadata.hasDngForwardMatrix2);
    metadata.dngIlluminant1 = value.value("dngIlluminant1", metadata.dngIlluminant1);
    metadata.dngIlluminant2 = value.value("dngIlluminant2", metadata.dngIlluminant2);
    metadata.dngCompression = value.value("dngCompression", metadata.dngCompression);
    metadata.dngPhotometricInterpretation = value.value("dngPhotometricInterpretation", metadata.dngPhotometricInterpretation);
    metadata.dngCfaLayout = value.value("dngCfaLayout", metadata.dngCfaLayout);
    metadata.hasDngAnalogBalance = value.value("hasDngAnalogBalance", metadata.hasDngAnalogBalance);
    metadata.hasDngCameraCalibration1 = value.value("hasDngCameraCalibration1", metadata.hasDngCameraCalibration1);
    metadata.hasDngCameraCalibration2 = value.value("hasDngCameraCalibration2", metadata.hasDngCameraCalibration2);
    metadata.dngBaselineExposure = value.value("dngBaselineExposure", metadata.dngBaselineExposure);
    metadata.hasDngBaselineExposure = value.value("hasDngBaselineExposure", metadata.hasDngBaselineExposure);
    metadata.dngGainMapCount = value.value("dngGainMapCount", metadata.dngGainMapCount);
    metadata.dngUnsupportedOpcodeCount = value.value("dngUnsupportedOpcodeCount", metadata.dngUnsupportedOpcodeCount);
    metadata.uploadFormat = value.value("uploadFormat", metadata.uploadFormat);
    metadata.error = value.value("error", metadata.error);
    const auto copyArray = [](const nlohmann::json& arrayJson, auto& target) {
        if (!arrayJson.is_array()) return;
        for (std::size_t i = 0; i < target.size() && i < arrayJson.size(); ++i) {
            target[i] = arrayJson[i].get<float>();
        }
    };
    copyArray(value.value("perChannelBlack", nlohmann::json::array()), metadata.perChannelBlack);
    copyArray(value.value("cameraWhiteBalance", nlohmann::json::array()), metadata.cameraWhiteBalance);
    copyArray(value.value("daylightWhiteBalance", nlohmann::json::array()), metadata.daylightWhiteBalance);
    copyArray(value.value("cameraToSrgb", value.value("cameraToXyz", nlohmann::json::array())), metadata.cameraToSrgb);
    copyArray(value.value("dngAsShotNeutral", nlohmann::json::array()), metadata.dngAsShotNeutral);
    copyArray(value.value("dngColorMatrix1", nlohmann::json::array()), metadata.dngColorMatrix1);
    copyArray(value.value("dngColorMatrix2", nlohmann::json::array()), metadata.dngColorMatrix2);
    copyArray(value.value("dngForwardMatrix1", nlohmann::json::array()), metadata.dngForwardMatrix1);
    copyArray(value.value("dngForwardMatrix2", nlohmann::json::array()), metadata.dngForwardMatrix2);
    copyArray(value.value("dngBlackLevelPattern", nlohmann::json::array()), metadata.dngBlackLevelPattern);
    copyArray(value.value("dngAnalogBalance", nlohmann::json::array()), metadata.dngAnalogBalance);
    copyArray(value.value("dngCameraCalibration1", nlohmann::json::array()), metadata.dngCameraCalibration1);
    copyArray(value.value("dngCameraCalibration2", nlohmann::json::array()), metadata.dngCameraCalibration2);
    const auto copyIntArray = [](const nlohmann::json& arrayJson, auto& target) {
        if (!arrayJson.is_array()) return;
        for (std::size_t i = 0; i < target.size() && i < arrayJson.size(); ++i) {
            target[i] = arrayJson[i].get<int>();
        }
    };
    copyIntArray(value.value("dngCfaRepeatPatternDim", nlohmann::json::array()), metadata.dngCfaRepeatPatternDim);
    copyIntArray(value.value("dngCfaPattern", nlohmann::json::array()), metadata.dngCfaPattern);
    copyIntArray(value.value("dngCfaPlaneColor", nlohmann::json::array()), metadata.dngCfaPlaneColor);
    copyIntArray(value.value("dngBlackLevelRepeatDim", nlohmann::json::array()), metadata.dngBlackLevelRepeatDim);
    const nlohmann::json warnings = value.value("warnings", nlohmann::json::array());
    if (warnings.is_array()) {
        for (const nlohmann::json& warning : warnings) {
            if (warning.is_string()) metadata.warnings.push_back(warning.get<std::string>());
        }
    }
    const nlohmann::json gainMaps = value.value("dngGainMaps", nlohmann::json::array());
    if (gainMaps.is_array()) {
        for (const nlohmann::json& item : gainMaps) {
            if (!item.is_object()) continue;
            Raw::DngGainMapOpcode map;
            map.top = item.value("top", 0);
            map.left = item.value("left", 0);
            map.bottom = item.value("bottom", 0);
            map.right = item.value("right", 0);
            map.plane = item.value("plane", 0);
            map.planes = item.value("planes", 1);
            map.rowPitch = item.value("rowPitch", 1);
            map.colPitch = item.value("colPitch", 1);
            map.mapPointsV = item.value("mapPointsV", 0);
            map.mapPointsH = item.value("mapPointsH", 0);
            map.mapPlanes = item.value("mapPlanes", 1);
            map.mapSpacingV = item.value("mapSpacingV", 0.0);
            map.mapSpacingH = item.value("mapSpacingH", 0.0);
            map.mapOriginV = item.value("mapOriginV", 0.0);
            map.mapOriginH = item.value("mapOriginH", 0.0);
            const nlohmann::json gains = item.value("gains", nlohmann::json::array());
            if (gains.is_array()) {
                for (const nlohmann::json& gain : gains) {
                    if (gain.is_number()) map.gains.push_back(gain.get<float>());
                }
            }
            metadata.dngGainMaps.push_back(std::move(map));
        }
        metadata.dngGainMapCount = static_cast<int>(metadata.dngGainMaps.size());
    }
    return metadata;
}

}
