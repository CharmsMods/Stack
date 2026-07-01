#include "EditorNodeGraphRawSerialization.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace EditorNodeGraph {

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

std::string WhiteBalanceModeToString(Raw::WhiteBalanceMode mode) {
    switch (mode) {
        case Raw::WhiteBalanceMode::AsShot: return "AsShot";
        case Raw::WhiteBalanceMode::Auto: return "Auto";
        case Raw::WhiteBalanceMode::Neutral: return "Neutral";
        case Raw::WhiteBalanceMode::Manual: return "Manual";
    }
    return "AsShot";
}

Raw::WhiteBalanceMode WhiteBalanceModeFromString(const std::string& value) {
    if (value == "Auto") return Raw::WhiteBalanceMode::Auto;
    if (value == "Neutral") return Raw::WhiteBalanceMode::Neutral;
    if (value == "Manual") return Raw::WhiteBalanceMode::Manual;
    return Raw::WhiteBalanceMode::AsShot;
}

std::string DemosaicMethodToString(Raw::DemosaicMethod method) {
    (void)method;
    return "Bilinear";
}

Raw::DemosaicMethod DemosaicMethodFromString(const std::string& value) {
    (void)value;
    return Raw::DemosaicMethod::Bilinear;
}

std::string HighlightModeToString(Raw::HighlightReconstructionMode mode) {
    switch (mode) {
        case Raw::HighlightReconstructionMode::Off: return "Off";
        case Raw::HighlightReconstructionMode::ClipNeutral: return "ClipNeutral";
        case Raw::HighlightReconstructionMode::Luminance: return "Luminance";
        case Raw::HighlightReconstructionMode::ColorReconstruction: return "ColorReconstruction";
    }
    return "Off";
}

Raw::HighlightReconstructionMode HighlightModeFromString(const std::string& value) {
    if (value == "ClipNeutral") return Raw::HighlightReconstructionMode::ClipNeutral;
    if (value == "Luminance") return Raw::HighlightReconstructionMode::Luminance;
    if (value == "ColorReconstruction") return Raw::HighlightReconstructionMode::ColorReconstruction;
    return Raw::HighlightReconstructionMode::Off;
}

std::string RawDebugViewToString(Raw::RawDebugView view) {
    switch (view) {
        case Raw::RawDebugView::FinalOutput: return "FinalOutput";
        case Raw::RawDebugView::NormalizedMosaic: return "NormalizedMosaic";
        case Raw::RawDebugView::CfaFalseColor: return "CfaFalseColor";
        case Raw::RawDebugView::DemosaicedCameraRgb: return "DemosaicedCameraRgb";
        case Raw::RawDebugView::WhiteBalancedCameraRgb: return "WhiteBalancedCameraRgb";
        case Raw::RawDebugView::CameraTransformedRgb: return "CameraTransformedRgb";
        case Raw::RawDebugView::ClippedRawChannels: return "ClippedRawChannels";
        case Raw::RawDebugView::PreDenoiseMosaic: return "PreDenoiseMosaic";
        case Raw::RawDebugView::PostDenoiseMosaic: return "PostDenoiseMosaic";
        case Raw::RawDebugView::HotPixelMask: return "HotPixelMask";
        case Raw::RawDebugView::DenoiseDifference: return "DenoiseDifference";
        case Raw::RawDebugView::FalseColorMask: return "FalseColorMask";
        case Raw::RawDebugView::DefringeMask: return "DefringeMask";
        case Raw::RawDebugView::HighlightEdgeMask: return "HighlightEdgeMask";
    }
    return "FinalOutput";
}

Raw::RawDebugView RawDebugViewFromString(const std::string& value) {
    if (value == "NormalizedMosaic") return Raw::RawDebugView::NormalizedMosaic;
    if (value == "CfaFalseColor") return Raw::RawDebugView::CfaFalseColor;
    if (value == "DemosaicedCameraRgb") return Raw::RawDebugView::DemosaicedCameraRgb;
    if (value == "WhiteBalancedCameraRgb") return Raw::RawDebugView::WhiteBalancedCameraRgb;
    if (value == "CameraTransformedRgb") return Raw::RawDebugView::CameraTransformedRgb;
    if (value == "ClippedRawChannels") return Raw::RawDebugView::ClippedRawChannels;
    if (value == "PreDenoiseMosaic") return Raw::RawDebugView::PreDenoiseMosaic;
    if (value == "PostDenoiseMosaic") return Raw::RawDebugView::PostDenoiseMosaic;
    if (value == "HotPixelMask") return Raw::RawDebugView::HotPixelMask;
    if (value == "DenoiseDifference") return Raw::RawDebugView::DenoiseDifference;
    if (value == "FalseColorMask" || value == "False Color Mask") return Raw::RawDebugView::FalseColorMask;
    if (value == "DefringeMask" || value == "Defringe Mask") return Raw::RawDebugView::DefringeMask;
    if (value == "HighlightEdgeMask" || value == "Highlight Edge Mask") return Raw::RawDebugView::HighlightEdgeMask;
    return Raw::RawDebugView::FinalOutput;
}

