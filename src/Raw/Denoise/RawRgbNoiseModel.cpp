#include "Raw/Denoise/RawRgbNoiseModel.h"

#include "Raw/RawGpuPipeline.h"
#include "Raw/RawProcessingMath.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Raw::Denoise {
namespace {

std::array<float, 9> IdentityMatrix3() {
    return {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
}

bool InvertMatrix3(
    const std::array<float, 9>& matrix,
    std::array<float, 9>& inverse) {
    const double a = matrix[0];
    const double b = matrix[1];
    const double c = matrix[2];
    const double d = matrix[3];
    const double e = matrix[4];
    const double f = matrix[5];
    const double g = matrix[6];
    const double h = matrix[7];
    const double i = matrix[8];
    const double determinant =
        a * (e * i - f * h) -
        b * (d * i - f * g) +
        c * (d * h - e * g);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1.0e-12) {
        return false;
    }
    const double reciprocal = 1.0 / determinant;
    inverse = {
        static_cast<float>((e * i - f * h) * reciprocal),
        static_cast<float>((c * h - b * i) * reciprocal),
        static_cast<float>((b * f - c * e) * reciprocal),
        static_cast<float>((f * g - d * i) * reciprocal),
        static_cast<float>((a * i - c * g) * reciprocal),
        static_cast<float>((c * d - a * f) * reciprocal),
        static_cast<float>((d * h - e * g) * reciprocal),
        static_cast<float>((b * g - a * h) * reciprocal),
        static_cast<float>((a * e - b * d) * reciprocal)
    };
    return true;
}

std::array<float, 3> Multiply(
    const std::array<float, 9>& matrix,
    const std::array<float, 3>& value) {
    return {
        matrix[0] * value[0] + matrix[1] * value[1] + matrix[2] * value[2],
        matrix[3] * value[0] + matrix[4] * value[1] + matrix[5] * value[2],
        matrix[6] * value[0] + matrix[7] * value[1] + matrix[8] * value[2]
    };
}

std::array<float, 3> WorkingSpaceLumaWeights(RawWorkingSpace workingSpace) {
    return workingSpace == RawWorkingSpace::LinearSrgbD65
        ? std::array<float, 3> { 0.2126f, 0.7152f, 0.0722f }
        : std::array<float, 3> { 0.2627f, 0.6780f, 0.0593f };
}

} // namespace

RawRgbNoiseModel BuildRawRgbNoiseModel(
    const RawImageData& raw,
    const RawDevelopSettings& settings) {
    RawRgbNoiseModel result;
    if (raw.reconstructedCameraRgb || raw.normalizedMosaicInputContract !=
        NormalizedMosaicInputContract::None) {
        // Multi-frame virtual mosaics carry spatial variance sidecars. A
        // single-camera DNG profile is not a truthful substitute after
        // fusion, so the RGB stage uses its blind local estimate for now.
        return result;
    }
    const RawMetadata& metadata = raw.metadata;
    std::array<DngNoiseProfilePlane, 3> profile {};
    if (!Processing::ResolveDngNoiseProfile(metadata, profile)) {
        return result;
    }

    const std::array<float, 3> whiteBalance =
        ResolveRawWhiteBalance(metadata, settings);
    std::array<float, 9> cameraToWorking =
        settings.cameraTransformEnabled && !settings.debugBypassCameraTransform
            ? BuildRawCameraToWorkingTransform(
                  metadata,
                  settings,
                  metadata.pixelLayout != RawPixelLayout::LinearRgb)
            : IdentityMatrix3();
    if (settings.debugTransposeCameraMatrix) {
        std::swap(cameraToWorking[1], cameraToWorking[3]);
        std::swap(cameraToWorking[2], cameraToWorking[6]);
        std::swap(cameraToWorking[5], cameraToWorking[7]);
    }
    const float baselineExposure =
        settings.applyBaselineExposure && metadata.hasDngBaselineExposure
            ? metadata.dngBaselineExposure
            : 0.0f;
    const float exposure = std::exp2(
        std::clamp(settings.exposureStops + baselineExposure, -24.0f, 24.0f));

    // A maps normalized camera planes to the exact neutral scene-linear RGB
    // domain consumed by the RGB denoiser.
    std::array<float, 9> cameraToScene {};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            cameraToScene[static_cast<std::size_t>(row * 3 + column)] =
                exposure *
                cameraToWorking[static_cast<std::size_t>(row * 3 + column)] *
                std::max(0.001f, whiteBalance[static_cast<std::size_t>(column)]);
        }
    }

    std::array<float, 9> sceneToCamera {};
    if (!InvertMatrix3(cameraToScene, sceneToCamera)) {
        return result;
    }
    const std::array<float, 3> cameraSignalAtUnitGray =
        Multiply(sceneToCamera, { 1.0f, 1.0f, 1.0f });
    const std::array<float, 3> luma =
        WorkingSpaceLumaWeights(settings.workingSpace);
    const std::array<std::array<float, 3>, 3> opponentRows {{
        { luma[0], luma[1], luma[2] },
        { -luma[0], -luma[1], 1.0f - luma[2] },
        { 1.0f - luma[0], -luma[1], -luma[2] }
    }};

    for (int component = 0; component < 3; ++component) {
        double shot = 0.0;
        double read = 0.0;
        for (int plane = 0; plane < 3; ++plane) {
            double transformedWeight = 0.0;
            for (int channel = 0; channel < 3; ++channel) {
                transformedWeight +=
                    static_cast<double>(opponentRows[component][channel]) *
                    static_cast<double>(cameraToScene[channel * 3 + plane]);
            }
            const double weightSquared = transformedWeight * transformedWeight;
            const double neutralCameraSignal = std::max(
                0.0,
                static_cast<double>(cameraSignalAtUnitGray[plane]));
            shot += weightSquared * profile[plane].shotScale * neutralCameraSignal;
            read += weightSquared * profile[plane].readNoiseVariance;
        }
        if (!std::isfinite(shot) || !std::isfinite(read) ||
            shot < 0.0 || read < 0.0) {
            return RawRgbNoiseModel{};
        }
        result.shotScale[component] = static_cast<float>(shot);
        result.readNoiseVariance[component] = static_cast<float>(read);
    }
    result.profiled = true;
    return result;
}

} // namespace Raw::Denoise
