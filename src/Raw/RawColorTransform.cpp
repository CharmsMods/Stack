#include "RawColorTransform.h"
#include <algorithm>
#include <cmath>

namespace Raw {
namespace {
std::array<float, 3> ResolveWhiteBalance(const RawMetadata& metadata, const RawDevelopSettings& settings) {
    if (settings.whiteBalanceMode == WhiteBalanceMode::Manual) {
        return settings.manualWhiteBalance;
    }
    if (settings.whiteBalanceMode == WhiteBalanceMode::Neutral) {
        return { 1.0f, 1.0f, 1.0f };
    }
    if (settings.whiteBalanceMode == WhiteBalanceMode::Auto) {
        std::array<float, 3> wb {
            std::max(0.001f, metadata.daylightWhiteBalance[0]),
            std::max(0.001f, metadata.daylightWhiteBalance[1]),
            std::max(0.001f, metadata.daylightWhiteBalance[2])
        };
        const float green = std::max(0.001f, wb[1]);
        return { wb[0] / green, 1.0f, wb[2] / green };
    }
    std::array<float, 3> wb {
        std::max(0.001f, metadata.cameraWhiteBalance[0]),
        std::max(0.001f, metadata.cameraWhiteBalance[1]),
        std::max(0.001f, metadata.cameraWhiteBalance[2])
    };
    const float green = std::max(0.001f, wb[1]);
    return { wb[0] / green, 1.0f, wb[2] / green };
}

std::array<float, 9> IdentityMatrix3() {
    return {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
}

std::array<float, 9> MultiplyMatrix3(const std::array<float, 9>& a, const std::array<float, 9>& b) {
    std::array<float, 9> result {};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            float v = 0.0f;
            for (int k = 0; k < 3; ++k) {
                v += a[static_cast<std::size_t>(r * 3 + k)] * b[static_cast<std::size_t>(k * 3 + c)];
            }
            result[static_cast<std::size_t>(r * 3 + c)] = v;
        }
    }
    return result;
}

std::array<float, 9> LerpMatrix3(const std::array<float, 9>& a, const std::array<float, 9>& b, float t) {
    std::array<float, 9> result {};
    const float u = std::clamp(t, 0.0f, 1.0f);
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = a[i] * (1.0f - u) + b[i] * u;
    }
    return result;
}

std::array<float, 9> ApplyDiagonalRight(const std::array<float, 9>& matrix, const std::array<float, 3>& diagonal) {
    std::array<float, 9> result = matrix;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            result[static_cast<std::size_t>(r * 3 + c)] *= diagonal[static_cast<std::size_t>(c)];
        }
    }
    return result;
}

std::array<float, 9> ApplyDiagonalLeft(const std::array<float, 9>& matrix, const std::array<float, 3>& diagonal) {
    std::array<float, 9> result = matrix;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            result[static_cast<std::size_t>(r * 3 + c)] *= diagonal[static_cast<std::size_t>(r)];
        }
    }
    return result;
}

std::array<float, 9> DiagonalMatrix3(const std::array<float, 3>& diagonal) {
    return {
        diagonal[0], 0.0f, 0.0f,
        0.0f, diagonal[1], 0.0f,
        0.0f, 0.0f, diagonal[2]
    };
}

std::array<float, 3> MultiplyMatrixVector3(const std::array<float, 9>& matrix, const std::array<float, 3>& vector) {
    return {
        matrix[0] * vector[0] + matrix[1] * vector[1] + matrix[2] * vector[2],
        matrix[3] * vector[0] + matrix[4] * vector[1] + matrix[5] * vector[2],
        matrix[6] * vector[0] + matrix[7] * vector[1] + matrix[8] * vector[2]
    };
}

bool InvertMatrix3(const std::array<float, 9>& m, std::array<float, 9>& out) {
    const float det =
        m[0] * (m[4] * m[8] - m[5] * m[7]) -
        m[1] * (m[3] * m[8] - m[5] * m[6]) +
        m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) < 0.000001f) {
        return false;
    }
    const float invDet = 1.0f / det;
    out = {
        (m[4] * m[8] - m[5] * m[7]) * invDet,
        (m[2] * m[7] - m[1] * m[8]) * invDet,
        (m[1] * m[5] - m[2] * m[4]) * invDet,
        (m[5] * m[6] - m[3] * m[8]) * invDet,
        (m[0] * m[8] - m[2] * m[6]) * invDet,
        (m[2] * m[3] - m[0] * m[5]) * invDet,
        (m[3] * m[7] - m[4] * m[6]) * invDet,
        (m[1] * m[6] - m[0] * m[7]) * invDet,
        (m[0] * m[4] - m[1] * m[3]) * invDet
    };
    return true;
}

