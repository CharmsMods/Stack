#include "RawImageData.h"

#include <algorithm>

namespace Raw {

namespace {

bool OrientationSwapsDimensions(int orientation) {
    return orientation == 5 || orientation == 6 || orientation == 7 || orientation == 8;
}

int VisibleWidthOrRaw(const RawMetadata& metadata) {
    return metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth;
}

int VisibleHeightOrRaw(const RawMetadata& metadata) {
    return metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight;
}

} // namespace

const char* CfaPatternName(CfaPattern pattern) {
    switch (pattern) {
        case CfaPattern::RGGB: return "RGGB";
        case CfaPattern::BGGR: return "BGGR";
        case CfaPattern::GBRG: return "GBRG";
        case CfaPattern::GRBG: return "GRBG";
        case CfaPattern::Unknown:
        default:
            return "Unknown";
    }
}

const char* WhiteBalanceModeName(WhiteBalanceMode mode) {
    switch (mode) {
        case WhiteBalanceMode::AsShot: return "As Shot";
        case WhiteBalanceMode::Auto: return "Auto";
        case WhiteBalanceMode::Neutral: return "Neutral";
        case WhiteBalanceMode::Manual: return "Manual";
    }
    return "As Shot";
}

const char* DemosaicMethodName(DemosaicMethod method) {
    switch (method) {
        case DemosaicMethod::Bilinear: return "Fast / Bilinear";
        case DemosaicMethod::QualityPlaceholder: return "Quality / Edge-Aware";
    }
    return "Fast / Bilinear";
}

const char* RawPixelLayoutName(RawPixelLayout layout) {
    switch (layout) {
        case RawPixelLayout::MosaicBayer: return "Mosaic RAW";
        case RawPixelLayout::LinearRgb: return "Linear RGB";
        case RawPixelLayout::Unknown:
        default:
            return "Unknown";
    }
}

const char* RawSampleFormatName(RawSampleFormat format) {
    switch (format) {
        case RawSampleFormat::UInt16: return "UInt16";
        case RawSampleFormat::Float32: return "Float32";
        case RawSampleFormat::Unknown:
        default:
            return "Unknown";
    }
}

const char* RawDebugViewName(RawDebugView view) {
    switch (view) {
        case RawDebugView::FinalOutput: return "Final Output";
        case RawDebugView::NormalizedMosaic: return "Normalized Mosaic";
        case RawDebugView::CfaFalseColor: return "CFA False Color";
        case RawDebugView::DemosaicedCameraRgb: return "Demosaiced Camera RGB";
        case RawDebugView::WhiteBalancedCameraRgb: return "White Balanced RGB";
        case RawDebugView::CameraTransformedRgb: return "Camera Transformed RGB";
        case RawDebugView::ClippedRawChannels: return "Clipped RAW Channels";
        case RawDebugView::PreDenoiseMosaic: return "Pre-Denoise Mosaic";
        case RawDebugView::PostDenoiseMosaic: return "Post-Denoise Mosaic";
        case RawDebugView::HotPixelMask: return "Hot Pixel Mask";
        case RawDebugView::DenoiseDifference: return "Denoise Difference";
        case RawDebugView::FalseColorMask: return "False Color Mask";
        case RawDebugView::DefringeMask: return "Defringe Mask";
        case RawDebugView::HighlightEdgeMask: return "Highlight Edge Mask";
    }
    return "Final Output";
}

const char* RawCameraTransformSourceName(RawCameraTransformSource source) {
    switch (source) {
        case RawCameraTransformSource::LibRawRgbCam: return "LibRaw rgb_cam";
        case RawCameraTransformSource::DngAuto: return "DNG Auto";
        case RawCameraTransformSource::DngForwardMatrix1: return "DNG ForwardMatrix 1";
        case RawCameraTransformSource::DngForwardMatrix2: return "DNG ForwardMatrix 2";
        case RawCameraTransformSource::DngColorMatrixInverse: return "DNG ColorMatrix inverse";
    }
    return "LibRaw rgb_cam";
}

const char* HighlightReconstructionModeName(HighlightReconstructionMode mode) {
    switch (mode) {
        case HighlightReconstructionMode::Off: return "Off";
        case HighlightReconstructionMode::ClipNeutral: return "Clip / Neutral";
        case HighlightReconstructionMode::Luminance: return "Luminance";
        case HighlightReconstructionMode::ColorReconstruction: return "Color Reconstruction";
    }
    return "Off";
}

int DisplayWidth(const RawMetadata& metadata) {
    return std::max(1, OrientationSwapsDimensions(metadata.orientation)
        ? VisibleHeightOrRaw(metadata)
        : VisibleWidthOrRaw(metadata));
}

int DisplayHeight(const RawMetadata& metadata) {
    return std::max(1, OrientationSwapsDimensions(metadata.orientation)
        ? VisibleWidthOrRaw(metadata)
        : VisibleHeightOrRaw(metadata));
}

} // namespace Raw