std::string RawDetailFusionModeToString(Raw::RawDetailFusionMode mode) {
    switch (mode) {
        case Raw::RawDetailFusionMode::ManualMask: return "ManualMask";
        case Raw::RawDetailFusionMode::AutoAnalyze: return "AutoAnalyze";
        case Raw::RawDetailFusionMode::Hybrid: return "Hybrid";
    }
    return "AutoAnalyze";
}

Raw::RawDetailFusionMode RawDetailFusionModeFromString(const std::string& value) {
    if (value == "ManualMask" || value == "Manual Mask") return Raw::RawDetailFusionMode::ManualMask;
    if (value == "Hybrid") return Raw::RawDetailFusionMode::Hybrid;
    return Raw::RawDetailFusionMode::AutoAnalyze;
}

std::string RawDetailFusionDebugViewToString(Raw::RawDetailFusionDebugView view) {
    switch (view) {
        case Raw::RawDetailFusionDebugView::FinalImage: return "FinalImage";
        case Raw::RawDetailFusionDebugView::ExposureMap: return "ExposureMap";
        case Raw::RawDetailFusionDebugView::Confidence: return "Confidence";
        case Raw::RawDetailFusionDebugView::HighlightSafety: return "HighlightSafety";
        case Raw::RawDetailFusionDebugView::ShadowProtection: return "ShadowProtection";
        case Raw::RawDetailFusionDebugView::SampleSelection: return "SampleSelection";
        case Raw::RawDetailFusionDebugView::SmoothGradient: return "SmoothGradient";
        case Raw::RawDetailFusionDebugView::TrueEdge: return "TrueEdge";
        case Raw::RawDetailFusionDebugView::TextureDetail: return "TextureDetail";
        case Raw::RawDetailFusionDebugView::DebandRisk: return "DebandRisk";
        case Raw::RawDetailFusionDebugView::AutoRange: return "AutoRange";
        case Raw::RawDetailFusionDebugView::NoiseFloorSnr: return "NoiseFloorSnr";
        case Raw::RawDetailFusionDebugView::HighlightHeadroom: return "HighlightHeadroom";
        case Raw::RawDetailFusionDebugView::ChannelSaturation: return "ChannelSaturation";
        case Raw::RawDetailFusionDebugView::RejectedDetail: return "RejectedDetail";
    }
    return "FinalImage";
}

Raw::RawDetailFusionDebugView RawDetailFusionDebugViewFromString(const std::string& value) {
    if (value == "ExposureMap" || value == "Effective EV Map") return Raw::RawDetailFusionDebugView::ExposureMap;
    if (value == "Confidence" || value == "Confidence Map") return Raw::RawDetailFusionDebugView::Confidence;
    if (value == "HighlightSafety" || value == "Highlight Safety") return Raw::RawDetailFusionDebugView::HighlightSafety;
    if (value == "ShadowProtection" || value == "Shadow / Noise Protection") return Raw::RawDetailFusionDebugView::ShadowProtection;
    if (value == "SampleSelection" || value == "Sample Selection") return Raw::RawDetailFusionDebugView::SampleSelection;
    if (value == "SmoothGradient" || value == "Smooth Gradient Protection") return Raw::RawDetailFusionDebugView::SmoothGradient;
    if (value == "TrueEdge" || value == "True Edge Map") return Raw::RawDetailFusionDebugView::TrueEdge;
    if (value == "TextureDetail" || value == "Texture Detail Map") return Raw::RawDetailFusionDebugView::TextureDetail;
    if (value == "DebandRisk" || value == "Deband Risk") return Raw::RawDetailFusionDebugView::DebandRisk;
    if (value == "AutoRange" || value == "Auto Range Map") return Raw::RawDetailFusionDebugView::AutoRange;
    if (value == "NoiseFloorSnr" || value == "Noise Floor / SNR") return Raw::RawDetailFusionDebugView::NoiseFloorSnr;
    if (value == "HighlightHeadroom" || value == "Highlight Headroom") return Raw::RawDetailFusionDebugView::HighlightHeadroom;
    if (value == "ChannelSaturation" || value == "Channel Saturation") return Raw::RawDetailFusionDebugView::ChannelSaturation;
    if (value == "RejectedDetail" || value == "Rejected Detail") return Raw::RawDetailFusionDebugView::RejectedDetail;
    return Raw::RawDetailFusionDebugView::FinalImage;
}

std::string HdrMergeDebugViewToString(Raw::HdrMergeDebugView view) {
    switch (view) {
        case Raw::HdrMergeDebugView::FinalImage: return "FinalImage";
        case Raw::HdrMergeDebugView::Contribution: return "Contribution";
        case Raw::HdrMergeDebugView::Clipping: return "Clipping";
        case Raw::HdrMergeDebugView::NoiseLimited: return "NoiseLimited";
        case Raw::HdrMergeDebugView::AlignmentConfidence: return "AlignmentConfidence";
        case Raw::HdrMergeDebugView::MotionMask: return "MotionMask";
        case Raw::HdrMergeDebugView::RejectedSamples: return "RejectedSamples";
    }
    return "FinalImage";
}