std::array<float, 9> DngXyzD50ToLinearSrgb() {
    return {
         3.1338561f, -1.6168667f, -0.4906146f,
        -0.9787684f,  1.9161415f,  0.0334540f,
         0.0719453f, -0.2289914f,  1.4052427f
    };
}

std::array<float, 9> DngXyzD50ToLinearRec2020() {
    static constexpr std::array<float, 9> kBradfordD50ToD65 = {
         0.9555766f, -0.0230393f,  0.0631636f,
        -0.0282895f,  1.0099416f,  0.0210077f,
         0.0122982f, -0.0204830f,  1.3299098f
    };
    static constexpr std::array<float, 9> kXyzD65ToLinearRec2020 = {
         1.7166512f, -0.3556708f, -0.2533663f,
        -0.6666844f,  1.6164812f,  0.0157685f,
         0.0176399f, -0.0427706f,  0.9421031f
    };
    return MultiplyMatrix3(kXyzD65ToLinearRec2020, kBradfordD50ToD65);
}

std::array<float, 9> DngXyzD50ToWorking(const RawDevelopSettings& settings) {
    return settings.workingSpace == RawWorkingSpace::LinearRec2020D65
        ? DngXyzD50ToLinearRec2020()
        : DngXyzD50ToLinearSrgb();
}

std::array<float, 9> LinearSrgbToLinearRec2020() {
    return {
        0.6274039f, 0.3292830f, 0.0433131f,
        0.0690973f, 0.9195404f, 0.0113623f,
        0.0163914f, 0.0880133f, 0.8955953f
    };
}

float CalibrationIlluminantTemperature(int illuminant) {
    switch (illuminant) {
        case 17: return 2856.0f; // Standard light A
        case 18: return 4874.0f; // Standard light B
        case 19: return 6774.0f; // Standard light C
        case 20: return 5500.0f; // D55
        case 21: return 6500.0f; // D65
        case 22: return 7500.0f; // D75
        case 23: return 5000.0f; // D50
        case 24: return 3200.0f; // ISO studio tungsten
        case 9: return 5500.0f;  // Fine weather
        case 10: return 6500.0f; // Cloudy
        case 11: return 7500.0f; // Shade
        default: return 0.0f;
    }
}

float CorrelatedColorTemperature(const std::array<float, 3>& xyz) {
    const float sum = xyz[0] + xyz[1] + xyz[2];
    if (!std::isfinite(sum) || sum <= 0.000001f) {
        return 0.0f;
    }
    const float x = xyz[0] / sum;
    const float y = xyz[1] / sum;
    const float denominator = 0.1858f - y;
    if (std::abs(denominator) < 0.000001f) {
        return 0.0f;
    }
    const float n = (x - 0.3320f) / denominator;
    const float cct = 449.0f * n * n * n + 3525.0f * n * n + 6823.3f * n + 5520.33f;
    return std::isfinite(cct) ? std::clamp(cct, 1667.0f, 25000.0f) : 0.0f;
}

float DngWarmthFromAsShotNeutral(const RawMetadata& metadata) {
    const float temperature1 = CalibrationIlluminantTemperature(metadata.dngIlluminant1);
    const float temperature2 = CalibrationIlluminantTemperature(metadata.dngIlluminant2);
    if (!metadata.hasDngAsShotNeutral ||
        !metadata.hasDngColorMatrix1 ||
        !metadata.hasDngColorMatrix2 ||
        temperature1 <= 0.0f ||
        temperature2 <= 0.0f ||
        std::abs(temperature1 - temperature2) < 1.0f) {
        return 0.5f;
    }

    float weight = 0.5f;
    for (int iteration = 0; iteration < 8; ++iteration) {
        const std::array<float, 9> colorMatrix =
            LerpMatrix3(metadata.dngColorMatrix1, metadata.dngColorMatrix2, weight);
        std::array<float, 9> cameraToXyz {};
        if (!InvertMatrix3(colorMatrix, cameraToXyz)) {
            break;
        }
        const std::array<float, 3> xyz =
            MultiplyMatrixVector3(cameraToXyz, metadata.dngAsShotNeutral);
        const float targetTemperature = CorrelatedColorTemperature(xyz);
        if (targetTemperature <= 0.0f) {
            break;
        }
        const float denominator = 1.0f / temperature2 - 1.0f / temperature1;
        if (std::abs(denominator) < 0.0000001f) {
            break;
        }
        const float nextWeight = std::clamp(
            (1.0f / targetTemperature - 1.0f / temperature1) / denominator,
            0.0f,
            1.0f);
        if (std::abs(nextWeight - weight) < 0.0001f) {
            weight = nextWeight;
            break;
        }
        weight = nextWeight;
    }
    return weight;
}

