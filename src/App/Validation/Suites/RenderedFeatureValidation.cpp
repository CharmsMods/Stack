#include "App/Validation/ValidationSuites.h"

#include "Raw/LibRawDecoder.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Raw/RenderedFeatureEvidence.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#if defined(STACK_ENABLE_LIBRAW)
#include <libraw/libraw.h>
#endif

namespace Stack::Validation {
namespace {

using RenderedFeatures::FeatureContext;
using RenderedFeatures::FeatureRecord;
using RenderedFeatures::FeatureValue;
using RenderedFeatures::LinearRgbImage;

struct Options {
    std::vector<std::filesystem::path> inputs;
    std::filesystem::path output;
    std::size_t maxFiles = std::numeric_limits<std::size_t>::max();
    int baseMaxDimension = 1024;
    int fullResolutionPerFormat = 1;
};

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool IsRawPath(const std::filesystem::path& path) {
    static const std::set<std::string> extensions {
        ".arw", ".cr2", ".cr3", ".dng", ".nef", ".nrw", ".orf", ".raf", ".raw", ".rw2"
    };
    return extensions.find(Lower(path.extension().string())) != extensions.end();
}

bool ParseOptions(int argc, char** argv, Options& options, std::string& error) {
    for (int i = 0; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        auto requireValue = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                error = std::string(name) + " requires a value.";
                return nullptr;
            }
            return argv[++i];
        };
        if (arg == "--folder" || arg == "--file") {
            const char* value = requireValue(arg.c_str());
            if (!value) return false;
            options.inputs.emplace_back(value);
        } else if (arg == "--output") {
            const char* value = requireValue("--output");
            if (!value) return false;
            options.output = value;
        } else if (arg == "--max-files") {
            const char* value = requireValue("--max-files");
            if (!value) return false;
            try { options.maxFiles = std::max<std::size_t>(1, std::stoull(value)); }
            catch (...) { error = "--max-files must be a positive integer."; return false; }
        } else if (arg == "--base-max-dimension") {
            const char* value = requireValue("--base-max-dimension");
            if (!value) return false;
            try { options.baseMaxDimension = std::max(128, std::stoi(value)); }
            catch (...) { error = "--base-max-dimension must be an integer >= 128."; return false; }
        } else if (arg == "--full-resolution-per-format") {
            const char* value = requireValue("--full-resolution-per-format");
            if (!value) return false;
            try { options.fullResolutionPerFormat = std::max(0, std::stoi(value)); }
            catch (...) { error = "--full-resolution-per-format must be a non-negative integer."; return false; }
        } else if (!arg.empty() && arg[0] == '-') {
            error = "Unknown rendered-feature validation option: " + arg;
            return false;
        } else if (!arg.empty()) {
            options.inputs.emplace_back(arg);
        }
    }
    if (options.inputs.empty()) {
        error = "Provide at least one --file, --folder, or positional RAW path.";
        return false;
    }
    return true;
}

std::vector<std::filesystem::path> CollectSources(
    const Options& options,
    std::vector<std::string>& warnings) {
    std::vector<std::filesystem::path> sources;
    std::error_code ec;
    for (const std::filesystem::path& input : options.inputs) {
        if (std::filesystem::is_regular_file(input, ec)) {
            if (IsRawPath(input)) sources.push_back(std::filesystem::absolute(input, ec));
            else warnings.push_back("Skipped non-RAW file: " + input.filename().string());
            continue;
        }
        if (std::filesystem::is_directory(input, ec)) {
            for (std::filesystem::recursive_directory_iterator iterator(
                     input, std::filesystem::directory_options::skip_permission_denied, ec), end;
                 iterator != end; iterator.increment(ec)) {
                if (ec) {
                    warnings.push_back("Directory enumeration warning: " + ec.message());
                    ec.clear();
                    continue;
                }
                if (iterator->is_regular_file(ec) && IsRawPath(iterator->path())) {
                    sources.push_back(std::filesystem::absolute(iterator->path(), ec));
                }
            }
            continue;
        }
        warnings.push_back("Input does not exist: " + input.filename().string());
    }
    std::sort(sources.begin(), sources.end());
    sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
    if (sources.size() > options.maxFiles) sources.resize(options.maxFiles);
    return sources;
}

