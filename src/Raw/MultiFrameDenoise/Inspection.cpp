#include "Raw/MultiFrameDenoise/Inspection.h"

#include "Raw/RawProcessingMath.h"
#include "Utils/PngEncodingUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace Raw::Mfd {
namespace {

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool CheckedSampleCount(PixelExtent extent, std::size_t& count) {
    count = 0u;
    if (extent.width == 0u || extent.height == 0u ||
        extent.width > std::numeric_limits<std::uint64_t>::max() /
            extent.height) {
        return false;
    }
    const std::uint64_t samples = extent.width * extent.height;
    if (samples > std::numeric_limits<std::size_t>::max()) return false;
    count = static_cast<std::size_t>(samples);
    return true;
}

bool KnownPattern(CfaPattern pattern) {
    return pattern == CfaPattern::RGGB || pattern == CfaPattern::BGGR ||
        pattern == CfaPattern::GBRG || pattern == CfaPattern::GRBG;
}

bool FiniteSamples(const std::vector<float>& samples) {
    return std::all_of(samples.begin(), samples.end(), [](float value) {
        return std::isfinite(value);
    });
}

std::uint8_t Byte(double value) {
    return static_cast<std::uint8_t>(std::lround(
        255.0 * std::clamp(value, 0.0, 1.0)));
}

bool WriteBytesAtomic(
    const std::filesystem::path& path,
    const std::vector<unsigned char>& bytes,
    std::string* error) {
    if (bytes.empty()) return Fail(error, "An inspection artifact encoded no bytes.");
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::error_code filesystemError;
    std::filesystem::remove(temporary, filesystemError);
    filesystemError.clear();
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return Fail(error, "An inspection artifact could not be opened.");
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, filesystemError);
            return Fail(error, "An inspection artifact was incomplete.");
        }
    }
    if (std::filesystem::exists(path, filesystemError) && !filesystemError) {
        std::filesystem::remove(path, filesystemError);
    }
    if (filesystemError) {
        std::filesystem::remove(temporary, filesystemError);
        return Fail(error, "An older inspection artifact could not be replaced.");
    }
    std::filesystem::rename(temporary, path, filesystemError);
    if (filesystemError) {
        std::filesystem::remove(temporary, filesystemError);
        return Fail(error, "An inspection artifact could not be published.");
    }
    return true;
}

bool WritePng(
    const std::filesystem::path& path,
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    int channels,
    std::string* error) {
    return WriteBytesAtomic(
        path,
        Stack::PngEncoding::EncodeInterleaved(
            pixels, width, height, channels),
        error);
}

double PercentileSorted(
    const std::vector<double>& sorted,
    double percentile) {
    if (sorted.empty()) return 0.0;
    const double coordinate = std::clamp(percentile, 0.0, 1.0) *
        static_cast<double>(sorted.size() - 1u);
    const std::size_t lower = static_cast<std::size_t>(std::floor(coordinate));
    const std::size_t upper = std::min(lower + 1u, sorted.size() - 1u);
    const double fraction = coordinate - static_cast<double>(lower);
    return (1.0 - fraction) * sorted[lower] + fraction * sorted[upper];
}

std::vector<unsigned char> BuildPreview(
    const std::vector<float>& mosaic,
    int width,
    int height,
    CfaPattern pattern,
    double scale) {
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * height * 3u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::array<float, 3> rgb = Raw::Processing::DemosaicMalvarHeCutlerAt(
                mosaic, width, height, pattern, x, y);
            for (float& channel : rgb) {
                channel = Raw::Processing::EncodeSrgb(static_cast<float>(
                    std::max(0.0, scale * static_cast<double>(channel))));
            }
            const std::size_t index = static_cast<std::size_t>(
                (y * width + x) * 3);
            pixels[index + 0u] = Byte(rgb[0]);
            pixels[index + 1u] = Byte(rgb[1]);
            pixels[index + 2u] = Byte(rgb[2]);
        }
    }
    return pixels;
}

