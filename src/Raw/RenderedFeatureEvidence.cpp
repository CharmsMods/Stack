#include "Raw/RenderedFeatureEvidence.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <sstream>
#include <tuple>
#include <unordered_map>

namespace Stack::RenderedFeatures {
namespace {

constexpr double kEpsilon = 1.0e-12;
constexpr double kPi = 3.14159265358979323846;

struct LuminanceData {
    std::vector<double> linear;
    std::vector<double> ev;
    std::vector<float> logLuma;
    std::vector<std::uint8_t> valid;
    double validFraction = 0.0;
    double negativeChannelFraction = 0.0;
    double gamutPressureFraction = 0.0;
};

double SmoothStep(double edge0, double edge1, double value) {
    if (!(edge1 > edge0)) return value >= edge1 ? 1.0 : 0.0;
    const double t = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double Quantile(std::vector<double> values, double q) {
    if (values.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort(values.begin(), values.end());
    const double position = std::clamp(q, 0.0, 1.0) * static_cast<double>(values.size() - 1);
    const std::size_t low = static_cast<std::size_t>(std::floor(position));
    const std::size_t high = static_cast<std::size_t>(std::ceil(position));
    const double t = position - static_cast<double>(low);
    return values[low] * (1.0 - t) + values[high] * t;
}

double Median(std::vector<double> values) {
    return Quantile(std::move(values), 0.5);
}

bool FiniteMatrix(const std::array<double, 9>& matrix) {
    return std::all_of(matrix.begin(), matrix.end(), [](double value) {
        return std::isfinite(value);
    });
}

bool ValidContext(const FeatureContext& context) {
    return !context.sourceIdentity.empty() &&
        !context.recipeIdentity.empty() &&
        !context.stage.empty() &&
        !context.colorSpace.empty() &&
        !context.colorTransformIdentity.empty() &&
        FiniteMatrix(context.workingToXyz) &&
        std::isfinite(context.referenceGrey) && context.referenceGrey > 0.0;
}

std::string RecordIdentity(const LinearRgbImage& image, const FeatureContext& context) {
    std::ostringstream stream;
    stream << kRenderedFeatureVersion << '|'
        << context.sourceIdentity << '|'
        << context.recipeIdentity << '|'
        << context.stage << '|'
        << context.colorSpace << '|'
        << context.colorTransformIdentity << '|'
        << context.transferFunction << '|'
        << context.rawEvidenceIdentity << '|'
        << context.cropIdentity << '|'
        << image.width << 'x' << image.height;
    return stream.str();
}

FeatureValue MakeFeature(
    const std::string& id,
    double value,
    const std::string& units,
    FeatureDisposition disposition,
    const LinearRgbImage& image,
    const FeatureContext& context,
    double validFraction,
    double uncertainty01,
    int minimumWidth,
    int minimumHeight,
    const std::string& reason = {}) {
    FeatureValue feature;
    feature.id = id;
    feature.disposition = disposition;
    feature.valid = std::isfinite(value) && image.width >= minimumWidth && image.height >= minimumHeight;
    feature.value = feature.valid ? value : 0.0;
    feature.units = units;
    feature.stage = context.stage;
    feature.colorSpace = context.colorSpace;
    feature.colorTransformIdentity = context.colorTransformIdentity;
    feature.transferFunction = context.transferFunction;
    feature.referenceGrey = context.referenceGrey;
    feature.rawEvidenceIdentity = context.rawEvidenceIdentity;
    feature.reference = "grey=" + std::to_string(context.referenceGrey) +
        ";color-transform=" + context.colorTransformIdentity;
    feature.width = image.width;
    feature.height = image.height;
    feature.cropIdentity = context.cropIdentity;
    feature.validFraction = std::clamp(validFraction, 0.0, 1.0);
    feature.uncertainty01 = feature.valid ? std::clamp(uncertainty01, 0.0, 1.0) : 1.0;
    feature.minimumWidth = minimumWidth;
    feature.minimumHeight = minimumHeight;
    feature.reason = feature.valid
        ? reason
        : (!std::isfinite(value) ? "non-finite-or-unavailable" : "below-minimum-resolution");
    return feature;
}

FeatureValue UnavailableFeature(
    const std::string& id,
    const std::string& units,
    FeatureDisposition disposition,
    const LinearRgbImage& image,
    const FeatureContext& context,
    const std::string& reason,
    int minimumWidth = 1,
    int minimumHeight = 1) {
    FeatureValue feature = MakeFeature(
        id,
        std::numeric_limits<double>::quiet_NaN(),
        units,
        disposition,
        image,
        context,
        0.0,
        1.0,
        minimumWidth,
        minimumHeight,
        reason);
    feature.reason = reason;
    return feature;
}

LuminanceData BuildLuminance(const LinearRgbImage& image, const FeatureContext& context) {
    LuminanceData result;
    if (!image.Valid() || !ValidContext(context)) return result;
    const std::size_t count = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    result.linear.assign(count, 0.0);
    result.logLuma.assign(count, static_cast<float>(std::log(kEpsilon)));
    result.valid.assign(count, 0);
    result.ev.reserve(count);
    std::uint64_t negativeChannels = 0;
    std::uint64_t gamutPressure = 0;
    std::uint64_t validCount = 0;
    const std::array<double, 3> yRow {
        context.workingToXyz[3], context.workingToXyz[4], context.workingToXyz[5]
    };
    for (std::size_t i = 0; i < count; ++i) {
        const double r = image.pixels[i * 3];
        const double g = image.pixels[i * 3 + 1];
        const double b = image.pixels[i * 3 + 2];
        if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) continue;
        negativeChannels += static_cast<std::uint64_t>(r < 0.0) +
            static_cast<std::uint64_t>(g < 0.0) +
            static_cast<std::uint64_t>(b < 0.0);
        if (r < 0.0 || g < 0.0 || b < 0.0 || r > 1.0 || g > 1.0 || b > 1.0) ++gamutPressure;
        const double y = yRow[0] * r + yRow[1] * g + yRow[2] * b;
        result.linear[i] = y;
        if (!std::isfinite(y) || y <= 0.0) continue;
        const double ev = std::log2(std::max(kEpsilon, y) / context.referenceGrey);
        result.ev.push_back(ev);
        result.logLuma[i] = static_cast<float>(std::log(std::max(kEpsilon, y)));
        result.valid[i] = 1;
        ++validCount;
    }
    const double denominator = static_cast<double>(std::max<std::size_t>(1, count));
    result.validFraction = static_cast<double>(validCount) / denominator;
    result.negativeChannelFraction = static_cast<double>(negativeChannels) / (denominator * 3.0);
    result.gamutPressureFraction = static_cast<double>(gamutPressure) / denominator;
    return result;
}

std::vector<float> BoxBlur(
    const std::vector<float>& source,
    int width,
    int height,
    int radius) {
    if (width <= 0 || height <= 0 || source.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) return {};
    radius = std::max(0, radius);
    if (radius == 0) return source;
    const int integralWidth = width + 1;
    std::vector<double> integral(static_cast<std::size_t>(integralWidth) * static_cast<std::size_t>(height + 1), 0.0);
    for (int y = 0; y < height; ++y) {
        double rowSum = 0.0;
        for (int x = 0; x < width; ++x) {
            rowSum += source[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
            integral[static_cast<std::size_t>(y + 1) * static_cast<std::size_t>(integralWidth) + static_cast<std::size_t>(x + 1)] =
                integral[static_cast<std::size_t>(y) * static_cast<std::size_t>(integralWidth) + static_cast<std::size_t>(x + 1)] + rowSum;
        }
    }
    std::vector<float> output(source.size(), 0.0f);
    for (int y = 0; y < height; ++y) {
        const int top = std::max(0, y - radius);
        const int bottom = std::min(height, y + radius + 1);
        for (int x = 0; x < width; ++x) {
            const int left = std::max(0, x - radius);
            const int right = std::min(width, x + radius + 1);
            const double sum =
                integral[static_cast<std::size_t>(bottom) * static_cast<std::size_t>(integralWidth) + static_cast<std::size_t>(right)] -
                integral[static_cast<std::size_t>(top) * static_cast<std::size_t>(integralWidth) + static_cast<std::size_t>(right)] -
                integral[static_cast<std::size_t>(bottom) * static_cast<std::size_t>(integralWidth) + static_cast<std::size_t>(left)] +
                integral[static_cast<std::size_t>(top) * static_cast<std::size_t>(integralWidth) + static_cast<std::size_t>(left)];
            const double count = static_cast<double>((right - left) * (bottom - top));
            output[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                static_cast<float>(sum / std::max(1.0, count));
        }
    }
    return output;
}

std::vector<double> RegionEv(
    const LuminanceData& data,
    int width,
    int height,
    const std::function<bool(int, int)>& predicate) {
    std::vector<double> values;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            if (!data.valid[index] || !predicate(x, y)) continue;
            values.push_back(std::log2(std::max(kEpsilon, data.linear[index])));
        }
    }
    return values;
}

std::array<double, 3> NormalizeGain(std::array<double, 3> gain) {
    for (double value : gain) if (!std::isfinite(value) || value <= kEpsilon) return { 1.0, 1.0, 1.0 };
    const double geometricMean = std::cbrt(gain[0] * gain[1] * gain[2]);
    for (double& value : gain) value /= geometricMean;
    return gain;
}

std::array<double, 3> GrayWorldEstimate(
    const LinearRgbImage& image,
    int left,
    int top,
    int right,
    int bottom,
    double power) {
    std::array<double, 3> sums {};
    std::uint64_t count = 0;
    for (int y = std::max(0, top); y < std::min(image.height, bottom); ++y) {
        for (int x = std::max(0, left); x < std::min(image.width, right); ++x) {
            const std::size_t index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 3;
            const double r = std::max(0.0, static_cast<double>(image.pixels[index]));
            const double g = std::max(0.0, static_cast<double>(image.pixels[index + 1]));
            const double b = std::max(0.0, static_cast<double>(image.pixels[index + 2]));
            if (!std::isfinite(r + g + b)) continue;
            sums[0] += std::pow(r, power);
            sums[1] += std::pow(g, power);
            sums[2] += std::pow(b, power);
            ++count;
        }
    }
    if (count == 0) return { 1.0, 1.0, 1.0 };
    for (double& value : sums) value = std::pow(value / static_cast<double>(count), 1.0 / power);
    const double target = std::cbrt(std::max(kEpsilon, sums[0] * sums[1] * sums[2]));
    return NormalizeGain({
        target / std::max(kEpsilon, sums[0]),
        target / std::max(kEpsilon, sums[1]),
        target / std::max(kEpsilon, sums[2])
    });
}

std::array<double, 3> GrayEdgeEstimate(const LinearRgbImage& image) {
    std::array<double, 3> sums {};
    std::uint64_t count = 0;
    for (int y = 0; y + 1 < image.height; ++y) {
        for (int x = 0; x + 1 < image.width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 3;
            const std::size_t ix = i + 3;
            const std::size_t iy = i + static_cast<std::size_t>(image.width) * 3;
            for (int c = 0; c < 3; ++c) {
                const double dx = static_cast<double>(image.pixels[ix + static_cast<std::size_t>(c)]) - image.pixels[i + static_cast<std::size_t>(c)];
                const double dy = static_cast<double>(image.pixels[iy + static_cast<std::size_t>(c)]) - image.pixels[i + static_cast<std::size_t>(c)];
                sums[static_cast<std::size_t>(c)] += std::sqrt(dx * dx + dy * dy);
            }
            ++count;
        }
    }
    if (count == 0) return { 1.0, 1.0, 1.0 };
    const double target = std::cbrt(std::max(kEpsilon, sums[0] * sums[1] * sums[2]));
    return NormalizeGain({
        target / std::max(kEpsilon, sums[0]),
        target / std::max(kEpsilon, sums[1]),
        target / std::max(kEpsilon, sums[2])
    });
}

double GainDisagreement(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    double maximum = 0.0;
    for (int c = 0; c < 3; ++c) {
        maximum = std::max(maximum, std::abs(std::log2(std::max(kEpsilon, a[static_cast<std::size_t>(c)]) /
            std::max(kEpsilon, b[static_cast<std::size_t>(c)]))));
    }
    return maximum;
}

double Hue(double r, double g, double b, double& chroma) {
    const double maximum = std::max({ r, g, b });
    const double minimum = std::min({ r, g, b });
    chroma = maximum - minimum;
    if (chroma <= kEpsilon) return 0.0;
    double hue = 0.0;
    if (maximum == r) hue = std::fmod((g - b) / chroma, 6.0);
    else if (maximum == g) hue = (b - r) / chroma + 2.0;
    else hue = (r - g) / chroma + 4.0;
    hue *= 60.0;
    if (hue < 0.0) hue += 360.0;
    return hue;
}

double CircularHueDistance(double a, double b) {
    const double delta = std::abs(a - b);
    return std::min(delta, 360.0 - delta);
}

double SrgbEncode(double linear) {
    linear = std::max(0.0, linear);
    return linear <= 0.0031308
        ? 12.92 * linear
        : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

FeatureRecord BeginRecord(const LinearRgbImage& image, const FeatureContext& context) {
    FeatureRecord record;
    record.sourceIdentity = context.sourceIdentity;
    record.recipeIdentity = context.recipeIdentity;
    record.stage = context.stage;
    record.colorSpace = context.colorSpace;
    record.colorTransformIdentity = context.colorTransformIdentity;
    record.transferFunction = context.transferFunction;
    record.referenceGrey = context.referenceGrey;
    record.rawEvidenceIdentity = context.rawEvidenceIdentity;
    record.cropIdentity = context.cropIdentity;
    record.width = image.width;
    record.height = image.height;
    record.sourceWidth = context.sourceWidth > 0 ? context.sourceWidth : image.width;
    record.sourceHeight = context.sourceHeight > 0 ? context.sourceHeight : image.height;
    record.orientationNormalized = context.orientationNormalized;
    record.recordIdentity = RecordIdentity(image, context);
    return record;
}

void FinalizeRecord(FeatureRecord& record, const std::chrono::steady_clock::time_point& started) {
    const auto finished = std::chrono::steady_clock::now();
    record.runtimeMs = std::chrono::duration<double, std::milli>(finished - started).count();
    const double perFeature = record.values.empty() ? 0.0 : record.runtimeMs / static_cast<double>(record.values.size());
    for (FeatureValue& value : record.values) value.runtimeMs = perFeature;
    record.valid = !record.values.empty() &&
        std::any_of(record.values.begin(), record.values.end(), [](const FeatureValue& value) {
            return value.valid && value.disposition == FeatureDisposition::Accepted;
        });
    record.statusMessage = record.valid
        ? "Rendered feature record complete; diagnostic only and not scored."
        : "Rendered feature record incomplete; no accepted feature is valid.";
}

nlohmann::json SerializeFeature(const FeatureValue& feature) {
    return {
        { "id", feature.id },
        { "version", feature.version },
        { "disposition", FeatureDispositionName(feature.disposition) },
        { "valid", feature.valid },
        { "value", feature.valid ? nlohmann::json(feature.value) : nlohmann::json(nullptr) },
        { "units", feature.units },
        { "stage", feature.stage },
        { "colorSpace", feature.colorSpace },
        { "colorTransformIdentity", feature.colorTransformIdentity },
        { "transferFunction", feature.transferFunction },
        { "referenceGrey", feature.referenceGrey },
        { "rawEvidenceIdentity", feature.rawEvidenceIdentity },
        { "reference", feature.reference },
        { "width", feature.width },
        { "height", feature.height },
        { "cropIdentity", feature.cropIdentity },
        { "validFraction", feature.validFraction },
        { "uncertainty01", feature.uncertainty01 },
        { "runtimeMs", feature.runtimeMs },
        { "minimumWidth", feature.minimumWidth },
        { "minimumHeight", feature.minimumHeight },
        { "reason", feature.reason }
    };
}

} // namespace

bool LinearRgbImage::Valid() const {
    return width > 0 && height > 0 &&
        pixels.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3;
}

bool ScalarMask::Valid() const {
    return width > 0 && height > 0 &&
        values.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

LinearRgbImage ResizeLinearRgb(const LinearRgbImage& source, int targetWidth, int targetHeight) {
    LinearRgbImage output;
    if (!source.Valid() || targetWidth <= 0 || targetHeight <= 0) return output;
    output.width = targetWidth;
    output.height = targetHeight;
    output.pixels.assign(static_cast<std::size_t>(targetWidth) * static_cast<std::size_t>(targetHeight) * 3, 0.0f);
    for (int y = 0; y < targetHeight; ++y) {
        const double sourceY = (static_cast<double>(y) + 0.5) * source.height / targetHeight - 0.5;
        const int y0 = std::clamp(static_cast<int>(std::floor(sourceY)), 0, source.height - 1);
        const int y1 = std::min(source.height - 1, y0 + 1);
        const double ty = std::clamp(sourceY - std::floor(sourceY), 0.0, 1.0);
        for (int x = 0; x < targetWidth; ++x) {
            const double sourceX = (static_cast<double>(x) + 0.5) * source.width / targetWidth - 0.5;
            const int x0 = std::clamp(static_cast<int>(std::floor(sourceX)), 0, source.width - 1);
            const int x1 = std::min(source.width - 1, x0 + 1);
            const double tx = std::clamp(sourceX - std::floor(sourceX), 0.0, 1.0);
            for (int c = 0; c < 3; ++c) {
                auto at = [&](int sx, int sy) {
                    return source.pixels[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(source.width) + static_cast<std::size_t>(sx)) * 3 + static_cast<std::size_t>(c)];
                };
                const double top = at(x0, y0) * (1.0 - tx) + at(x1, y0) * tx;
                const double bottom = at(x0, y1) * (1.0 - tx) + at(x1, y1) * tx;
                output.pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(targetWidth) + static_cast<std::size_t>(x)) * 3 + static_cast<std::size_t>(c)] =
                    static_cast<float>(top * (1.0 - ty) + bottom * ty);
            }
        }
    }
    return output;
}

LinearRgbImage OrientLinearRgb(const LinearRgbImage& source, int exifOrientation) {
    if (!source.Valid() || exifOrientation < 1 || exifOrientation > 8) return {};
    const bool swapDimensions = exifOrientation >= 5 && exifOrientation <= 8;
    LinearRgbImage output;
    output.width = swapDimensions ? source.height : source.width;
    output.height = swapDimensions ? source.width : source.height;
    output.pixels.assign(static_cast<std::size_t>(output.width) * static_cast<std::size_t>(output.height) * 3, 0.0f);
    for (int y = 0; y < output.height; ++y) {
        for (int x = 0; x < output.width; ++x) {
            int sx = x;
            int sy = y;
            switch (exifOrientation) {
                case 1: sx = x; sy = y; break;
                case 2: sx = source.width - 1 - x; sy = y; break;
                case 3: sx = source.width - 1 - x; sy = source.height - 1 - y; break;
                case 4: sx = x; sy = source.height - 1 - y; break;
                case 5: sx = y; sy = x; break;
                case 6: sx = y; sy = source.height - 1 - x; break;
                case 7: sx = source.width - 1 - y; sy = source.height - 1 - x; break;
                case 8: sx = source.width - 1 - y; sy = x; break;
            }
            sx = std::clamp(sx, 0, source.width - 1);
            sy = std::clamp(sy, 0, source.height - 1);
            for (int c = 0; c < 3; ++c) {
                output.pixels[(static_cast<std::size_t>(y) * static_cast<std::size_t>(output.width) + static_cast<std::size_t>(x)) * 3 + static_cast<std::size_t>(c)] =
                    source.pixels[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(source.width) + static_cast<std::size_t>(sx)) * 3 + static_cast<std::size_t>(c)];
            }
        }
    }
    return output;
}