FeatureContext LinearSrgbContext(
    const std::string& sourceIdentity,
    int sourceWidth,
    int sourceHeight,
    const std::string& stage) {
    FeatureContext context;
    context.sourceIdentity = sourceIdentity;
    context.recipeIdentity = "phase-02-neutral-libraw-validation-render-v1";
    context.stage = stage;
    context.colorSpace = "linear-sRGB-from-libraw-validation-render";
    context.colorTransformIdentity = "IEC-61966-2-1-linear-sRGB-to-XYZ-D65-v1";
    context.transferFunction = "linear";
    context.workingToXyz = {
        0.4124564, 0.3575761, 0.1804375,
        0.2126729, 0.7151522, 0.0721750,
        0.0193339, 0.1191920, 0.9503041
    };
    context.referenceGrey = 0.18;
    context.cropIdentity = "libraw-default-visible-crop";
    context.orientationNormalized = true;
    context.sourceWidth = sourceWidth;
    context.sourceHeight = sourceHeight;
    return context;
}

#if defined(STACK_ENABLE_LIBRAW)
bool LoadLinearSrgb(
    const std::filesystem::path& path,
    bool halfSize,
    LinearRgbImage& image,
    std::string& error) {
    LibRaw processor;
    processor.imgdata.params.use_camera_wb = 1;
    processor.imgdata.params.no_auto_bright = 1;
    processor.imgdata.params.output_bps = 16;
    processor.imgdata.params.output_color = 1;
    processor.imgdata.params.gamm[0] = 1.0;
    processor.imgdata.params.gamm[1] = 1.0;
    processor.imgdata.params.half_size = halfSize ? 1 : 0;
    int status = processor.open_file(path.string().c_str());
    if (status != LIBRAW_SUCCESS) {
        error = std::string("LibRaw open failed: ") + libraw_strerror(status);
        return false;
    }
    status = processor.unpack();
    if (status != LIBRAW_SUCCESS) {
        error = std::string("LibRaw unpack failed: ") + libraw_strerror(status);
        return false;
    }
    status = processor.dcraw_process();
    if (status != LIBRAW_SUCCESS) {
        error = std::string("LibRaw validation render failed: ") + libraw_strerror(status);
        return false;
    }
    int memoryError = LIBRAW_SUCCESS;
    libraw_processed_image_t* processed = processor.dcraw_make_mem_image(&memoryError);
    if (!processed || memoryError != LIBRAW_SUCCESS || processed->type != LIBRAW_IMAGE_BITMAP ||
        processed->width <= 0 || processed->height <= 0 || processed->colors < 3 ||
        (processed->bits != 8 && processed->bits != 16)) {
        error = "LibRaw validation render did not return an RGB bitmap.";
        if (processed) LibRaw::dcraw_clear_mem(processed);
        return false;
    }
    image.width = static_cast<int>(processed->width);
    image.height = static_cast<int>(processed->height);
    const std::size_t pixelCount = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    image.pixels.assign(pixelCount * 3, 0.0f);
    if (processed->bits == 16) {
        const auto* source = reinterpret_cast<const std::uint16_t*>(processed->data);
        for (std::size_t i = 0; i < pixelCount; ++i) {
            for (int c = 0; c < 3; ++c) {
                image.pixels[i * 3 + static_cast<std::size_t>(c)] =
                    static_cast<float>(source[i * processed->colors + static_cast<std::size_t>(c)]) / 65535.0f;
            }
        }
    } else {
        const auto* source = reinterpret_cast<const std::uint8_t*>(processed->data);
        for (std::size_t i = 0; i < pixelCount; ++i) {
            for (int c = 0; c < 3; ++c) {
                image.pixels[i * 3 + static_cast<std::size_t>(c)] =
                    static_cast<float>(source[i * processed->colors + static_cast<std::size_t>(c)]) / 255.0f;
            }
        }
    }
    LibRaw::dcraw_clear_mem(processed);
    return image.Valid();
}
#else
bool LoadLinearSrgb(const std::filesystem::path&, bool, LinearRgbImage&, std::string& error) {
    error = "Stack was built without LibRaw.";
    return false;
}
#endif

