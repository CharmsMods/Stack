#pragma once

#include "Persistence/RawProjectModel.h"
#include "Raw/MultiFrameDenoise/Evaluation.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Raw::Mfd {

inline constexpr std::uint32_t kCorpusIntakeContractVersion = 1;
inline constexpr const char* kCorpusIntakeContractId =
    "ra-cfa-v1-corpus-intake-v1";
inline constexpr const char* kLockedCorpusReportContractId =
    "ra-cfa-v1-locked-corpus-report-v1";

struct MfdCorpusSampleSpec {
    std::string sampleId;
    MfdCorpusLayer layer = MfdCorpusLayer::ControlledCamera;
    std::string cameraModeLabel;
    std::vector<std::filesystem::path> framePaths;
    std::uint64_t referenceFrameIndex = 0u;
    std::filesystem::path referenceMomentGroundTruthPath;
};

struct MfdCorpusIntakeSpec {
    std::uint32_t contractVersion = kCorpusIntakeContractVersion;
    std::string contractId = kCorpusIntakeContractId;
    std::vector<MfdCorpusSampleSpec> entries;
};

struct MfdCorpusFileIdentity {
    bool valid = false;
    std::string sha256;
    std::uint64_t byteLength = 0u;
};

struct MfdInspectedCorpusFrame {
    MfdCorpusFileIdentity identity;
    Stack::Project::RawCaptureCompatibilitySummary compatibility;
};

struct MfdCorpusIntakeServices {
    std::function<bool(
        const std::filesystem::path&,
        MfdInspectedCorpusFrame&,
        std::string&)> inspectRawFrame;
    std::function<bool(
        const std::filesystem::path&,
        MfdCorpusFileIdentity&,
        std::string&)> identifyFile;
};

struct MfdLockedCorpusFrame {
    std::filesystem::path informationalSourcePath;
    MfdCorpusFileIdentity identity;
    Stack::Project::RawCaptureCompatibilitySummary compatibility;
    bool referenceFrame = false;
};

struct MfdLockedCorpusEntry {
    bool valid = false;
    std::string message;
    MfdCorpusEntryEvidence evidence;
    std::string cameraModeLabel;
    std::uint64_t referenceFrameIndex = 0u;
    std::vector<MfdLockedCorpusFrame> frames;
    std::filesystem::path informationalGroundTruthPath;
    MfdCorpusFileIdentity groundTruthIdentity;
};

struct MfdCorpusIntakeReport {
    bool valid = false;
    bool manifestLocked = false;
    std::string message;
    std::string manifestSha256;
    std::vector<MfdLockedCorpusEntry> entries;
    std::vector<std::string> errors;
};

bool DeserializeMfdCorpusIntakeSpec(
    const nlohmann::json& value,
    const std::filesystem::path& baseDirectory,
    MfdCorpusIntakeSpec& spec,
    std::string* error = nullptr);

nlohmann::json SerializeMfdCorpusIntakeTemplate();

MfdCorpusIntakeServices MakeFilesystemMfdCorpusIntakeServices();

bool LockMfdCorpusIntake(
    const MfdCorpusIntakeSpec& spec,
    const MfdCorpusIntakeServices& services,
    MfdCorpusIntakeReport& report,
    std::string* error = nullptr);

nlohmann::json SerializeMfdCorpusIntakeReport(
    const MfdCorpusIntakeReport& report);

} // namespace Raw::Mfd