FeatureRecord AnalyzeSceneLinear(
    const LinearRgbImage& image,
    const FeatureContext& context,
    const RawEvidence::RawTechnicalEvidenceRecord* rawEvidence) {
    const auto started = std::chrono::steady_clock::now();
    FeatureRecord record = BeginRecord(image, context);
    if (!image.Valid() || !ValidContext(context) || context.transferFunction != "linear") {
        record.warnings.push_back("invalid-image-context-or-nonlinear-scene-input");
        FinalizeRecord(record, started);
        return record;
    }
    const LuminanceData luma = BuildLuminance(image, context);
    if (luma.ev.empty()) {
        record.warnings.push_back("no-positive-scene-luminance");
        FinalizeRecord(record, started);
        return record;
    }

    static constexpr std::array<std::pair<const char*, double>, 10> percentiles {{
        { "p001", 0.001 }, { "p01", 0.01 }, { "p05", 0.05 }, { "p10", 0.10 },
        { "p25", 0.25 }, { "p50", 0.50 }, { "p75", 0.75 }, { "p95", 0.95 },
        { "p99", 0.99 }, { "p999", 0.999 }
    }};
    std::unordered_map<std::string, double> percentileValues;
    for (const auto& [name, q] : percentiles) {
        const double value = Quantile(luma.ev, q);
        percentileValues[name] = value;
        record.values.push_back(MakeFeature(
            std::string("scene.ev_") + name,
            value,
            "EV",
            FeatureDisposition::Accepted,
            image,
            context,
            luma.validFraction,
            std::min(0.5, 500.0 / static_cast<double>(luma.ev.size())),
            16,
            16,
            "working-to-XYZ Y row; no silent Rec.709 substitution"));
    }
    const double meanLog = std::accumulate(luma.ev.begin(), luma.ev.end(), 0.0) /
        static_cast<double>(luma.ev.size());
    record.values.push_back(MakeFeature("scene.log_average_ev", meanLog, "EV", FeatureDisposition::Accepted,
        image, context, luma.validFraction, 0.05, 16, 16));
    record.values.push_back(MakeFeature("scene.occupied_range_ev",
        percentileValues["p99"] - percentileValues["p01"], "EV", FeatureDisposition::Accepted,
        image, context, luma.validFraction, 0.08, 16, 16));
    record.values.push_back(MakeFeature("scene.negative_channel_fraction", luma.negativeChannelFraction,
        "fraction", FeatureDisposition::Accepted, image, context, 1.0, 0.02, 1, 1));
    record.values.push_back(MakeFeature("color.gamut_pressure_fraction", luma.gamutPressureFraction,
        "fraction", FeatureDisposition::Accepted, image, context, 1.0, 0.08, 1, 1,
        "pre-map working-space pressure; not display clipping"));

    const double p25 = percentileValues["p25"];
    const double p50 = percentileValues["p50"];
    const double p95 = percentileValues["p95"];
    const double shadowMass = static_cast<double>(std::count_if(luma.ev.begin(), luma.ev.end(), [p50](double value) {
        return value < p50 - 2.0;
    })) / static_cast<double>(luma.ev.size());
    const double highlightMass = static_cast<double>(std::count_if(luma.ev.begin(), luma.ev.end(), [p50](double value) {
        return value > p50 + 2.0;
    })) / static_cast<double>(luma.ev.size());
    record.values.push_back(MakeFeature("scene.shadow_mass_fraction", shadowMass, "fraction",
        FeatureDisposition::Accepted, image, context, luma.validFraction, 0.10, 32, 32));
    record.values.push_back(MakeFeature("scene.highlight_mass_fraction", highlightMass, "fraction",
        FeatureDisposition::Accepted, image, context, luma.validFraction, 0.10, 32, 32));
    const double highKeyAmbiguity = SmoothStep(0.0, 2.0, p50) * (1.0 - SmoothStep(0.05, 0.30, shadowMass));
    const double lowKeyAmbiguity = (1.0 - SmoothStep(-3.5, -1.0, p50)) *
        (1.0 - SmoothStep(0.10, 0.45, highlightMass));
    record.values.push_back(MakeFeature("scene.high_key_ambiguity", highKeyAmbiguity, "confidence-0-to-1",
        FeatureDisposition::NeedsHumanStudy, image, context, luma.validFraction, 0.65, 64, 64,
        "histogram evidence only; never intent truth"));
    record.values.push_back(MakeFeature("scene.low_key_ambiguity", lowKeyAmbiguity, "confidence-0-to-1",
        FeatureDisposition::NeedsHumanStudy, image, context, luma.validFraction, 0.65, 64, 64,
        "histogram evidence only; never intent truth"));

    if (context.orientationNormalized) {
        const int borderX = std::max(1, image.width * 15 / 100);
        const int borderY = std::max(1, image.height * 15 / 100);
        const int centerLeft = image.width / 4;
        const int centerRight = image.width - centerLeft;
        const int centerTop = image.height / 4;
        const int centerBottom = image.height - centerTop;
        auto toGreyRelative = [&](std::vector<double> values) {
            if (values.empty()) return std::numeric_limits<double>::quiet_NaN();
            return Median(std::move(values)) - std::log2(context.referenceGrey);
        };
        const double centerEv = toGreyRelative(RegionEv(luma, image.width, image.height,
            [&](int x, int y) { return x >= centerLeft && x < centerRight && y >= centerTop && y < centerBottom; }));
        const double borderEv = toGreyRelative(RegionEv(luma, image.width, image.height,
            [&](int x, int y) { return x < borderX || x >= image.width - borderX || y < borderY || y >= image.height - borderY; }));
        const double topEv = toGreyRelative(RegionEv(luma, image.width, image.height,
            [&](int, int y) { return y < image.height / 4; }));
        const double bottomEv = toGreyRelative(RegionEv(luma, image.width, image.height,
            [&](int, int y) { return y >= image.height * 3 / 4; }));
        record.values.push_back(MakeFeature("region.center_median_ev", centerEv, "EV", FeatureDisposition::Accepted,
            image, context, luma.validFraction, 0.30, 64, 64, "center heuristic; no semantic claim"));
        record.values.push_back(MakeFeature("region.border_median_ev", borderEv, "EV", FeatureDisposition::Accepted,
            image, context, luma.validFraction, 0.25, 64, 64));
        record.values.push_back(MakeFeature("region.top_median_ev", topEv, "EV", FeatureDisposition::Accepted,
            image, context, luma.validFraction, 0.25, 64, 64));
        record.values.push_back(MakeFeature("region.bottom_median_ev", bottomEv, "EV", FeatureDisposition::Accepted,
            image, context, luma.validFraction, 0.25, 64, 64));
        record.values.push_back(MakeFeature("region.bright_border_center_conflict_ev", borderEv - centerEv,
            "EV", FeatureDisposition::Accepted, image, context, luma.validFraction, 0.35, 64, 64,
            "orientation-normalized regional conflict; not subject certainty"));
    } else {
        for (const char* id : { "region.center_median_ev", "region.border_median_ev", "region.top_median_ev",
                               "region.bottom_median_ev", "region.bright_border_center_conflict_ev" }) {
            record.values.push_back(UnavailableFeature(id, "EV", FeatureDisposition::Accepted, image, context,
                "orientation-not-normalized", 64, 64));
        }
    }

    for (int radius : { 1, 2, 4, 8 }) {
        const int minimum = std::max(32, radius * 8);
        const std::vector<float> base = BoxBlur(luma.logLuma, image.width, image.height, radius);
        double detailSum = 0.0;
        double gradientSum = 0.0;
        std::uint64_t count = 0;
        for (int y = 1; y + 1 < image.height; ++y) {
            for (int x = 1; x + 1 < image.width; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x);
                if (!luma.valid[index]) continue;
                const double detail = static_cast<double>(luma.logLuma[index]) - base[index];
                detailSum += detail * detail;
                const double gx = 0.5 * (base[index + 1] - base[index - 1]);
                const double gy = 0.5 * (base[index + static_cast<std::size_t>(image.width)] -
                    base[index - static_cast<std::size_t>(image.width)]);
                gradientSum += gx * gx + gy * gy;
                ++count;
            }
        }
        const double validFraction = static_cast<double>(count) /
            static_cast<double>(std::max(1, (image.width - 2) * (image.height - 2)));
        record.values.push_back(MakeFeature("multiscale.detail_rms_r" + std::to_string(radius),
            count > 0 ? std::sqrt(detailSum / count) : std::numeric_limits<double>::quiet_NaN(),
            "natural-log-luminance-rms", FeatureDisposition::Accepted, image, context,
            validFraction, 0.20, minimum, minimum));
        record.values.push_back(MakeFeature("multiscale.base_gradient_rms_r" + std::to_string(radius),
            count > 0 ? std::sqrt(gradientSum / count) : std::numeric_limits<double>::quiet_NaN(),
            "natural-log-luminance-gradient-rms", FeatureDisposition::Accepted, image, context,
            validFraction, 0.20, minimum, minimum));
    }

    const std::vector<float> lumaBase = BoxBlur(luma.logLuma, image.width, image.height, 1);
    std::vector<double> gradientMagnitudes;
    gradientMagnitudes.reserve(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height));
    for (int y = 1; y + 1 < image.height; ++y) {
        for (int x = 1; x + 1 < image.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x);
            const double gx = 0.5 * (lumaBase[index + 1] - lumaBase[index - 1]);
            const double gy = 0.5 * (lumaBase[index + static_cast<std::size_t>(image.width)] - lumaBase[index - static_cast<std::size_t>(image.width)]);
            gradientMagnitudes.push_back(std::sqrt(gx * gx + gy * gy));
        }
    }
    const double smoothThreshold = Quantile(gradientMagnitudes, 0.35);
    const double shadowThreshold = std::pow(2.0, p25) * context.referenceGrey;
    double lumaNoiseSum = 0.0;
    double chromaNoiseSum = 0.0;
    std::uint64_t noiseCount = 0;
    for (int y = 1; y + 1 < image.height; ++y) {
        for (int x = 1; x + 1 < image.width; ++x) {
            const std::size_t pixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x);
            const std::size_t gradientIndex = static_cast<std::size_t>(y - 1) * static_cast<std::size_t>(image.width - 2) + static_cast<std::size_t>(x - 1);
            if (!luma.valid[pixel] || luma.linear[pixel] > shadowThreshold ||
                gradientIndex >= gradientMagnitudes.size() || gradientMagnitudes[gradientIndex] > smoothThreshold) continue;
            const double residual = static_cast<double>(luma.logLuma[pixel]) - lumaBase[pixel];
            lumaNoiseSum += residual * residual;
            const std::size_t rgb = pixel * 3;
            const double rg = image.pixels[rgb] - image.pixels[rgb + 1];
            const double bg = image.pixels[rgb + 2] - image.pixels[rgb + 1];
            double meanRg = 0.0;
            double meanBg = 0.0;
            int samples = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const std::size_t neighbor = (static_cast<std::size_t>(y + dy) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x + dx)) * 3;
                    meanRg += image.pixels[neighbor] - image.pixels[neighbor + 1];
                    meanBg += image.pixels[neighbor + 2] - image.pixels[neighbor + 1];
                    ++samples;
                }
            }
            meanRg /= samples;
            meanBg /= samples;
            chromaNoiseSum += (rg - meanRg) * (rg - meanRg) + (bg - meanBg) * (bg - meanBg);
            ++noiseCount;
        }
    }
    const double noiseValidFraction = static_cast<double>(noiseCount) /
        static_cast<double>(std::max(1, image.width * image.height));
    record.values.push_back(MakeFeature("noise.rendered_luma_residual_rms",
        noiseCount > 0 ? std::sqrt(lumaNoiseSum / noiseCount) : std::numeric_limits<double>::quiet_NaN(),
        "natural-log-luminance-rms", FeatureDisposition::Accepted, image, context,
        noiseValidFraction, noiseValidFraction < 0.01 ? 0.75 : 0.45, 128, 128,
        "smooth-shadow high-frequency residual; texture confusion remains uncertainty"));
    record.values.push_back(MakeFeature("noise.rendered_chroma_residual_rms",
        noiseCount > 0 ? std::sqrt(chromaNoiseSum / (2.0 * noiseCount)) : std::numeric_limits<double>::quiet_NaN(),
        "linear-rgb-opponent-rms", FeatureDisposition::Accepted, image, context,
        noiseValidFraction, noiseValidFraction < 0.01 ? 0.80 : 0.50, 128, 128));
    const double maximumLift = std::max(context.globalLiftEv, context.maximumLocalLiftEv);
    record.values.push_back(MakeFeature("noise.predicted_visible_amplification",
        std::exp2(maximumLift), "linear-gain", FeatureDisposition::Accepted,
        image, context, 1.0, 0.05, 1, 1, "gain scales signal and noise; it does not create sensor SNR"));

    const std::array<double, 3> gray = GrayWorldEstimate(image, 0, 0, image.width, image.height, 1.0);
    const std::array<double, 3> shades = GrayWorldEstimate(image, 0, 0, image.width, image.height, 6.0);
    const std::array<double, 3> edge = GrayEdgeEstimate(image);
    const double estimatorDisagreement = std::max({
        GainDisagreement(gray, shades), GainDisagreement(gray, edge), GainDisagreement(shades, edge)
    });
    record.values.push_back(MakeFeature("wb.estimator_disagreement_ev", estimatorDisagreement,
        "max-channel-log2-gain-distance", FeatureDisposition::Accepted, image, context,
        luma.validFraction, 0.40, 128, 128, "uncertainty only; never a WB truth target"));
    std::vector<std::array<double, 3>> quadrantGains;
    for (int qy = 0; qy < 2; ++qy) {
        for (int qx = 0; qx < 2; ++qx) {
            quadrantGains.push_back(GrayWorldEstimate(
                image,
                qx * image.width / 2,
                qy * image.height / 2,
                (qx + 1) * image.width / 2,
                (qy + 1) * image.height / 2,
                1.0));
        }
    }
    double spatialDisagreement = 0.0;
    for (std::size_t i = 0; i < quadrantGains.size(); ++i) {
        for (std::size_t j = i + 1; j < quadrantGains.size(); ++j) {
            spatialDisagreement = std::max(spatialDisagreement, GainDisagreement(quadrantGains[i], quadrantGains[j]));
        }
    }
    record.values.push_back(MakeFeature("wb.spatial_disagreement_ev", spatialDisagreement,
        "max-channel-log2-gain-distance", FeatureDisposition::Accepted, image, context,
        luma.validFraction, 0.45, 128, 128, "mixed-light uncertainty; no local-WB authorization"));

    const bool rawEvidenceMatchesSource = rawEvidence != nullptr && rawEvidence->valid &&
        rawEvidence->featureVersion == RawEvidence::kRawTechnicalEvidenceVersion &&
        rawEvidence->sourceIdentity.sha256 == context.sourceIdentity &&
        !context.rawEvidenceIdentity.empty() &&
        rawEvidence->evidenceIdentitySha256 == context.rawEvidenceIdentity;
    if (rawEvidenceMatchesSource) {
        const auto addRawFraction = [&](const char* id, const RawEvidence::EvidenceMeasurement& measurement) {
            if (measurement.valid) {
                record.values.push_back(MakeFeature(id, measurement.value, measurement.units,
                    FeatureDisposition::Accepted, image, context, 1.0,
                    std::min(1.0, measurement.uncertainty01 + 0.05), 1, 1,
                    "carried from " + rawEvidence->featureVersion));
            } else {
                record.values.push_back(UnavailableFeature(id, measurement.units,
                    FeatureDisposition::Accepted, image, context, measurement.reason));
            }
        };
        addRawFraction("highlight.raw_partial_clip_fraction", rawEvidence->clipping.singleChannelClippedFraction);
        addRawFraction("highlight.raw_multi_clip_fraction", rawEvidence->clipping.multiChannelClippedFraction);
        addRawFraction("highlight.raw_all_clip_fraction", rawEvidence->clipping.allChannelClippedFraction);
        double minimumSnr = std::numeric_limits<double>::infinity();
        for (const RawEvidence::NoisePlaneEvidence& plane : rawEvidence->noise) {
            for (const RawEvidence::EvidenceMeasurement& snr : plane.snrBySignal) {
                if (snr.valid) minimumSnr = std::min(minimumSnr, snr.value);
            }
        }
        if (std::isfinite(minimumSnr)) {
            record.values.push_back(MakeFeature("noise.raw_predicted_snr_min", minimumSnr,
                "linear-SNR", FeatureDisposition::Accepted, image, context, 1.0, 0.15, 1, 1,
                "Phase 01 NoiseProfile prior"));
        } else {
            record.values.push_back(UnavailableFeature("noise.raw_predicted_snr_min", "linear-SNR",
                FeatureDisposition::Accepted, image, context, "Phase 01 NoiseProfile unavailable"));
        }
    } else {
        if (rawEvidence != nullptr && rawEvidence->valid) {
            record.warnings.push_back("Phase 01 raw evidence rejected: source or feature identity mismatch");
        }
        for (const char* id : { "highlight.raw_partial_clip_fraction", "highlight.raw_multi_clip_fraction",
                               "highlight.raw_all_clip_fraction", "noise.raw_predicted_snr_min" }) {
            record.values.push_back(UnavailableFeature(id,
                std::string(id).find("snr") != std::string::npos ? "linear-SNR" : "fraction",
                FeatureDisposition::Accepted, image, context, "Phase 01 record missing or identity-invalid"));
        }
    }

    FinalizeRecord(record, started);
    return record;
}

