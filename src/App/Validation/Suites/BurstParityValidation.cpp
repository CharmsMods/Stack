#include "BurstEngine/Client.h"
#include "Project/BracketingState.h"
#include "Raw/Bracketing/GroupSuggestion.h"
#include "Raw/Bracketing/ResultSerialization.h"
#include "Raw/Bracketing/SourceSetIdentity.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include "Raw/MultiFrameDenoise/Preparation.h"
#include "Raw/RawCaptureCompatibility.h"
#include "Raw/RawGalleryInputPolicy.h"
#include "Raw/RawLoader.h"
#include "Raw/RawProcessingMath.h"
#include "Raw/RawTechnicalEvidence.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <set>

namespace Stack::Validation {
namespace {
using Json = nlohmann::json;
using namespace Raw::Bracketing;
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
// Suggested group UUIDs identify their document instance, not a different
// calculation. Preserve all settings and frame identities while comparing.
Json CanonicalRecipe(Json recipe) {
    auto& groups = recipe.at("groups");
    for (std::size_t i = 0; i < groups.size(); ++i)
        groups[i]["id"] = "group-" + std::to_string(i);
    return recipe;
}
struct Inputs {
    ProcessingRequest request;
    std::map<std::string, Raw::RawMetadata> metadata;
    Json identities = Json::array();
    std::string setIdentity;
};
Inputs LoadInputs(const std::filesystem::path& directory) {
    Inputs inputs;
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.is_regular_file() && RawWorkspace::IsGalleryRawPath(entry.path()))
            paths.push_back(std::filesystem::absolute(entry.path()).lexically_normal());
    Require(paths.size() >= 2 && paths.size() <= 64,
        "This focused comparison requires one known burst of two to 64 original captures.");
    std::stable_sort(paths.begin(), paths.end(), [](const auto& left, const auto& right) {
        const auto a = RawWorkspace::RawGalleryPathOrderKey(left.filename().u8string());
        const auto b = RawWorkspace::RawGalleryPathOrderKey(right.filename().u8string());
        return a != b ? a < b : left.generic_u8string() < right.generic_u8string();
    });
    std::set<std::string> seen;
    std::vector<CaptureExposure> exposures;
    std::vector<CaptureContentIdentity> identities;
    Project::RawCaptureCompatibilitySummary reference;
    for (const auto& path : paths) {
        const auto identity = RawEvidence::ComputeSourceIdentity(path);
        Require(identity.valid, "Cannot identify " + path.filename().u8string() + ": " + identity.reason);
        Require(seen.insert(identity.sha256).second, "The comparison folder contains duplicate captures.");
        Raw::RawMetadata metadata;
        Require(Raw::RawLoader::LoadMetadata(path.u8string(), metadata), "Cannot read " + path.filename().u8string());
        const auto compatibility = Project::BuildRawCaptureCompatibilitySummary(metadata);
        Require(compatibility.supported, compatibility.rejectionReason);
        std::string error;
        if (inputs.request.sources.empty()) reference = compatibility;
        else Require(Project::AreHdrCapturesStructurallyCompatible(reference, compatibility, &error), error);
        inputs.request.sources.push_back({identity.sha256, identity.sha256, identity.byteSize, path, path.filename().u8string()});
        exposures.push_back({identity.sha256, compatibility.exposureTimeSeconds,
            compatibility.apertureFNumber, compatibility.isoSpeed});
        identities.push_back({identity.sha256, identity.byteSize});
        inputs.identities.push_back({{"frameId", identity.sha256}, {"sha256", identity.sha256},
            {"bytes", identity.byteSize}, {"path", path.u8string()}});
        inputs.metadata.emplace(identity.sha256, std::move(metadata));
    }
    // Reuse the production exposure policy. The optional standalone report
    // verifies that this explicit folder is exactly one automatically found set.
    inputs.request.recipe = SuggestGroups(exposures);
    inputs.setIdentity = SourceSetIdentity(identities);
    return inputs;
}
} // namespace