LinearRgbImage ScaleImage(const LinearRgbImage& source, double scale) {
    LinearRgbImage output = source;
    for (float& value : output.pixels) value = static_cast<float>(value * scale);
    return output;
}

LinearRgbImage AddDeterministicNoise(const LinearRgbImage& source, double amplitude) {
    LinearRgbImage output = source;
    std::uint32_t state = 0x9e3779b9u;
    for (std::size_t i = 0; i < output.pixels.size(); ++i) {
        state = state * 1664525u + 1013904223u;
        const double unit = static_cast<double>((state >> 8) & 0xffffu) / 32767.5 - 1.0;
        const double channelScale = (i % 3 == 1) ? 0.7 : ((i % 3 == 2) ? 1.2 : 1.0);
        output.pixels[i] = static_cast<float>(std::max(1.0e-6,
            static_cast<double>(output.pixels[i]) + amplitude * channelScale * unit));
    }
    return output;
}

LinearRgbImage AddMixedIlluminant(const LinearRgbImage& source) {
    LinearRgbImage output = source;
    for (int y = 0; y < output.height; ++y) {
        for (int x = 0; x < output.width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(output.width) + static_cast<std::size_t>(x)) * 3;
            const bool left = x < output.width / 2;
            output.pixels[i] *= left ? 1.25f : 0.80f;
            output.pixels[i + 2] *= left ? 0.80f : 1.25f;
        }
    }
    return output;
}

LinearRgbImage BlurByResolutionCycle(const LinearRgbImage& source) {
    const int width = std::max(1, source.width / 4);
    const int height = std::max(1, source.height / 4);
    return RenderedFeatures::ResizeLinearRgb(
        RenderedFeatures::ResizeLinearRgb(source, width, height), source.width, source.height);
}

LinearRgbImage AddHaloPerturbation(const LinearRgbImage& source) {
    const LinearRgbImage base = BlurByResolutionCycle(source);
    LinearRgbImage output = source;
    for (std::size_t i = 0; i < output.pixels.size(); ++i) {
        output.pixels[i] = static_cast<float>(source.pixels[i] + 0.85 * (source.pixels[i] - base.pixels[i]));
    }
    return output;
}

const FeatureValue* Feature(const FeatureRecord& record, const char* id) {
    return RenderedFeatures::FindFeature(record, id);
}

nlohmann::json KnownSignalResult(
    const char* signal,
    const char* feature,
    bool valid,
    double baseline,
    double perturbed,
    const char* expectedDirection) {
    return {
        { "signal", signal },
        { "feature", feature },
        { "valid", valid },
        { "baseline", valid ? nlohmann::json(baseline) : nlohmann::json(nullptr) },
        { "perturbed", valid ? nlohmann::json(perturbed) : nlohmann::json(nullptr) },
        { "expectedDirection", expectedDirection },
        { "directionObserved", valid && ((std::string(expectedDirection) == "increase" && perturbed > baseline) ||
            (std::string(expectedDirection) == "decrease" && perturbed < baseline)) }
    };
}

nlohmann::json CompareSignal(
    const char* signal,
    const char* id,
    const FeatureRecord& baseline,
    const FeatureRecord& perturbed,
    const char* direction) {
    const FeatureValue* a = Feature(baseline, id);
    const FeatureValue* b = Feature(perturbed, id);
    const bool valid = a && b && a->valid && b->valid;
    return KnownSignalResult(signal, id, valid, valid ? a->value : 0.0,
        valid ? b->value : 0.0, direction);
}

} // namespace