FeatureRecord AnalyzeRegionMask(
    const LinearRgbImage& sceneImage,
    const ScalarMask& mask,
    const FeatureContext& context) {
    const auto started = std::chrono::steady_clock::now();
    FeatureRecord record = BeginRecord(sceneImage, context);
    if (!sceneImage.Valid() || !mask.Valid() || mask.width != sceneImage.width ||
        mask.height != sceneImage.height || !ValidContext(context) ||
        context.transferFunction != "linear" || !context.orientationNormalized) {
        record.warnings.push_back("mask-scene-shape-context-or-orientation-invalid");
        FinalizeRecord(record, started);
        return record;
    }

    const LuminanceData luma = BuildLuminance(sceneImage, context);
    double foregroundWeight = 0.0;
    double backgroundWeight = 0.0;
    double foregroundEvSum = 0.0;
    double backgroundEvSum = 0.0;
    double uncertaintySum = 0.0;
    double maskStrengthSum = 0.0;
    std::uint64_t validMaskCount = 0;
    for (std::size_t i = 0; i < mask.values.size(); ++i) {
        const double rawMask = mask.values[i];
        if (!std::isfinite(rawMask)) continue;
        const double m = std::clamp(rawMask, 0.0, 1.0);
        uncertaintySum += 4.0 * m * (1.0 - m);
        maskStrengthSum += m;
        ++validMaskCount;
        if (!luma.valid[i]) continue;
        const double ev = std::log2(std::max(kEpsilon, luma.linear[i]) / context.referenceGrey);
        foregroundEvSum += m * ev;
        foregroundWeight += m;
        backgroundEvSum += (1.0 - m) * ev;
        backgroundWeight += 1.0 - m;
    }
    const double validFraction = static_cast<double>(validMaskCount) /
        static_cast<double>(std::max<std::size_t>(1, mask.values.size()));
    record.values.push_back(MakeFeature("mask.mean_strength", validMaskCount > 0
            ? maskStrengthSum / static_cast<double>(validMaskCount)
            : std::numeric_limits<double>::quiet_NaN(),
        "fraction", FeatureDisposition::Accepted, sceneImage, context,
        validFraction, 0.10, 64, 64, "continuous mask statistic; no semantic truth claim"));
    record.values.push_back(MakeFeature("mask.uncertainty_mean", validMaskCount > 0
            ? uncertaintySum / static_cast<double>(validMaskCount)
            : std::numeric_limits<double>::quiet_NaN(),
        "uncertainty-0-to-1", FeatureDisposition::Accepted, sceneImage, context,
        validFraction, 0.20, 64, 64, "4*m*(1-m); zero for decisive masks and one at 0.5"));
    record.values.push_back(MakeFeature("region.mask_foreground_mean_ev", foregroundWeight > kEpsilon
            ? foregroundEvSum / foregroundWeight
            : std::numeric_limits<double>::quiet_NaN(),
        "EV", FeatureDisposition::Accepted, sceneImage, context,
        validFraction, foregroundWeight > 32.0 ? 0.25 : 0.80, 64, 64));
    record.values.push_back(MakeFeature("region.mask_background_mean_ev", backgroundWeight > kEpsilon
            ? backgroundEvSum / backgroundWeight
            : std::numeric_limits<double>::quiet_NaN(),
        "EV", FeatureDisposition::Accepted, sceneImage, context,
        validFraction, backgroundWeight > 32.0 ? 0.25 : 0.80, 64, 64));
    record.values.push_back(MakeFeature("region.mask_background_foreground_conflict_ev",
        foregroundWeight > kEpsilon && backgroundWeight > kEpsilon
            ? backgroundEvSum / backgroundWeight - foregroundEvSum / foregroundWeight
            : std::numeric_limits<double>::quiet_NaN(),
        "EV", FeatureDisposition::Accepted, sceneImage, context,
        validFraction, std::min(foregroundWeight, backgroundWeight) > 32.0 ? 0.30 : 0.85,
        64, 64, "signed regional conflict; semantics belong to the mask producer"));

    std::vector<double> luminanceGradients;
    luminanceGradients.reserve(static_cast<std::size_t>(std::max(0, sceneImage.width - 2)) *
        static_cast<std::size_t>(std::max(0, sceneImage.height - 2)));
    struct BoundarySample { double maskGradient; double lumaGradient; };
    std::vector<BoundarySample> boundarySamples;
    boundarySamples.reserve(luminanceGradients.capacity());
    for (int y = 1; y + 1 < sceneImage.height; ++y) {
        for (int x = 1; x + 1 < sceneImage.width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(sceneImage.width) + static_cast<std::size_t>(x);
            const double lgx = 0.5 * (luma.logLuma[i + 1] - luma.logLuma[i - 1]);
            const double lgy = 0.5 * (luma.logLuma[i + static_cast<std::size_t>(sceneImage.width)] -
                luma.logLuma[i - static_cast<std::size_t>(sceneImage.width)]);
            const double lumaGradient = std::sqrt(lgx * lgx + lgy * lgy);
            luminanceGradients.push_back(lumaGradient);
            const double mgx = 0.5 * (std::clamp(static_cast<double>(mask.values[i + 1]), 0.0, 1.0) -
                std::clamp(static_cast<double>(mask.values[i - 1]), 0.0, 1.0));
            const double mgy = 0.5 * (std::clamp(static_cast<double>(mask.values[i + static_cast<std::size_t>(sceneImage.width)]), 0.0, 1.0) -
                std::clamp(static_cast<double>(mask.values[i - static_cast<std::size_t>(sceneImage.width)]), 0.0, 1.0));
            const double maskGradient = std::sqrt(mgx * mgx + mgy * mgy);
            if (std::isfinite(maskGradient + lumaGradient) && maskGradient > 0.0) {
                boundarySamples.push_back({ maskGradient, lumaGradient });
            }
        }
    }
    const double lumaScale = Quantile(luminanceGradients, 0.90);
    double boundaryWeight = 0.0;
    double supportSum = 0.0;
    for (const BoundarySample& sample : boundarySamples) {
        const double support = std::clamp(sample.lumaGradient / std::max(kEpsilon, lumaScale), 0.0, 1.0);
        supportSum += sample.maskGradient * support;
        boundaryWeight += sample.maskGradient;
    }
    const double boundarySupport = boundaryWeight > kEpsilon
        ? supportSum / boundaryWeight
        : std::numeric_limits<double>::quiet_NaN();
    record.values.push_back(MakeFeature("mask.boundary_support", boundarySupport,
        "normalized-gradient-support", FeatureDisposition::Accepted, sceneImage, context,
        validFraction, boundaryWeight > 1.0 ? 0.35 : 0.85, 128, 128,
        "continuous alignment with scene-luminance gradients; not a semantic accuracy score"));
    record.values.push_back(MakeFeature("mask.boundary_leakage_risk",
        std::isfinite(boundarySupport) ? 1.0 - boundarySupport : boundarySupport,
        "uncertainty-0-to-1", FeatureDisposition::Accepted, sceneImage, context,
        validFraction, boundaryWeight > 1.0 ? 0.45 : 0.90, 128, 128,
        "unsupported-boundary evidence; requires semantic labels before perceptual use"));

    FinalizeRecord(record, started);
    return record;
}

