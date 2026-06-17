#include "EditorNodeGraphSerializer.h"

#include "EditorNodeGraphDefinitions.h"
#include "Library/LibraryManager.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include <algorithm>
#include <cmath>
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
        case NodeKind::RawDecode: return "RawDecode";
        case NodeKind::RawDevelop: return "RawDevelop";
        case NodeKind::RawDetailAutoMask: return "RawDetailAutoMask";
        case NodeKind::RawDetailFusion: return "RawDetailFusion";
        case NodeKind::HdrMerge: return "HdrMerge";
        case NodeKind::Lut: return "Lut";
        case NodeKind::Layer: return "Layer";
        case NodeKind::Output: return "Output";
        case NodeKind::Composite: return "Composite";
        case NodeKind::Scope: return "Scope";
        case NodeKind::MaskGenerator: return "MaskGenerator";
        case NodeKind::MaskCombine: return "MaskCombine";
        case NodeKind::Mix: return "Mix";
        case NodeKind::Preview: return "Preview";
        case NodeKind::MaskUtility: return "MaskUtility";
        case NodeKind::ImageToMask: return "ImageToMask";
        case NodeKind::ImageGenerator: return "ImageGenerator";
        case NodeKind::ChannelSplit: return "ChannelSplit";
        case NodeKind::ChannelCombine: return "ChannelCombine";
        case NodeKind::CustomMask: return "CustomMask";
        case NodeKind::DataMath: return "DataMath";
    }
    return "Layer";
}

std::string LutImportFormatToString(ColorLut::LutImportFormat format) {
    switch (format) {
        case ColorLut::LutImportFormat::Cube: return "Cube";
        case ColorLut::LutImportFormat::Format3dl: return "3dl";
        case ColorLut::LutImportFormat::Spi1d: return "Spi1d";
        case ColorLut::LutImportFormat::Spi3d: return "Spi3d";
        case ColorLut::LutImportFormat::Unknown:
        default:
            return "Unknown";
    }
}

ColorLut::LutImportFormat LutImportFormatFromString(const std::string& value) {
    if (value == "Cube") return ColorLut::LutImportFormat::Cube;
    if (value == "3dl" || value == "Format3dl") return ColorLut::LutImportFormat::Format3dl;
    if (value == "Spi1d") return ColorLut::LutImportFormat::Spi1d;
    if (value == "Spi3d") return ColorLut::LutImportFormat::Spi3d;
    return ColorLut::LutImportFormat::Unknown;
}

std::string LutUseModeToString(ColorLut::LutUseMode mode) {
    switch (mode) {
        case ColorLut::LutUseMode::PreViewTransform: return "PreViewTransform";
        case ColorLut::LutUseMode::PostViewTransform:
        default:
            return "PostViewTransform";
    }
}

ColorLut::LutUseMode LutUseModeFromString(const std::string& value) {
    if (value == "PreViewTransform") return ColorLut::LutUseMode::PreViewTransform;
    return ColorLut::LutUseMode::PostViewTransform;
}

std::string LutTransferFunctionToString(ColorLut::LutTransferFunction transform) {
    switch (transform) {
        case ColorLut::LutTransferFunction::SrgbEncode: return "SrgbEncode";
        case ColorLut::LutTransferFunction::Gamma22Encode: return "Gamma22Encode";
        case ColorLut::LutTransferFunction::SrgbDecode: return "SrgbDecode";
        case ColorLut::LutTransferFunction::Gamma22Decode: return "Gamma22Decode";
        case ColorLut::LutTransferFunction::None:
        default:
            return "None";
    }
}

ColorLut::LutTransferFunction LutTransferFunctionFromString(const std::string& value) {
    if (value == "SrgbEncode") return ColorLut::LutTransferFunction::SrgbEncode;
    if (value == "Gamma22Encode") return ColorLut::LutTransferFunction::Gamma22Encode;
    if (value == "SrgbDecode") return ColorLut::LutTransferFunction::SrgbDecode;
    if (value == "Gamma22Decode") return ColorLut::LutTransferFunction::Gamma22Decode;
    return ColorLut::LutTransferFunction::None;
}

nlohmann::json SerializeLut1DStage(const ColorLut::Lut1DStage& stage) {
    return {
        { "size", stage.size },
        { "values", stage.values },
        { "domainMin", { stage.domainMin[0], stage.domainMin[1], stage.domainMin[2] } },
        { "domainMax", { stage.domainMax[0], stage.domainMax[1], stage.domainMax[2] } }
    };
}

ColorLut::Lut1DStage DeserializeLut1DStage(const nlohmann::json& value) {
    ColorLut::Lut1DStage stage;
    stage.size = value.value("size", 0);
    stage.values = value.value("values", std::vector<float>{});
    const nlohmann::json domainMin = value.value("domainMin", nlohmann::json::array());
    const nlohmann::json domainMax = value.value("domainMax", nlohmann::json::array());
    for (int i = 0; i < 3; ++i) {
        stage.domainMin[static_cast<std::size_t>(i)] =
            domainMin.is_array() && domainMin.size() > static_cast<std::size_t>(i)
                ? domainMin[static_cast<std::size_t>(i)].get<float>()
                : stage.domainMin[static_cast<std::size_t>(i)];
        stage.domainMax[static_cast<std::size_t>(i)] =
            domainMax.is_array() && domainMax.size() > static_cast<std::size_t>(i)
                ? domainMax[static_cast<std::size_t>(i)].get<float>()
                : stage.domainMax[static_cast<std::size_t>(i)];
    }
    return stage;
}

nlohmann::json SerializeLut3DStage(const ColorLut::Lut3DStage& stage) {
    return {
        { "size", stage.size },
        { "values", stage.values },
        { "domainMin", { stage.domainMin[0], stage.domainMin[1], stage.domainMin[2] } },
        { "domainMax", { stage.domainMax[0], stage.domainMax[1], stage.domainMax[2] } }
    };
}