nlohmann::json SummaryJson(const MfdInspectionSummary& summary) {
    nlohmann::json files = nlohmann::json::array();
    for (const std::filesystem::path& path : summary.writtenFiles) {
        files.push_back(path.filename().generic_u8string());
    }
    return {
        { "contractId", kInspectionContractId },
        { "contractVersion", kInspectionContractVersion },
        { "valid", summary.valid },
        { "message", summary.message },
        { "pixelCount", summary.pixelCount },
        { "contributingPixelCount", summary.contributingPixelCount },
        { "exactReferencePixelCount", summary.exactReferencePixelCount },
        { "contributingPixelFraction", summary.contributingPixelFraction },
        { "exactReferencePixelFraction", summary.exactReferencePixelFraction },
        { "meanAbsoluteDelta", summary.meanAbsoluteDelta },
        { "percentile95AbsoluteDelta", summary.percentile95AbsoluteDelta },
        { "percentile99AbsoluteDelta", summary.percentile99AbsoluteDelta },
        { "maximumAbsoluteDelta", summary.maximumAbsoluteDelta },
        { "meanEffectiveSampleCount", summary.meanEffectiveSampleCount },
        { "previewLinearScale", summary.previewLinearScale },
        { "previewColorStatus",
            "neutral camera-native RGB; no white balance or camera transform" },
        { "files", std::move(files) }
    };
}

bool WriteJsonAtomic(
    const std::filesystem::path& path,
    const nlohmann::json& value,
    std::string* error) {
    const std::string text = value.dump(2) + "\n";
    const std::vector<unsigned char> bytes(text.begin(), text.end());
    return WriteBytesAtomic(path, bytes, error);
}

} // namespace

bool WriteMfdInspectionPacket(
    const MfdInspectionInput& input,
    const std::filesystem::path& outputDirectory,
    MfdInspectionSummary& summary,
    std::string* error) {
    MfdInspectionInputView view;
    view.sampleId = input.sampleId;
    view.cfaPattern = input.cfaPattern;
    view.extent = input.extent;
    view.referenceNormalizedMosaic = &input.referenceNormalizedMosaic;
    view.outputNormalizedMosaic = &input.outputNormalizedMosaic;
    view.fusionDiagnostics = &input.fusionDiagnostics;
    return WriteMfdInspectionPacketView(
        view, outputDirectory, summary, error);
}