FeatureRecord AnalyzeDisplayMapped(
    const LinearRgbImage& image,
    const FeatureContext& context,
    const DisplayModel& displayModel) {
    const auto started = std::chrono::steady_clock::now();
    FeatureRecord record = BeginRecord(image, context);
    if (!image.Valid() || !ValidContext(context) || context.transferFunction != "linear") {
        record.warnings.push_back("display-input-must-be-declared-linear-display-rgb");
        FinalizeRecord(record, started);
        return record;
    }
    std::vector<double> linearY;
    std::vector<double> encodedY;
    linearY.reserve(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height));
    encodedY.reserve(linearY.capacity());
    std::uint64_t high = 0;
    std::uint64_t low = 0;
    const std::array<double, 3> yRow {
        context.workingToXyz[3], context.workingToXyz[4], context.workingToXyz[5]
    };
    for (std::size_t i = 0; i < image.pixels.size(); i += 3) {
        const double r = image.pixels[i];
        const double g = image.pixels[i + 1];
        const double b = image.pixels[i + 2];
        if (!std::isfinite(r + g + b)) continue;
        if (std::max({ r, g, b }) >= 1.0) ++high;
        if (std::min({ r, g, b }) <= 0.0) ++low;
        linearY.push_back(std::max(0.0, yRow[0] * r + yRow[1] * g + yRow[2] * b));
        encodedY.push_back(std::clamp(
            yRow[0] * SrgbEncode(r) + yRow[1] * SrgbEncode(g) + yRow[2] * SrgbEncode(b), 0.0, 1.0));
    }
    const double validFraction = static_cast<double>(linearY.size()) /
        static_cast<double>(std::max<std::size_t>(1, image.pixels.size() / 3));
    const double count = static_cast<double>(std::max<std::size_t>(1, linearY.size()));
    record.values.push_back(MakeFeature("display.linear_clip_high_fraction", high / count, "fraction",
        FeatureDisposition::Accepted, image, context, validFraction, 0.05, 16, 16));
    record.values.push_back(MakeFeature("display.linear_clip_low_fraction", low / count, "fraction",
        FeatureDisposition::Accepted, image, context, validFraction, 0.05, 16, 16));
    for (const auto& [name, q] : std::array<std::pair<const char*, double>, 3>{{ { "p05", 0.05 }, { "p50", 0.5 }, { "p95", 0.95 } }}) {
        record.values.push_back(MakeFeature(std::string("display.linear_") + name,
            Quantile(linearY, q), "linear-display-luminance-normalized",
            FeatureDisposition::Accepted, image, context, validFraction, 0.05, 16, 16));
        FeatureContext encodedContext = context;
        encodedContext.transferFunction = displayModel.transferFunction;
        record.values.push_back(MakeFeature(std::string("display.encoded_") + name,
            Quantile(encodedY, q), "encoded-display-code-value",
            FeatureDisposition::Accepted, image, encodedContext, validFraction, 0.05, 16, 16));
    }
    const double linearP01 = Quantile(linearY, 0.01);
    const double linearP05 = Quantile(linearY, 0.05);
    const double linearP95 = Quantile(linearY, 0.95);
    const double linearP99 = Quantile(linearY, 0.99);
    const double linearP999 = Quantile(linearY, 0.999);
    record.values.push_back(MakeFeature("display.highlight_rolloff_ratio",
        (linearP999 - linearP99) / std::max(kEpsilon, linearP99 - linearP95),
        "tail-slope-ratio", FeatureDisposition::PrototypeOnly, image, context,
        validFraction, 0.50, 128, 128, "distribution continuity proxy; not a spatial halo metric"));
    record.values.push_back(MakeFeature("display.shadow_toe_ratio",
        (linearP05 - linearP01) / std::max(kEpsilon, linearP95 - linearP05),
        "tail-slope-ratio", FeatureDisposition::PrototypeOnly, image, context,
        validFraction, 0.50, 128, 128));
    if (displayModel.absoluteLuminanceKnown &&
        displayModel.peakLuminanceCdM2 > displayModel.blackLuminanceCdM2 &&
        displayModel.blackLuminanceCdM2 >= 0.0 && displayModel.ambientReflectionCdM2 >= 0.0) {
        const double black = displayModel.blackLuminanceCdM2 + displayModel.ambientReflectionCdM2;
        const double white = displayModel.peakLuminanceCdM2 + displayModel.ambientReflectionCdM2;
        record.values.push_back(MakeFeature("display.absolute_dynamic_range",
            white / std::max(kEpsilon, black), "luminance-ratio",
            FeatureDisposition::Accepted, image, context, 1.0, 0.10, 1, 1,
            "calibrated display model"));
    } else {
        record.values.push_back(UnavailableFeature("display.absolute_dynamic_range", "luminance-ratio",
            FeatureDisposition::Accepted, image, context,
            "absolute display luminance/black/ambient unavailable"));
    }
    FinalizeRecord(record, started);
    return record;
}