ColorLut::Lut3DStage DeserializeLut3DStage(const nlohmann::json& value) {
    ColorLut::Lut3DStage stage;
    stage.size = value.value("size", 0);
    stage.values = value.value("values", std::vector<float>{});
    const nlohmann::json domainMin = value.value("domainMin", nlohmann::json::array());
    const nlohmann::json domainMax = value.value("domainMax", nlohmann::json::array());
    for (int i = 0; i < 3; ++i) {
        stage.domainMin[static_cast<std::size_t>(i)] =
            domainMin.is_array() && domainMin.size() > static_cast<std::size_t>(i)
                ? domainMin[static_cast<std::size_t>(i)].get<float>()
                : stage.domainMin[static_cast<std::size_t>(i)];
        stage.domainMax[static_cast<std::size_t>(i)] =
            domainMax.is_array() && domainMax.size() > static_cast<std::size_t>(i)
                ? domainMax[static_cast<std::size_t>(i)].get<float>()
                : stage.domainMax[static_cast<std::size_t>(i)];
    }
    return stage;
}

nlohmann::json SerializeLutPayload(const ColorLut::LutPayload& payload) {
    return {
        { "sourcePath", payload.sourcePath },
        { "label", payload.label },
        { "importedTitle", payload.importedTitle },
        { "importError", payload.importError },
        { "importFormat", LutImportFormatToString(payload.importFormat) },
        { "useMode", LutUseModeToString(payload.useMode) },
        { "inputTransform", LutTransferFunctionToString(payload.inputTransform) },
        { "outputTransform", LutTransferFunctionToString(payload.outputTransform) },
        { "lut1D", SerializeLut1DStage(payload.lut1D) },
        { "shaper1D", SerializeLut1DStage(payload.shaper1D) },
        { "lut3D", SerializeLut3DStage(payload.lut3D) }
    };
}

ColorLut::LutPayload DeserializeLutPayload(const nlohmann::json& value) {
    ColorLut::LutPayload payload;
    payload.sourcePath = value.value("sourcePath", std::string());
    payload.label = value.value("label", std::string());
    payload.importedTitle = value.value("importedTitle", std::string());
    payload.importError = value.value("importError", std::string());
    payload.importFormat = LutImportFormatFromString(value.value("importFormat", std::string("Unknown")));
    const std::string serializedMode = value.value(
        "useMode",
        value.value("lookupMode", std::string("PostViewTransform")));
    payload.useMode = LutUseModeFromString(serializedMode);
    payload.inputTransform = LutTransferFunctionFromString(value.value("inputTransform", std::string("None")));
    payload.outputTransform = LutTransferFunctionFromString(value.value("outputTransform", std::string("None")));
    payload.lut1D = DeserializeLut1DStage(value.value("lut1D", nlohmann::json::object()));
    payload.shaper1D = DeserializeLut1DStage(value.value("shaper1D", nlohmann::json::object()));
    payload.lut3D = DeserializeLut3DStage(value.value("lut3D", nlohmann::json::object()));
    return payload;
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
    if (value == "High") return Raw::HdrMergeDeghostMode::High;
    return Raw::HdrMergeDeghostMode::Low;
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

std::string MaskCombineModeToString(MaskCombineMode mode) {
    switch (mode) {
        case MaskCombineMode::Add: return "Add";
        case MaskCombineMode::Subtract: return "Subtract";
        case MaskCombineMode::Intersect: return "Intersect";
        case MaskCombineMode::Exclude: return "Exclude";
    }
    return "Intersect";
}

MaskCombineMode MaskCombineModeFromString(const std::string& value) {
    if (value == "Add") return MaskCombineMode::Add;
    if (value == "Subtract") return MaskCombineMode::Subtract;
    if (value == "Exclude") return MaskCombineMode::Exclude;
    return MaskCombineMode::Intersect;
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
    nlohmann::json extraSampleRgb = nlohmann::json::array();
    for (int i = 0; i < 4; ++i) {
        extraSampleRgb.push_back({ settings.extraSampleRgb[i][0], settings.extraSampleRgb[i][1], settings.extraSampleRgb[i][2] });
    }
    return {
        { "low", settings.low },
        { "high", settings.high },
        { "softness", settings.softness },
        { "invert", settings.invert },
        { "sampleCount", settings.sampleCount },
        { "sampleRgb", { settings.sampleRgb[0], settings.sampleRgb[1], settings.sampleRgb[2] } },
        { "sampleLuma", settings.sampleLuma },
        { "extraSampleRgb", extraSampleRgb },
        { "extraSampleLuma", { settings.extraSampleLuma[0], settings.extraSampleLuma[1], settings.extraSampleLuma[2], settings.extraSampleLuma[3] } },
        { "sampleU", settings.sampleU },
        { "sampleV", settings.sampleV },
        { "toneSimilarity", settings.toneSimilarity },
        { "colorSimilarity", settings.colorSimilarity },
        { "regionRadius", settings.regionRadius },
        { "regionFeather", settings.regionFeather },
        { "edgeSensitivity", settings.edgeSensitivity },
        { "localCoherence", settings.localCoherence }
    };
}

ImageToMaskSettings DeserializeImageToMaskSettings(const nlohmann::json& value) {
    ImageToMaskSettings settings;
    if (!value.is_object()) return settings;
    settings.low = value.value("low", settings.low);
    settings.high = value.value("high", settings.high);
    settings.softness = value.value("softness", settings.softness);
    settings.invert = value.value("invert", settings.invert);
    settings.sampleCount = std::clamp(value.value("sampleCount", settings.sampleCount), 1, 5);
    if (value.contains("sampleRgb") && value["sampleRgb"].is_array() && value["sampleRgb"].size() >= 3) {
        settings.sampleRgb[0] = value["sampleRgb"][0].get<float>();
        settings.sampleRgb[1] = value["sampleRgb"][1].get<float>();
        settings.sampleRgb[2] = value["sampleRgb"][2].get<float>();
    }
    settings.sampleLuma = value.value("sampleLuma", settings.sampleLuma);
    if (value.contains("extraSampleRgb") && value["extraSampleRgb"].is_array()) {
        for (std::size_t i = 0; i < std::min<std::size_t>(4, value["extraSampleRgb"].size()); ++i) {
            const nlohmann::json& sample = value["extraSampleRgb"][i];
            if (!sample.is_array() || sample.size() < 3) {
                continue;
            }
            settings.extraSampleRgb[i][0] = sample[0].get<float>();
            settings.extraSampleRgb[i][1] = sample[1].get<float>();
            settings.extraSampleRgb[i][2] = sample[2].get<float>();
        }
    }
    if (value.contains("extraSampleLuma") && value["extraSampleLuma"].is_array()) {
        for (std::size_t i = 0; i < std::min<std::size_t>(4, value["extraSampleLuma"].size()); ++i) {
            settings.extraSampleLuma[i] = value["extraSampleLuma"][i].get<float>();
        }
    }
    settings.sampleU = value.value("sampleU", settings.sampleU);
    settings.sampleV = value.value("sampleV", settings.sampleV);
    settings.toneSimilarity = value.value("toneSimilarity", settings.toneSimilarity);
    settings.colorSimilarity = value.value("colorSimilarity", settings.colorSimilarity);
    settings.regionRadius = value.value("regionRadius", settings.regionRadius);
    settings.regionFeather = value.value("regionFeather", settings.regionFeather);
    settings.edgeSensitivity = value.value("edgeSensitivity", settings.edgeSensitivity);
    settings.localCoherence = value.value("localCoherence", settings.localCoherence);
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
        case MixBlendMode::Average: return "Average";
        case MixBlendMode::Add: return "Add";
        case MixBlendMode::Multiply: return "Multiply";
        case MixBlendMode::Screen: return "Screen";
        case MixBlendMode::AlphaOver: return "AlphaOver";
    }
    return "Normal";
}