Raw::HdrMergeDebugView HdrMergeDebugViewFromString(const std::string& value) {
    if (value == "Contribution") return Raw::HdrMergeDebugView::Contribution;
    if (value == "Clipping") return Raw::HdrMergeDebugView::Clipping;
    if (value == "NoiseLimited" || value == "Noise / Black Limited") return Raw::HdrMergeDebugView::NoiseLimited;
    if (value == "AlignmentConfidence" || value == "Alignment Confidence") return Raw::HdrMergeDebugView::AlignmentConfidence;
    if (value == "MotionMask" || value == "Motion Mask") return Raw::HdrMergeDebugView::MotionMask;
    if (value == "RejectedSamples" || value == "Rejected Samples") return Raw::HdrMergeDebugView::RejectedSamples;
    return Raw::HdrMergeDebugView::FinalImage;
}

std::string HdrMergeAlignmentModeToString(Raw::HdrMergeAlignmentMode mode) {
    switch (mode) {
        case Raw::HdrMergeAlignmentMode::Off: return "Off";
        case Raw::HdrMergeAlignmentMode::Translation: return "Translation";
        case Raw::HdrMergeAlignmentMode::WideTranslation: return "WideTranslation";
    }
    return "Off";
}

Raw::HdrMergeAlignmentMode HdrMergeAlignmentModeFromString(const std::string& value) {
    if (value == "Translation") return Raw::HdrMergeAlignmentMode::Translation;
    if (value == "WideTranslation" || value == "Wide Translation" || value == "Handheld") {
        return Raw::HdrMergeAlignmentMode::WideTranslation;
    }
    return Raw::HdrMergeAlignmentMode::Off;
}

std::string HdrMergeExposureModeToString(Raw::HdrMergeExposureMode mode) {
    switch (mode) {
        case Raw::HdrMergeExposureMode::Manual: return "Manual";
        case Raw::HdrMergeExposureMode::Metadata:
        default:
            return "Metadata";
    }
}

Raw::HdrMergeExposureMode HdrMergeExposureModeFromString(const std::string& value) {
    if (value == "Manual") return Raw::HdrMergeExposureMode::Manual;
    return Raw::HdrMergeExposureMode::Metadata;
}

std::string HdrMergeReferenceModeToString(Raw::HdrMergeReferenceMode mode) {
    switch (mode) {
        case Raw::HdrMergeReferenceMode::Auto: return "Auto";
        case Raw::HdrMergeReferenceMode::Frame1: return "Frame1";
        case Raw::HdrMergeReferenceMode::Frame2: return "Frame2";
        case Raw::HdrMergeReferenceMode::Frame3: return "Frame3";
    }
    return "Auto";
}

Raw::HdrMergeReferenceMode HdrMergeReferenceModeFromString(const std::string& value) {
    if (value == "Frame1" || value == "Frame 1") return Raw::HdrMergeReferenceMode::Frame1;
    if (value == "Frame2" || value == "Frame 2") return Raw::HdrMergeReferenceMode::Frame2;
    if (value == "Frame3" || value == "Frame 3") return Raw::HdrMergeReferenceMode::Frame3;
    return Raw::HdrMergeReferenceMode::Auto;
}

std::string HdrMergeDeghostModeToString(Raw::HdrMergeDeghostMode mode) {
    switch (mode) {
        case Raw::HdrMergeDeghostMode::Off: return "Off";
        case Raw::HdrMergeDeghostMode::Low: return "Low";
        case Raw::HdrMergeDeghostMode::Medium: return "Medium";
        case Raw::HdrMergeDeghostMode::High: return "High";
    }
    return "Medium";
}

Raw::HdrMergeDeghostMode HdrMergeDeghostModeFromString(const std::string& value) {
    if (value == "Off") return Raw::HdrMergeDeghostMode::Off;
    if (value == "Low") return Raw::HdrMergeDeghostMode::Low;
    if (value == "Medium") return Raw::HdrMergeDeghostMode::Medium;
    if (value == "High") return Raw::HdrMergeDeghostMode::High;
    return Raw::HdrMergeDeghostMode::Medium;
}

std::string HdrMergeMotionPriorityToString(Raw::HdrMergeMotionPriority mode) {
    switch (mode) {
        case Raw::HdrMergeMotionPriority::PreserveReference: return "PreserveReference";
        case Raw::HdrMergeMotionPriority::AverageCleanAreas: return "AverageCleanAreas";
    }
    return "PreserveReference";
}