FeatureRecord CompareRenderedImages(
    const LinearRgbImage& reference,
    const LinearRgbImage& candidate,
    const FeatureContext& context) {
    const auto started = std::chrono::steady_clock::now();
    FeatureRecord record = BeginRecord(candidate, context);
    if (!reference.Valid() || !candidate.Valid() || reference.width != candidate.width ||
        reference.height != candidate.height || !ValidContext(context)) {
        record.warnings.push_back("reference-candidate-shape-or-context-mismatch");
        FinalizeRecord(record, started);
        return record;
    }
    const LuminanceData ref = BuildLuminance(reference, context);
    const LuminanceData cand = BuildLuminance(candidate, context);
    std::vector<double> edgeMagnitudes;
    edgeMagnitudes.reserve(static_cast<std::size_t>(reference.width) * static_cast<std::size_t>(reference.height));
    for (int y = 2; y + 2 < reference.height; ++y) {
        for (int x = 2; x + 2 < reference.width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(reference.width) + static_cast<std::size_t>(x);
            const double gx = 0.5 * (ref.logLuma[i + 1] - ref.logLuma[i - 1]);
            const double gy = 0.5 * (ref.logLuma[i + static_cast<std::size_t>(reference.width)] - ref.logLuma[i - static_cast<std::size_t>(reference.width)]);
            edgeMagnitudes.push_back(std::sqrt(gx * gx + gy * gy));
        }
    }
    const double edgeThreshold = Quantile(edgeMagnitudes, 0.90);
    double reversal = 0.0;
    double overshoot = 0.0;
    double band = 0.0;
    double shift = 0.0;
    double refTexture = 0.0;
    double candTexture = 0.0;
    std::uint64_t edgeCount = 0;
    const std::vector<float> refBase = BoxBlur(ref.logLuma, reference.width, reference.height, 2);
    const std::vector<float> candBase = BoxBlur(cand.logLuma, candidate.width, candidate.height, 2);
    for (int y = 3; y + 3 < reference.height; ++y) {
        for (int x = 3; x + 3 < reference.width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(reference.width) + static_cast<std::size_t>(x);
            const double rgx = 0.5 * (ref.logLuma[i + 1] - ref.logLuma[i - 1]);
            const double rgy = 0.5 * (ref.logLuma[i + static_cast<std::size_t>(reference.width)] - ref.logLuma[i - static_cast<std::size_t>(reference.width)]);
            const double magnitude = std::sqrt(rgx * rgx + rgy * rgy);
            if (!(magnitude >= edgeThreshold) || magnitude <= 1.0e-6) continue;
            const double cgx = 0.5 * (cand.logLuma[i + 1] - cand.logLuma[i - 1]);
            const double cgy = 0.5 * (cand.logLuma[i + static_cast<std::size_t>(candidate.width)] - cand.logLuma[i - static_cast<std::size_t>(candidate.width)]);
            const double dot = rgx * cgx + rgy * cgy;
            if (dot < 0.0) reversal += std::sqrt(cgx * cgx + cgy * cgy) / magnitude;
            double localMin = std::numeric_limits<double>::infinity();
            double localMax = -std::numeric_limits<double>::infinity();
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    const std::size_t n = static_cast<std::size_t>(y + dy) * static_cast<std::size_t>(reference.width) + static_cast<std::size_t>(x + dx);
                    localMin = std::min(localMin, static_cast<double>(ref.logLuma[n]));
                    localMax = std::max(localMax, static_cast<double>(ref.logLuma[n]));
                }
            }
            overshoot += std::max(0.0, static_cast<double>(cand.logLuma[i]) - localMax) +
                std::max(0.0, localMin - static_cast<double>(cand.logLuma[i]));
            const double difference = (cand.logLuma[i] - ref.logLuma[i]) - (candBase[i] - refBase[i]);
            band += std::abs(difference) / std::max(0.05, magnitude);
            const bool horizontal = std::abs(rgx) >= std::abs(rgy);
            int bestOffset = 0;
            double bestGradient = -1.0;
            for (int offset = -2; offset <= 2; ++offset) {
                const int sx = x + (horizontal ? offset : 0);
                const int sy = y + (horizontal ? 0 : offset);
                const std::size_t s = static_cast<std::size_t>(sy) * static_cast<std::size_t>(candidate.width) + static_cast<std::size_t>(sx);
                const double gradient = horizontal
                    ? std::abs(cand.logLuma[s + 1] - cand.logLuma[s - 1])
                    : std::abs(cand.logLuma[s + static_cast<std::size_t>(candidate.width)] - cand.logLuma[s - static_cast<std::size_t>(candidate.width)]);
                if (gradient > bestGradient) {
                    bestGradient = gradient;
                    bestOffset = offset;
                }
            }
            shift += std::abs(bestOffset);
            const double rd = ref.logLuma[i] - refBase[i];
            const double cd = cand.logLuma[i] - candBase[i];
            refTexture += rd * rd;
            candTexture += cd * cd;
            ++edgeCount;
        }
    }
    const double validFraction = static_cast<double>(edgeCount) /
        static_cast<double>(std::max(1, (reference.width - 6) * (reference.height - 6)));
    if (edgeCount > 0) {
        record.values.push_back(MakeFeature("halo.gradient_reversal_energy", reversal / edgeCount,
            "candidate-normal-gradient-per-reference-gradient", FeatureDisposition::Accepted,
            candidate, context, validFraction, validFraction < 0.001 ? 0.75 : 0.35, 128, 128));
        record.values.push_back(MakeFeature("halo.overshoot_log_luma", overshoot / edgeCount,
            "natural-log-luminance", FeatureDisposition::Accepted,
            candidate, context, validFraction, 0.45, 128, 128));
        record.values.push_back(MakeFeature("halo.adjacent_band_energy", band / edgeCount,
            "contrast-normalized-band-energy", FeatureDisposition::Accepted,
            candidate, context, validFraction, 0.45, 128, 128));
        record.values.push_back(MakeFeature("halo.edge_shift_pixels", shift / edgeCount,
            "pixels", FeatureDisposition::Accepted,
            candidate, context, validFraction, 0.40, 128, 128));
        record.values.push_back(MakeFeature("halo.edge_texture_energy_ratio",
            candTexture / std::max(kEpsilon, refTexture), "ratio",
            FeatureDisposition::PrototypeOnly, candidate, context, validFraction, 0.55, 128, 128,
            "contrast-halo/texture suppression diagnostic"));
    } else {
        for (const auto& [id, units, disposition] : std::array<std::tuple<const char*, const char*, FeatureDisposition>, 5>{{
            { "halo.gradient_reversal_energy", "candidate-normal-gradient-per-reference-gradient", FeatureDisposition::Accepted },
            { "halo.overshoot_log_luma", "natural-log-luminance", FeatureDisposition::Accepted },
            { "halo.adjacent_band_energy", "contrast-normalized-band-energy", FeatureDisposition::Accepted },
            { "halo.edge_shift_pixels", "pixels", FeatureDisposition::Accepted },
            { "halo.edge_texture_energy_ratio", "ratio", FeatureDisposition::PrototypeOnly }
        }}) {
            record.values.push_back(UnavailableFeature(id, units, disposition, candidate, context,
                "no-confident-reference-edges", 128, 128));
        }
    }

    double hueWeighted = 0.0;
    double hueWeight = 0.0;
    double saturationDelta = 0.0;
    std::uint64_t colorCount = 0;
    for (std::size_t i = 0; i < reference.pixels.size(); i += 3) {
        double refChroma = 0.0;
        double candChroma = 0.0;
        const double refHue = Hue(reference.pixels[i], reference.pixels[i + 1], reference.pixels[i + 2], refChroma);
        const double candHue = Hue(candidate.pixels[i], candidate.pixels[i + 1], candidate.pixels[i + 2], candChroma);
        const double weight = std::min(refChroma, candChroma);
        if (weight > 0.02 && std::isfinite(refHue + candHue)) {
            hueWeighted += CircularHueDistance(refHue, candHue) * weight;
            hueWeight += weight;
        }
        saturationDelta += std::abs(candChroma - refChroma);
        ++colorCount;
    }
    record.values.push_back(hueWeight > 0.0
        ? MakeFeature("color.hue_shift_degrees", hueWeighted / hueWeight, "degrees",
            FeatureDisposition::Accepted, candidate, context, hueWeight / std::max(1.0, static_cast<double>(colorCount)),
            0.30, 64, 64, "chroma-weighted; near-neutral hue excluded")
        : UnavailableFeature("color.hue_shift_degrees", "degrees", FeatureDisposition::Accepted,
            candidate, context, "insufficient-reliable-chroma", 64, 64));
    record.values.push_back(MakeFeature("color.chroma_change_mean", saturationDelta / std::max<std::uint64_t>(1, colorCount),
        "linear-rgb-max-minus-min", FeatureDisposition::PrototypeOnly, candidate, context, 1.0, 0.35, 64, 64));

    const FeatureRecord refFeatures = AnalyzeSceneLinear(reference, context, nullptr);
    const FeatureRecord candFeatures = AnalyzeSceneLinear(candidate, context, nullptr);
    for (int radius : { 1, 2, 4, 8 }) {
        const std::string id = "multiscale.detail_rms_r" + std::to_string(radius);
        const FeatureValue* a = FindFeature(refFeatures, id);
        const FeatureValue* b = FindFeature(candFeatures, id);
        record.values.push_back(a && b && a->valid && b->valid
            ? MakeFeature("structure.detail_energy_ratio_r" + std::to_string(radius),
                b->value / std::max(kEpsilon, a->value), "ratio", FeatureDisposition::Accepted,
                candidate, context, std::min(a->validFraction, b->validFraction),
                std::max(a->uncertainty01, b->uncertainty01),
                std::max(a->minimumWidth, b->minimumWidth), std::max(a->minimumHeight, b->minimumHeight))
            : UnavailableFeature("structure.detail_energy_ratio_r" + std::to_string(radius), "ratio",
                FeatureDisposition::Accepted, candidate, context, "reference-or-candidate-scale-feature-unavailable"));
    }

    FinalizeRecord(record, started);
    return record;
}

