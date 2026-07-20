#include "App/Validation/ValidationSuites.h"

#include "Raw/LibRawDecoder.h"
#include "Raw/RawTechnicalEvidence.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace Stack::Validation {
namespace {

struct Options {
    std::vector<std::filesystem::path> inputs;
    std::filesystem::path output;
    std::size_t maxSamples = 1000000;
    std::size_t maxFiles = std::numeric_limits<std::size_t>::max();
    bool measureDefectivePixels = true;
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
        } else if (arg == "--max-samples") {
            const char* value = requireValue("--max-samples");
            if (!value) return false;
            try {
                options.maxSamples = std::max<std::size_t>(1, std::stoull(value));
            } catch (...) {
                error = "--max-samples must be a positive integer.";
                return false;
            }
        } else if (arg == "--max-files") {
            const char* value = requireValue("--max-files");
            if (!value) return false;
            try {
                options.maxFiles = std::max<std::size_t>(1, std::stoull(value));
            } catch (...) {
                error = "--max-files must be a positive integer.";
                return false;
            }
        } else if (arg == "--no-defective-pixels") {
            options.measureDefectivePixels = false;
        } else if (!arg.empty() && arg[0] == '-') {
            error = "Unknown RAW evidence option: " + arg;
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
                     input,
                     std::filesystem::directory_options::skip_permission_denied,
                     ec), end;
                 iterator != end;
                 iterator.increment(ec)) {
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

nlohmann::json CountMap(const std::unordered_map<std::string, int>& values) {
    nlohmann::json result = nlohmann::json::object();
    for (const auto& [key, value] : values) result[key] = value;
    return result;
}

} // namespace