MixBlendMode MixBlendModeFromString(const std::string& value) {
    if (value == "Average") return MixBlendMode::Average;
    if (value == "Add") return MixBlendMode::Add;
    if (value == "Multiply") return MixBlendMode::Multiply;
    if (value == "Screen") return MixBlendMode::Screen;
    if (value == "AlphaOver" || value == "Alpha Over") return MixBlendMode::AlphaOver;
    return MixBlendMode::Normal;
}

std::string DataMathModeToString(DataMathMode mode) {
    switch (mode) {
        case DataMathMode::Clamp: return "Clamp";
        case DataMathMode::Add: return "Add";
        case DataMathMode::Subtract: return "Subtract";
        case DataMathMode::Multiply: return "Multiply";
        case DataMathMode::Divide: return "Divide";
        case DataMathMode::Average: return "Average";
        case DataMathMode::Min: return "Min";
        case DataMathMode::Max: return "Max";
        case DataMathMode::Difference: return "Difference";
        case DataMathMode::Remap: return "Remap";
    }
    return "Clamp";
}

DataMathMode DataMathModeFromString(const std::string& value) {
    if (value == "Add") return DataMathMode::Add;
    if (value == "Subtract") return DataMathMode::Subtract;
    if (value == "Multiply") return DataMathMode::Multiply;
    if (value == "Divide") return DataMathMode::Divide;
    if (value == "Average") return DataMathMode::Average;
    if (value == "Min" || value == "Minimum") return DataMathMode::Min;
    if (value == "Max" || value == "Maximum") return DataMathMode::Max;
    if (value == "Difference" || value == "AbsDiff") return DataMathMode::Difference;
    if (value == "Remap") return DataMathMode::Remap;
    return DataMathMode::Clamp;
}

nlohmann::json SerializeDataMathSettings(const DataMathSettings& settings) {
    return {
        { "constantA", settings.constantA },
        { "constantB", settings.constantB },
        { "minValue", settings.minValue },
        { "maxValue", settings.maxValue },
        { "outMin", settings.outMin },
        { "outMax", settings.outMax }
    };
}

DataMathSettings DeserializeDataMathSettings(const nlohmann::json& value) {
    DataMathSettings settings;
    if (!value.is_object()) return settings;
    settings.constantA = value.value("constantA", settings.constantA);
    settings.constantB = value.value("constantB", settings.constantB);
    settings.minValue = value.value("minValue", settings.minValue);
    settings.maxValue = value.value("maxValue", settings.maxValue);
    settings.outMin = value.value("outMin", settings.outMin);
    settings.outMax = value.value("outMax", settings.outMax);
    return settings;
}

std::string ImageToMaskKindToString(ImageToMaskKind kind) {
    switch (kind) {
        case ImageToMaskKind::Luminance: return "Luminance";
        case ImageToMaskKind::SampledRange: return "SampledRange";
    }
    return "Luminance";
}

ImageToMaskKind ImageToMaskKindFromString(const std::string& value) {
    if (value == "SampledRange" || value == "Sampled Range") return ImageToMaskKind::SampledRange;
    return ImageToMaskKind::Luminance;
}

std::vector<unsigned char> ReadBinaryJson(const nlohmann::json& value) {
    if (!value.is_binary()) {
        return {};
    }

    const auto& binaryValue = value.get_binary();
    return std::vector<unsigned char>(binaryValue.begin(), binaryValue.end());
}

std::string CustomMaskReferenceModeToString(CustomMaskReferenceMode mode) {
    return mode == CustomMaskReferenceMode::GraphNode ? "GraphNode" : "CustomSize";
}

CustomMaskReferenceMode CustomMaskReferenceModeFromString(const std::string& value) {
    return value == "GraphNode" ? CustomMaskReferenceMode::GraphNode : CustomMaskReferenceMode::CustomSize;
}

std::string CustomMaskObjectTypeToString(CustomMaskObjectType type) {
    switch (type) {
        case CustomMaskObjectType::Rectangle: return "Rectangle";
        case CustomMaskObjectType::Ellipse: return "Ellipse";
        case CustomMaskObjectType::Polygon: return "Polygon";
        case CustomMaskObjectType::FreeformPath: return "FreeformPath";
    }
    return "Rectangle";
}

CustomMaskObjectType CustomMaskObjectTypeFromString(const std::string& value) {
    if (value == "Ellipse") return CustomMaskObjectType::Ellipse;
    if (value == "Polygon") return CustomMaskObjectType::Polygon;
    if (value == "FreeformPath") return CustomMaskObjectType::FreeformPath;
    return CustomMaskObjectType::Rectangle;
}