FeatureAgreement CompareFeatureRecords(const FeatureRecord& reference, const FeatureRecord& proxy) {
    FeatureAgreement agreement;
    if (reference.sourceIdentity != proxy.sourceIdentity ||
        reference.recipeIdentity != proxy.recipeIdentity ||
        reference.stage != proxy.stage ||
        reference.colorSpace != proxy.colorSpace ||
        reference.colorTransformIdentity != proxy.colorTransformIdentity ||
        reference.transferFunction != proxy.transferFunction ||
        reference.referenceGrey != proxy.referenceGrey ||
        reference.rawEvidenceIdentity != proxy.rawEvidenceIdentity ||
        reference.cropIdentity != proxy.cropIdentity) {
        agreement.missingOrMismatched.push_back("identity-stage-space-or-crop-mismatch");
        return agreement;
    }
    double sum = 0.0;
    for (const FeatureValue& value : reference.values) {
        if (!value.valid || value.disposition == FeatureDisposition::Rejected ||
            value.id.rfind("region.", 0) == 0) continue;
        const FeatureValue* other = FindFeature(proxy, value.id);
        if (!other || !other->valid || other->units != value.units ||
            proxy.width < other->minimumWidth || proxy.height < other->minimumHeight) {
            agreement.missingOrMismatched.push_back(value.id);
            continue;
        }
        const double delta = std::abs(value.value - other->value) /
            std::max(1.0e-6, std::max(std::abs(value.value), std::abs(other->value)));
        sum += delta;
        agreement.maxRelativeDelta = std::max(agreement.maxRelativeDelta, delta);
        ++agreement.comparableFeatureCount;
    }
    agreement.valid = agreement.comparableFeatureCount > 0;
    agreement.meanRelativeDelta = agreement.valid ? sum / agreement.comparableFeatureCount : 0.0;
    return agreement;
}