Raw::HdrMergeMotionPriority HdrMergeMotionPriorityFromString(const std::string& value) {
    if (value == "AverageCleanAreas" || value == "Average Clean Areas") {
        return Raw::HdrMergeMotionPriority::AverageCleanAreas;
    }
    return Raw::HdrMergeMotionPriority::PreserveReference;
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
    if (value == "MosaicBayer" || value == "Mosaic RAW") return Raw::RawPixelLayout::MosaicBayer;
    if (value == "LinearRgb" || value == "Linear RGB") return Raw::RawPixelLayout::LinearRgb;
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

std::string RawCameraTransformSourceToString(Raw::RawCameraTransformSource source) {
    switch (source) {
        case Raw::RawCameraTransformSource::LibRawRgbCam: return "LibRawRgbCam";
        case Raw::RawCameraTransformSource::DngAuto: return "DngAuto";
        case Raw::RawCameraTransformSource::DngForwardMatrix1: return "DngForwardMatrix1";
        case Raw::RawCameraTransformSource::DngForwardMatrix2: return "DngForwardMatrix2";
        case Raw::RawCameraTransformSource::DngColorMatrixInverse: return "DngColorMatrixInverse";
    }
    return "DngAuto";
}

Raw::RawCameraTransformSource RawCameraTransformSourceFromString(const std::string& value) {
    if (value == "LibRawRgbCam" || value == "LibRaw rgb_cam") return Raw::RawCameraTransformSource::LibRawRgbCam;
    if (value == "DngAuto" || value == "DNG Auto") return Raw::RawCameraTransformSource::DngAuto;
    if (value == "DngForwardMatrix1" || value == "DNG ForwardMatrix 1") return Raw::RawCameraTransformSource::DngForwardMatrix1;
    if (value == "DngForwardMatrix2" || value == "DNG ForwardMatrix 2") return Raw::RawCameraTransformSource::DngForwardMatrix2;
    if (value == "DngColorMatrixInverse" || value == "DNG ColorMatrix inverse") return Raw::RawCameraTransformSource::DngColorMatrixInverse;
    return Raw::RawCameraTransformSource::DngAuto;
}

nlohmann::json SerializeRawMetadata(const Raw::RawMetadata& metadata) {
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
        { "dngUnsupportedOpcodeCount", metadata.dngUnsupportedOpcodeCount },
        { "uploadFormat", metadata.uploadFormat },
        { "warnings", metadata.warnings },
        { "error", metadata.error }
    };
}

Raw::RawMetadata DeserializeRawMetadata(const nlohmann::json& value) {
    Raw::RawMetadata metadata;
    if (!value.is_object()) return metadata;
    metadata.sourcePath = value.value("sourcePath", metadata.sourcePath);
    metadata.cameraMake = value.value("cameraMake", metadata.cameraMake);
    metadata.cameraModel = value.value("cameraModel", metadata.cameraModel);
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
    return metadata;
}

nlohmann::json SerializeRawSettings(const Raw::RawDevelopSettings& settings) {
    return {
        { "exposureStops", settings.exposureStops },
        { "whiteBalanceMode", WhiteBalanceModeToString(settings.whiteBalanceMode) },
        { "manualWhiteBalance", settings.manualWhiteBalance },
        { "overrideBlackLevel", settings.overrideBlackLevel },
        { "blackLevelOverride", settings.blackLevelOverride },
        { "overrideWhiteLevel", settings.overrideWhiteLevel },
        { "whiteLevelOverride", settings.whiteLevelOverride },
        { "highlightMode", HighlightModeToString(settings.highlightMode) },
        { "highlightStrength", settings.highlightStrength },
        { "highlightThreshold", settings.highlightThreshold },
        { "demosaicMethod", DemosaicMethodToString(settings.demosaicMethod) },
        { "cameraTransformEnabled", settings.cameraTransformEnabled },
        { "cameraTransformSource", RawCameraTransformSourceToString(settings.cameraTransformSource) },
        { "debugView", RawDebugViewToString(settings.debugView) },
        { "debugBypassCameraTransform", settings.debugBypassCameraTransform },
        { "debugTransposeCameraMatrix", settings.debugTransposeCameraMatrix },
        { "rotationDegrees", settings.rotationDegrees },
        { "rotateToFitFrame", settings.rotateToFitFrame },
        { "flipHorizontally", settings.flipHorizontally },
        { "flipVertically", settings.flipVertically },
        { "falseColorSuppression", settings.falseColorSuppression },
        { "defringeStrength", settings.defringeStrength },
        { "highlightEdgeCleanup", settings.highlightEdgeCleanup },
        { "chromaRadius", settings.chromaRadius },
        { "preserveRealColor", settings.preserveRealColor },
        { "lateralRedCyan", settings.lateralRedCyan },
        { "lateralBlueYellow", settings.lateralBlueYellow },
        { "mosaicDenoiseEnabled", settings.mosaicDenoise.enabled },
        { "mosaicDenoiseHotPixelSuppression", settings.mosaicDenoise.hotPixelSuppression },
        { "mosaicDenoiseHotPixelThreshold", settings.mosaicDenoise.hotPixelThreshold },
        { "mosaicDenoiseLumaStrength", settings.mosaicDenoise.lumaStrength },
        { "mosaicDenoiseChromaStrength", settings.mosaicDenoise.chromaStrength },
        { "mosaicDenoiseRadius", settings.mosaicDenoise.radius },
        { "mosaicDenoiseEdgeProtection", settings.mosaicDenoise.edgeProtection },
        { "mosaicDenoiseIterations", settings.mosaicDenoise.iterations }
    };
}