std::string CustomMaskOperationToString(CustomMaskOperation operation) {
    switch (operation) {
        case CustomMaskOperation::Add: return "Add";
        case CustomMaskOperation::Subtract: return "Subtract";
        case CustomMaskOperation::Intersect: return "Intersect";
        case CustomMaskOperation::Exclude: return "Exclude";
    }
    return "Add";
}

CustomMaskOperation CustomMaskOperationFromString(const std::string& value) {
    if (value == "Subtract") return CustomMaskOperation::Subtract;
    if (value == "Intersect") return CustomMaskOperation::Intersect;
    if (value == "Exclude") return CustomMaskOperation::Exclude;
    return CustomMaskOperation::Add;
}

std::string CustomMaskToolToString(CustomMaskTool tool) {
    switch (tool) {
        case CustomMaskTool::Brush: return "Brush";
        case CustomMaskTool::Erase: return "Erase";
        case CustomMaskTool::Select: return "Select";
        case CustomMaskTool::Rectangle: return "Rectangle";
        case CustomMaskTool::Ellipse: return "Ellipse";
        case CustomMaskTool::Polygon: return "Polygon";
        case CustomMaskTool::FreeformPath: return "FreeformPath";
    }
    return "Brush";
}

CustomMaskTool CustomMaskToolFromString(const std::string& value) {
    if (value == "Erase") return CustomMaskTool::Erase;
    if (value == "Select") return CustomMaskTool::Select;
    if (value == "Rectangle") return CustomMaskTool::Rectangle;
    if (value == "Ellipse") return CustomMaskTool::Ellipse;
    if (value == "Polygon") return CustomMaskTool::Polygon;
    if (value == "FreeformPath") return CustomMaskTool::FreeformPath;
    return CustomMaskTool::Brush;
}

std::vector<unsigned char> EncodeCustomMaskRasterU16(const std::vector<float>& raster) {
    std::vector<unsigned char> bytes;
    bytes.resize(raster.size() * 2);
    for (std::size_t i = 0; i < raster.size(); ++i) {
        const float clamped = std::clamp(raster[i], 0.0f, 1.0f);
        const auto value = static_cast<std::uint16_t>(std::lround(clamped * 65535.0f));
        bytes[i * 2 + 0] = static_cast<unsigned char>(value & 0xffu);
        bytes[i * 2 + 1] = static_cast<unsigned char>((value >> 8) & 0xffu);
    }
    return bytes;
}

std::vector<float> DecodeCustomMaskRasterU16(const std::vector<unsigned char>& bytes, int width, int height) {
    const std::size_t expected =
        static_cast<std::size_t>(std::max(0, width)) * static_cast<std::size_t>(std::max(0, height));
    std::vector<float> raster(expected, 0.0f);
    const std::size_t count = std::min(expected, bytes.size() / 2);
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint16_t value =
            static_cast<std::uint16_t>(bytes[i * 2 + 0]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[i * 2 + 1]) << 8);
        raster[i] = static_cast<float>(value) / 65535.0f;
    }
    return raster;
}

nlohmann::json SerializeCustomMaskObject(const CustomMaskObject& object) {
    nlohmann::json points = nlohmann::json::array();
    for (const Vec2& point : object.points) {
        points.push_back({ { "x", point.x }, { "y", point.y } });
    }

    return {
        { "id", object.id },
        { "type", CustomMaskObjectTypeToString(object.type) },
        { "operation", CustomMaskOperationToString(object.operation) },
        { "points", std::move(points) },
        { "enabled", object.enabled },
        { "invert", object.invert },
        { "strength", object.strength },
        { "feather", object.feather },
        { "blur", object.blur }
    };
}

CustomMaskObject DeserializeCustomMaskObject(const nlohmann::json& value) {
    CustomMaskObject object;
    if (!value.is_object()) {
        return object;
    }
    object.id = value.value("id", object.id);
    object.type = CustomMaskObjectTypeFromString(value.value("type", std::string("Rectangle")));
    object.operation = CustomMaskOperationFromString(value.value("operation", std::string("Add")));
    object.enabled = value.value("enabled", object.enabled);
    object.invert = value.value("invert", object.invert);
    object.strength = value.value("strength", object.strength);
    object.feather = value.value("feather", object.feather);
    object.blur = value.value("blur", object.blur);
    const bool legacyDefaultShapeFeather =
        value.contains("feather") &&
        std::abs(object.feather - 0.02f) <= 0.00001f &&
        (object.type == CustomMaskObjectType::Rectangle ||
         object.type == CustomMaskObjectType::Ellipse ||
         object.type == CustomMaskObjectType::Polygon) &&
        object.blur <= 0.00001f;
    if (legacyDefaultShapeFeather) {
        object.feather = 0.0f;
    }
    const nlohmann::json points = value.value("points", nlohmann::json::array());
    if (points.is_array()) {
        for (const nlohmann::json& pointJson : points) {
            if (!pointJson.is_object()) continue;
            Vec2 point;
            point.x = pointJson.value("x", 0.0f);
            point.y = pointJson.value("y", 0.0f);
            object.points.push_back(point);
        }
    }
    return object;
}

nlohmann::json SerializeCustomMaskPayload(const CustomMaskPayload& payload) {
    nlohmann::json objects = nlohmann::json::array();
    for (const CustomMaskObject& object : payload.objects) {
        objects.push_back(SerializeCustomMaskObject(object));
    }

    return {
        { "schemaVersion", payload.schemaVersion },
        { "referenceMode", CustomMaskReferenceModeToString(payload.referenceMode) },
        { "referenceNodeId", payload.referenceNodeId },
        { "referenceSocketId", payload.referenceSocketId },
        { "width", payload.width },
        { "height", payload.height },
        { "aspectLocked", payload.aspectLocked },
        { "rasterFormat", "u16le-normalized" },
        { "rasterLayer", nlohmann::json::binary(EncodeCustomMaskRasterU16(payload.rasterLayer)) },
        { "objects", std::move(objects) },
        { "nextObjectId", payload.nextObjectId },
        { "globalOps", {
            { "invert", payload.invert },
            { "blurRadius", payload.blurRadius },
            { "expandContract", payload.expandContract }
        } },
        { "editor", {
            { "activeTool", CustomMaskToolToString(payload.activeTool) },
            { "brushSize", payload.brushSize },
            { "brushSoftness", payload.brushSoftness },
            { "brushOpacity", payload.brushOpacity },
            { "showCanvasReferenceImage", payload.showCanvasReferenceImage },
            { "showCanvasMaskImpact", payload.showCanvasMaskImpact },
            { "showCanvasMaskStrength", payload.showCanvasMaskStrength },
            { "selectedObjectId", payload.selectedObjectId }
        } }
    };
}