bool FeatureRecordMatches(
    const FeatureRecord& record,
    const std::string& sourceIdentity,
    const std::string& recipeIdentity,
    const std::string& stage) {
    return record.valid && record.featureVersion == kRenderedFeatureVersion &&
        record.sourceIdentity == sourceIdentity &&
        record.recipeIdentity == recipeIdentity &&
        record.stage == stage;
}

const FeatureValue* FindFeature(const FeatureRecord& record, const std::string& id) {
    const auto found = std::find_if(record.values.begin(), record.values.end(), [&](const FeatureValue& value) {
        return value.id == id;
    });
    return found == record.values.end() ? nullptr : &*found;
}

EdgeProfileMetrics EvaluateEdgeProfile(
    const std::vector<double>& reference,
    const std::vector<double>& candidate,
    int centerIndex,
    int plateauRadius) {
    EdgeProfileMetrics metrics;
    if (reference.size() != candidate.size() || reference.size() < 9 ||
        centerIndex <= plateauRadius || centerIndex + plateauRadius >= static_cast<int>(reference.size()) - 1) {
        metrics.reason = "invalid-profile-shape-or-center";
        return metrics;
    }
    const int leftEnd = centerIndex - plateauRadius;
    const int rightStart = centerIndex + plateauRadius;
    const double refLeft = Median(std::vector<double>(reference.begin(), reference.begin() + leftEnd));
    const double refRight = Median(std::vector<double>(reference.begin() + rightStart, reference.end()));
    const double candLeft = Median(std::vector<double>(candidate.begin(), candidate.begin() + leftEnd));
    const double candRight = Median(std::vector<double>(candidate.begin() + rightStart, candidate.end()));
    metrics.referenceContrast = std::abs(refRight - refLeft);
    if (metrics.referenceContrast <= kEpsilon) {
        metrics.reason = "reference-edge-contrast-too-small";
        return metrics;
    }
    int refPeak = 0;
    int candPeak = 0;
    double refPeakValue = -1.0;
    double candPeakValue = -1.0;
    double previousCandidateGradient = 0.0;
    double previousReferenceGradient = 0.0;
    for (std::size_t i = 0; i + 1 < reference.size(); ++i) {
        const double rg = reference[i + 1] - reference[i];
        const double cg = candidate[i + 1] - candidate[i];
        if (std::abs(rg) > refPeakValue) {
            refPeakValue = std::abs(rg);
            refPeak = static_cast<int>(i);
        }
        if (std::abs(cg) > candPeakValue) {
            candPeakValue = std::abs(cg);
            candPeak = static_cast<int>(i);
        }
        if (rg * cg < 0.0) metrics.reversalEnergy += std::abs(cg) / metrics.referenceContrast;
        if (i > 0 && previousCandidateGradient * cg < 0.0 && previousReferenceGradient * rg >= 0.0) {
            metrics.newExtremaCount += 1.0;
        }
        previousCandidateGradient = cg;
        previousReferenceGradient = rg;
    }
    metrics.edgeShiftPixels = std::abs(candPeak - refPeak);
    const auto nearBegin = candidate.begin() + std::max(0, centerIndex - plateauRadius);
    const auto nearEnd = candidate.begin() + std::min(static_cast<int>(candidate.size()), centerIndex + plateauRadius + 1);
    const double nearMax = *std::max_element(nearBegin, nearEnd);
    const double nearMin = *std::min_element(nearBegin, nearEnd);
    metrics.overshoot = std::max(0.0, nearMax - std::max(candLeft, candRight)) / metrics.referenceContrast;
    metrics.undershoot = std::max(0.0, std::min(candLeft, candRight) - nearMin) / metrics.referenceContrast;
    double band = 0.0;
    int bandCount = 0;
    for (int i = std::max(0, centerIndex - plateauRadius * 2);
         i <= std::min(static_cast<int>(reference.size()) - 1, centerIndex + plateauRadius * 2);
         ++i) {
        const double plateauDifference = i < centerIndex ? candLeft - refLeft : candRight - refRight;
        band += std::abs((candidate[static_cast<std::size_t>(i)] - reference[static_cast<std::size_t>(i)]) - plateauDifference);
        ++bandCount;
    }
    metrics.adjacentBandEnergy = band / std::max(1, bandCount) / metrics.referenceContrast;
    metrics.valid = true;
    metrics.reason = "oriented-1d-edge-profile";
    return metrics;
}