bool WriteMfdInspectionPacketView(
    const MfdInspectionInputView& input,
    const std::filesystem::path& outputDirectory,
    MfdInspectionSummary& summary,
    std::string* error) {
    summary = {};
    std::size_t sampleCount = 0u;
    if (input.sampleId.empty() || outputDirectory.empty() ||
        !KnownPattern(input.cfaPattern) ||
        !CheckedSampleCount(input.extent, sampleCount) ||
        input.extent.width > static_cast<std::uint64_t>(
            std::numeric_limits<int>::max()) ||
        input.extent.height > static_cast<std::uint64_t>(
            std::numeric_limits<int>::max()) ||
        input.referenceNormalizedMosaic == nullptr ||
        input.outputNormalizedMosaic == nullptr ||
        input.fusionDiagnostics == nullptr ||
        input.referenceNormalizedMosaic->size() != sampleCount ||
        input.outputNormalizedMosaic->size() != sampleCount ||
        input.fusionDiagnostics->size() != sampleCount ||
        !FiniteSamples(*input.referenceNormalizedMosaic) ||
        !FiniteSamples(*input.outputNormalizedMosaic)) {
        return Fail(error, "The MFD inspection input contract is invalid.");
    }
    const std::vector<float>& referenceNormalizedMosaic =
        *input.referenceNormalizedMosaic;
    const std::vector<float>& outputNormalizedMosaic =
        *input.outputNormalizedMosaic;
    const std::vector<FusionPixelDiagnostics>& fusionDiagnostics =
        *input.fusionDiagnostics;
    std::error_code filesystemError;
    std::filesystem::create_directories(outputDirectory, filesystemError);
    if (filesystemError) {
        return Fail(error, "The MFD inspection directory could not be created.");
    }

    std::vector<double> absoluteDelta;
    std::vector<double> positiveReference;
    try {
        absoluteDelta.resize(sampleCount);
        positiveReference.reserve(sampleCount);
    } catch (const std::bad_alloc&) {
        return Fail(error, "The MFD inspection statistics exceeded memory.");
    }
    double deltaSum = 0.0;
    double effectiveSum = 0.0;
    double maximumEffective = 1.0;
    for (std::size_t index = 0u; index < sampleCount; ++index) {
        const double delta = std::abs(
            static_cast<double>(outputNormalizedMosaic[index]) -
            referenceNormalizedMosaic[index]);
        absoluteDelta[index] = delta;
        deltaSum += delta;
        if (referenceNormalizedMosaic[index] > 0.0f) {
            positiveReference.push_back(referenceNormalizedMosaic[index]);
        }
        const FusionPixelDiagnostics& diagnostic =
            fusionDiagnostics[index];
        if (diagnostic.contributingAlternateCount > 0u) {
            ++summary.contributingPixelCount;
        }
        if (diagnostic.exactReferenceCopy) {
            ++summary.exactReferencePixelCount;
        }
        effectiveSum += diagnostic.effectiveSampleCount;
        maximumEffective = std::max(
            maximumEffective, diagnostic.effectiveSampleCount);
    }
    std::sort(absoluteDelta.begin(), absoluteDelta.end());
    std::sort(positiveReference.begin(), positiveReference.end());
    summary.pixelCount = sampleCount;
    summary.contributingPixelFraction = static_cast<double>(
        summary.contributingPixelCount) / sampleCount;
    summary.exactReferencePixelFraction = static_cast<double>(
        summary.exactReferencePixelCount) / sampleCount;
    summary.meanAbsoluteDelta = deltaSum / sampleCount;
    summary.percentile95AbsoluteDelta =
        PercentileSorted(absoluteDelta, 0.95);
    summary.percentile99AbsoluteDelta =
        PercentileSorted(absoluteDelta, 0.99);
    summary.maximumAbsoluteDelta = absoluteDelta.back();
    summary.meanEffectiveSampleCount = effectiveSum / sampleCount;
    const double referenceHigh = std::max(
        PercentileSorted(positiveReference, 0.995), 1.0e-6);
    summary.previewLinearScale = 0.90 / referenceHigh;

    const int width = static_cast<int>(input.extent.width);
    const int height = static_cast<int>(input.extent.height);
    std::vector<unsigned char> referencePreview;
    std::vector<unsigned char> outputPreview;
    std::vector<unsigned char> differencePreview(sampleCount * 3u);
    std::vector<unsigned char> effectivePreview(sampleCount);
    std::vector<unsigned char> contributionPreview(sampleCount);
    std::vector<unsigned char> fallbackPreview(sampleCount);
    try {
        referencePreview = BuildPreview(
            referenceNormalizedMosaic,
            width,
            height,
            input.cfaPattern,
            summary.previewLinearScale);
        outputPreview = BuildPreview(
            outputNormalizedMosaic,
            width,
            height,
            input.cfaPattern,
            summary.previewLinearScale);
    } catch (const std::bad_alloc&) {
        return Fail(error, "The MFD inspection previews exceeded memory.");
    }

    const double differenceScale = std::max(
        summary.percentile99AbsoluteDelta, 1.0e-8);
    for (std::size_t index = 0u; index < sampleCount; ++index) {
        const double signedDelta =
            static_cast<double>(outputNormalizedMosaic[index]) -
            referenceNormalizedMosaic[index];
        const double magnitude = std::clamp(
            std::abs(signedDelta) / differenceScale, 0.0, 1.0);
        const double sign = signedDelta >= 0.0 ? 1.0 : -1.0;
        differencePreview[index * 3u + 0u] = Byte(
            0.5 + 0.5 * sign * magnitude);
        differencePreview[index * 3u + 1u] = Byte(
            0.5 - 0.5 * sign * magnitude);
        differencePreview[index * 3u + 2u] = Byte(
            0.5 - 0.5 * sign * magnitude);
        const FusionPixelDiagnostics& diagnostic =
            fusionDiagnostics[index];
        effectivePreview[index] = Byte(
            diagnostic.effectiveSampleCount / maximumEffective);
        contributionPreview[index] =
            diagnostic.contributingAlternateCount > 0u ? 255u : 0u;
        fallbackPreview[index] = diagnostic.exactReferenceCopy ? 255u : 0u;
    }

    const auto write = [&](const char* name,
                           const std::vector<unsigned char>& pixels,
                           int channels) {
        const std::filesystem::path path = outputDirectory / name;
        if (!WritePng(path, pixels, width, height, channels, error)) {
            return false;
        }
        summary.writtenFiles.push_back(path);
        return true;
    };
    if (!write("reference-preview.png", referencePreview, 3) ||
        !write("output-preview.png", outputPreview, 3) ||
        !write("signed-difference.png", differencePreview, 3) ||
        !write("effective-samples.png", effectivePreview, 1) ||
        !write("alternate-contribution.png", contributionPreview, 1) ||
        !write("exact-reference-mask.png", fallbackPreview, 1)) {
        return false;
    }
    summary.valid = true;
    summary.message =
        "MFD offline inspection previews and contribution diagnostics are valid.";
    const std::filesystem::path summaryPath =
        outputDirectory / "inspection.json";
    if (!WriteJsonAtomic(summaryPath, SummaryJson(summary), error)) {
        summary.valid = false;
        return false;
    }
    summary.writtenFiles.push_back(summaryPath);
    if (error) error->clear();
    return true;
}

nlohmann::json SerializeMfdInspectionSummary(
    const MfdInspectionSummary& summary) {
    return SummaryJson(summary);
}

} // namespace Raw::Mfd