Raw::RawDevelopSettings DeserializeRawSettings(const nlohmann::json& value) {
    Raw::RawDevelopSettings settings;
    if (!value.is_object()) return settings;
    settings.exposureStops = value.value("exposureStops", settings.exposureStops);
    settings.whiteBalanceMode = WhiteBalanceModeFromString(value.value("whiteBalanceMode", std::string("AsShot")));
    settings.overrideBlackLevel = value.value("overrideBlackLevel", settings.overrideBlackLevel);
    settings.blackLevelOverride = value.value("blackLevelOverride", settings.blackLevelOverride);
    settings.overrideWhiteLevel = value.value("overrideWhiteLevel", settings.overrideWhiteLevel);
    settings.whiteLevelOverride = value.value("whiteLevelOverride", settings.whiteLevelOverride);
    settings.highlightMode = HighlightModeFromString(value.value("highlightMode", std::string("Off")));
    settings.highlightStrength = value.value("highlightStrength", settings.highlightStrength);
    settings.highlightThreshold = value.value("highlightThreshold", settings.highlightThreshold);
    settings.demosaicMethod = DemosaicMethodFromString(value.value("demosaicMethod", std::string("Bilinear")));
    settings.cameraTransformEnabled = value.value("cameraTransformEnabled", settings.cameraTransformEnabled);
    settings.cameraTransformSource = RawCameraTransformSourceFromString(value.value("cameraTransformSource", std::string("DngAuto")));
    settings.debugView = RawDebugViewFromString(value.value("debugView", std::string("FinalOutput")));
    if (value.value("debugShowNormalizedMosaic", false)) {
        settings.debugView = Raw::RawDebugView::NormalizedMosaic;
    }
    settings.debugBypassCameraTransform = value.value("debugBypassCameraTransform", settings.debugBypassCameraTransform);
    settings.debugTransposeCameraMatrix = value.value("debugTransposeCameraMatrix", settings.debugTransposeCameraMatrix);
    settings.rotationDegrees = value.value("rotationDegrees", settings.rotationDegrees);
    settings.rotateToFitFrame = value.value("rotateToFitFrame", settings.rotateToFitFrame);
    settings.flipHorizontally = value.value("flipHorizontally", settings.flipHorizontally);
    settings.flipVertically = value.value("flipVertically", settings.flipVertically);
    settings.falseColorSuppression = value.value("falseColorSuppression", settings.falseColorSuppression);
    settings.defringeStrength = value.value("defringeStrength", settings.defringeStrength);
    settings.highlightEdgeCleanup = value.value("highlightEdgeCleanup", settings.highlightEdgeCleanup);
    settings.chromaRadius = value.value("chromaRadius", settings.chromaRadius);
    settings.preserveRealColor = value.value("preserveRealColor", settings.preserveRealColor);
    settings.lateralRedCyan = value.value("lateralRedCyan", settings.lateralRedCyan);
    settings.lateralBlueYellow = value.value("lateralBlueYellow", settings.lateralBlueYellow);
    settings.mosaicDenoise.enabled = value.value("mosaicDenoiseEnabled", settings.mosaicDenoise.enabled);
    settings.mosaicDenoise.hotPixelSuppression = value.value("mosaicDenoiseHotPixelSuppression", settings.mosaicDenoise.hotPixelSuppression);
    settings.mosaicDenoise.hotPixelThreshold = value.value("mosaicDenoiseHotPixelThreshold", settings.mosaicDenoise.hotPixelThreshold);
    settings.mosaicDenoise.lumaStrength = value.value("mosaicDenoiseLumaStrength", settings.mosaicDenoise.lumaStrength);
    settings.mosaicDenoise.chromaStrength = value.value("mosaicDenoiseChromaStrength", settings.mosaicDenoise.chromaStrength);
    settings.mosaicDenoise.radius = value.value("mosaicDenoiseRadius", settings.mosaicDenoise.radius);
    settings.mosaicDenoise.edgeProtection = value.value("mosaicDenoiseEdgeProtection", settings.mosaicDenoise.edgeProtection);
    settings.mosaicDenoise.iterations = value.value("mosaicDenoiseIterations", settings.mosaicDenoise.iterations);
    settings.mosaicDenoise.hotPixelThreshold = std::clamp(settings.mosaicDenoise.hotPixelThreshold, 0.001f, 1.0f);
    settings.mosaicDenoise.lumaStrength = std::clamp(settings.mosaicDenoise.lumaStrength, 0.0f, 1.0f);
    settings.mosaicDenoise.chromaStrength = std::clamp(settings.mosaicDenoise.chromaStrength, 0.0f, 1.0f);
    settings.mosaicDenoise.radius = std::clamp(settings.mosaicDenoise.radius, 1, 4);
    settings.mosaicDenoise.edgeProtection = std::clamp(settings.mosaicDenoise.edgeProtection, 0.0f, 1.0f);
    settings.mosaicDenoise.iterations = std::clamp(settings.mosaicDenoise.iterations, 1, 2);
    settings.falseColorSuppression = std::clamp(settings.falseColorSuppression, 0.0f, 1.0f);
    settings.defringeStrength = std::clamp(settings.defringeStrength, 0.0f, 1.0f);
    settings.highlightEdgeCleanup = std::clamp(settings.highlightEdgeCleanup, 0.0f, 1.0f);
    settings.chromaRadius = std::clamp(settings.chromaRadius, 1, 3);
    settings.preserveRealColor = std::clamp(settings.preserveRealColor, 0.0f, 1.0f);
    settings.lateralRedCyan = std::clamp(settings.lateralRedCyan, -3.0f, 3.0f);
    settings.lateralBlueYellow = std::clamp(settings.lateralBlueYellow, -3.0f, 3.0f);
    const nlohmann::json wb = value.value("manualWhiteBalance", nlohmann::json::array());
    if (wb.is_array()) {
        for (std::size_t i = 0; i < settings.manualWhiteBalance.size() && i < wb.size(); ++i) {
            settings.manualWhiteBalance[i] = wb[i].get<float>();
        }
    }
    return settings;
}