CustomMaskPayload DeserializeCustomMaskPayload(const nlohmann::json& value) {
    CustomMaskPayload payload;
    if (!value.is_object()) {
        payload.rasterLayer.assign(
            static_cast<std::size_t>(payload.width) * static_cast<std::size_t>(payload.height),
            0.0f);
        return payload;
    }

    payload.schemaVersion = value.value("schemaVersion", payload.schemaVersion);
    payload.referenceMode = CustomMaskReferenceModeFromString(value.value("referenceMode", std::string("CustomSize")));
    payload.referenceNodeId = value.value("referenceNodeId", payload.referenceNodeId);
    payload.referenceSocketId = value.value("referenceSocketId", payload.referenceSocketId);
    payload.width = std::clamp(value.value("width", payload.width), 1, 8192);
    payload.height = std::clamp(value.value("height", payload.height), 1, 8192);
    payload.aspectLocked = value.value("aspectLocked", payload.aspectLocked);
    payload.rasterLayer = DecodeCustomMaskRasterU16(ReadBinaryJson(value.value("rasterLayer", nlohmann::json())), payload.width, payload.height);

    const nlohmann::json objects = value.value("objects", nlohmann::json::array());
    if (objects.is_array()) {
        for (const nlohmann::json& objectJson : objects) {
            payload.objects.push_back(DeserializeCustomMaskObject(objectJson));
        }
    }
    payload.nextObjectId = value.value("nextObjectId", payload.nextObjectId);
    for (const CustomMaskObject& object : payload.objects) {
        payload.nextObjectId = std::max(payload.nextObjectId, object.id + 1);
    }

    const nlohmann::json globalOps = value.value("globalOps", nlohmann::json::object());
    if (globalOps.is_object()) {
        payload.invert = globalOps.value("invert", payload.invert);
        payload.blurRadius = globalOps.value("blurRadius", payload.blurRadius);
        payload.expandContract = globalOps.value("expandContract", payload.expandContract);
    }

    const nlohmann::json editor = value.value("editor", nlohmann::json::object());
    if (editor.is_object()) {
        payload.activeTool = CustomMaskToolFromString(editor.value("activeTool", std::string("Brush")));
        payload.brushSize = editor.value("brushSize", payload.brushSize);
        payload.brushSoftness = editor.value("brushSoftness", payload.brushSoftness);
        payload.brushOpacity = editor.value("brushOpacity", payload.brushOpacity);
        payload.showCanvasReferenceImage = editor.value("showCanvasReferenceImage", payload.showCanvasReferenceImage);
        payload.showCanvasMaskImpact = editor.value("showCanvasMaskImpact", payload.showCanvasMaskImpact);
        payload.showCanvasMaskStrength = editor.value("showCanvasMaskStrength", payload.showCanvasMaskStrength);
        payload.selectedObjectId = editor.value("selectedObjectId", payload.selectedObjectId);
    }

    const std::size_t expected =
        static_cast<std::size_t>(payload.width) * static_cast<std::size_t>(payload.height);
    if (payload.rasterLayer.size() != expected) {
        payload.rasterLayer.assign(expected, 0.0f);
    }
    return payload;
}

nlohmann::json SerializeDevelopSubjectImportanceRegion(const DevelopSubjectImportanceRegion& region) {
    return {
        { "id", region.id },
        { "mode", DevelopSubjectImportanceModeStableString(region.mode) },
        { "enabled", region.enabled },
        { "centerX", region.centerX },
        { "centerY", region.centerY },
        { "radiusX", region.radiusX },
        { "radiusY", region.radiusY },
        { "feather", region.feather },
        { "strength", region.strength }
    };
}

DevelopSubjectImportanceRegion DeserializeDevelopSubjectImportanceRegion(const nlohmann::json& value) {
    DevelopSubjectImportanceRegion region;
    if (!value.is_object()) {
        return region;
    }
    region.id = value.value("id", region.id);
    region.mode = DevelopSubjectImportanceModeFromStableString(
        value.value("mode", std::string("Important")));
    region.enabled = value.value("enabled", region.enabled);
    region.centerX = std::clamp(value.value("centerX", region.centerX), 0.0f, 1.0f);
    region.centerY = std::clamp(value.value("centerY", region.centerY), 0.0f, 1.0f);
    region.radiusX = std::clamp(value.value("radiusX", region.radiusX), 0.01f, 1.0f);
    region.radiusY = std::clamp(value.value("radiusY", region.radiusY), 0.01f, 1.0f);
    region.feather = std::clamp(value.value("feather", region.feather), 0.0f, 1.0f);
    region.strength = std::clamp(value.value("strength", region.strength), 0.0f, 1.0f);
    return region;
}

nlohmann::json SerializeDevelopSubjectImportanceStrokePoint(const DevelopSubjectImportanceStrokePoint& point) {
    return {
        { "x", point.x },
        { "y", point.y }
    };
}

DevelopSubjectImportanceStrokePoint DeserializeDevelopSubjectImportanceStrokePoint(const nlohmann::json& value) {
    DevelopSubjectImportanceStrokePoint point;
    if (!value.is_object()) {
        return point;
    }
    point.x = std::clamp(value.value("x", point.x), 0.0f, 1.0f);
    point.y = std::clamp(value.value("y", point.y), 0.0f, 1.0f);
    return point;
}

nlohmann::json SerializeDevelopSubjectImportanceStroke(const DevelopSubjectImportanceStroke& stroke) {
    nlohmann::json points = nlohmann::json::array();
    for (const DevelopSubjectImportanceStrokePoint& point : stroke.points) {
        points.push_back(SerializeDevelopSubjectImportanceStrokePoint(point));
    }
    return {
        { "id", stroke.id },
        { "mode", DevelopSubjectImportanceModeStableString(stroke.mode) },
        { "enabled", stroke.enabled },
        { "subtract", stroke.subtract },
        { "radius", stroke.radius },
        { "feather", stroke.feather },
        { "strength", stroke.strength },
        { "points", std::move(points) }
    };
}