std::array<float, 9> SelectDngForwardMatrix(const RawMetadata& metadata, RawCameraTransformSource source) {
    if (source == RawCameraTransformSource::DngForwardMatrix1 && metadata.hasDngForwardMatrix1) {
        return metadata.dngForwardMatrix1;
    }
    if (source == RawCameraTransformSource::DngForwardMatrix2 && metadata.hasDngForwardMatrix2) {
        return metadata.dngForwardMatrix2;
    }
    if (metadata.hasDngForwardMatrix1 && metadata.hasDngForwardMatrix2) {
        return LerpMatrix3(metadata.dngForwardMatrix1, metadata.dngForwardMatrix2, DngWarmthFromAsShotNeutral(metadata));
    }
    if (metadata.hasDngForwardMatrix2) {
        return metadata.dngForwardMatrix2;
    }
    return metadata.dngForwardMatrix1;
}

std::array<float, 9> SelectDngCameraCalibration(const RawMetadata& metadata, RawCameraTransformSource source) {
    const std::array<float, 9> calibration1 =
        metadata.hasDngCameraCalibration1 ? metadata.dngCameraCalibration1 : IdentityMatrix3();
    const std::array<float, 9> calibration2 =
        metadata.hasDngCameraCalibration2 ? metadata.dngCameraCalibration2 : IdentityMatrix3();

    if (source == RawCameraTransformSource::DngForwardMatrix1) {
        if (metadata.hasDngForwardMatrix1) {
            return calibration1;
        }
        if (metadata.hasDngForwardMatrix2) {
            return calibration2;
        }
    }
    if (source == RawCameraTransformSource::DngForwardMatrix2) {
        if (metadata.hasDngForwardMatrix2) {
            return calibration2;
        }
        if (metadata.hasDngForwardMatrix1) {
            return calibration1;
        }
    }
    if (metadata.hasDngForwardMatrix1 && metadata.hasDngForwardMatrix2) {
        return LerpMatrix3(calibration1, calibration2, DngWarmthFromAsShotNeutral(metadata));
    }
    if (metadata.hasDngForwardMatrix2) {
        return calibration2;
    }
    return calibration1;
}

std::array<float, 3> CameraNeutralFromWhiteBalance(const RawMetadata& metadata, const RawDevelopSettings& settings) {
    const std::array<float, 3> wb = ResolveWhiteBalance(metadata, settings);
    return {
        1.0f / std::max(0.001f, wb[0]),
        1.0f / std::max(0.001f, wb[1]),
        1.0f / std::max(0.001f, wb[2])
    };
}

bool IsUsableNeutral(const std::array<float, 3>& neutral) {
    return std::isfinite(neutral[0]) && std::isfinite(neutral[1]) && std::isfinite(neutral[2]) &&
        neutral[0] > 0.0001f && neutral[1] > 0.0001f && neutral[2] > 0.0001f;
}

std::array<float, 9> BuildDngForwardMatrixToWorking(
    const RawMetadata& metadata,
    const RawDevelopSettings& settings,
    bool inputAlreadyWhiteBalanced) {
    const std::array<float, 9> forwardMatrix =
        SelectDngForwardMatrix(metadata, settings.cameraTransformSource);
    const std::array<float, 9> calibration =
        SelectDngCameraCalibration(metadata, settings.cameraTransformSource);
    const std::array<float, 3> analogBalance =
        metadata.hasDngAnalogBalance ? metadata.dngAnalogBalance : std::array<float, 3> { 1.0f, 1.0f, 1.0f };
    const std::array<float, 9> analogCalibration = ApplyDiagonalLeft(calibration, analogBalance);

    std::array<float, 9> inverseAnalogCalibration {};
    if (!InvertMatrix3(analogCalibration, inverseAnalogCalibration)) {
        return MultiplyMatrix3(DngXyzD50ToWorking(settings), forwardMatrix);
    }

    const std::array<float, 3> cameraNeutral = CameraNeutralFromWhiteBalance(metadata, settings);
    const std::array<float, 3> referenceNeutral = MultiplyMatrixVector3(inverseAnalogCalibration, cameraNeutral);
    if (!IsUsableNeutral(cameraNeutral) || !IsUsableNeutral(referenceNeutral)) {
        return MultiplyMatrix3(DngXyzD50ToWorking(settings), forwardMatrix);
    }

    // DNG ForwardMatrix expects raw camera RGB. The Bayer shader has already
    // applied white balance, so compensate with cameraNeutral when needed.
    const std::array<float, 9> referenceWhiteBalance = DiagonalMatrix3({
        1.0f / referenceNeutral[0],
        1.0f / referenceNeutral[1],
        1.0f / referenceNeutral[2]
    });
    std::array<float, 9> cameraToXyz =
        MultiplyMatrix3(MultiplyMatrix3(forwardMatrix, referenceWhiteBalance), inverseAnalogCalibration);

    if (inputAlreadyWhiteBalanced) {
        cameraToXyz = MultiplyMatrix3(cameraToXyz, DiagonalMatrix3(cameraNeutral));
    }

    return MultiplyMatrix3(DngXyzD50ToWorking(settings), cameraToXyz);
}