nlohmann::json SerializeRawDetailFusionSettings(const Raw::RawDetailFusionSettings& settings) {
    return {
        { "mode", RawDetailFusionModeToString(settings.mode) },
        { "debugView", RawDetailFusionDebugViewToString(settings.debugView) },
        { "autoSafetyEnabled", settings.autoSafetyEnabled },
        { "overrideMinEv", settings.overrideMinEv },
        { "overrideMaxEv", settings.overrideMaxEv },
        { "overrideBaseEv", settings.overrideBaseEv },
        { "overrideNoiseProtection", settings.overrideNoiseProtection },
        { "overrideHighlightProtection", settings.overrideHighlightProtection },
        { "overrideShadowLiftLimit", settings.overrideShadowLiftLimit },
        { "overrideWellExposedTarget", settings.overrideWellExposedTarget },
        { "minEvBias", settings.minEvBias },
        { "maxEvBias", settings.maxEvBias },
        { "baseEvBias", settings.baseEvBias },
        { "noiseProtectionBias", settings.noiseProtectionBias },
        { "highlightProtectionBias", settings.highlightProtectionBias },
        { "shadowLiftLimitBias", settings.shadowLiftLimitBias },
        { "wellExposedTargetBias", settings.wellExposedTargetBias },
        { "minEv", settings.minEv },
        { "maxEv", settings.maxEv },
        { "baseEv", settings.baseEv },
        { "strength", settings.strength },
        { "sampleCount", settings.sampleCount },
        { "baseRadiusPercent", settings.baseRadiusPercent },
        { "highlightProtection", settings.highlightProtection },
        { "shadowLiftLimit", settings.shadowLiftLimit },
        { "noiseProtection", settings.noiseProtection },
        { "detailWeight", settings.detailWeight },
        { "wellExposedTarget", settings.wellExposedTarget },
        { "smoothGradientProtection", settings.smoothGradientProtection },
        { "textureSensitivity", settings.textureSensitivity },
        { "skyBias", settings.skyBias },
        { "invertMask", settings.invertMask },
        { "maskBlackPoint", settings.maskBlackPoint },
        { "maskWhitePoint", settings.maskWhitePoint },
        { "maskGamma", settings.maskGamma },
        { "smoothnessRadius", settings.smoothnessRadius },
        { "smoothAreaRadius", settings.smoothAreaRadius },
        { "edgeAwareness", settings.edgeAwareness },
        { "haloGuard", settings.haloGuard },
        { "maskDebandDither", settings.maskDebandDither },
        { "manualBlend", settings.manualBlend }
    };
}