DevelopSubjectImportanceStroke DeserializeDevelopSubjectImportanceStroke(const nlohmann::json& value) {
    DevelopSubjectImportanceStroke stroke;
    if (!value.is_object()) {
        return stroke;
    }
    stroke.id = value.value("id", stroke.id);
    stroke.mode = DevelopSubjectImportanceModeFromStableString(
        value.value("mode", std::string("Important")));
    stroke.enabled = value.value("enabled", stroke.enabled);
    stroke.subtract = value.value("subtract", stroke.subtract);
    stroke.radius = std::clamp(value.value("radius", stroke.radius), 0.005f, 0.25f);
    stroke.feather = std::clamp(value.value("feather", stroke.feather), 0.0f, 1.0f);
    stroke.strength = std::clamp(value.value("strength", stroke.strength), 0.0f, 1.0f);
    const nlohmann::json points = value.value("points", nlohmann::json::array());
    if (points.is_array()) {
        for (const nlohmann::json& pointJson : points) {
            stroke.points.push_back(DeserializeDevelopSubjectImportanceStrokePoint(pointJson));
            if (stroke.points.size() >= 192) {
                break;
            }
        }
    }
    return stroke;
}

nlohmann::json SerializeDevelopSubjectImportanceMap(const DevelopSubjectImportanceMap& map) {
    nlohmann::json regions = nlohmann::json::array();
    for (const DevelopSubjectImportanceRegion& region : map.regions) {
        regions.push_back(SerializeDevelopSubjectImportanceRegion(region));
    }
    nlohmann::json strokes = nlohmann::json::array();
    for (const DevelopSubjectImportanceStroke& stroke : map.strokes) {
        strokes.push_back(SerializeDevelopSubjectImportanceStroke(stroke));
    }
    return {
        { "schemaVersion", map.schemaVersion },
        { "enabled", map.enabled },
        { "showOverlay", map.showOverlay },
        { "overlayOpacity", map.overlayOpacity },
        { "showInterpretedMapOverlay", map.showInterpretedMapOverlay },
        { "interpretedMapOpacity", map.interpretedMapOpacity },
        { "showRefinedMapOverlay", map.showRefinedMapOverlay },
        { "refinedMapOpacity", map.refinedMapOpacity },
        { "brushEnabled", map.brushEnabled },
        { "brushSubtract", map.brushSubtract },
        { "brushMode", DevelopSubjectImportanceModeStableString(map.brushMode) },
        { "brushRadius", map.brushRadius },
        { "brushFeather", map.brushFeather },
        { "brushStrength", map.brushStrength },
        { "activeRegionId", map.activeRegionId },
        { "activeStrokeId", map.activeStrokeId },
        { "nextRegionId", map.nextRegionId },
        { "nextStrokeId", map.nextStrokeId },
        { "regions", std::move(regions) },
        { "strokes", std::move(strokes) }
    };
}