bool ValidateRawEvidenceFoundation(int argc, char** argv) {
    Options options;
    std::string error;
    if (!ParseOptions(argc, argv, options, error)) {
        std::cerr << "RAW evidence validation: " << error << '\n';
        std::cerr << "Usage: Stack.exe --validate-raw-evidence-foundation --folder <path> [--folder <path>] --output <json> [--max-samples N] [--no-defective-pixels]\n";
        return false;
    }

    std::vector<std::string> warnings;
    const std::vector<std::filesystem::path> sources = CollectSources(options, warnings);
    if (sources.empty()) {
        std::cerr << "RAW evidence validation found no supported RAW files.\n";
        return false;
    }

    const auto started = std::chrono::steady_clock::now();
    std::set<std::string> seenContent;
    nlohmann::json records = nlohmann::json::array();
    int duplicates = 0;
    int identityFailures = 0;
    int decodeFailures = 0;
    int evidenceFailures = 0;
    int dngFiles = 0;
    int arwFiles = 0;
    int activeAreaCount = 0;
    int maskedAreasCount = 0;
    int lrlCount = 0;
    int noiseProfileCount = 0;
    int asShotNeutralCount = 0;
    int gainMapCount = 0;
    int profileGainMapCount = 0;
    int noClipCount = 0;
    int partialClipCount = 0;
    int allClipCount = 0;
    double minimumSnr = std::numeric_limits<double>::infinity();
    double maximumSnr = 0.0;
    double totalEvidenceMs = 0.0;
    std::uint64_t peakRawBytes = 0;
    std::unordered_map<std::string, int> cameraCounts;
    std::unordered_map<std::string, int> limitingPlaneCounts;
    std::unordered_map<std::string, int> orientationCounts;
    std::unordered_map<std::string, int> bitDepthCounts;

    for (const std::filesystem::path& path : sources) {
        Raw::RawImageData raw;
        const bool decoded = Raw::DecodeWithLibRaw(path.string(), raw, {});
        if (!decoded) {
            ++decodeFailures;
            records.push_back({
                { "fileName", path.filename().string() },
                { "status", "decode-failed" },
                { "reason", raw.metadata.error }
            });
            continue;
        }

        RawEvidence::SourceIdentity identity;
        identity.sha256 = raw.metadata.sourceContentSha256;
        identity.byteSize = raw.metadata.sourceByteSize;
        identity.valid = identity.sha256.size() == 64;
        identity.reason = identity.valid ? "content-sha256-from-decoder" : "decoder-source-identity-unavailable";
        if (!identity.valid) {
            ++identityFailures;
            records.push_back({
                { "fileName", path.filename().string() },
                { "status", "source-identity-failed" },
                { "reason", identity.reason }
            });
            continue;
        }
        if (!seenContent.insert(identity.sha256).second) {
            ++duplicates;
            continue;
        }

        RawEvidence::BuildOptions build;
        build.maxSamples = options.maxSamples;
        build.measureDefectivePixels = options.measureDefectivePixels;
        build.decode.decoderBackend = "libraw";
        const RawEvidence::RawTechnicalEvidenceRecord evidence =
            RawEvidence::BuildRawTechnicalEvidence(raw, identity, build);
        if (!evidence.valid) ++evidenceFailures;
        totalEvidenceMs += evidence.runtimeMs;
        peakRawBytes = std::max(peakRawBytes,
            static_cast<std::uint64_t>(raw.rawBuffer.size() * sizeof(std::uint16_t) +
                raw.linearUInt16Buffer.size() * sizeof(std::uint16_t) +
                raw.linearFloatBuffer.size() * sizeof(float)));

        const std::string extension = Lower(path.extension().string());
        if (extension == ".dng") ++dngFiles;
        if (extension == ".arw") ++arwFiles;
        if (evidence.metadataCoverage.hasActiveArea) ++activeAreaCount;
        if (evidence.metadataCoverage.hasMaskedAreas) ++maskedAreasCount;
        if (evidence.metadataCoverage.hasLinearResponseLimit) ++lrlCount;
        if (evidence.metadataCoverage.hasNoiseProfile) ++noiseProfileCount;
        if (evidence.metadataCoverage.hasAsShotNeutral) ++asShotNeutralCount;
        if (evidence.metadataCoverage.hasOpcodeList2GainMap) ++gainMapCount;
        if (evidence.metadataCoverage.hasProfileGainTableMap || evidence.metadataCoverage.hasProfileGainTableMap2) ++profileGainMapCount;
        const std::string camera = raw.metadata.cameraMake + " " + raw.metadata.cameraModel;
        ++cameraCounts[camera.empty() ? "unknown" : camera];
        ++limitingPlaneCounts[evidence.limitingPlane.empty() ? "unavailable" : evidence.limitingPlane];
        ++orientationCounts[std::to_string(raw.metadata.orientation)];
        ++bitDepthCounts[std::to_string(raw.metadata.bitDepth)];
        if (evidence.clipping.valid) {
            if (evidence.clipping.allChannelClippedFraction.valid && evidence.clipping.allChannelClippedFraction.value > 0.0) ++allClipCount;
            else if ((evidence.clipping.singleChannelClippedFraction.valid && evidence.clipping.singleChannelClippedFraction.value > 0.0) ||
                     (evidence.clipping.multiChannelClippedFraction.valid && evidence.clipping.multiChannelClippedFraction.value > 0.0)) ++partialClipCount;
            else ++noClipCount;
        }
        for (const RawEvidence::NoisePlaneEvidence& plane : evidence.noise) {
            for (const RawEvidence::EvidenceMeasurement& snr : plane.snrBySignal) {
                if (!snr.valid) continue;
                minimumSnr = std::min(minimumSnr, snr.value);
                maximumSnr = std::max(maximumSnr, snr.value);
            }
        }

        nlohmann::json serialized = RawEvidence::SerializeRawTechnicalEvidence(evidence);
        serialized["fileName"] = path.filename().string();
        serialized["format"] = extension.empty() ? "unknown" : extension.substr(1);
        serialized["cameraMake"] = raw.metadata.cameraMake;
        serialized["cameraModel"] = raw.metadata.cameraModel;
        serialized["iso"] = raw.metadata.hasIsoSpeed ? nlohmann::json(raw.metadata.isoSpeed) : nlohmann::json(nullptr);
        serialized["bitDepth"] = raw.metadata.bitDepth;
        records.push_back(std::move(serialized));
    }

    const int uniqueSources = static_cast<int>(seenContent.size());
    const auto finished = std::chrono::steady_clock::now();
    const double totalRuntimeMs = std::chrono::duration<double, std::milli>(finished - started).count();
    nlohmann::json report = {
        { "reportVersion", "phase-01-corpus-coverage-v1" },
        { "rawEvidenceVersion", RawEvidence::kRawTechnicalEvidenceVersion },
        { "normalizationVersion", RawEvidence::kRawNormalizationVersion },
        { "recipeMutation", false },
        { "pass94ConsumedNewEvidence", false },
        { "inputPathCount", options.inputs.size() },
        { "discoveredFileCount", sources.size() },
        { "uniqueContentCount", uniqueSources },
        { "duplicateContentCount", duplicates },
        { "identityFailureCount", identityFailures },
        { "decodeFailureCount", decodeFailures },
        { "evidenceFailureCount", evidenceFailures },
        { "formatCounts", { { "dng", dngFiles }, { "arw", arwFiles }, { "other", uniqueSources - dngFiles - arwFiles } } },
        { "metadataAvailability", {
            { "activeArea", activeAreaCount }, { "maskedAreas", maskedAreasCount },
            { "linearResponseLimit", lrlCount }, { "noiseProfile", noiseProfileCount },
            { "asShotNeutral", asShotNeutralCount }, { "opcodeList2GainMap", gainMapCount },
            { "profileGainTableMap", profileGainMapCount }
        } },
        { "clipClassFileCounts", {
            { "noClip", noClipCount }, { "partialChannelClip", partialClipCount },
            { "allChannelClip", allClipCount }
        } },
        { "cameraCounts", CountMap(cameraCounts) },
        { "limitingPlaneCounts", CountMap(limitingPlaneCounts) },
        { "orientationCounts", CountMap(orientationCounts) },
        { "bitDepthCounts", CountMap(bitDepthCounts) },
        { "predictedSnrRange", {
            { "valid", std::isfinite(minimumSnr) },
            { "minimum", std::isfinite(minimumSnr) ? nlohmann::json(minimumSnr) : nlohmann::json(nullptr) },
            { "maximum", std::isfinite(minimumSnr) ? nlohmann::json(maximumSnr) : nlohmann::json(nullptr) },
            { "units", "linear-SNR" }
        } },
        { "runtime", {
            { "totalMs", totalRuntimeMs }, { "evidenceOnlyMs", totalEvidenceMs },
            { "meanEvidenceMs", uniqueSources > 0 ? totalEvidenceMs / uniqueSources : 0.0 },
            { "peakDecodedRawBytes", peakRawBytes }
        } },
        { "warnings", warnings },
        { "records", std::move(records) }
    };

    if (!options.output.empty()) {
        std::error_code ec;
        if (!options.output.parent_path().empty()) std::filesystem::create_directories(options.output.parent_path(), ec);
        std::ofstream output(options.output, std::ios::binary | std::ios::trunc);
        if (!output) {
            std::cerr << "RAW evidence validation could not write " << options.output.string() << '\n';
            return false;
        }
        output << report.dump(2) << '\n';
    } else {
        std::cout << report.dump(2) << '\n';
    }

    std::cout << "RAW evidence coverage: " << uniqueSources << " unique source(s), "
        << decodeFailures << " decode failure(s), " << evidenceFailures << " incomplete record(s).\n";
    return identityFailures == 0 && decodeFailures == 0 && evidenceFailures == 0;
}

} // namespace Stack::Validation