Raw::RawDetailFusionSettings DeserializeRawDetailFusionSettings(const nlohmann::json& value) {
    Raw::RawDetailFusionSettings settings;
    if (!value.is_object()) return settings;
    settings.mode = RawDetailFusionModeFromString(value.value("mode", std::string("AutoAnalyze")));
    settings.debugView = RawDetailFusionDebugViewFromString(value.value("debugView", std::string("FinalImage")));
    settings.autoSafetyEnabled = value.value("autoSafetyEnabled", settings.autoSafetyEnabled);
    settings.overrideMinEv = value.value("overrideMinEv", settings.overrideMinEv);
    settings.overrideMaxEv = value.value("overrideMaxEv", settings.overrideMaxEv);
    settings.overrideBaseEv = value.value("overrideBaseEv", settings.overrideBaseEv);
    settings.overrideNoiseProtection = value.value("overrideNoiseProtection", settings.overrideNoiseProtection);
    settings.overrideHighlightProtection = value.value("overrideHighlightProtection", settings.overrideHighlightProtection);
    settings.overrideShadowLiftLimit = value.value("overrideShadowLiftLimit", settings.overrideShadowLiftLimit);
    settings.overrideWellExposedTarget = value.value("overrideWellExposedTarget", settings.overrideWellExposedTarget);
    settings.minEvBias = value.value("minEvBias", settings.minEvBias);
    settings.maxEvBias = value.value("maxEvBias", settings.maxEvBias);
    settings.baseEvBias = value.value("baseEvBias", settings.baseEvBias);
    settings.noiseProtectionBias = value.value("noiseProtectionBias", settings.noiseProtectionBias);
    settings.highlightProtectionBias = value.value("highlightProtectionBias", settings.highlightProtectionBias);
    settings.shadowLiftLimitBias = value.value("shadowLiftLimitBias", settings.shadowLiftLimitBias);
    settings.wellExposedTargetBias = value.value("wellExposedTargetBias", settings.wellExposedTargetBias);
    settings.minEv = value.value("minEv", settings.minEv);
    settings.maxEv = value.value("maxEv", settings.maxEv);
    settings.baseEv = value.value("baseEv", settings.baseEv);
    settings.strength = value.value("strength", settings.strength);
    settings.sampleCount = value.value("sampleCount", settings.sampleCount);
    settings.baseRadiusPercent = value.value("baseRadiusPercent", settings.baseRadiusPercent);
    settings.highlightProtection = value.value("highlightProtection", settings.highlightProtection);
    settings.shadowLiftLimit = value.value("shadowLiftLimit", settings.shadowLiftLimit);
    settings.noiseProtection = value.value("noiseProtection", settings.noiseProtection);
    settings.detailWeight = value.value("detailWeight", settings.detailWeight);
    settings.wellExposedTarget = value.value("wellExposedTarget", settings.wellExposedTarget);
    settings.smoothGradientProtection = value.value("smoothGradientProtection", settings.smoothGradientProtection);
    settings.textureSensitivity = value.value("textureSensitivity", settings.textureSensitivity);
    settings.skyBias = value.value("skyBias", settings.skyBias);
    settings.invertMask = value.value("invertMask", settings.invertMask);
    settings.maskBlackPoint = value.value("maskBlackPoint", settings.maskBlackPoint);
    settings.maskWhitePoint = value.value("maskWhitePoint", settings.maskWhitePoint);
    settings.maskGamma = value.value("maskGamma", settings.maskGamma);
    settings.smoothnessRadius = value.value("smoothnessRadius", settings.smoothnessRadius);
    settings.smoothAreaRadius = value.value("smoothAreaRadius", settings.smoothAreaRadius);
    settings.edgeAwareness = value.value("edgeAwareness", settings.edgeAwareness);
    settings.haloGuard = value.value("haloGuard", settings.haloGuard);
    settings.maskDebandDither = value.value("maskDebandDither", settings.maskDebandDither);
    settings.manualBlend = value.value("manualBlend", settings.manualBlend);
    settings.minEv = std::clamp(settings.minEv, -2.5f, 0.5f);
    settings.maxEv = std::clamp(settings.maxEv, std::max(settings.minEv + 0.01f, 0.25f), 2.5f);
    settings.baseEv = std::clamp(settings.baseEv, -1.0f, 1.0f);
    settings.minEvBias = std::clamp(settings.minEvBias, -2.0f, 2.0f);
    settings.maxEvBias = std::clamp(settings.maxEvBias, -2.0f, 2.0f);
    settings.baseEvBias = std::clamp(settings.baseEvBias, -1.25f, 1.25f);
    settings.noiseProtectionBias = std::clamp(settings.noiseProtectionBias, -1.0f, 1.0f);
    settings.highlightProtectionBias = std::clamp(settings.highlightProtectionBias, -1.0f, 1.0f);
    settings.shadowLiftLimitBias = std::clamp(settings.shadowLiftLimitBias, -1.0f, 1.0f);
    settings.wellExposedTargetBias = std::clamp(settings.wellExposedTargetBias, -1.0f, 1.0f);
    settings.strength = std::clamp(settings.strength, 0.0f, 1.25f);
    settings.sampleCount = std::clamp(settings.sampleCount, 3, 33);
    settings.baseRadiusPercent = std::clamp(settings.baseRadiusPercent, 0.002f, 0.030f);
    settings.highlightProtection = std::clamp(settings.highlightProtection, 0.0f, 1.0f);
    settings.shadowLiftLimit = std::clamp(settings.shadowLiftLimit, 0.0f, 1.0f);
    settings.noiseProtection = std::clamp(settings.noiseProtection, 0.0f, 1.0f);
    settings.detailWeight = std::clamp(settings.detailWeight, 0.0f, 1.0f);
    settings.wellExposedTarget = std::clamp(settings.wellExposedTarget, 0.10f, 0.55f);
    settings.smoothGradientProtection = std::clamp(settings.smoothGradientProtection, 0.0f, 1.0f);
    settings.textureSensitivity = std::clamp(settings.textureSensitivity, 0.0f, 1.0f);
    settings.skyBias = std::clamp(settings.skyBias, 0.0f, 1.0f);
    settings.maskBlackPoint = std::clamp(settings.maskBlackPoint, 0.0f, 1.0f);
    settings.maskWhitePoint = std::clamp(settings.maskWhitePoint, settings.maskBlackPoint + 0.001f, 1.0f);
    settings.maskGamma = std::clamp(settings.maskGamma, 0.05f, 8.0f);
    settings.smoothnessRadius = std::clamp(settings.smoothnessRadius, 0, 16);
    settings.smoothAreaRadius = std::clamp(settings.smoothAreaRadius, 0, 32);
    settings.edgeAwareness = std::clamp(settings.edgeAwareness, 0.0f, 1.0f);
    settings.haloGuard = std::clamp(settings.haloGuard, 0.0f, 1.0f);
    settings.maskDebandDither = std::clamp(settings.maskDebandDither, 0.0f, 1.0f);
    settings.manualBlend = std::clamp(settings.manualBlend, 0.0f, 1.0f);
    return settings;
}