std::vector<float> BilateralBasePrototype(
    const std::vector<float>& signal,
    int width,
    int height,
    int radius,
    double sigmaSpatial,
    double sigmaRange) {
    if (width <= 0 || height <= 0 || signal.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) ||
        radius < 0 || sigmaSpatial <= 0.0 || sigmaRange <= 0.0) return {};
    std::vector<float> output(signal.size(), 0.0f);
    const double spatialDenominator = 2.0 * sigmaSpatial * sigmaSpatial;
    const double rangeDenominator = 2.0 * sigmaRange * sigmaRange;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            double sum = 0.0;
            double weightSum = 0.0;
            for (int dy = -radius; dy <= radius; ++dy) {
                const int sy = std::clamp(y + dy, 0, height - 1);
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int sx = std::clamp(x + dx, 0, width - 1);
                    const std::size_t sample = static_cast<std::size_t>(sy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(sx);
                    const double spatial = std::exp(-(dx * dx + dy * dy) / spatialDenominator);
                    const double delta = signal[sample] - signal[index];
                    const double range = std::exp(-(delta * delta) / rangeDenominator);
                    const double weight = spatial * range;
                    sum += signal[sample] * weight;
                    weightSum += weight;
                }
            }
            output[index] = static_cast<float>(sum / std::max(kEpsilon, weightSum));
        }
    }
    return output;
}

std::vector<float> GuidedBasePrototype(
    const std::vector<float>& guide,
    int width,
    int height,
    int radius,
    double epsilon) {
    if (width <= 0 || height <= 0 || guide.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) ||
        radius < 0 || epsilon <= 0.0) return {};
    const std::vector<float> meanI = BoxBlur(guide, width, height, radius);
    std::vector<float> squared(guide.size(), 0.0f);
    for (std::size_t i = 0; i < guide.size(); ++i) squared[i] = guide[i] * guide[i];
    const std::vector<float> meanII = BoxBlur(squared, width, height, radius);
    std::vector<float> a(guide.size(), 0.0f);
    std::vector<float> b(guide.size(), 0.0f);
    for (std::size_t i = 0; i < guide.size(); ++i) {
        const double variance = std::max(0.0, static_cast<double>(meanII[i]) - meanI[i] * meanI[i]);
        a[i] = static_cast<float>(variance / (variance + epsilon));
        b[i] = meanI[i] - a[i] * meanI[i];
    }
    const std::vector<float> meanA = BoxBlur(a, width, height, radius);
    const std::vector<float> meanB = BoxBlur(b, width, height, radius);
    std::vector<float> output(guide.size(), 0.0f);
    for (std::size_t i = 0; i < guide.size(); ++i) output[i] = meanA[i] * guide[i] + meanB[i];
    return output;
}

std::vector<float> LocalLaplacianPrototype(
    const std::vector<float>& signal,
    int width,
    int height,
    int levels,
    double detailExponent,
    double edgeScale) {
    if (width <= 0 || height <= 0 || signal.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) ||
        levels <= 0 || detailExponent <= 0.0 || edgeScale < 0.0) return {};
    std::vector<float> current = signal;
    std::vector<float> output(signal.size(), 0.0f);
    for (int level = 0; level < levels; ++level) {
        const int radius = 1 << std::min(level, 6);
        const std::vector<float> base = BoxBlur(current, width, height, radius);
        const double levelScale = edgeScale / static_cast<double>(levels);
        for (std::size_t i = 0; i < current.size(); ++i) {
            const double detail = current[i] - base[i];
            const double remapped = std::copysign(std::pow(std::abs(detail), detailExponent), detail);
            output[i] += static_cast<float>(remapped * levelScale);
        }
        current = base;
    }
    for (std::size_t i = 0; i < output.size(); ++i) output[i] += current[i];
    return output;
}

const char* FeatureDispositionName(FeatureDisposition disposition) {
    switch (disposition) {
        case FeatureDisposition::Accepted: return "accepted";
        case FeatureDisposition::PrototypeOnly: return "prototype-only";
        case FeatureDisposition::Rejected: return "rejected";
        case FeatureDisposition::NeedsHumanStudy: return "needs-human-study";
    }
    return "prototype-only";
}

nlohmann::json SerializeFeatureRecord(const FeatureRecord& record) {
    nlohmann::json values = nlohmann::json::array();
    for (const FeatureValue& value : record.values) values.push_back(SerializeFeature(value));
    return {
        { "schemaVersion", record.schemaVersion },
        { "featureVersion", record.featureVersion },
        { "valid", record.valid },
        { "sourceIdentity", record.sourceIdentity },
        { "recipeIdentity", record.recipeIdentity },
        { "stage", record.stage },
        { "colorSpace", record.colorSpace },
        { "colorTransformIdentity", record.colorTransformIdentity },
        { "transferFunction", record.transferFunction },
        { "referenceGrey", record.referenceGrey },
        { "rawEvidenceIdentity", record.rawEvidenceIdentity },
        { "cropIdentity", record.cropIdentity },
        { "width", record.width },
        { "height", record.height },
        { "sourceWidth", record.sourceWidth },
        { "sourceHeight", record.sourceHeight },
        { "orientationNormalized", record.orientationNormalized },
        { "recordIdentity", record.recordIdentity },
        { "values", std::move(values) },
        { "warnings", record.warnings },
        { "runtimeMs", record.runtimeMs },
        { "statusMessage", record.statusMessage }
    };
}

nlohmann::json SerializeFeatureAgreement(const FeatureAgreement& agreement) {
    return {
        { "valid", agreement.valid },
        { "comparableFeatureCount", agreement.comparableFeatureCount },
        { "meanRelativeDelta", agreement.meanRelativeDelta },
        { "maxRelativeDelta", agreement.maxRelativeDelta },
        { "missingOrMismatched", agreement.missingOrMismatched }
    };
}

} // namespace Stack::RenderedFeatures