DevelopSubjectImportanceMap DeserializeDevelopSubjectImportanceMap(const nlohmann::json& value) {
    DevelopSubjectImportanceMap map;
    if (!value.is_object()) {
        return map;
    }
    map.schemaVersion = std::max(1, value.value("schemaVersion", map.schemaVersion));
    map.enabled = value.value("enabled", map.enabled);
    map.showOverlay = value.value("showOverlay", map.showOverlay);
    map.overlayOpacity = std::clamp(value.value("overlayOpacity", map.overlayOpacity), 0.05f, 1.0f);
    map.showInterpretedMapOverlay =
        value.value("showInterpretedMapOverlay", map.showInterpretedMapOverlay);
    map.interpretedMapOpacity =
        std::clamp(value.value("interpretedMapOpacity", map.interpretedMapOpacity), 0.05f, 1.0f);
    map.showRefinedMapOverlay =
        value.value("showRefinedMapOverlay", map.showRefinedMapOverlay);
    map.refinedMapOpacity =
        std::clamp(value.value("refinedMapOpacity", map.refinedMapOpacity), 0.05f, 1.0f);
    map.brushEnabled = value.value("brushEnabled", map.brushEnabled);
    map.brushSubtract = value.value("brushSubtract", map.brushSubtract);
    map.brushMode = DevelopSubjectImportanceModeFromStableString(
        value.value("brushMode", std::string("Important")));
    map.brushRadius = std::clamp(value.value("brushRadius", map.brushRadius), 0.005f, 0.25f);
    map.brushFeather = std::clamp(value.value("brushFeather", map.brushFeather), 0.0f, 1.0f);
    map.brushStrength = std::clamp(value.value("brushStrength", map.brushStrength), 0.0f, 1.0f);
    map.activeRegionId = std::max(0, value.value("activeRegionId", map.activeRegionId));
    map.activeStrokeId = std::max(0, value.value("activeStrokeId", map.activeStrokeId));
    map.nextRegionId = std::max(1, value.value("nextRegionId", map.nextRegionId));
    map.nextStrokeId = std::max(1, value.value("nextStrokeId", map.nextStrokeId));
    const nlohmann::json regions = value.value("regions", nlohmann::json::array());
    if (regions.is_array()) {
        for (const nlohmann::json& regionJson : regions) {
            map.regions.push_back(DeserializeDevelopSubjectImportanceRegion(regionJson));
            if (map.regions.size() >= 32) {
                break;
            }
        }
    }
    for (const DevelopSubjectImportanceRegion& region : map.regions) {
        map.nextRegionId = std::max(map.nextRegionId, region.id + 1);
    }
    const nlohmann::json strokes = value.value("strokes", nlohmann::json::array());
    if (strokes.is_array()) {
        for (const nlohmann::json& strokeJson : strokes) {
            map.strokes.push_back(DeserializeDevelopSubjectImportanceStroke(strokeJson));
            if (map.strokes.size() >= 128) {
                break;
            }
        }
    }
    for (const DevelopSubjectImportanceStroke& stroke : map.strokes) {
        map.nextStrokeId = std::max(map.nextStrokeId, stroke.id + 1);
    }
    if (map.regions.empty()) {
        map.activeRegionId = 0;
    } else {
        const auto activeIt = std::find_if(
            map.regions.begin(),
            map.regions.end(),
            [&](const DevelopSubjectImportanceRegion& region) {
                return region.id == map.activeRegionId;
            });
        if (activeIt == map.regions.end()) {
            map.activeRegionId = map.regions.front().id;
        }
    }
    if (map.strokes.empty()) {
        map.activeStrokeId = 0;
    } else {
        const auto activeIt = std::find_if(
            map.strokes.begin(),
            map.strokes.end(),
            [&](const DevelopSubjectImportanceStroke& stroke) {
                return stroke.id == map.activeStrokeId;
            });
        if (activeIt == map.strokes.end()) {
            map.activeStrokeId = map.strokes.back().id;
        }
    }
    return map;
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
        item["maskCombineMode"] = MaskCombineModeToString(node.maskCombineMode);
        item["maskUtilityKind"] = MaskUtilityKindToString(node.maskUtilityKind);
        item["maskUtilitySettings"] = SerializeMaskUtilitySettings(node.maskUtilitySettings);
        item["imageToMaskKind"] = ImageToMaskKindToString(node.imageToMaskKind);
        item["imageToMaskSettings"] = SerializeImageToMaskSettings(node.imageToMaskSettings);
        item["imageGeneratorKind"] = ImageGeneratorKindToString(node.imageGeneratorKind);
        item["imageGeneratorSettings"] = SerializeImageGeneratorSettings(node.imageGeneratorSettings);
        item["mixBlendMode"] = MixBlendModeToString(node.mixBlendMode);
        item["mixFactor"] = node.mixFactor;
        item["dataMathMode"] = DataMathModeToString(node.dataMathMode);
        item["dataMathSettings"] = SerializeDataMathSettings(node.dataMathSettings);
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
        } else if (node.kind == NodeKind::RawDecode) {
            item["rawSettings"] = SerializeRawSettings(node.rawDecode.settings);
        } else if (node.kind == NodeKind::RawDevelop) {
            item["rawSettings"] = SerializeRawSettings(node.rawDevelop.settings);
            item["scenePrepEnabled"] = node.rawDevelop.scenePrepEnabled;
            item["scenePrepSettings"] = SerializeRawDetailFusionSettings(node.rawDevelop.scenePrepSettings);
            item["integratedToneEnabled"] = node.rawDevelop.integratedToneEnabled;
            item["integratedToneLayer"] = node.rawDevelop.integratedToneLayerJson;
            item["developSubjectImportance"] =
                SerializeDevelopSubjectImportanceMap(node.rawDevelop.subjectImportance);
            item["developAutoGuidance"] = {
                { "autoIntent", EditorNodeGraph::DevelopAutoIntentStableString(node.rawDevelop.autoGuidance.intent) },
                { "autoStrength", node.rawDevelop.autoGuidance.autoStrength },
                { "exposureBias", node.rawDevelop.autoGuidance.exposureBias },
                { "dynamicRange", node.rawDevelop.autoGuidance.dynamicRange },
                { "shadowLift", node.rawDevelop.autoGuidance.shadowLift },
                { "highlightGuard", node.rawDevelop.autoGuidance.highlightGuard },
                { "highlightCharacter", node.rawDevelop.autoGuidance.highlightCharacter },
                { "contrastBias", node.rawDevelop.autoGuidance.contrastBias },
                { "subjectSceneBias", node.rawDevelop.autoGuidance.subjectSceneBias },
                { "moodReadabilityBias", node.rawDevelop.autoGuidance.moodReadabilityBias }
            };
            item["uiMode"] =
                node.rawDevelop.uiMode == EditorNodeGraph::RawDevelopUiMode::Manual ? "Manual" : "Auto";
        } else if (node.kind == NodeKind::RawDetailAutoMask) {
            item["rawDetailAutoMaskSettings"] = SerializeRawDetailFusionSettings(node.rawDetailAutoMask.settings);
        } else if (node.kind == NodeKind::RawDetailFusion) {
            item["rawDetailFusionSettings"] = SerializeRawDetailFusionSettings(node.rawDetailFusion.settings);
        } else if (node.kind == NodeKind::HdrMerge) {
            item["hdrMergeSettings"] = SerializeHdrMergeSettings(node.hdrMerge.settings);
        } else if (node.kind == NodeKind::Lut) {
            item["lut"] = SerializeLutPayload(node.lut);
        } else if (node.kind == NodeKind::CustomMask) {
            item["customMask"] = SerializeCustomMaskPayload(node.customMask);
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
                InvalidateImagePayloadRuntime(imageNode->image);
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
        } else if (kind == "RawDecode") {
            node.kind = NodeKind::RawDecode;
            node.rawDecode.settings = DeserializeRawSettings(item.value("rawSettings", nlohmann::json::object()));
            if (node.title.empty() || node.title == "RAW Decode") node.title = "RAW Decode";
        } else if (kind == "RawDevelop") {
            node.kind = NodeKind::RawDevelop;
            node.rawDevelop.settings = DeserializeRawSettings(item.value("rawSettings", nlohmann::json::object()));
            node.rawDevelop.scenePrepEnabled = item.value("scenePrepEnabled", node.rawDevelop.scenePrepEnabled);
            node.rawDevelop.scenePrepSettings = DeserializeRawDetailFusionSettings(item.value("scenePrepSettings", nlohmann::json::object()));
            node.rawDevelop.integratedToneEnabled = item.value("integratedToneEnabled", true);
            node.rawDevelop.integratedToneLayerJson = item.value("integratedToneLayer", nlohmann::json::object());
            node.rawDevelop.subjectImportance =
                DeserializeDevelopSubjectImportanceMap(
                    item.value("developSubjectImportance", nlohmann::json::object()));
            const nlohmann::json autoGuidance = item.value("developAutoGuidance", nlohmann::json::object());
            node.rawDevelop.autoGuidance.intent = EditorNodeGraph::DevelopAutoIntentFromStableString(
                autoGuidance.value("autoIntent", std::string("NaturalFinished")));
            node.rawDevelop.autoGuidance.autoStrength = autoGuidance.value("autoStrength", node.rawDevelop.autoGuidance.autoStrength);
            node.rawDevelop.autoGuidance.exposureBias = autoGuidance.value("exposureBias", node.rawDevelop.autoGuidance.exposureBias);
            node.rawDevelop.autoGuidance.dynamicRange = autoGuidance.value("dynamicRange", node.rawDevelop.autoGuidance.dynamicRange);
            node.rawDevelop.autoGuidance.shadowLift = autoGuidance.value("shadowLift", node.rawDevelop.autoGuidance.shadowLift);
            node.rawDevelop.autoGuidance.highlightGuard = autoGuidance.value("highlightGuard", node.rawDevelop.autoGuidance.highlightGuard);
            node.rawDevelop.autoGuidance.highlightCharacter = autoGuidance.value("highlightCharacter", node.rawDevelop.autoGuidance.highlightCharacter);
            node.rawDevelop.autoGuidance.contrastBias = autoGuidance.value("contrastBias", node.rawDevelop.autoGuidance.contrastBias);
            node.rawDevelop.autoGuidance.subjectSceneBias = autoGuidance.value("subjectSceneBias", node.rawDevelop.autoGuidance.subjectSceneBias);
            node.rawDevelop.autoGuidance.moodReadabilityBias = autoGuidance.value("moodReadabilityBias", node.rawDevelop.autoGuidance.moodReadabilityBias);
            const std::string uiMode = item.value("uiMode", std::string("Auto"));
            node.rawDevelop.uiMode =
                (uiMode == "Manual" || uiMode == "Advanced")
                    ? EditorNodeGraph::RawDevelopUiMode::Manual
                    : EditorNodeGraph::RawDevelopUiMode::Auto;
            if (node.title.empty() || node.title == "RAW Develop") node.title = "Develop";
        } else if (kind == "RawDetailAutoMask") {
            node.kind = NodeKind::RawDetailAutoMask;
            node.rawDetailAutoMask.settings = DeserializeRawDetailFusionSettings(item.value("rawDetailAutoMaskSettings", nlohmann::json::object()));
            if (node.title.empty()) node.title = "RAW Detail Auto Mask";
        } else if (kind == "RawDetailFusion") {
            node.kind = NodeKind::RawDetailFusion;
            node.rawDetailFusion.settings = DeserializeRawDetailFusionSettings(item.value("rawDetailFusionSettings", nlohmann::json::object()));
            if (node.title.empty() || node.title == "RAW Detail Fusion" || node.title == "Auto Gain") node.title = "Pre-Local Exposure";
        } else if (kind == "HdrMerge") {
            node.kind = NodeKind::HdrMerge;
            node.hdrMerge.settings = DeserializeHdrMergeSettings(item.value("hdrMergeSettings", nlohmann::json::object()));
            if (node.title.empty()) node.title = "HDR Merge";
        } else if (kind == "Lut" || kind == "LUT") {
            node.kind = NodeKind::Lut;
            node.lut = DeserializeLutPayload(item.value("lut", nlohmann::json::object()));
            if (node.title.empty()) node.title = "LUT";
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
        } else if (kind == "MaskCombine") {
            node.kind = NodeKind::MaskCombine;
            node.maskCombineMode = MaskCombineModeFromString(item.value("maskCombineMode", std::string("Intersect")));
            if (node.title.empty() ||
                node.title == "Add Mask" ||
                node.title == "Subtract Mask" ||
                node.title == "Intersect Mask" ||
                node.title == "Exclude Mask" ||
                node.title == "Add Scalars" ||
                node.title == "Subtract Scalars" ||
                node.title == "Intersect Scalars" ||
                node.title == "Difference Scalars" ||
                node.title == "Difference Mask") {
                EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
            }
        } else if (kind == "CustomMask") {
            node.kind = NodeKind::CustomMask;
            node.customMask = DeserializeCustomMaskPayload(item.value("customMask", nlohmann::json::object()));
            if (node.title.empty()) node.title = "Custom Mask";
        } else if (kind == "Mix") {
            node.kind = NodeKind::Mix;
            node.mixBlendMode = MixBlendModeFromString(item.value("mixBlendMode", std::string("Normal")));
            node.mixFactor = item.value("mixFactor", 0.5f);
            if (node.title.empty()) {
                node.title = "Blend Images";
            }
        } else if (kind == "DataMath") {
            node.kind = NodeKind::DataMath;
            node.dataMathMode = DataMathModeFromString(item.value("dataMathMode", std::string("Clamp")));
            node.dataMathSettings = DeserializeDataMathSettings(item.value("dataMathSettings", nlohmann::json::object()));
            if (node.title.empty() ||
                node.title == "Clamp Data" ||
                node.title == "Add Data" ||
                node.title == "Subtract Data" ||
                node.title == "Multiply Data" ||
                node.title == "Divide Data" ||
                node.title == "Average Data" ||
                node.title == "Minimum Data" ||
                node.title == "Maximum Data" ||
                node.title == "Difference Data" ||
                node.title == "Remap Data") {
                EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
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
            if (node.title.empty() ||
                node.title == "Invert Mask" ||
                node.title == "Levels Mask" ||
                node.title == "Threshold Mask" ||
                node.title == "Invert Scalar" ||
                node.title == "Remap Scalar" ||
                node.title == "Threshold Scalar" ||
                node.title == "Remap Mask") {
                EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
            }
        } else if (kind == "ImageToMask") {
            node.kind = NodeKind::ImageToMask;
            node.imageToMaskKind = ImageToMaskKindFromString(item.value("imageToMaskKind", std::string("Luminance")));
            node.imageToMaskSettings = DeserializeImageToMaskSettings(item.value("imageToMaskSettings", nlohmann::json::object()));
            if (node.title.empty() ||
                node.title == "Luminance Mask" ||
                node.title == "Sampled Range Mask" ||
                node.title == "Image To Scalar" ||
                node.title == "Sampled Range Scalar" ||
                node.title == "Image To Mask") {
                EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
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