// One shipping-client processing operation and one all-sample DNG comparison.
// Inputs are read-only; diagnostic artifacts go to a newly reserved directory.
bool ValidateBurstParity(const std::filesystem::path& inputDirectory,
    const std::filesystem::path& standaloneDng, const std::filesystem::path& output,
    const std::filesystem::path& standaloneReport) {
    Json report = {{"passed", false}, {"inputDirectory", inputDirectory.u8string()},
        {"standaloneDng", standaloneDng.u8string()}};
    bool ownsOutput = false;
    try {
        Require(!std::filesystem::exists(output), "Choose a new validation output directory.");
        Require(std::filesystem::create_directories(output), "Could not reserve the validation output directory.");
        ownsOutput = true;
        auto inputs = LoadInputs(inputDirectory);
        auto& request = inputs.request;
        report["sourceCount"] = request.sources.size();
        report["sources"] = inputs.identities;
        report["sourceSetIdentity"] = inputs.setIdentity;
        report["recipe"] = Serialize(request.recipe);
        report["canonicalRecipe"] = CanonicalRecipe(report["recipe"]);
        report["standaloneSettingsChecked"] = false;
        if (!standaloneReport.empty()) {
            std::ifstream stream(standaloneReport);
            const auto standalone = Json::parse(stream);
            Require(CanonicalRecipe(standalone.at("recipe")) == report["canonicalRecipe"], "Standalone and Stack processing settings differ.");
            const auto& sources = standalone.at("sources");
            Require(sources.size() == request.sources.size(), "Standalone selected a different source count.");
            for (std::size_t i = 0; i < sources.size(); ++i) {
                Require(sources[i].at("frameId") == request.sources[i].frameId &&
                    sources[i].at("sha256") == request.sources[i].sha256 && sources[i].at("bytes") == request.sources[i].bytes,
                    "Standalone source identity/order differs from Stack.");
            }
            report["standaloneSettingsChecked"] = true;
            report["standaloneProcessingSeconds"] = standalone.at("processingSeconds");
            report["standaloneStageSeconds"] = standalone.at("stageSeconds");
            report["standaloneRequestedWorkers"] = standalone.at("requestedWorkers");
            report["standaloneMemoryBudgetBytes"] = standalone.at("memoryBudgetBytes");
            report["standaloneDiagnostics"] = standalone.at("diagnostics");
        }
        const auto memory = Raw::Mfd::ResolveMfdProcessingMemoryBudget(0, Raw::Mfd::QueryPhysicalMemorySnapshot());
        Require(memory.valid, memory.message);
        request.memoryBudgetBytes = memory.budgetBytes;
        request.automaticMemoryBudget = true;
        request.workerCount = ResolveBracketingWorkerCount(memory.budgetBytes);
        request.cacheDirectory = output / "process-cache";
        request.removeCacheOnRelease = true;
        std::mutex progressMutex;
        unsigned progressEvents = 0, presentationEvents = 0;
        request.reportProgress = [&](double fraction, const std::string& stage) {
            std::lock_guard<std::mutex> lock(progressMutex);
            if (progressEvents++ % 20 == 0)
                std::cout << "Burst parity: " << int(fraction * 100) << "% " << stage << '\n' << std::flush;
        };
        request.reportPresentation = [&](const ProcessingProgress&) {
            std::lock_guard<std::mutex> lock(progressMutex);
            ++presentationEvents;
        };
        const auto start = std::chrono::steady_clock::now();
        auto result = BurstEngine::Process(request);
        report["processingSeconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        Require(result.status == BracketingResult::Status::Completed, result.message);
        Require(result.raw && result.raw->normalizedMosaicBuffer && result.analysis && result.analysis->sessionLease,
            "Stack did not receive a complete result with a live engine session.");
        Require(result.raw->normalizedMosaicInputContract == Raw::NormalizedMosaicInputContract::BracketingPreGain,
            "Stack returned an unexpected measurement domain.");
        report["requestedWorkerCount"] = request.workerCount;
        report["memoryBudgetBytes"] = request.memoryBudgetBytes;
        report["progressEvents"] = progressEvents;
        report["presentationEvents"] = presentationEvents;
        report["stageSeconds"] = result.analysis->stageSeconds;
        report["diagnostics"] = result.analysis->diagnostics;
        report["resultIdentity"] = result.identity;
        report["analysisIdentity"] = result.analysis->identity;
        report["fallbackSamples"] = result.fallbackSamples;
        report["unrecoverableSamples"] = result.unrecoverableSamples;
        report["invalidSamples"] = result.invalidSamples;
        report["alignments"] = Json::object();
        for (const auto& [id, alignment] : result.analysis->alignments)
            report["alignments"][id] = {{"model", int(alignment.model)}, {"accepted", alignment.accepted},
                {"translationX", alignment.translationX}, {"translationY", alignment.translationY},
                {"rotationDegrees", alignment.rotationDegrees}, {"overlapFraction", alignment.overlapFraction},
                {"acceptedCoverage", alignment.acceptedCoverage}, {"uncertainCoverage", alignment.uncertainCoverage},
                {"rejectedCoverage", alignment.rejectedCoverage}, {"localApplied", alignment.localApplied},
                {"backend", alignment.backend}};

        Raw::RawImageData imported;
        Require(Raw::RawLoader::LoadFile(standaloneDng.u8string(), imported), "Could not decode the standalone DNG: " + imported.metadata.error);
        Require(imported.normalizedMosaicBuffer && imported.normalizedMosaicInputContract == Raw::NormalizedMosaicInputContract::FloatDngPreGain,
            "Standalone output is not a floating-point CFA DNG.");
        const auto& m = result.raw->metadata;
        const auto& d = imported.metadata;
        Require(d.rawWidth == m.rawWidth && d.rawHeight == m.rawHeight && d.cfaPattern == m.cfaPattern && d.orientation == m.orientation,
            "Standalone and Stack measurement dimensions, CFA or orientation differ.");
        const auto& expected = *result.raw->normalizedMosaicBuffer;
        const auto& actual = *imported.normalizedMosaicBuffer;
        Require(actual.size() == expected.size() && actual.size() == std::uint64_t(m.rawWidth) * m.rawHeight,
            "Standalone and Stack measurement sample counts differ.");
        const auto& origin = inputs.metadata.at(request.recipe.originFrameId);
        const auto active = Raw::Processing::ResolveActiveArea(d);
        const Raw::RawDevelopSettings settings;
        // Reuse public preparation for only the origin's calibration. Calling
        // EvaluatePointwiseGain for each output pixel would redundantly validate
        // every source gain-map grid millions of times. A directory cache keeps
        // this comparison bounded even while the completed engine lease lives.
        const auto calibrationDirectory = output / "reference-calibration";
        Raw::Mfd::DirectoryNormalizedTileCache calibrationCache(calibrationDirectory);
        Raw::Mfd::PreparedRawFrame calibration;
        {
            const auto source = std::find_if(request.sources.begin(), request.sources.end(),
                [&](const Source& value) { return value.frameId == request.recipe.originFrameId; });
            Require(source != request.sources.end(), "The origin capture is missing.");
            Raw::RawImageData originRaw;
            Require(Raw::RawLoader::LoadFile(source->path.u8string(), originRaw), "Could not decode the reference calibration.");
            const auto prepared = Raw::Mfd::PrepareRawFrame(originRaw, {}, calibrationCache);
            Require(prepared.success, "Could not prepare reference calibration: " + prepared.message);
            calibration = prepared.frame;
            Require(calibration.activeExtent.width == unsigned(m.rawWidth) && calibration.activeExtent.height == unsigned(m.rawHeight),
                "Reference calibration does not match the result dimensions.");
        }
        double maxError = 0, squaredError = 0, maximumExpected = 0, maxNormalizedError = 0;
        std::uint64_t unequalNormalized = 0;
        for (unsigned ty = 0; ty < calibration.tileRows; ++ty) for (unsigned tx = 0; tx < calibration.tileColumns; ++tx) {
            Raw::Mfd::PreparedRawTile tile;
            std::string error;
            Require(Raw::Mfd::ReadPreparedTile(calibration, calibrationCache, tx, ty, tile, &error) == Raw::Mfd::TileCacheReadStatus::Hit,
                "Could not read reference calibration: " + error);
            for (unsigned ly = 0; ly < tile.extent.height; ++ly) for (unsigned lx = 0; lx < tile.extent.width; ++lx) {
                const auto x = unsigned(tile.originX + lx), y = unsigned(tile.originY + ly);
                const auto i = std::size_t(y) * m.rawWidth + x;
                const float gain = tile.comparisonGain[std::size_t(ly) * tile.extent.width + lx];
                // OutputDng writes float(preGain * referenceGain), without WB,
                // clipping or exposure. RawLoader divides it by DNG WhiteLevel.
                const float calibrated = expected[i] * gain;
                const int color = Raw::Processing::CfaColorAt(d, active, x, y);
                const float black = Raw::Processing::BlackLevelAt(d, settings, active, x, y, color);
                const float white = Raw::Processing::WhiteLevelForColor(d, settings, color);
                const float normalized = (calibrated - black) / (white - black);
                Require(std::isfinite(actual[i]) && std::isfinite(normalized), "Non-finite measurement in parity comparison.");
                const double delta = double(actual[i] - normalized) * (white - black);
                squaredError += delta * delta;
                maxError = std::max(maxError, std::abs(delta));
                maximumExpected = std::max(maximumExpected, std::abs(double(calibrated)));
                maxNormalizedError = std::max(maxNormalizedError, std::abs(double(actual[i]) - normalized));
                unequalNormalized += actual[i] != normalized;
            }
        }
        std::error_code calibrationCleanupError;
        std::filesystem::remove_all(calibrationDirectory, calibrationCleanupError);
        const double tolerance = 8 * std::numeric_limits<float>::epsilon() * std::max(1.0, maximumExpected);
        report["measurement"] = {{"width", m.rawWidth}, {"height", m.rawHeight}, {"sampleCount", actual.size()},
            {"maximumAbsoluteError", maxError}, {"rootMeanSquareError", std::sqrt(squaredError / actual.size())},
            {"maximumNormalizedError", maxNormalizedError}, {"unequalNormalizedSamples", unequalNormalized},
            {"exactNormalizedMatch", unequalNormalized == 0}, {"absoluteTolerance", tolerance},
            {"dngWhiteLevel", d.whiteLevel}, {"dngBaselineExposure", d.dngBaselineExposure},
            {"referenceGainMaps", origin.dngGainMaps.size()}, {"withinTolerance", maxError <= tolerance}};

        Project::RawProjectSnapshot document;
        document.projectId = "burst-parity-validation";
        document.hdrInputRevision = 1;
        Project::MultiFrameSourceSet set;
        set.sourceSetId = inputs.setIdentity;
        set.settings["bracketing"] = Serialize(request.recipe);
        Project::BracketingState state;
        state.recipe = request.recipe;
        Project::BracketingJob job;
        job.projectId = document.projectId; job.setId = set.sourceSetId;
        job.revision = document.hdrInputRevision; job.recipeIdentity = set.settings["bracketing"].dump();
        job.result = std::move(result);
        const auto measurement = job.result.raw;
        Require(Project::CanAdoptBracketingJob(state, job, document, set) && Project::AdoptBracketingJob(state, job),
            "Stack rejected its completed engine result.");
        Require(state.result->raw == measurement, "Stack adoption replaced the computed measurement.");
        report["stackResultAdoptedWithoutMeasurementChange"] = true;
        Require(Serialization::WriteResultFile(output / "result.bracket", *state.result), "Could not save the comparison artifact.");
        for (const auto& source : request.sources) {
            const auto current = RawEvidence::ComputeSourceIdentity(source.path);
            Require(current.valid && current.sha256 == source.sha256 && current.byteSize == source.bytes, "An original capture changed.");
        }
        report["originalsUnchanged"] = true;
        Require(maxError <= tolerance, "Standalone and Stack full-resolution measurements differ beyond float rounding tolerance.");
        report["passed"] = true;
    } catch (const std::exception& error) {
        report["error"] = error.what();
        std::cerr << "Burst parity check failed: " << error.what() << '\n';
    }
    if (ownsOutput) std::ofstream(output / "burst-parity-check.json") << report.dump(2);
    if (report.at("passed").get<bool>()) std::cout << "Burst parity check passed: " << (output / "burst-parity-check.json") << '\n';
    return report.at("passed").get<bool>();
}
} // namespace Stack::Validation
