#include "EditorNodeGraphSerializer.h"

#include "Library/LibraryManager.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include <algorithm>
#include <cstring>

namespace EditorNodeGraph {
namespace {

void PngWriteCallback(void* context, void* data, int size) {
    auto* bytes = static_cast<std::vector<unsigned char>*>(context);
    const auto* begin = static_cast<unsigned char*>(data);
    bytes->insert(bytes->end(), begin, begin + size);
}

std::vector<unsigned char> EncodePng(const std::vector<unsigned char>& pixels, int width, int height, int channels) {
    std::vector<unsigned char> pngBytes;
    if (pixels.empty() || width <= 0 || height <= 0) {
        return pngBytes;
    }

    const int safeChannels = std::max(1, channels);
    stbi_write_png_to_func(PngWriteCallback, &pngBytes, width, height, safeChannels, pixels.data(), width * safeChannels);
    return pngBytes;
}

std::vector<unsigned char> EncodePngForImageStorage(
    const std::vector<unsigned char>& bottomLeftPixels,
    int width,
    int height,
    int channels) {
    if (bottomLeftPixels.empty() || width <= 0 || height <= 0) {
        return {};
    }

    std::vector<unsigned char> topLeftPixels = bottomLeftPixels;
    LibraryManager::FlipImageRowsInPlace(topLeftPixels, width, height, std::max(1, channels));
    return EncodePng(topLeftPixels, width, height, channels);
}

bool DecodePngBytes(const std::vector<unsigned char>& pngBytes, ImagePayload& payload) {
    if (pngBytes.empty()) {
        return false;
    }

    // The editor stores image-node pixels in the same bottom-left-oriented layout
    // used by the render pipeline's GL uploads. Keep saved image payloads aligned
    // with fresh imports so reopening a project cannot flip the source image.
    stbi_set_flip_vertically_on_load_thread(1);
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(pngBytes.data(), static_cast<int>(pngBytes.size()), &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }

    payload.pngBytes = pngBytes;
    payload.pixels.assign(pixels, pixels + (width * height * 4));
    payload.width = width;
    payload.height = height;
    payload.channels = 4;
    payload.originalChannels = channels;
    stbi_image_free(pixels);
    return true;
}

std::string NodeKindToString(NodeKind kind) {
    switch (kind) {
        case NodeKind::Image: return "Image";
        case NodeKind::RawSource: return "RawSource";
        case NodeKind::RawNeuralDenoise: return "RawNeuralDenoise";
        case NodeKind::RawDevelop: return "RawDevelop";
        case NodeKind::RawDetailAutoMask: return "RawDetailAutoMask";
        case NodeKind::RawDetailFusion: return "RawDetailFusion";
        case NodeKind::Layer: return "Layer";
        case NodeKind::Output: return "Output";
        case NodeKind::Composite: return "Composite";
        case NodeKind::Scope: return "Scope";
        case NodeKind::MaskGenerator: return "MaskGenerator";
        case NodeKind::Mix: return "Mix";
        case NodeKind::Preview: return "Preview";
        case NodeKind::MaskUtility: return "MaskUtility";
        case NodeKind::ImageToMask: return "ImageToMask";
        case NodeKind::ImageGenerator: return "ImageGenerator";
        case NodeKind::ChannelSplit: return "ChannelSplit";
        case NodeKind::ChannelCombine: return "ChannelCombine";
    }
    return "Layer";
}

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
    return method == Raw::DemosaicMethod::QualityPlaceholder ? "QualityPlaceholder" : "Bilinear";
}

Raw::DemosaicMethod DemosaicMethodFromString(const std::string& value) {
    return value == "QualityPlaceholder" ? Raw::DemosaicMethod::QualityPlaceholder : Raw::DemosaicMethod::Bilinear;
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
    settings.minEv = std::clamp(settings.minEv, -8.0f, 8.0f);
    settings.maxEv = std::clamp(settings.maxEv, settings.minEv + 0.01f, 8.0f);
    settings.baseEv = std::clamp(settings.baseEv, -8.0f, 8.0f);
    settings.minEvBias = std::clamp(settings.minEvBias, -3.0f, 3.0f);
    settings.maxEvBias = std::clamp(settings.maxEvBias, -3.0f, 3.0f);
    settings.baseEvBias = std::clamp(settings.baseEvBias, -3.0f, 3.0f);
    settings.noiseProtectionBias = std::clamp(settings.noiseProtectionBias, -1.0f, 1.0f);
    settings.highlightProtectionBias = std::clamp(settings.highlightProtectionBias, -1.0f, 1.0f);
    settings.shadowLiftLimitBias = std::clamp(settings.shadowLiftLimitBias, -1.0f, 1.0f);
    settings.wellExposedTargetBias = std::clamp(settings.wellExposedTargetBias, -0.5f, 0.5f);
    settings.strength = std::clamp(settings.strength, 0.0f, 4.0f);
    settings.sampleCount = std::clamp(settings.sampleCount, 3, 33);
    settings.highlightProtection = std::clamp(settings.highlightProtection, 0.0f, 1.0f);
    settings.shadowLiftLimit = std::clamp(settings.shadowLiftLimit, 0.0f, 1.0f);
    settings.noiseProtection = std::clamp(settings.noiseProtection, 0.0f, 1.0f);
    settings.detailWeight = std::clamp(settings.detailWeight, 0.0f, 1.0f);
    settings.wellExposedTarget = std::clamp(settings.wellExposedTarget, 0.01f, 1.0f);
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

std::string ScopeKindToString(ScopeKind kind) {
    switch (kind) {
        case ScopeKind::Histogram: return "Histogram";
        case ScopeKind::Vectorscope: return "Vectorscope";
        case ScopeKind::RGBParade: return "RGBParade";
    }
    return "Histogram";
}

ScopeKind ScopeKindFromString(const std::string& value) {
    if (value == "Vectorscope") return ScopeKind::Vectorscope;
    if (value == "RGBParade" || value == "RGB Parade") return ScopeKind::RGBParade;
    return ScopeKind::Histogram;
}

std::string MaskGeneratorKindToString(MaskGeneratorKind kind) {
    switch (kind) {
        case MaskGeneratorKind::Solid: return "Solid";
        case MaskGeneratorKind::LinearGradient: return "LinearGradient";
        case MaskGeneratorKind::RadialGradient: return "RadialGradient";
        case MaskGeneratorKind::Noise: return "Noise";
    }
    return "Solid";
}

MaskGeneratorKind MaskGeneratorKindFromString(const std::string& value) {
    if (value == "LinearGradient" || value == "Linear Gradient") return MaskGeneratorKind::LinearGradient;
    if (value == "RadialGradient" || value == "Radial Gradient") return MaskGeneratorKind::RadialGradient;
    if (value == "Noise" || value == "Noise Mask") return MaskGeneratorKind::Noise;
    return MaskGeneratorKind::Solid;
}

std::string MaskUtilityKindToString(MaskUtilityKind kind) {
    switch (kind) {
        case MaskUtilityKind::Invert: return "Invert";
        case MaskUtilityKind::Levels: return "Levels";
        case MaskUtilityKind::Threshold: return "Threshold";
    }
    return "Invert";
}

MaskUtilityKind MaskUtilityKindFromString(const std::string& value) {
    if (value == "Levels") return MaskUtilityKind::Levels;
    if (value == "Threshold") return MaskUtilityKind::Threshold;
    return MaskUtilityKind::Invert;
}

std::string ImageGeneratorKindToString(ImageGeneratorKind kind) {
    switch (kind) {
        case ImageGeneratorKind::SolidColor: return "SolidColor";
        case ImageGeneratorKind::ColorGradient: return "ColorGradient";
        case ImageGeneratorKind::Square: return "Square";
        case ImageGeneratorKind::Circle: return "Circle";
        case ImageGeneratorKind::Text: return "Text";
    }
    return "SolidColor";
}

ImageGeneratorKind ImageGeneratorKindFromString(const std::string& value) {
    if (value == "ColorGradient" || value == "Color Gradient") return ImageGeneratorKind::ColorGradient;
    if (value == "Square") return ImageGeneratorKind::Square;
    if (value == "Circle") return ImageGeneratorKind::Circle;
    if (value == "Text") return ImageGeneratorKind::Text;
    return ImageGeneratorKind::SolidColor;
}

nlohmann::json SerializeMaskUtilitySettings(const MaskUtilitySettings& settings) {
    return {
        { "blackPoint", settings.blackPoint },
        { "whitePoint", settings.whitePoint },
        { "gamma", settings.gamma },
        { "threshold", settings.threshold },
        { "softness", settings.softness },
        { "invert", settings.invert }
    };
}

MaskUtilitySettings DeserializeMaskUtilitySettings(const nlohmann::json& value) {
    MaskUtilitySettings settings;
    if (!value.is_object()) return settings;
    settings.blackPoint = value.value("blackPoint", settings.blackPoint);
    settings.whitePoint = value.value("whitePoint", settings.whitePoint);
    settings.gamma = value.value("gamma", settings.gamma);
    settings.threshold = value.value("threshold", settings.threshold);
    settings.softness = value.value("softness", settings.softness);
    settings.invert = value.value("invert", settings.invert);
    return settings;
}

nlohmann::json SerializeImageToMaskSettings(const ImageToMaskSettings& settings) {
    return {
        { "low", settings.low },
        { "high", settings.high },
        { "softness", settings.softness },
        { "invert", settings.invert }
    };
}

ImageToMaskSettings DeserializeImageToMaskSettings(const nlohmann::json& value) {
    ImageToMaskSettings settings;
    if (!value.is_object()) return settings;
    settings.low = value.value("low", settings.low);
    settings.high = value.value("high", settings.high);
    settings.softness = value.value("softness", settings.softness);
    settings.invert = value.value("invert", settings.invert);
    return settings;
}

nlohmann::json SerializeImageGeneratorSettings(const ImageGeneratorSettings& settings) {
    return {
        { "colorA", { settings.colorA[0], settings.colorA[1], settings.colorA[2], settings.colorA[3] } },
        { "colorB", { settings.colorB[0], settings.colorB[1], settings.colorB[2], settings.colorB[3] } },
        { "angle", settings.angle },
        { "offset", settings.offset },
        { "text", settings.text },
        { "fontSize", settings.fontSize }
    };
}

ImageGeneratorSettings DeserializeImageGeneratorSettings(const nlohmann::json& value) {
    ImageGeneratorSettings settings;
    if (!value.is_object()) return settings;
    const nlohmann::json colorA = value.value("colorA", nlohmann::json::array());
    const nlohmann::json colorB = value.value("colorB", nlohmann::json::array());
    for (int i = 0; i < 4; ++i) {
        if (colorA.is_array() && static_cast<int>(colorA.size()) > i) settings.colorA[i] = colorA[i].get<float>();
        if (colorB.is_array() && static_cast<int>(colorB.size()) > i) settings.colorB[i] = colorB[i].get<float>();
    }
    settings.angle = value.value("angle", settings.angle);
    settings.offset = value.value("offset", settings.offset);
    settings.text = value.value("text", settings.text);
    settings.fontSize = value.value("fontSize", settings.fontSize);
    return settings;
}

nlohmann::json SerializeMaskSettings(const MaskGeneratorSettings& settings) {
    return {
        { "value", settings.value },
        { "angle", settings.angle },
        { "offset", settings.offset },
        { "scale", settings.scale },
        { "centerX", settings.centerX },
        { "centerY", settings.centerY },
        { "radius", settings.radius },
        { "feather", settings.feather },
        { "invert", settings.invert }
    };
}

MaskGeneratorSettings DeserializeMaskSettings(const nlohmann::json& value) {
    MaskGeneratorSettings settings;
    if (!value.is_object()) {
        return settings;
    }
    settings.value = value.value("value", settings.value);
    settings.angle = value.value("angle", settings.angle);
    settings.offset = value.value("offset", settings.offset);
    settings.scale = value.value("scale", settings.scale);
    settings.centerX = value.value("centerX", settings.centerX);
    settings.centerY = value.value("centerY", settings.centerY);
    settings.radius = value.value("radius", settings.radius);
    settings.feather = value.value("feather", settings.feather);
    settings.invert = value.value("invert", settings.invert);
    return settings;
}

std::string MixBlendModeToString(MixBlendMode mode) {
    switch (mode) {
        case MixBlendMode::Normal: return "Normal";
        case MixBlendMode::Add: return "Add";
        case MixBlendMode::Multiply: return "Multiply";
        case MixBlendMode::Screen: return "Screen";
        case MixBlendMode::AlphaOver: return "AlphaOver";
    }
    return "Normal";
}

MixBlendMode MixBlendModeFromString(const std::string& value) {
    if (value == "Add") return MixBlendMode::Add;
    if (value == "Multiply") return MixBlendMode::Multiply;
    if (value == "Screen") return MixBlendMode::Screen;
    if (value == "AlphaOver" || value == "Alpha Over") return MixBlendMode::AlphaOver;
    return MixBlendMode::Normal;
}

std::string ImageToMaskKindToString(ImageToMaskKind kind) {
    switch (kind) {
        case ImageToMaskKind::Luminance: return "Luminance";
    }
    return "Luminance";
}

ImageToMaskKind ImageToMaskKindFromString(const std::string& value) {
    (void)value;
    return ImageToMaskKind::Luminance;
}

std::vector<unsigned char> ReadBinaryJson(const nlohmann::json& value) {
    if (!value.is_binary()) {
        return {};
    }

    const auto& binaryValue = value.get_binary();
    return std::vector<unsigned char>(binaryValue.begin(), binaryValue.end());
}

} // namespace

nlohmann::json ExtractLayerArray(const nlohmann::json& pipelineData) {
    if (pipelineData.is_array()) {
        return pipelineData;
    }
    if (pipelineData.is_object()) {
        const nlohmann::json layers = pipelineData.value("layers", nlohmann::json::array());
        return layers.is_array() ? layers : nlohmann::json::array();
    }
    return nlohmann::json::array();
}

nlohmann::json SerializeGraphPayload(const nlohmann::json& layerArray, const Graph& graph) {
    nlohmann::json root = nlohmann::json::object();
    root["layers"] = layerArray.is_array() ? layerArray : nlohmann::json::array();

    nlohmann::json graphJson = nlohmann::json::object();
    graphJson["version"] = 3;
    graphJson["nextNodeId"] = graph.GetNextNodeId();
    graphJson["nextGroupId"] = graph.GetNextGroupId();
    graphJson["selectedNodeId"] = graph.GetSelectedNodeId();
    graphJson["activeImageNodeId"] = graph.GetActiveImageNodeId();
    graphJson["outputNodeId"] = graph.GetOutputNodeId();
    graphJson["outputNodeIds"] = graph.GetOutputNodeIds();

    nlohmann::json nodesJson = nlohmann::json::array();
    for (const Node& node : graph.GetNodes()) {
        nlohmann::json item = nlohmann::json::object();
        item["id"] = node.id;
        item["kind"] = NodeKindToString(node.kind);
        item["layerIndex"] = node.layerIndex;
        item["typeId"] = node.typeId;
        item["title"] = node.title;
        item["x"] = node.position.x;
        item["y"] = node.position.y;
        item["expanded"] = node.expanded;
        item["scopeKind"] = ScopeKindToString(node.scopeKind);
        item["maskKind"] = MaskGeneratorKindToString(node.maskKind);
        item["maskSettings"] = SerializeMaskSettings(node.maskSettings);
        item["maskUtilityKind"] = MaskUtilityKindToString(node.maskUtilityKind);
        item["maskUtilitySettings"] = SerializeMaskUtilitySettings(node.maskUtilitySettings);
        item["imageToMaskKind"] = ImageToMaskKindToString(node.imageToMaskKind);
        item["imageToMaskSettings"] = SerializeImageToMaskSettings(node.imageToMaskSettings);
        item["imageGeneratorKind"] = ImageGeneratorKindToString(node.imageGeneratorKind);
        item["imageGeneratorSettings"] = SerializeImageGeneratorSettings(node.imageGeneratorSettings);
        item["mixBlendMode"] = MixBlendModeToString(node.mixBlendMode);
        item["mixFactor"] = node.mixFactor;
        item["outputEnabled"] = node.outputEnabled;

        if (node.kind == NodeKind::Image) {
            item["label"] = node.image.label;
            item["sourcePath"] = node.image.sourcePath;
            item["width"] = node.image.width;
            item["height"] = node.image.height;
            item["channels"] = node.image.channels;
            item["originalChannels"] = node.image.originalChannels;
            item["pngBytes"] = nlohmann::json::binary(node.image.pngBytes);
        } else if (node.kind == NodeKind::RawSource) {
            item["label"] = node.rawSource.label;
            item["sourcePath"] = node.rawSource.sourcePath;
            item["rawMetadata"] = SerializeRawMetadata(node.rawSource.metadata);
        } else if (node.kind == NodeKind::RawNeuralDenoise) {
            item["neuralDenoiseSettings"] = NeuralDenoise::SerializeSettings(node.rawNeuralDenoise.settings);
        } else if (node.kind == NodeKind::RawDevelop) {
            item["rawSettings"] = SerializeRawSettings(node.rawDevelop.settings);
        } else if (node.kind == NodeKind::RawDetailAutoMask) {
            item["rawDetailAutoMaskSettings"] = SerializeRawDetailFusionSettings(node.rawDetailAutoMask.settings);
        } else if (node.kind == NodeKind::RawDetailFusion) {
            item["rawDetailFusionSettings"] = SerializeRawDetailFusionSettings(node.rawDetailFusion.settings);
        }

        nodesJson.push_back(std::move(item));
    }
    graphJson["nodes"] = std::move(nodesJson);

    nlohmann::json linksJson = nlohmann::json::array();
    for (const Link& link : graph.GetLinks()) {
        linksJson.push_back({
            { "fromNodeId", link.fromNodeId },
            { "fromSocket", link.fromSocketId },
            { "toNodeId", link.toNodeId },
            { "toSocket", link.toSocketId }
        });
    }
    graphJson["links"] = std::move(linksJson);

    nlohmann::json groupsJson = nlohmann::json::array();
    for (const NodeGroup& group : graph.GetGroups()) {
        groupsJson.push_back({
            { "id", group.id },
            { "title", group.title },
            { "x", group.position.x },
            { "y", group.position.y },
            { "width", group.size.x },
            { "height", group.size.y }
        });
    }
    graphJson["groups"] = std::move(groupsJson);

    root["nodeGraph"] = std::move(graphJson);
    return root;
}

void DeserializeGraphPayload(
    const nlohmann::json& pipelineData,
    Graph& graph,
    int layerCount,
    const std::vector<unsigned char>& fallbackSourcePixels,
    int fallbackSourceWidth,
    int fallbackSourceHeight,
    int fallbackSourceChannels) {

    graph.Clear();

    const bool hasFallbackSource =
        !fallbackSourcePixels.empty() && fallbackSourceWidth > 0 && fallbackSourceHeight > 0;

    if (!pipelineData.is_object() || !pipelineData.contains("nodeGraph")) {
        graph.ResetFromLayers(layerCount, hasFallbackSource);
        if (hasFallbackSource) {
            if (Node* imageNode = graph.FindNode(graph.GetActiveImageNodeId())) {
                imageNode->image.label = "Image";
                imageNode->image.width = fallbackSourceWidth;
                imageNode->image.height = fallbackSourceHeight;
                imageNode->image.channels = std::max(1, fallbackSourceChannels);
                imageNode->image.pixels = fallbackSourcePixels;
                imageNode->image.pngBytes = EncodePngForImageStorage(
                    fallbackSourcePixels,
                    fallbackSourceWidth,
                    fallbackSourceHeight,
                    std::max(1, fallbackSourceChannels));
            }
        }
        return;
    }

    const nlohmann::json graphJson = pipelineData.value("nodeGraph", nlohmann::json::object());
    const nlohmann::json nodesJson = graphJson.value("nodes", nlohmann::json::array());

    int maxNodeId = 0;
    for (const nlohmann::json& item : nodesJson) {
        if (!item.is_object()) continue;

        Node node;
        node.id = item.value("id", 0);
        node.layerIndex = item.value("layerIndex", -1);
        node.typeId = item.value("typeId", std::string());
        node.title = item.value("title", std::string());
        node.position.x = item.value("x", 0.0f);
        node.position.y = item.value("y", 0.0f);
        node.expanded = item.value("expanded", false);
        node.outputEnabled = item.value("outputEnabled", true);

        const std::string kind = item.value("kind", std::string("Layer"));
        if (kind == "ExportBoundsSettings" || kind == "Composite") {
            continue;
        }

        if (kind == "Image") {
            node.kind = NodeKind::Image;
            node.image.label = item.value("label", node.title.empty() ? std::string("Image") : node.title);
            node.image.sourcePath = item.value("sourcePath", std::string());
            DecodePngBytes(ReadBinaryJson(item.value("pngBytes", nlohmann::json())), node.image);
            node.image.originalChannels = item.value("originalChannels", node.image.originalChannels);
            if (node.title.empty()) node.title = node.image.label.empty() ? "Image" : node.image.label;
        } else if (kind == "RawSource") {
            node.kind = NodeKind::RawSource;
            node.rawSource.label = item.value("label", node.title.empty() ? std::string("RAW") : node.title);
            node.rawSource.sourcePath = item.value("sourcePath", std::string());
            node.rawSource.metadata = DeserializeRawMetadata(item.value("rawMetadata", nlohmann::json::object()));
            if (node.rawSource.metadata.sourcePath.empty()) {
                node.rawSource.metadata.sourcePath = node.rawSource.sourcePath;
            }
            if (node.title.empty()) node.title = node.rawSource.label.empty() ? "RAW" : node.rawSource.label;
        } else if (kind == "RawNeuralDenoise") {
            node.kind = NodeKind::RawNeuralDenoise;
            node.rawNeuralDenoise.settings = NeuralDenoise::DeserializeSettings(item.value("neuralDenoiseSettings", nlohmann::json::object()));
            if (node.title.empty()) node.title = "RAW/CFA Neural Denoise";
        } else if (kind == "RawDevelop") {
            node.kind = NodeKind::RawDevelop;
            node.rawDevelop.settings = DeserializeRawSettings(item.value("rawSettings", nlohmann::json::object()));
            if (node.title.empty()) node.title = "RAW Develop";
        } else if (kind == "RawDetailAutoMask") {
            node.kind = NodeKind::RawDetailAutoMask;
            node.rawDetailAutoMask.settings = DeserializeRawDetailFusionSettings(item.value("rawDetailAutoMaskSettings", nlohmann::json::object()));
            if (node.title.empty()) node.title = "RAW Detail Auto Mask";
        } else if (kind == "RawDetailFusion") {
            node.kind = NodeKind::RawDetailFusion;
            node.rawDetailFusion.settings = DeserializeRawDetailFusionSettings(item.value("rawDetailFusionSettings", nlohmann::json::object()));
            if (node.title.empty() || node.title == "RAW Detail Fusion") node.title = "Auto Gain";
        } else if (kind == "Output") {
            node.kind = NodeKind::Output;
            if (node.title.empty()) node.title = "Output";
        } else if (kind == "Composite") {
            node.kind = NodeKind::Composite;
            if (node.title.empty()) node.title = "Composite";
        } else if (kind == "Scope") {
            node.kind = NodeKind::Scope;
            node.scopeKind = ScopeKindFromString(item.value("scopeKind", std::string("Histogram")));
            if (node.title.empty()) {
                node.title = ScopeKindToString(node.scopeKind);
            }
        } else if (kind == "MaskGenerator") {
            node.kind = NodeKind::MaskGenerator;
            node.maskKind = MaskGeneratorKindFromString(item.value("maskKind", std::string("Solid")));
            node.maskSettings = DeserializeMaskSettings(item.value("maskSettings", nlohmann::json::object()));
            if (node.title.empty()) {
                node.title = node.maskKind == MaskGeneratorKind::Solid ? "Solid Mask" :
                    (node.maskKind == MaskGeneratorKind::LinearGradient ? "Linear Gradient Mask" :
                    (node.maskKind == MaskGeneratorKind::RadialGradient ? "Radial Gradient Mask" : "Noise Mask"));
            }
        } else if (kind == "Mix") {
            node.kind = NodeKind::Mix;
            node.mixBlendMode = MixBlendModeFromString(item.value("mixBlendMode", std::string("Normal")));
            node.mixFactor = item.value("mixFactor", 0.5f);
            if (node.title.empty()) {
                node.title = "Mix";
            }
        } else if (kind == "Preview") {
            node.kind = NodeKind::Preview;
            if (node.title.empty()) {
                node.title = "Preview";
            }
        } else if (kind == "MaskUtility") {
            node.kind = NodeKind::MaskUtility;
            node.maskUtilityKind = MaskUtilityKindFromString(item.value("maskUtilityKind", std::string("Invert")));
            node.maskUtilitySettings = DeserializeMaskUtilitySettings(item.value("maskUtilitySettings", nlohmann::json::object()));
            if (node.title.empty()) {
                node.title = node.maskUtilityKind == MaskUtilityKind::Invert ? "Invert Mask" :
                    (node.maskUtilityKind == MaskUtilityKind::Levels ? "Levels Mask" : "Threshold Mask");
            }
        } else if (kind == "ImageToMask") {
            node.kind = NodeKind::ImageToMask;
            node.imageToMaskKind = ImageToMaskKindFromString(item.value("imageToMaskKind", std::string("Luminance")));
            node.imageToMaskSettings = DeserializeImageToMaskSettings(item.value("imageToMaskSettings", nlohmann::json::object()));
            if (node.title.empty()) {
                node.title = "Luminance Mask";
            }
        } else if (kind == "ImageGenerator") {
            node.kind = NodeKind::ImageGenerator;
            node.imageGeneratorKind = ImageGeneratorKindFromString(item.value("imageGeneratorKind", std::string("SolidColor")));
            node.imageGeneratorSettings = DeserializeImageGeneratorSettings(item.value("imageGeneratorSettings", nlohmann::json::object()));
            if (node.title.empty()) {
                switch (node.imageGeneratorKind) {
                    case ImageGeneratorKind::SolidColor: node.title = "Solid Color Image"; break;
                    case ImageGeneratorKind::ColorGradient: node.title = "Color Gradient Image"; break;
                    case ImageGeneratorKind::Square: node.title = "Square"; break;
                    case ImageGeneratorKind::Circle: node.title = "Circle"; break;
                    case ImageGeneratorKind::Text: node.title = "Text"; break;
                }
            }
        } else if (kind == "ChannelSplit") {
            node.kind = NodeKind::ChannelSplit;
            if (node.title.empty()) {
                node.title = "Channel Split";
            }
        } else if (kind == "ChannelCombine") {
            node.kind = NodeKind::ChannelCombine;
            if (node.title.empty()) {
                node.title = "Channel Combine";
            }
        } else {
            node.kind = NodeKind::Layer;
            const LayerDescriptor* descriptor = LayerRegistry::FindDescriptorByTypeId(node.typeId);
            if (descriptor) {
                node.layerType = descriptor->type;
                if (node.title.empty()) node.title = descriptor->displayName;
            } else if (node.title.empty()) {
                node.title = "Layer";
            }
        }

        maxNodeId = std::max(maxNodeId, node.id);
        graph.GetNodes().push_back(std::move(node));
    }

    const nlohmann::json outputNodeIdsJson = graphJson.value("outputNodeIds", nlohmann::json::array());
    if (outputNodeIdsJson.is_array()) {
        for (const nlohmann::json& outputIdJson : outputNodeIdsJson) {
            const int outputId = outputIdJson.is_number_integer() ? outputIdJson.get<int>() : -1;
            const Node* outputNode = graph.FindNode(outputId);
            if (outputNode && outputNode->kind == NodeKind::Output) {
                graph.SetOutputNodeId(outputId);
                break;
            }
        }
    }
    if (graph.GetOutputNodeId() <= 0) {
        const int legacyOutputNodeId = graphJson.value("outputNodeId", -1);
        const Node* outputNode = graph.FindNode(legacyOutputNodeId);
        if (outputNode && outputNode->kind == NodeKind::Output) {
            graph.SetOutputNodeId(legacyOutputNodeId);
        }
    }

    graph.SetNextNodeId(std::max(maxNodeId + 1, graphJson.value("nextNodeId", maxNodeId + 1)));
    graph.SelectNode(graphJson.value("selectedNodeId", -1));
    graph.SetActiveImageNodeId(graphJson.value("activeImageNodeId", -1));

    const nlohmann::json linksJson = graphJson.value("links", nlohmann::json::array());
    for (const nlohmann::json& item : linksJson) {
        if (!item.is_object()) continue;
        const int from = item.value("fromNodeId", item.value("from", 0));
        const int to = item.value("toNodeId", item.value("to", 0));
        if (from <= 0 || to <= 0) {
            continue;
        }

        const Node* fromNode = graph.FindNode(from);
        const Node* toNode = graph.FindNode(to);
        if (!fromNode || !toNode) {
            continue;
        }

        const std::string fromSocket = item.value("fromSocket", graph.DefaultOutputSocket(*fromNode));
        const std::string toSocket = item.value("toSocket", graph.DefaultInputSocket(*toNode));
        if (!fromSocket.empty() && !toSocket.empty() && !graph.HasLink(from, fromSocket, to, toSocket)) {
            graph.TryConnectSockets(from, fromSocket, to, toSocket);
        }
    }

    if (graph.GetLinks().empty() && graph.GetActiveImageNodeId() > 0) {
        graph.RebuildLinks();
    }

    const nlohmann::json groupsJson = graphJson.value("groups", nlohmann::json::array());
    int maxGroupId = 0;
    if (groupsJson.is_array()) {
        for (const nlohmann::json& item : groupsJson) {
            if (!item.is_object()) continue;
            NodeGroup group;
            group.id = item.value("id", 0);
            group.title = item.value("title", "New Group");
            group.position.x = item.value("x", 0.0f);
            group.position.y = item.value("y", 0.0f);
            group.size.x = item.value("width", 200.0f);
            group.size.y = item.value("height", 150.0f);
            maxGroupId = std::max(maxGroupId, group.id);
            graph.GetGroups().push_back(std::move(group));
        }
    }
    graph.SetNextGroupId(std::max(maxGroupId + 1, graphJson.value("nextGroupId", maxGroupId + 1)));

    graph.EnsureOutputNode();
    graph.SyncLayerNodes(layerCount);
}

} // namespace EditorNodeGraph