nlohmann::json SerializeHdrMergeSettings(const Raw::HdrMergeSettings& settings) {
    return {
        { "debugView", HdrMergeDebugViewToString(settings.debugView) },
        { "alignmentMode", HdrMergeAlignmentModeToString(settings.alignmentMode) },
        { "exposureMode", HdrMergeExposureModeToString(settings.exposureMode) },
        { "referenceMode", HdrMergeReferenceModeToString(settings.referenceMode) },
        { "deghostMode", HdrMergeDeghostModeToString(settings.deghostMode) },
        { "motionPriority", HdrMergeMotionPriorityToString(settings.motionPriority) },
        { "manualExposureEv1", settings.manualExposureEv[0] },
        { "manualExposureEv2", settings.manualExposureEv[1] },
        { "manualExposureEv3", settings.manualExposureEv[2] },
        { "exposureOffsetEv1", settings.exposureOffsetEv[0] },
        { "exposureOffsetEv2", settings.exposureOffsetEv[1] },
        { "exposureOffsetEv3", settings.exposureOffsetEv[2] },
        { "autoReliability", settings.autoReliability },
        { "clipThreshold", settings.clipThreshold },
        { "clipFeather", settings.clipFeather },
        { "blackThreshold", settings.blackThreshold },
        { "blackFeather", settings.blackFeather },
        { "readNoise", settings.readNoise },
        { "noiseAware", settings.noiseAware }
    };
}

Raw::HdrMergeSettings DeserializeHdrMergeSettings(const nlohmann::json& value) {
    Raw::HdrMergeSettings settings;
    if (!value.is_object()) {
        return settings;
    }
    settings.debugView = HdrMergeDebugViewFromString(value.value("debugView", std::string("FinalImage")));
    settings.alignmentMode = HdrMergeAlignmentModeFromString(value.value("alignmentMode", std::string("Off")));
    settings.exposureMode = HdrMergeExposureModeFromString(value.value("exposureMode", std::string("Metadata")));
    settings.referenceMode = HdrMergeReferenceModeFromString(value.value("referenceMode", std::string("Auto")));
    settings.deghostMode = HdrMergeDeghostModeFromString(value.value("deghostMode", std::string("Low")));
    settings.motionPriority = HdrMergeMotionPriorityFromString(value.value("motionPriority", std::string("PreserveReference")));
    settings.frameCount = std::clamp(value.value("frameCount", settings.frameCount), 2, 3);
    settings.manualExposureEv[0] = std::clamp(
        value.value("manualExposureEv1", value.value("exposureEv1", settings.manualExposureEv[0])),
        -12.0f,
        12.0f);
    settings.manualExposureEv[1] = std::clamp(
        value.value("manualExposureEv2", value.value("exposureEv2", settings.manualExposureEv[1])),
        -12.0f,
        12.0f);
    settings.manualExposureEv[2] = std::clamp(
        value.value("manualExposureEv3", value.value("exposureEv3", settings.manualExposureEv[2])),
        -12.0f,
        12.0f);
    settings.exposureOffsetEv[0] = std::clamp(value.value("exposureOffsetEv1", settings.exposureOffsetEv[0]), -4.0f, 4.0f);
    settings.exposureOffsetEv[1] = std::clamp(value.value("exposureOffsetEv2", settings.exposureOffsetEv[1]), -4.0f, 4.0f);
    settings.exposureOffsetEv[2] = std::clamp(value.value("exposureOffsetEv3", settings.exposureOffsetEv[2]), -4.0f, 4.0f);
    settings.autoReliability = value.value("autoReliability", settings.autoReliability);
    settings.clipThreshold = std::clamp(value.value("clipThreshold", settings.clipThreshold), 0.50f, 4.0f);
    settings.clipFeather = std::clamp(value.value("clipFeather", settings.clipFeather), 0.001f, 1.0f);
    settings.blackThreshold = std::clamp(value.value("blackThreshold", settings.blackThreshold), 0.0f, 0.25f);
    settings.blackFeather = std::clamp(value.value("blackFeather", settings.blackFeather), 0.001f, 0.50f);
    settings.readNoise = std::clamp(value.value("readNoise", settings.readNoise), 0.0f, 0.10f);
    settings.noiseAware = value.value("noiseAware", settings.noiseAware);
    return settings;
}

} // namespace EditorNodeGraph