bool ValidateRenderedFeatureFoundation(int argc, char** argv) {
    Options options;
    std::string error;
    if (!ParseOptions(argc, argv, options, error)) {
        std::cerr << "Rendered feature validation: " << error << '\n';
        std::cerr << "Usage: Stack.exe --validate-rendered-feature-foundation --file <raw> [--folder <path>] --output <json> [--max-files N] [--base-max-dimension N] [--full-resolution-per-format N]\n";
        return false;
    }

    std::vector<std::string> warnings;
    const std::vector<std::filesystem::path> sources = CollectSources(options, warnings);
    if (sources.empty()) {
        std::cerr << "Rendered feature validation found no supported RAW files.\n";
        return false;
    }

    const auto started = std::chrono::steady_clock::now();
    nlohmann::json records = nlohmann::json::array();
    std::map<std::string, int> fullByFormat;
    std::map<std::string, int> directionObserved;
    std::map<std::string, int> directionValid;
    int decodeFailures = 0;
    int featureFailures = 0;
    int fullReferenceCount = 0;

    for (const std::filesystem::path& path : sources) {
        Raw::RawImageData raw;
        if (!Raw::DecodeWithLibRaw(path.string(), raw, {})) {
            ++decodeFailures;
            records.push_back({ { "fileName", path.filename().string() }, { "status", "raw-evidence-decode-failed" }, { "reason", raw.metadata.error } });
            continue;
        }
        RawEvidence::SourceIdentity identity;
        identity.sha256 = raw.metadata.sourceContentSha256;
        identity.byteSize = raw.metadata.sourceByteSize;
        identity.valid = identity.sha256.size() == 64;
        identity.reason = identity.valid ? "content-sha256-from-decoder" : "source-identity-unavailable";
        RawEvidence::BuildOptions buildOptions;
        buildOptions.maxSamples = 500000;
        buildOptions.measureDefectivePixels = false;
        const RawEvidence::RawTechnicalEvidenceRecord rawEvidence =
            RawEvidence::BuildRawTechnicalEvidence(raw, identity, buildOptions);
        const std::string cameraMake = raw.metadata.cameraMake;
        const std::string cameraModel = raw.metadata.cameraModel;
        const int rawWidth = raw.metadata.rawWidth;
        const int rawHeight = raw.metadata.rawHeight;
        raw = Raw::RawImageData {};

        const std::string format = Lower(path.extension().string());
        const bool useFull = fullByFormat[format] < options.fullResolutionPerFormat;
        LinearRgbImage decoded;
        std::string renderError;
        if (!LoadLinearSrgb(path, !useFull, decoded, renderError)) {
            ++decodeFailures;
            records.push_back({ { "fileName", path.filename().string() }, { "status", "validation-render-failed" }, { "reason", renderError } });
            continue;
        }
        if (useFull) {
            ++fullByFormat[format];
            ++fullReferenceCount;
        }
        const int renderedWidth = decoded.width;
        const int renderedHeight = decoded.height;
        FeatureContext context = LinearSrgbContext(identity.sha256, renderedWidth, renderedHeight,
            "libraw-validation-linear-scene");
        context.rawEvidenceIdentity = rawEvidence.evidenceIdentitySha256;
        const FeatureRecord resolutionReference =
            RenderedFeatures::AnalyzeSceneLinear(decoded, context, &rawEvidence);
        if (!resolutionReference.valid) ++featureFailures;

        const int longest = std::max(decoded.width, decoded.height);
        LinearRgbImage base = longest > options.baseMaxDimension
            ? RenderedFeatures::ResizeLinearRgb(
                decoded,
                std::max(1, decoded.width * options.baseMaxDimension / longest),
                std::max(1, decoded.height * options.baseMaxDimension / longest))
            : decoded;
        if (useFull) decoded = LinearRgbImage {};
        const FeatureRecord baseline = RenderedFeatures::AnalyzeSceneLinear(base, context, &rawEvidence);

        const LinearRgbImage exposure = ScaleImage(base, 2.0);
        const FeatureRecord exposureRecord = RenderedFeatures::AnalyzeSceneLinear(exposure, context, &rawEvidence);
        const LinearRgbImage noisy = AddDeterministicNoise(base, 0.006);
        const FeatureRecord noiseRecord = RenderedFeatures::AnalyzeSceneLinear(noisy, context, &rawEvidence);
        const LinearRgbImage mixed = AddMixedIlluminant(base);
        const FeatureRecord mixedRecord = RenderedFeatures::AnalyzeSceneLinear(mixed, context, &rawEvidence);
        const LinearRgbImage blurred = BlurByResolutionCycle(base);
        const FeatureRecord blurredRecord = RenderedFeatures::AnalyzeSceneLinear(blurred, context, &rawEvidence);
        const LinearRgbImage halo = AddHaloPerturbation(base);
        const FeatureRecord cleanComparison = RenderedFeatures::CompareRenderedImages(base, base, context);
        const FeatureRecord haloComparison = RenderedFeatures::CompareRenderedImages(base, halo, context);
        const FeatureRecord colorComparison = RenderedFeatures::CompareRenderedImages(base, mixed, context);

        FeatureContext displayContext = context;
        displayContext.stage = "libraw-validation-linear-display";
        const FeatureRecord displayBase = RenderedFeatures::AnalyzeDisplayMapped(base, displayContext);
        const FeatureRecord displayLift = RenderedFeatures::AnalyzeDisplayMapped(ScaleImage(base, 2.0), displayContext);

        nlohmann::json signals = nlohmann::json::array();
        signals.push_back(CompareSignal("known +1 EV multiplication", "scene.ev_p50", baseline, exposureRecord, "increase"));
        signals.push_back(CompareSignal("deterministic chromatic noise", "noise.rendered_chroma_residual_rms", baseline, noiseRecord, "increase"));
        signals.push_back(CompareSignal("opposed left/right color cast", "wb.spatial_disagreement_ev", baseline, mixedRecord, "increase"));
        signals.push_back(CompareSignal("resolution-cycle blur", "multiscale.detail_rms_r1", baseline, blurredRecord, "decrease"));
        signals.push_back(CompareSignal("unsharp halo injection", "halo.adjacent_band_energy", cleanComparison, haloComparison, "increase"));
        signals.push_back(CompareSignal("opposed left/right color cast", "color.hue_shift_degrees", cleanComparison, colorComparison, "increase"));
        signals.push_back(CompareSignal("+1 EV display multiplication", "display.linear_clip_high_fraction", displayBase, displayLift, "increase"));
        for (const nlohmann::json& signal : signals) {
            const std::string id = signal.at("feature").get<std::string>();
            if (signal.at("valid").get<bool>()) {
                ++directionValid[id];
                if (signal.at("directionObserved").get<bool>()) ++directionObserved[id];
            }
        }

        nlohmann::json proxyLadder = nlohmann::json::array();
        const LinearRgbImage& proxySource = useFull ? base : decoded;
        for (int maxDimension : { 512, 256, 128 }) {
            const int proxyLongest = std::max(proxySource.width, proxySource.height);
            const LinearRgbImage proxy = proxyLongest > maxDimension
                ? RenderedFeatures::ResizeLinearRgb(proxySource,
                    std::max(1, proxySource.width * maxDimension / proxyLongest),
                    std::max(1, proxySource.height * maxDimension / proxyLongest))
                : proxySource;
            const FeatureRecord proxyRecord = RenderedFeatures::AnalyzeSceneLinear(proxy, context, &rawEvidence);
            proxyLadder.push_back({
                { "width", proxy.width }, { "height", proxy.height },
                { "agreement", RenderedFeatures::SerializeFeatureAgreement(
                    RenderedFeatures::CompareFeatureRecords(resolutionReference, proxyRecord)) }
            });
        }

        records.push_back({
            { "fileName", path.filename().string() },
            { "sourceIdentitySha256", identity.sha256 },
            { "format", format.empty() ? "unknown" : format.substr(1) },
            { "cameraMake", cameraMake },
            { "cameraModel", cameraModel },
            { "rawDimensions", { { "width", rawWidth }, { "height", rawHeight } } },
            { "validationRender", {
                { "backend", "LibRaw dcraw_process" },
                { "halfSize", !useFull },
                { "width", renderedWidth }, { "height", renderedHeight },
                { "space", "linear sRGB" }, { "cameraWhiteBalance", true },
                { "autoBrightness", false }, { "orientationNormalized", true }
            } },
            { "fullResolutionReference", useFull },
            { "phase01EvidenceIdentity", rawEvidence.evidenceIdentitySha256 },
            { "baselineFeatureRecord", RenderedFeatures::SerializeFeatureRecord(baseline) },
            { "knownSignalCorrelations", std::move(signals) },
            { "proxyResolutionLadder", std::move(proxyLadder) },
            { "estimatedPeakImageBytes", static_cast<std::uint64_t>(renderedWidth) * static_cast<std::uint64_t>(renderedHeight) * 3u * sizeof(float) },
            { "status", baseline.valid && resolutionReference.valid ? "complete" : "incomplete" }
        });
    }

    nlohmann::json correlationSummary = nlohmann::json::object();
    for (const auto& [id, valid] : directionValid) {
        correlationSummary[id] = {
            { "validComparisons", valid },
            { "expectedDirectionObserved", directionObserved[id] },
            { "allValidComparisonsMoveAsExpected", valid > 0 && directionObserved[id] == valid }
        };
    }
    const auto finished = std::chrono::steady_clock::now();
    const double runtimeMs = std::chrono::duration<double, std::milli>(finished - started).count();
    const std::size_t completeRecordCount = records.size();
    nlohmann::json report = {
        { "reportVersion", "phase-02-real-corpus-correlation-v1" },
        { "renderedFeatureVersion", RenderedFeatures::kRenderedFeatureVersion },
        { "rawEvidenceVersion", RawEvidence::kRawTechnicalEvidenceVersion },
        { "recipeMutation", false },
        { "candidateOptimization", false },
        { "combinedWinnerScore", false },
        { "numericObjectiveThresholdSelection", false },
        { "sourceCount", sources.size() },
        { "completeRecordCount", completeRecordCount },
        { "decodeFailureCount", decodeFailures },
        { "featureFailureCount", featureFailures },
        { "fullResolutionReferenceCount", fullReferenceCount },
        { "diagnosticRenderNotice", "Validation-only LibRaw linear-sRGB render; not Stack Pass 94 and not production recipe evidence." },
        { "knownSignalCorrelationSummary", std::move(correlationSummary) },
        { "runtimeMs", runtimeMs },
        { "warnings", warnings },
        { "records", std::move(records) }
    };

    if (!options.output.empty()) {
        std::error_code ec;
        if (!options.output.parent_path().empty()) std::filesystem::create_directories(options.output.parent_path(), ec);
        std::ofstream output(options.output, std::ios::binary | std::ios::trunc);
        if (!output) {
            std::cerr << "Rendered feature validation could not write " << options.output.string() << '\n';
            return false;
        }
        output << report.dump(2) << '\n';
    } else {
        std::cout << report.dump(2) << '\n';
    }
    std::cout << "Rendered feature coverage: " << completeRecordCount << " complete source record(s), "
              << fullReferenceCount << " full-resolution reference(s), " << decodeFailures
              << " decode failure(s).\n";
    return completeRecordCount > 0 && decodeFailures == 0 && featureFailures == 0 &&
        (options.fullResolutionPerFormat == 0 || fullReferenceCount > 0);
}

} // namespace Stack::Validation