std::array<float, 9> SelectDngColorMatrix(const RawMetadata& metadata) {
    std::array<float, 9> color = IdentityMatrix3();
    std::array<float, 9> calibration = IdentityMatrix3();
    if (metadata.hasDngColorMatrix1 && metadata.hasDngColorMatrix2) {
        color = LerpMatrix3(metadata.dngColorMatrix1, metadata.dngColorMatrix2, DngWarmthFromAsShotNeutral(metadata));
        calibration = LerpMatrix3(metadata.dngCameraCalibration1, metadata.dngCameraCalibration2, DngWarmthFromAsShotNeutral(metadata));
    } else if (metadata.hasDngColorMatrix2) {
        color = metadata.dngColorMatrix2;
        calibration = metadata.dngCameraCalibration2;
    } else {
        color = metadata.dngColorMatrix1;
        calibration = metadata.dngCameraCalibration1;
    }
    if (metadata.hasDngAnalogBalance) {
        color = ApplyDiagonalRight(color, metadata.dngAnalogBalance);
    }
    if (metadata.hasDngCameraCalibration1 || metadata.hasDngCameraCalibration2) {
        color = MultiplyMatrix3(color, calibration);
    }
    return color;
}

std::array<float, 9> BuildCameraToWorking(
    const RawMetadata& metadata,
    const RawDevelopSettings& settings,
    bool inputAlreadyWhiteBalanced) {
    if (metadata.isDng) {
        if ((settings.cameraTransformSource == RawCameraTransformSource::DngAuto ||
            settings.cameraTransformSource == RawCameraTransformSource::DngForwardMatrix1 ||
            settings.cameraTransformSource == RawCameraTransformSource::DngForwardMatrix2) &&
            (metadata.hasDngForwardMatrix1 || metadata.hasDngForwardMatrix2)) {
            return BuildDngForwardMatrixToWorking(metadata, settings, inputAlreadyWhiteBalanced);
        }
        if (settings.cameraTransformSource == RawCameraTransformSource::DngForwardMatrix1 && metadata.hasDngForwardMatrix1) {
            return BuildDngForwardMatrixToWorking(metadata, settings, inputAlreadyWhiteBalanced);
        }
        if (settings.cameraTransformSource == RawCameraTransformSource::DngForwardMatrix2 && metadata.hasDngForwardMatrix2) {
            return BuildDngForwardMatrixToWorking(metadata, settings, inputAlreadyWhiteBalanced);
        }
        if (settings.cameraTransformSource == RawCameraTransformSource::DngColorMatrixInverse ||
            settings.cameraTransformSource == RawCameraTransformSource::DngAuto) {
            std::array<float, 9> inverse {};
            if ((metadata.hasDngColorMatrix1 || metadata.hasDngColorMatrix2) &&
                InvertMatrix3(SelectDngColorMatrix(metadata), inverse)) {
                return MultiplyMatrix3(DngXyzD50ToWorking(settings), inverse);
            }
        }
        if (settings.cameraTransformSource == RawCameraTransformSource::DngForwardMatrix1 && metadata.hasDngForwardMatrix2) {
            return BuildDngForwardMatrixToWorking(metadata, settings, inputAlreadyWhiteBalanced);
        }
    }

    // Stack does not have a full color-management module yet. For V1 this uses
    // LibRaw's camera-to-sRGB matrix as the approximate working transform.
    if (!metadata.hasCameraMatrix) {
        return IdentityMatrix3();
    }
    return settings.workingSpace == RawWorkingSpace::LinearRec2020D65
        ? MultiplyMatrix3(LinearSrgbToLinearRec2020(), metadata.cameraToSrgb)
        : metadata.cameraToSrgb;
}

} // namespace

std::array<float, 3> ResolveRawWhiteBalance(
    const RawMetadata& metadata,
    const RawDevelopSettings& settings) {
    return ResolveWhiteBalance(metadata, settings);
}

std::array<float, 9> BuildRawCameraToWorkingTransform(
    const RawMetadata& metadata,
    const RawDevelopSettings& settings,
    bool inputAlreadyWhiteBalanced) {
    return BuildCameraToWorking(
        metadata,
        settings,
        inputAlreadyWhiteBalanced);
}

} // namespace Raw
