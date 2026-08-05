#include "App/Validation/ValidationSuites.h"

#include "Editor/RawWorkspaceAutoBaseState.h"
#include "Raw/RawAutoBase.h"
#include "Raw/RawAutoStartPoint.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawImageAnalysis.h"
#include "Raw/RawImageData.h"
#include "Raw/RawLoader.h"
#include "Raw/RawWorkspace.h"
#include "ThirdParty/json.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Stack::Validation {
namespace {

constexpr int kAnnotationTemplateSchemaVersion = 8;
constexpr int kStageEvidenceTemplateSchemaVersion = 9;
constexpr int kTemplateGenerationSchemaVersion = 10;
constexpr int kTuningConstantEvidenceGuideSchemaVersion = 1;
constexpr int kTuningConstantEvidenceStatusSchemaVersion = 1;
constexpr int kConstantReviewTemplateSchemaVersion = 4;
constexpr int kConstantReviewCheckSchemaVersion = 11;
constexpr int kConstantReviewRepairSchemaVersion = 9;
constexpr int kConstantReviewPatchBundleSchemaVersion = 1;
constexpr int kValidationWorkflowSchemaVersion = 8;
constexpr int kReadinessGateCatalogSchemaVersion = 2;
constexpr int kValidationGateStatusSchemaVersion = 3;
constexpr int kValidationEvidencePackageManifestSchemaVersion = 1;
constexpr int kValidationWorkflowReportSchemaVersion = 7;
constexpr int kAnnotationRepairSchemaVersion = 4;
constexpr int kStageEvidenceRepairSchemaVersion = 5;
constexpr int kAnnotationCheckSchemaVersion = 7;
constexpr int kStageEvidenceCheckSchemaVersion = 7;
constexpr int kRecordSidecarPreflightSchemaVersion = 11;
constexpr int kValidationSetSummarySchemaVersion = 36;
constexpr int kValidationRecordsSchemaVersion = 22;
constexpr int kValidationSummaryReportSchemaVersion = 35;

struct RawStartingPointAnnotationEntry {
    std::string primaryKey;
    std::vector<std::string> keys;
    nlohmann::json value = nlohmann::json::object();
};

struct RawStartingPointRecordOptions {
    std::filesystem::path workspaceRoot;
    std::filesystem::path outputPath;
    std::filesystem::path annotationPath;
    std::filesystem::path annotationTemplateOutputPath;
    std::filesystem::path stageEvidencePath;
    std::filesystem::path stageEvidenceTemplateOutputPath;
    std::filesystem::path sidecarPreflightOutputPath;
    int expectMinSources = 1;
    int maxSources = 0;
    bool loadRawSafety = false;
    bool templatesOnly = false;
    bool requireReadySidecars = false;
    int maxRawSafetySamples = 1000000;
    std::vector<std::string> tags;
    std::vector<RawStartingPointAnnotationEntry> annotationEntries;
    std::vector<RawStartingPointAnnotationEntry> stageEvidenceEntries;
};

struct ExpectedStartingPointCandidateUiLineSpec {
    std::string kind;
    std::string lineLabel;
};

struct TrackedStartingPointCandidateControlValueUiLineSpec {
    std::string key;
    std::string kind;
    std::string control;
    std::string lineLabel;
    std::string requiredDetailFragment;
};

struct ActionReadinessDetailGuardSpec {
    std::string label;
    std::vector<std::string> acceptedDetailFragments;
};

struct RawStartingPointAnnotationCheckOptions {
    std::filesystem::path workspaceRoot;
    std::filesystem::path annotationPath;
    std::filesystem::path outputPath;
    std::filesystem::path repairOutputPath;
    int expectMinSources = 1;
    int maxSources = 0;
    bool requireReady = false;
    std::vector<RawStartingPointAnnotationEntry> annotationEntries;
};

struct RawStartingPointStageEvidenceCheckOptions {
    std::filesystem::path workspaceRoot;
    std::filesystem::path stageEvidencePath;
    std::filesystem::path outputPath;
    std::filesystem::path repairOutputPath;
    int expectMinSources = 1;
    int maxSources = 0;
    std::vector<RawStartingPointAnnotationEntry> stageEvidenceEntries;
};

struct RawStartingPointSummaryOptions {
    std::filesystem::path inputPath;
    std::filesystem::path outputPath;
    std::filesystem::path constantReviewTemplateOutputPath;
    bool requireReady = false;
};

struct RawStartingPointConstantReviewCheckOptions {
    std::filesystem::path summaryPath;
    std::filesystem::path reviewTemplatePath;
    std::filesystem::path outputPath;
    std::filesystem::path repairOutputPath;
    std::filesystem::path suggestedReviewPatchBundleOutputPath;
    bool requireReady = false;
};

struct RawStartingPointValidationWorkflowOptions {
    std::filesystem::path workspaceRoot;
    std::filesystem::path annotationTemplatePath;
    std::filesystem::path annotationPath;
    std::filesystem::path stageEvidenceTemplatePath;
    std::filesystem::path stageEvidencePath;
    std::filesystem::path recordsPath;
    std::filesystem::path evidenceManifestOutputPath;
    std::filesystem::path outputPath;
};

struct RawStartingPointValidationGateStatusOptions {
    std::filesystem::path annotationCheckPath;
    std::filesystem::path stageEvidenceCheckPath;
    std::filesystem::path recordSidecarPreflightPath;
    std::filesystem::path validationSummaryPath;
    std::filesystem::path constantReviewCheckPath;
    std::filesystem::path outputPath;
    bool requireReady = false;
};

struct ConstantReviewValidationRecordEvidence {
    std::string canonicalId;
    nlohmann::json record = nlohmann::json::object();
    std::set<std::string> aliases;
};

struct ConstantReviewEvidenceIndex {
    bool available = false;
    std::string sourceReportPath;
    std::string unavailableReason;
    std::vector<ConstantReviewValidationRecordEvidence> records;
    std::map<std::string, std::size_t> aliasToRecord;
    std::set<std::string> knownRecordIds;
};

struct RawBufferSafetyEvidence {
    bool requested = false;
    bool loaded = false;
    Stack::RawAutoStartPoint::RawAutoStartPointRawSafetyStats stats;
    nlohmann::json details = nlohmann::json::object();
};

nlohmann::json JsonFloat(float value) {
    return std::isfinite(value) ? nlohmann::json(value) : nlohmann::json();
}

nlohmann::json JsonStringOrNull(const std::string& value) {
    return value.empty() ? nlohmann::json() : nlohmann::json(value);
}

nlohmann::json JsonStringVector(const std::vector<std::string>& values) {
    nlohmann::json result = nlohmann::json::array();
    for (const std::string& value : values) {
        result.push_back(value);
    }
    return result;
}

nlohmann::json BuildStartingPointDiagnosticCompletenessRow(
    const std::string& recordId,
    int recordIndex,
    bool recordValid,
    const std::string& recordStatus,
    bool hasStartingPointDiagnostics,
    bool hasCandidateDiagnostics,
    bool hasSourceLine,
    bool hasSourceScopeDetail,
    bool hasSelectedCandidate,
    bool hasAllExpectedCandidateKinds,
    bool hasAllExpectedCandidateScores,
    bool hasAllExpectedVisibleControlLines,
    bool hasAllTrackedCandidateControlValueLines,
    bool hasAllTrackedCandidateControlValueLineDetails,
    bool hasAllExpectedScoreComponentLines,
    bool hasAllExpectedWarningLines,
    bool hasAllExpectedCandidateStageDiagnostics,
    bool hasAllActionReadinessLines,
    bool hasAllActionReadinessGuardrailDetails,
    bool hasSelectedCandidateDetailLine,
    bool hasSelectedCandidateVisibleControlDetail,
    bool hasCandidateScoreOrderLine,
    bool hasCandidateScoreOrderGuardrailDetail,
    bool hasVisibleActionScopeLine,
    bool hasVisibleActionScopeGuardrailDetail,
    bool hasDryRunLine,
    bool hasDryRunReadOnlyDetail,
    bool hasRecipeWritesLine,
    bool hasRecipeWritesExplicitActionDetail,
    bool hasStageEvidenceLine,
    bool hasStageEvidenceSourceDetail,
    int partialEvidenceWarningCount,
    int partialEvidenceUiLineCount,
    const std::vector<std::string>& missingCandidateKinds,
    const std::vector<std::string>& missingCandidateScores,
    const std::vector<std::string>& missingVisibleControlLines,
    const std::vector<std::string>& missingCandidateControlValueLines,
    const std::vector<std::string>& missingCandidateControlValueLineDetails,
    const std::vector<std::string>& missingScoreComponentLines,
    const std::vector<std::string>& missingWarningLines,
    const std::vector<std::string>& missingCandidateStageDiagnostics,
    const std::vector<std::string>& missingActionReadinessLines,
    const std::vector<std::string>& missingActionReadinessGuardrailDetails) {
    const bool uiDiagnosticSetComplete =
        recordValid &&
        hasStartingPointDiagnostics &&
        hasCandidateDiagnostics &&
        hasSelectedCandidate &&
        hasAllExpectedCandidateKinds &&
        hasAllExpectedCandidateScores &&
        hasAllExpectedVisibleControlLines &&
        hasAllExpectedScoreComponentLines &&
        hasAllExpectedWarningLines &&
        hasAllActionReadinessLines;
    return {
        { "recordId", recordId },
        { "recordIndex", recordIndex },
        { "recordStatus", recordStatus },
        { "recordValid", recordValid },
        { "uiDiagnosticSetComplete", uiDiagnosticSetComplete },
        { "hasDiagnostics", hasStartingPointDiagnostics },
        { "hasCandidates", hasCandidateDiagnostics },
        { "hasSourceLine", hasSourceLine },
        { "hasSourceScopeDetail", hasSourceScopeDetail },
        { "sourceAttributionCoverageIsGating", false },
        { "missingSourceLine", !hasSourceLine },
        { "missingSourceScopeDetail", !hasSourceScopeDetail },
        { "hasSelectedCandidate", hasSelectedCandidate },
        { "hasExpectedCandidateKinds", hasAllExpectedCandidateKinds },
        { "hasExpectedCandidateScores", hasAllExpectedCandidateScores },
        { "hasExpectedVisibleControlLines", hasAllExpectedVisibleControlLines },
        { "hasTrackedCandidateControlValueLines", hasAllTrackedCandidateControlValueLines },
        { "hasTrackedCandidateControlValueLineDetails", hasAllTrackedCandidateControlValueLineDetails },
        { "candidateControlValueLineCoverageIsGating", false },
        { "hasExpectedScoreComponentLines", hasAllExpectedScoreComponentLines },
        { "hasExpectedWarningLines", hasAllExpectedWarningLines },
        { "hasExpectedCandidateStageDiagnostics", hasAllExpectedCandidateStageDiagnostics },
        { "candidateStageDiagnosticCoverageIsGating", false },
        { "hasActionReadinessDiagnostics", hasAllActionReadinessLines },
        { "hasActionReadinessGuardrailDetails", hasAllActionReadinessGuardrailDetails },
        { "actionReadinessGuardrailDetailCoverageIsGating", false },
        { "hasSelectedCandidateDetailLine", hasSelectedCandidateDetailLine },
        { "hasSelectedCandidateVisibleControlDetail", hasSelectedCandidateVisibleControlDetail },
        { "selectedCandidateDetailCoverageIsGating", false },
        { "missingSelectedCandidateDetailLine", !hasSelectedCandidateDetailLine },
        { "missingSelectedCandidateVisibleControlDetail", !hasSelectedCandidateVisibleControlDetail },
        { "hasCandidateScoreOrderLine", hasCandidateScoreOrderLine },
        { "hasCandidateScoreOrderGuardrailDetail", hasCandidateScoreOrderGuardrailDetail },
        { "candidateScoreOrderCoverageIsGating", false },
        { "missingCandidateScoreOrderLine", !hasCandidateScoreOrderLine },
        { "missingCandidateScoreOrderGuardrailDetail", !hasCandidateScoreOrderGuardrailDetail },
        { "hasVisibleActionScopeLine", hasVisibleActionScopeLine },
        { "hasVisibleActionScopeGuardrailDetail", hasVisibleActionScopeGuardrailDetail },
        { "visibleActionScopeCoverageIsGating", false },
        { "missingVisibleActionScopeLine", !hasVisibleActionScopeLine },
        { "missingVisibleActionScopeGuardrailDetail", !hasVisibleActionScopeGuardrailDetail },
        { "hasDryRunLine", hasDryRunLine },
        { "hasDryRunReadOnlyDetail", hasDryRunReadOnlyDetail },
        { "dryRunGuardrailCoverageIsGating", false },
        { "missingDryRunLine", !hasDryRunLine },
        { "missingDryRunReadOnlyDetail", !hasDryRunReadOnlyDetail },
        { "hasRecipeWritesLine", hasRecipeWritesLine },
        { "hasRecipeWritesExplicitActionDetail", hasRecipeWritesExplicitActionDetail },
        { "recipeWritesGuardrailCoverageIsGating", false },
        { "missingRecipeWritesLine", !hasRecipeWritesLine },
        { "missingRecipeWritesExplicitActionDetail", !hasRecipeWritesExplicitActionDetail },
        { "hasStageEvidenceLine", hasStageEvidenceLine },
        { "hasStageEvidenceSourceDetail", hasStageEvidenceSourceDetail },
        { "stageEvidenceUiLineCoverageIsGating", false },
        { "missingStageEvidenceLine", !hasStageEvidenceLine },
        { "missingStageEvidenceSourceDetail", !hasStageEvidenceSourceDetail },
        { "partialEvidenceWarningCount", partialEvidenceWarningCount },
        { "partialEvidenceUiLineCount", partialEvidenceUiLineCount },
        { "hasPartialEvidenceUiLine", partialEvidenceUiLineCount > 0 },
        { "partialEvidenceUiCoverageIsGating", false },
        { "missingPartialEvidenceUiLine", partialEvidenceWarningCount > 0 && partialEvidenceUiLineCount <= 0 },
        { "partialEvidenceLineCountMatchesWarnings", partialEvidenceWarningCount == partialEvidenceUiLineCount },
        { "missingCandidateKinds", JsonStringVector(missingCandidateKinds) },
        { "missingCandidateScores", JsonStringVector(missingCandidateScores) },
        { "missingVisibleControlLines", JsonStringVector(missingVisibleControlLines) },
        { "missingCandidateControlValueLines", JsonStringVector(missingCandidateControlValueLines) },
        { "missingCandidateControlValueLineDetails", JsonStringVector(missingCandidateControlValueLineDetails) },
        { "missingScoreComponentLines", JsonStringVector(missingScoreComponentLines) },
        { "missingWarningLines", JsonStringVector(missingWarningLines) },
        { "missingCandidateStageDiagnostics", JsonStringVector(missingCandidateStageDiagnostics) },
        { "missingActionReadinessLines", JsonStringVector(missingActionReadinessLines) },
        { "missingActionReadinessGuardrailDetails", JsonStringVector(missingActionReadinessGuardrailDetails) }
    };
}

std::string NormalizeAnnotationKey(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

void AddUniqueString(std::vector<std::string>& values, const std::string& value) {
    if (value.empty()) {
        return;
    }
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

void AddAnnotationKey(std::vector<std::string>& keys, const std::string& value) {
    AddUniqueString(keys, NormalizeAnnotationKey(value));
}

std::string JsonOptionalString(const nlohmann::json& object, const char* field) {
    if (!object.is_object() || field == nullptr || !object.contains(field) || !object[field].is_string()) {
        return {};
    }
    return object[field].get<std::string>();
}

std::string ExtractSourceIdentityToken(const nlohmann::json& object) {
    if (!object.is_object()) {
        return {};
    }

    const std::string directSourceToken =
        JsonOptionalString(object, "sourceIdentityToken");
    if (!directSourceToken.empty()) {
        return directSourceToken;
    }

    const std::string directIdentityToken =
        JsonOptionalString(object, "identityToken");
    if (!directIdentityToken.empty()) {
        return directIdentityToken;
    }

    const nlohmann::json source =
        object.value("source", nlohmann::json::object());
    if (!source.is_object()) {
        return {};
    }

    const std::string sourceToken =
        JsonOptionalString(source, "identityToken");
    if (!sourceToken.empty()) {
        return sourceToken;
    }

    return JsonOptionalString(source, "sourceIdentityToken");
}

std::vector<std::string> JsonStringList(const nlohmann::json& value) {
    std::vector<std::string> result;
    if (value.is_string()) {
        AddUniqueString(result, value.get<std::string>());
        return result;
    }
    if (!value.is_array()) {
        return result;
    }
    for (const nlohmann::json& item : value) {
        if (item.is_string()) {
            AddUniqueString(result, item.get<std::string>());
        }
    }
    return result;
}

std::string RecordSourceId(const nlohmann::json& record, int recordIndex) {
    if (record.is_object()) {
        const nlohmann::json source = record.value("source", nlohmann::json::object());
        if (source.is_object()) {
            static constexpr const char* fields[] = {
                "id",
                "relativePath",
                "fileName",
                "stem",
                "absolutePath",
                "fingerprint",
                "identityToken",
                "sourceIdentityToken"
            };
            for (const char* field : fields) {
                const std::string value = JsonOptionalString(source, field);
                if (!value.empty()) {
                    return value;
                }
            }
        }

        static constexpr const char* recordFields[] = {
            "id",
            "sourceId",
            "relativePath",
            "path",
            "fileName",
            "stem",
            "identityToken",
            "sourceIdentityToken"
        };
        for (const char* field : recordFields) {
            const std::string value = JsonOptionalString(record, field);
            if (!value.empty()) {
                return value;
            }
        }
    }
    return "record-" + std::to_string(recordIndex);
}

void AddAnnotationAliases(std::vector<std::string>& keys, const nlohmann::json& annotation) {
    static constexpr const char* directFields[] = {
        "id",
        "sourceId",
        "relativePath",
        "path",
        "fileName",
        "stem",
        "absolutePath",
        "fingerprint",
        "identityToken",
        "sourceIdentityToken"
    };
    for (const char* field : directFields) {
        AddAnnotationKey(keys, JsonOptionalString(annotation, field));
    }

    const nlohmann::json source = annotation.value("source", nlohmann::json::object());
    if (source.is_object()) {
        for (const char* field : directFields) {
            AddAnnotationKey(keys, JsonOptionalString(source, field));
        }
    }
}

std::string PrimaryAnnotationKeyFromValue(
    const nlohmann::json& annotation,
    const std::string& fallback) {
    if (!annotation.is_object()) {
        return NormalizeAnnotationKey(fallback);
    }

    static constexpr const char* primaryFields[] = {
        "id",
        "sourceId",
        "relativePath",
        "path",
        "fileName",
        "stem"
    };
    for (const char* field : primaryFields) {
        std::string value = JsonOptionalString(annotation, field);
        if (!value.empty()) {
            return NormalizeAnnotationKey(value);
        }
    }

    const nlohmann::json source = annotation.value("source", nlohmann::json::object());
    if (source.is_object()) {
        for (const char* field : primaryFields) {
            std::string value = JsonOptionalString(source, field);
            if (!value.empty()) {
                return NormalizeAnnotationKey(value);
            }
        }
    }
    return NormalizeAnnotationKey(fallback);
}

void AddAnnotationEntry(
    std::vector<RawStartingPointAnnotationEntry>& entries,
    const std::string& primaryKey,
    const nlohmann::json& value) {
    if (!value.is_object()) {
        return;
    }

    RawStartingPointAnnotationEntry entry;
    entry.primaryKey = NormalizeAnnotationKey(primaryKey);
    entry.value = value;
    AddAnnotationKey(entry.keys, entry.primaryKey);
    AddAnnotationAliases(entry.keys, value);
    if (entry.primaryKey.empty() && !entry.keys.empty()) {
        entry.primaryKey = entry.keys.front();
    }
    if (entry.primaryKey.empty()) {
        entry.primaryKey = "annotation-" + std::to_string(entries.size());
        AddAnnotationKey(entry.keys, entry.primaryKey);
    }
    entries.push_back(std::move(entry));
}

bool LoadEvidenceEntries(
    const std::filesystem::path& evidencePath,
    std::vector<RawStartingPointAnnotationEntry>& entries,
    const char* evidenceLabel,
    const char* fallbackPrefix) {
    entries.clear();
    if (evidencePath.empty()) {
        return true;
    }

    std::error_code ec;
    if (!std::filesystem::exists(evidencePath, ec) || ec ||
        !std::filesystem::is_regular_file(evidencePath, ec) || ec) {
        std::cerr << "RAW Starting Point record validation failed: "
                  << evidenceLabel << " file does not exist: "
                  << evidencePath.string() << "\n";
        return false;
    }

    std::ifstream in(evidencePath, std::ios::binary);
    if (!in) {
        std::cerr << "RAW Starting Point record validation failed: could not open "
                  << evidenceLabel << " file "
                  << evidencePath.string() << "\n";
        return false;
    }

    nlohmann::json root;
    try {
        in >> root;
    } catch (const std::exception& ex) {
        std::cerr << "RAW Starting Point record validation failed: invalid JSON in "
                  << evidenceLabel << " file "
                  << evidencePath.string() << ": " << ex.what() << "\n";
        return false;
    }

    const nlohmann::json* collection = &root;
    if (root.is_object()) {
        static constexpr const char* collectionFields[] = {
            "records",
            "bySource",
            "annotations",
            "stageEvidence",
            "evidence"
        };
        for (const char* field : collectionFields) {
            if (root.contains(field)) {
                collection = &root[field];
                break;
            }
        }
    }

    if (collection->is_object()) {
        for (auto it = collection->begin(); it != collection->end(); ++it) {
            AddAnnotationEntry(entries, it.key(), it.value());
        }
        return true;
    }
    if (collection->is_array()) {
        for (std::size_t i = 0; i < collection->size(); ++i) {
            const nlohmann::json& value = (*collection)[i];
            AddAnnotationEntry(
                entries,
                PrimaryAnnotationKeyFromValue(
                    value,
                    std::string(fallbackPrefix) + "-" + std::to_string(i)),
                value);
        }
        return true;
    }

    std::cerr << "RAW Starting Point record validation failed: "
              << evidenceLabel << " file must contain an object, records object, "
              << "bySource object, annotations object, stageEvidence object, "
              << "evidence object, or array.\n";
    return false;
}

bool LoadAnnotationEntries(
    const std::filesystem::path& annotationPath,
    std::vector<RawStartingPointAnnotationEntry>& entries) {
    return LoadEvidenceEntries(annotationPath, entries, "annotations", "annotation");
}

bool LoadStageEvidenceEntries(
    const std::filesystem::path& stageEvidencePath,
    std::vector<RawStartingPointAnnotationEntry>& entries) {
    return LoadEvidenceEntries(stageEvidencePath, entries, "stage evidence", "stage-evidence");
}

bool PathLooksLikeOption(const char* text) {
    return text != nullptr && text[0] == '-' && text[1] == '-';
}

void PrintUsage() {
    std::cerr
        << "RAW Starting Point record usage: --validate-raw-starting-point-records "
        << "<workspace-folder> [--out records.json] [--expect-min-sources N] "
        << "[--max-sources N] [--load-raw-safety] [--max-raw-safety-samples N] "
        << "[--tag name] [--annotations annotations.json] "
        << "[--stage-evidence stage-evidence.json] "
        << "[--annotation-template-out annotations-template.json] "
        << "[--stage-evidence-template-out stage-evidence-template.json] "
        << "[--templates-only] [--require-ready-sidecars] "
        << "[--sidecar-preflight-out preflight.json]\n";
}

bool ParseOptions(int rawArgCount, char** rawArgs, RawStartingPointRecordOptions& options) {
    if (rawArgCount <= 0 || rawArgs == nullptr || rawArgs[0] == nullptr || PathLooksLikeOption(rawArgs[0])) {
        PrintUsage();
        return false;
    }

    options.workspaceRoot = rawArgs[0];
    for (int i = 1; i < rawArgCount; ++i) {
        const std::string option = rawArgs[i] ? rawArgs[i] : "";
        auto requireValue = [&](const char* name) -> const char* {
            if (i + 1 >= rawArgCount || rawArgs[i + 1] == nullptr) {
                std::cerr << "RAW Starting Point record validation failed: "
                          << name << " requires a value.\n";
                return nullptr;
            }
            return rawArgs[++i];
        };

        if (option == "--out") {
            const char* value = requireValue("--out");
            if (value == nullptr) {
                return false;
            }
            options.outputPath = value;
        } else if (option == "--expect-min-sources") {
            const char* value = requireValue("--expect-min-sources");
            if (value == nullptr) {
                return false;
            }
            try {
                options.expectMinSources = std::max(0, std::stoi(value));
            } catch (...) {
                std::cerr << "RAW Starting Point record validation failed: invalid --expect-min-sources value.\n";
                return false;
            }
        } else if (option == "--max-sources") {
            const char* value = requireValue("--max-sources");
            if (value == nullptr) {
                return false;
            }
            try {
                options.maxSources = std::max(0, std::stoi(value));
            } catch (...) {
                std::cerr << "RAW Starting Point record validation failed: invalid --max-sources value.\n";
                return false;
            }
        } else if (option == "--load-raw-safety") {
            options.loadRawSafety = true;
        } else if (option == "--templates-only") {
            options.templatesOnly = true;
        } else if (option == "--require-ready-sidecars") {
            options.requireReadySidecars = true;
        } else if (option == "--sidecar-preflight-out") {
            const char* value = requireValue("--sidecar-preflight-out");
            if (value == nullptr) {
                return false;
            }
            options.sidecarPreflightOutputPath = value;
        } else if (option == "--max-raw-safety-samples") {
            const char* value = requireValue("--max-raw-safety-samples");
            if (value == nullptr) {
                return false;
            }
            try {
                options.maxRawSafetySamples = std::max(1, std::stoi(value));
            } catch (...) {
                std::cerr << "RAW Starting Point record validation failed: invalid --max-raw-safety-samples value.\n";
                return false;
            }
        } else if (option == "--tag") {
            const char* value = requireValue("--tag");
            if (value == nullptr) {
                return false;
            }
            options.tags.push_back(value);
        } else if (option == "--annotations") {
            const char* value = requireValue("--annotations");
            if (value == nullptr) {
                return false;
            }
            options.annotationPath = value;
        } else if (option == "--stage-evidence") {
            const char* value = requireValue("--stage-evidence");
            if (value == nullptr) {
                return false;
            }
            options.stageEvidencePath = value;
        } else if (option == "--annotation-template-out") {
            const char* value = requireValue("--annotation-template-out");
            if (value == nullptr) {
                return false;
            }
            options.annotationTemplateOutputPath = value;
        } else if (option == "--stage-evidence-template-out") {
            const char* value = requireValue("--stage-evidence-template-out");
            if (value == nullptr) {
                return false;
            }
            options.stageEvidenceTemplateOutputPath = value;
        } else {
            std::cerr << "RAW Starting Point record validation failed: unknown argument "
                      << option << "\n";
            return false;
        }
    }
    return true;
}

void PrintAnnotationCheckUsage() {
    std::cerr
        << "RAW Starting Point annotation check usage: "
        << "--check-raw-starting-point-annotations "
        << "<workspace-folder> <annotations.json> [--out report.json] "
        << "[--repair-out repair.json] [--expect-min-sources N] "
        << "[--max-sources N] [--require-ready]\n";
}

bool ParseAnnotationCheckOptions(
    int rawArgCount,
    char** rawArgs,
    RawStartingPointAnnotationCheckOptions& options) {
    if (rawArgCount < 2 ||
        rawArgs == nullptr ||
        rawArgs[0] == nullptr ||
        rawArgs[1] == nullptr ||
        PathLooksLikeOption(rawArgs[0]) ||
        PathLooksLikeOption(rawArgs[1])) {
        PrintAnnotationCheckUsage();
        return false;
    }

    options.workspaceRoot = rawArgs[0];
    options.annotationPath = rawArgs[1];
    for (int i = 2; i < rawArgCount; ++i) {
        const std::string option = rawArgs[i] ? rawArgs[i] : "";
        auto requireValue = [&](const char* name) -> const char* {
            if (i + 1 >= rawArgCount || rawArgs[i + 1] == nullptr) {
                std::cerr << "RAW Starting Point annotation check failed: "
                          << name << " requires a value.\n";
                return nullptr;
            }
            return rawArgs[++i];
        };

        if (option == "--out") {
            const char* value = requireValue("--out");
            if (value == nullptr) {
                return false;
            }
            options.outputPath = value;
        } else if (option == "--repair-out") {
            const char* value = requireValue("--repair-out");
            if (value == nullptr) {
                return false;
            }
            options.repairOutputPath = value;
        } else if (option == "--require-ready") {
            options.requireReady = true;
        } else if (option == "--expect-min-sources") {
            const char* value = requireValue("--expect-min-sources");
            if (value == nullptr) {
                return false;
            }
            try {
                options.expectMinSources = std::max(0, std::stoi(value));
            } catch (...) {
                std::cerr << "RAW Starting Point annotation check failed: invalid --expect-min-sources value.\n";
                return false;
            }
        } else if (option == "--max-sources") {
            const char* value = requireValue("--max-sources");
            if (value == nullptr) {
                return false;
            }
            try {
                options.maxSources = std::max(0, std::stoi(value));
            } catch (...) {
                std::cerr << "RAW Starting Point annotation check failed: invalid --max-sources value.\n";
                return false;
            }
        } else {
            std::cerr << "RAW Starting Point annotation check failed: unknown argument "
                      << option << "\n";
            return false;
        }
    }
    return true;
}

void PrintStageEvidenceCheckUsage() {
    std::cerr
        << "RAW Starting Point stage evidence check usage: "
        << "--check-raw-starting-point-stage-evidence "
        << "<workspace-folder> <stage-evidence.json> [--out report.json] "
        << "[--repair-out repair.json] [--expect-min-sources N] "
        << "[--max-sources N]\n";
}

bool ParseStageEvidenceCheckOptions(
    int rawArgCount,
    char** rawArgs,
    RawStartingPointStageEvidenceCheckOptions& options) {
    if (rawArgCount < 2 ||
        rawArgs == nullptr ||
        rawArgs[0] == nullptr ||
        rawArgs[1] == nullptr ||
        PathLooksLikeOption(rawArgs[0]) ||
        PathLooksLikeOption(rawArgs[1])) {
        PrintStageEvidenceCheckUsage();
        return false;
    }

    options.workspaceRoot = rawArgs[0];
    options.stageEvidencePath = rawArgs[1];
    for (int i = 2; i < rawArgCount; ++i) {
        const std::string option = rawArgs[i] ? rawArgs[i] : "";
        auto requireValue = [&](const char* name) -> const char* {
            if (i + 1 >= rawArgCount || rawArgs[i + 1] == nullptr) {
                std::cerr << "RAW Starting Point stage evidence check failed: "
                          << name << " requires a value.\n";
                return nullptr;
            }
            return rawArgs[++i];
        };

        if (option == "--out") {
            const char* value = requireValue("--out");
            if (value == nullptr) {
                return false;
            }
            options.outputPath = value;
        } else if (option == "--repair-out") {
            const char* value = requireValue("--repair-out");
            if (value == nullptr) {
                return false;
            }
            options.repairOutputPath = value;
        } else if (option == "--expect-min-sources") {
            const char* value = requireValue("--expect-min-sources");
            if (value == nullptr) {
                return false;
            }
            try {
                options.expectMinSources = std::max(0, std::stoi(value));
            } catch (...) {
                std::cerr << "RAW Starting Point stage evidence check failed: invalid --expect-min-sources value.\n";
                return false;
            }
        } else if (option == "--max-sources") {
            const char* value = requireValue("--max-sources");
            if (value == nullptr) {
                return false;
            }
            try {
                options.maxSources = std::max(0, std::stoi(value));
            } catch (...) {
                std::cerr << "RAW Starting Point stage evidence check failed: invalid --max-sources value.\n";
                return false;
            }
        } else {
            std::cerr << "RAW Starting Point stage evidence check failed: unknown argument "
                      << option << "\n";
            return false;
        }
    }
    return true;
}

nlohmann::json SerializeFloatArray(const std::array<float, 4>& values) {
    return nlohmann::json::array({
        JsonFloat(values[0]),
        JsonFloat(values[1]),
        JsonFloat(values[2]),
        JsonFloat(values[3])
    });
}

nlohmann::json SerializeFloat3(const std::array<float, 3>& values) {
    return nlohmann::json::array({
        JsonFloat(values[0]),
        JsonFloat(values[1]),
        JsonFloat(values[2])
    });
}

nlohmann::json SerializeMetadataSummary(const Stack::RawAnalysis::RawMetadataSummary& summary) {
    return {
        { "hasCameraWhiteBalance", summary.hasCameraWhiteBalance },
        { "hasBaselineExposure", summary.hasBaselineExposure },
        { "hasBaselineNoise", summary.hasBaselineNoise },
        { "hasBaselineSharpness", summary.hasBaselineSharpness },
        { "hasActiveArea", summary.hasActiveArea },
        { "hasMaskedAreas", summary.hasMaskedAreas },
        { "hasLinearResponseLimit", summary.hasLinearResponseLimit },
        { "appliedOpcodeList2Count", summary.appliedOpcodeList2Count },
        { "unsupportedOpcodeList1Count", summary.unsupportedOpcodeList1Count },
        { "unsupportedOpcodeList2Count", summary.unsupportedOpcodeList2Count },
        { "unsupportedOpcodeList3Count", summary.unsupportedOpcodeList3Count },
        { "cameraWbR", JsonFloat(summary.cameraWbR) },
        { "cameraWbG", JsonFloat(summary.cameraWbG) },
        { "cameraWbB", JsonFloat(summary.cameraWbB) },
        { "baselineExposureEv", JsonFloat(summary.baselineExposureEv) },
        { "baselineNoise", JsonFloat(summary.baselineNoise) },
        { "baselineSharpness", JsonFloat(summary.baselineSharpness) },
        { "iso", JsonFloat(summary.iso) },
        { "shutterSeconds", JsonFloat(summary.shutterSeconds) },
        { "aperture", JsonFloat(summary.aperture) },
        { "linearResponseLimit", JsonFloat(summary.linearResponseLimit) }
    };
}

nlohmann::json SerializeRawMetadata(const Raw::RawMetadata& metadata) {
    return {
        { "loaded", true },
        { "sourcePath", JsonStringOrNull(metadata.sourcePath) },
        { "cameraMake", JsonStringOrNull(metadata.cameraMake) },
        { "cameraModel", JsonStringOrNull(metadata.cameraModel) },
        { "dngUniqueCameraModel", JsonStringOrNull(metadata.dngUniqueCameraModel) },
        { "rawWidth", metadata.rawWidth },
        { "rawHeight", metadata.rawHeight },
        { "visibleWidth", metadata.visibleWidth },
        { "visibleHeight", metadata.visibleHeight },
        { "displayWidth", Raw::DisplayWidth(metadata) },
        { "displayHeight", Raw::DisplayHeight(metadata) },
        { "orientation", metadata.orientation },
        { "bitDepth", metadata.bitDepth },
        { "cfaPattern", Raw::CfaPatternName(metadata.cfaPattern) },
        { "pixelLayout", Raw::RawPixelLayoutName(metadata.pixelLayout) },
        { "sampleFormat", Raw::RawSampleFormatName(metadata.linearSampleFormat) },
        { "isDng", metadata.isDng },
        { "blackLevel", JsonFloat(metadata.blackLevel) },
        { "perChannelBlack", SerializeFloatArray(metadata.perChannelBlack) },
        { "whiteLevel", JsonFloat(metadata.whiteLevel) },
        { "blackLevelSource", JsonStringOrNull(metadata.blackLevelSource) },
        { "whiteLevelSource", JsonStringOrNull(metadata.whiteLevelSource) },
        { "whiteBalanceSource", JsonStringOrNull(metadata.whiteBalanceSource) },
        { "cameraMatrixSource", JsonStringOrNull(metadata.cameraMatrixSource) },
        { "rawMinimum", JsonFloat(metadata.rawMinimum) },
        { "rawMaximum", JsonFloat(metadata.rawMaximum) },
        { "defaultWhiteClipPercent", JsonFloat(metadata.defaultWhiteClipPercent) },
        { "cameraWhiteBalance", SerializeFloatArray(metadata.cameraWhiteBalance) },
        { "hasExposureTime", metadata.hasExposureTime },
        { "exposureTimeSeconds", JsonFloat(metadata.exposureTimeSeconds) },
        { "hasIsoSpeed", metadata.hasIsoSpeed },
        { "isoSpeed", JsonFloat(metadata.isoSpeed) },
        { "hasApertureFNumber", metadata.hasApertureFNumber },
        { "apertureFNumber", JsonFloat(metadata.apertureFNumber) },
        { "hasDngAsShotNeutral", metadata.hasDngAsShotNeutral },
        { "dngAsShotNeutral", SerializeFloat3(metadata.dngAsShotNeutral) },
        { "hasCameraMatrix", metadata.hasCameraMatrix },
        { "hasDngColorMatrix1", metadata.hasDngColorMatrix1 },
        { "hasDngColorMatrix2", metadata.hasDngColorMatrix2 },
        { "hasDngForwardMatrix1", metadata.hasDngForwardMatrix1 },
        { "hasDngForwardMatrix2", metadata.hasDngForwardMatrix2 },
        { "hasDngBaselineExposure", metadata.hasDngBaselineExposure },
        { "dngBaselineExposure", JsonFloat(metadata.dngBaselineExposure) },
        { "dngGainMapCount", metadata.dngGainMapCount },
        { "dngUnsupportedOpcodeCount", metadata.dngUnsupportedOpcodeCount },
        { "uploadFormat", JsonStringOrNull(metadata.uploadFormat) },
        { "dngTypeStatus", JsonStringOrNull(metadata.dngTypeStatus) },
        { "warnings", JsonStringVector(metadata.warnings) },
        { "error", JsonStringOrNull(metadata.error) }
    };
}

Stack::RawAutoStartPoint::RawAutoStartPointRawSafetyStats BuildMetadataOnlyRawSafety(
    const Raw::RawMetadata& metadata,
    const Stack::RawAnalysis::RawMetadataSummary& summary) {
    Stack::RawAutoStartPoint::RawAutoStartPointRawSafetyStats stats;
    stats.valid = true;
    stats.activeValidFraction =
        metadata.rawWidth > 0 && metadata.rawHeight > 0 &&
        metadata.visibleWidth > 0 && metadata.visibleHeight > 0
            ? std::clamp(
                (static_cast<float>(metadata.visibleWidth) * static_cast<float>(metadata.visibleHeight)) /
                    (static_cast<float>(metadata.rawWidth) * static_cast<float>(metadata.rawHeight)),
                0.0f,
                1.0f)
            : 0.0f;
    stats.blackLevelSource = metadata.blackLevelSource;
    stats.whiteLevelSource = metadata.whiteLevelSource;
    stats.rawWhiteP999 = metadata.whiteLevel > 0.0f
        ? std::clamp(metadata.rawMaximum / metadata.whiteLevel, 0.0f, 4.0f)
        : 0.0f;
    stats.linearResponseLimit = summary.linearResponseLimit;
    if (stats.rawWhiteP999 > 0.0f && stats.linearResponseLimit > 0.0f) {
        stats.headroomEv = std::log2(
            std::max(1.0e-6f, stats.linearResponseLimit) /
            std::max(1.0e-6f, stats.rawWhiteP999));
    }
    stats.wbScaledHeadroomEv = stats.headroomEv;
    stats.baselineExposureEv = summary.baselineExposureEv;
    stats.asShotWbAvailable = summary.hasCameraWhiteBalance;
    stats.colorMatrixConfidence =
        (metadata.hasDngForwardMatrix1 || metadata.hasDngForwardMatrix2)
            ? 1.0f
            : (metadata.hasCameraMatrix || metadata.hasDngColorMatrix1 || metadata.hasDngColorMatrix2
                ? 0.65f
                : 0.25f);
    stats.lensShadingConfidence = metadata.dngGainMapCount > 0 ? 0.70f : 0.0f;
    stats.statusMessage =
        "Metadata-only raw safety proxy. Use rendered validation records before tuning constants.";
    return stats;
}

int CfaColorAt(const Raw::RawMetadata& metadata, int x, int y) {
    const int px = x & 1;
    const int py = y & 1;
    switch (metadata.cfaPattern) {
        case Raw::CfaPattern::RGGB:
            if (py == 0 && px == 0) return 0;
            if (py == 1 && px == 1) return 2;
            return 1;
        case Raw::CfaPattern::BGGR:
            if (py == 0 && px == 0) return 2;
            if (py == 1 && px == 1) return 0;
            return 1;
        case Raw::CfaPattern::GBRG:
            if (py == 0 && px == 1) return 2;
            if (py == 1 && px == 0) return 0;
            return 1;
        case Raw::CfaPattern::GRBG:
            if (py == 0 && px == 1) return 0;
            if (py == 1 && px == 0) return 2;
            return 1;
        case Raw::CfaPattern::Unknown:
        default:
            return 1;
    }
}

float BlackForColor(const std::array<float, 4>& channelBlack, float fallback, int color) {
    if (color == 0 && channelBlack[0] > 0.0f) return channelBlack[0];
    if (color == 1 && channelBlack[1] > 0.0f) return channelBlack[1];
    if (color == 2 && channelBlack[2] > 0.0f) return channelBlack[2];
    return fallback;
}

float PercentileFromSorted(const std::vector<float>& values, float percentile) {
    if (values.empty()) {
        return 0.0f;
    }
    const float clamped = std::clamp(percentile, 0.0f, 1.0f);
    const std::size_t index = static_cast<std::size_t>(
        std::round(clamped * static_cast<float>(values.size() - 1)));
    return values[index];
}

float Fraction(std::size_t count, std::size_t total) {
    return total > 0
        ? static_cast<float>(count) / static_cast<float>(total)
        : 0.0f;
}

nlohmann::json SerializeSize3(const std::array<std::size_t, 3>& values) {
    return nlohmann::json::array({
        values[0],
        values[1],
        values[2]
    });
}

std::array<float, 3> ResolveCameraWbGains(const Raw::RawMetadata& metadata) {
    const auto positiveOrOne = [](float value) {
        return std::isfinite(value) && value > 0.001f ? value : 1.0f;
    };
    std::array<float, 3> gains {
        positiveOrOne(metadata.cameraWhiteBalance[0]),
        positiveOrOne(metadata.cameraWhiteBalance[1]),
        positiveOrOne(metadata.cameraWhiteBalance[2])
    };
    const float green = std::max(0.001f, gains[1]);
    gains[0] /= green;
    gains[1] = 1.0f;
    gains[2] /= green;
    return gains;
}

RawBufferSafetyEvidence BuildRawBufferSafetyEvidence(
    const Raw::RawImageData& raw,
    const Raw::RawMetadata& metadata,
    const Stack::RawAnalysis::RawMetadataSummary& summary,
    int maxSamples) {
    RawBufferSafetyEvidence evidence;
    evidence.requested = true;
    evidence.stats = BuildMetadataOnlyRawSafety(metadata, summary);

    evidence.details = {
        { "requested", true },
        { "loaded", false },
        { "source", "unavailable" },
        { "maxSamples", maxSamples }
    };

    const int rawW = metadata.rawWidth;
    const int rawH = metadata.rawHeight;
    const int visibleW = metadata.visibleWidth > 0 ? metadata.visibleWidth : rawW;
    const int visibleH = metadata.visibleHeight > 0 ? metadata.visibleHeight : rawH;
    if (rawW <= 0 || rawH <= 0 || visibleW <= 0 || visibleH <= 0) {
        evidence.details["statusMessage"] = "Raw buffer safety unavailable: missing raw or visible dimensions.";
        return evidence;
    }

    const std::size_t rawPixelCount =
        static_cast<std::size_t>(rawW) * static_cast<std::size_t>(rawH);
    if (raw.rawBuffer.size() < rawPixelCount) {
        evidence.details["statusMessage"] =
            "Raw buffer safety unavailable: decoded source did not expose a mosaiced rawBuffer.";
        evidence.details["rawBufferPixels"] = raw.rawBuffer.size();
        evidence.details["expectedRawPixels"] = rawPixelCount;
        evidence.details["pixelLayout"] = Raw::RawPixelLayoutName(metadata.pixelLayout);
        return evidence;
    }

    const int left = std::clamp(metadata.leftMargin, 0, std::max(0, rawW - 1));
    const int top = std::clamp(metadata.topMargin, 0, std::max(0, rawH - 1));
    const int clippedVisibleW = std::clamp(visibleW, 1, std::max(1, rawW - left));
    const int clippedVisibleH = std::clamp(visibleH, 1, std::max(1, rawH - top));
    const std::size_t visiblePixelCount =
        static_cast<std::size_t>(clippedVisibleW) * static_cast<std::size_t>(clippedVisibleH);
    const std::size_t sampleBudget = static_cast<std::size_t>(std::max(1, maxSamples));
    const std::size_t sampleStride =
        std::max<std::size_t>(1, (visiblePixelCount + sampleBudget - 1) / sampleBudget);

    std::array<float, 4> channelBlack = metadata.perChannelBlack;
    if (channelBlack[1] > 0.0f && channelBlack[3] > 0.0f) {
        channelBlack[1] = (channelBlack[1] + channelBlack[3]) * 0.5f;
    }

    const float fallbackBlack = std::isfinite(metadata.blackLevel) ? metadata.blackLevel : 0.0f;
    const float metadataWhite = std::isfinite(metadata.whiteLevel) && metadata.whiteLevel > fallbackBlack + 1.0f
        ? metadata.whiteLevel
        : 65535.0f;
    const float clipThreshold = 1.0f;
    const float nearClipMargin = 0.01f;
    const float nearClipThreshold = std::max(0.0f, clipThreshold - nearClipMargin);

    std::array<std::vector<float>, 3> channelValues;
    const std::size_t reservePerChannel =
        std::max<std::size_t>(1, std::min(sampleBudget, visiblePixelCount) / 3u);
    for (std::vector<float>& values : channelValues) {
        values.reserve(reservePerChannel);
    }

    std::array<std::size_t, 3> channelCounts { 0u, 0u, 0u };
    std::array<std::size_t, 3> channelClipCounts { 0u, 0u, 0u };
    std::array<std::size_t, 3> channelNearClipCounts { 0u, 0u, 0u };
    std::size_t sampledPixels = 0;
    std::size_t clippedMosaicSamples = 0;
    std::size_t visibleOrdinal = 0;

    for (int y = 0; y < clippedVisibleH; ++y) {
        const int rawY = top + y;
        for (int x = 0; x < clippedVisibleW; ++x, ++visibleOrdinal) {
            if ((visibleOrdinal % sampleStride) != 0) {
                continue;
            }

            const int rawX = left + x;
            const std::size_t rawIndex =
                static_cast<std::size_t>(rawY) * static_cast<std::size_t>(rawW) +
                static_cast<std::size_t>(rawX);
            int color = CfaColorAt(metadata, x, y);
            color = std::clamp(color, 0, 2);
            const float black = BlackForColor(channelBlack, fallbackBlack, color);
            const float white = std::max(black + 1.0f, metadataWhite);
            const float rawValue = static_cast<float>(raw.rawBuffer[rawIndex]);
            const float normalized = (rawValue - black) / std::max(1.0f, white - black);

            channelValues[static_cast<std::size_t>(color)].push_back(normalized);
            ++channelCounts[static_cast<std::size_t>(color)];
            ++sampledPixels;
            if (normalized >= clipThreshold) {
                ++channelClipCounts[static_cast<std::size_t>(color)];
                ++clippedMosaicSamples;
            }
            if (normalized >= nearClipThreshold) {
                ++channelNearClipCounts[static_cast<std::size_t>(color)];
            }
        }
    }

    if (sampledPixels == 0) {
        evidence.details["statusMessage"] = "Raw buffer safety unavailable: sampling produced no valid pixels.";
        return evidence;
    }

    Stack::RawAutoStartPoint::RawAutoStartPointRawSafetyStats stats;
    stats.valid = true;
    stats.activeValidFraction =
        rawPixelCount > 0 ? std::clamp(Fraction(visiblePixelCount, rawPixelCount), 0.0f, 1.0f) : 0.0f;
    stats.blackLevelSource = metadata.blackLevelSource;
    stats.whiteLevelSource = metadata.whiteLevelSource;
    stats.linearResponseLimit = clipThreshold;
    stats.baselineExposureEv = summary.baselineExposureEv;
    stats.asShotWbAvailable = summary.hasCameraWhiteBalance;
    stats.colorMatrixConfidence =
        (metadata.hasDngForwardMatrix1 || metadata.hasDngForwardMatrix2)
            ? 1.0f
            : (metadata.hasCameraMatrix || metadata.hasDngColorMatrix1 || metadata.hasDngColorMatrix2
                ? 0.65f
                : 0.25f);
    stats.lensShadingConfidence = metadata.dngGainMapCount > 0 ? 0.70f : 0.0f;

    for (int c = 0; c < 3; ++c) {
        std::vector<float>& values = channelValues[static_cast<std::size_t>(c)];
        if (!values.empty()) {
            std::sort(values.begin(), values.end());
            stats.perChannelP999[static_cast<std::size_t>(c)] = PercentileFromSorted(values, 0.999f);
        }
        stats.perChannelClippedFraction[static_cast<std::size_t>(c)] =
            Fraction(channelClipCounts[static_cast<std::size_t>(c)], channelCounts[static_cast<std::size_t>(c)]);
        stats.perChannelNearClippedFraction[static_cast<std::size_t>(c)] =
            Fraction(channelNearClipCounts[static_cast<std::size_t>(c)], channelCounts[static_cast<std::size_t>(c)]);
        stats.rawWhiteP999 = std::max(
            stats.rawWhiteP999,
            stats.perChannelP999[static_cast<std::size_t>(c)]);
    }

    if (stats.rawWhiteP999 > 0.0f) {
        stats.headroomEv = std::log2(
            std::max(1.0e-6f, clipThreshold) /
            std::max(1.0e-6f, stats.rawWhiteP999));
    }

    const std::array<float, 3> wbGains = ResolveCameraWbGains(metadata);
    const float wbGeom = std::cbrt(std::max(
        1.0e-6f,
        wbGains[0] * wbGains[1] * wbGains[2]));
    std::array<float, 3> wbNormalized {
        wbGains[0] / wbGeom,
        wbGains[1] / wbGeom,
        wbGains[2] / wbGeom
    };
    float wbScaledHeadroom = 100.0f;
    bool hasWbScaledHeadroom = false;
    for (int c = 0; c < 3; ++c) {
        if (channelCounts[static_cast<std::size_t>(c)] == 0 ||
            stats.perChannelP999[static_cast<std::size_t>(c)] <= 0.0f) {
            continue;
        }
        const float scaledP999 =
            stats.perChannelP999[static_cast<std::size_t>(c)] *
            std::max(0.001f, wbNormalized[static_cast<std::size_t>(c)]);
        wbScaledHeadroom = std::min(
            wbScaledHeadroom,
            std::log2(std::max(1.0e-6f, clipThreshold) / std::max(1.0e-6f, scaledP999)));
        hasWbScaledHeadroom = true;
    }
    stats.wbScaledHeadroomEv = hasWbScaledHeadroom ? wbScaledHeadroom : stats.headroomEv;
    stats.singleChannelClipFraction = Fraction(clippedMosaicSamples, sampledPixels);
    stats.multiChannelClipFraction = 0.0f;
    stats.fullClipFraction = 0.0f;
    const float maxChannelClip = std::max({
        stats.perChannelClippedFraction[0],
        stats.perChannelClippedFraction[1],
        stats.perChannelClippedFraction[2]
    });
    stats.highlightRecoverabilityScore = 1.0f - std::clamp(maxChannelClip / 0.02f, 0.0f, 1.0f);
    stats.statusMessage =
        "Sampled raw mosaic safety from decoded rawBuffer. Multi/full pixel clipping remains unavailable until channel-aligned stage evidence exists.";

    evidence.loaded = true;
    evidence.stats = stats;
    evidence.details = {
        { "requested", true },
        { "loaded", true },
        { "source", "rawBuffer" },
        { "statusMessage", stats.statusMessage },
        { "maxSamples", maxSamples },
        { "sampleStride", sampleStride },
        { "sampledPixels", sampledPixels },
        { "visiblePixels", visiblePixelCount },
        { "rawBufferPixels", raw.rawBuffer.size() },
        { "clipThreshold", JsonFloat(clipThreshold) },
        { "linearResponseLimitSource", "metadata white level; DNG LinearResponseLimit is not parsed yet" },
        { "nearClipMargin", JsonFloat(nearClipMargin) },
        { "perChannelSampleCount", SerializeSize3(channelCounts) },
        { "perChannelClipCount", SerializeSize3(channelClipCounts) },
        { "perChannelNearClipCount", SerializeSize3(channelNearClipCounts) },
        { "cameraWbNormalizedGain", SerializeFloat3(wbNormalized) },
        { "limitations", nlohmann::json::array({
            "Uses current metadata visible rectangle; full DNG ActiveArea/MaskedAreas parsing is not yet implemented.",
            "Bayer mosaic samples provide per-CFA channel clipping; multi-channel same-pixel clipping requires later staged RGB evidence.",
            "No constants are tuned from this record until representative human-reviewed validation records exist."
        }) }
    };
    return evidence;
}

nlohmann::json SerializeRecipeSummary(const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    return {
        { "preToneExposureEv", JsonFloat(recipe.preToneExposureEv) },
        { "whiteBalanceMode", Stack::RawRecipe::WhiteBalanceModeStableString(recipe.whiteBalance.mode) },
        { "localRangeEnabled", Stack::RawRecipe::IsLocalRangeEnabled(recipe) },
        { "localRangePointCount", recipe.localRange.points.size() },
        { "finishTonePointCount", recipe.finishTone.layerJson.value("points", nlohmann::json::array()).size() },
        { "viewTransform", {
            { "exposure", JsonFloat(recipe.viewTransform.layerJson.value("exposure", 0.0f)) },
            { "blackEv", JsonFloat(recipe.viewTransform.layerJson.value("blackEv", -8.0f)) },
            { "whiteEv", JsonFloat(recipe.viewTransform.layerJson.value("whiteEv", 4.0f)) },
            { "middleGrey", JsonFloat(recipe.viewTransform.layerJson.value("middleGrey", 0.18f)) }
        } }
    };
}

nlohmann::json BuildHumanReviewTemplate() {
    return {
        { "tooDark", nullptr },
        { "tooBright", nullptr },
        { "tooLocal", nullptr },
        { "tooFlat", nullptr },
        { "tooFinished", nullptr },
        { "nextManualControl", nullptr },
        { "notes", nullptr }
    };
}

std::vector<std::string> RequiredHumanReviewFields() {
    return {
        "tooDark",
        "tooBright",
        "tooLocal",
        "tooFlat",
        "tooFinished",
        "nextManualControl",
        "notes"
    };
}

void MergeAnnotationTags(
    std::vector<std::string>& tags,
    const RawStartingPointAnnotationEntry* annotation) {
    if (annotation == nullptr || !annotation->value.is_object()) {
        return;
    }

    static constexpr const char* tagFields[] = {
        "imageCategoryTags",
        "categoryTags",
        "tags"
    };
    for (const char* field : tagFields) {
        if (!annotation->value.contains(field)) {
            continue;
        }
        for (const std::string& tag : JsonStringList(annotation->value[field])) {
            AddUniqueString(tags, tag);
        }
    }
}

void ApplyAnnotationHumanReviewField(
    nlohmann::json& humanReview,
    const nlohmann::json& annotationSource,
    const std::string& field) {
    if (annotationSource.is_object() && annotationSource.contains(field)) {
        humanReview[field] = annotationSource[field];
    }
}

void MergeAnnotationHumanReview(
    nlohmann::json& humanReview,
    const RawStartingPointAnnotationEntry* annotation) {
    if (annotation == nullptr || !annotation->value.is_object()) {
        return;
    }

    const nlohmann::json* reviewSource = &annotation->value;
    if (annotation->value.contains("humanReview") &&
        annotation->value["humanReview"].is_object()) {
        reviewSource = &annotation->value["humanReview"];
    }

    for (const std::string& field : RequiredHumanReviewFields()) {
        if (reviewSource != &annotation->value) {
            ApplyAnnotationHumanReviewField(humanReview, annotation->value, field);
        }
        ApplyAnnotationHumanReviewField(humanReview, *reviewSource, field);
    }
}

nlohmann::json SerializeAnnotationMatch(
    const std::filesystem::path& annotationPath,
    const RawStartingPointAnnotationEntry* annotation,
    const std::string& matchedKey) {
    return {
        { "requested", !annotationPath.empty() },
        { "matched", annotation != nullptr },
        { "file", JsonStringOrNull(annotationPath.string()) },
        { "primaryKey", annotation == nullptr ? nlohmann::json() : nlohmann::json(annotation->primaryKey) },
        { "matchedKey", JsonStringOrNull(matchedKey) }
    };
}

nlohmann::json SerializeAnnotationMatch(
    const RawStartingPointRecordOptions& options,
    const RawStartingPointAnnotationEntry* annotation,
    const std::string& matchedKey) {
    return SerializeAnnotationMatch(options.annotationPath, annotation, matchedKey);
}

nlohmann::json SerializeStageEvidenceMatch(
    const RawStartingPointRecordOptions& options,
    const RawStartingPointAnnotationEntry* stageEvidence,
    const std::string& matchedKey,
    bool appliedStartingPointDiagnostics) {
    nlohmann::json result =
        SerializeAnnotationMatch(options.stageEvidencePath, stageEvidence, matchedKey);
    result["appliedStartingPointDiagnostics"] = appliedStartingPointDiagnostics;
    return result;
}

std::string SourceIdentityToken(const Stack::RawWorkspace::SourceRecord& source) {
    return "raw-source-v1|" +
        NormalizeAnnotationKey(source.relativePathKey) + "|" +
        std::to_string(static_cast<unsigned long long>(source.fileSizeBytes)) + "|" +
        std::to_string(static_cast<long long>(source.modifiedTimeTicks)) + "|" +
        source.fingerprint;
}

std::string StageEvidenceSourceIdentityToken(
    const RawStartingPointAnnotationEntry* stageEvidence) {
    if (stageEvidence == nullptr) {
        return {};
    }
    return ExtractSourceIdentityToken(stageEvidence->value);
}

bool StageEvidenceSourceIdentityMatches(
    const Stack::RawWorkspace::SourceRecord& source,
    const RawStartingPointAnnotationEntry* stageEvidence) {
    const std::string stageEvidenceToken =
        StageEvidenceSourceIdentityToken(stageEvidence);
    return stageEvidence == nullptr ||
        stageEvidenceToken.empty() ||
        stageEvidenceToken == SourceIdentityToken(source);
}

nlohmann::json SerializeStageEvidenceSourceIdentityCheck(
    const Stack::RawWorkspace::SourceRecord& source,
    const RawStartingPointAnnotationEntry* stageEvidence) {
    const std::string expectedToken = SourceIdentityToken(source);
    const std::string sidecarToken =
        StageEvidenceSourceIdentityToken(stageEvidence);
    return {
        { "expectedToken", expectedToken },
        { "sidecarToken", JsonStringOrNull(sidecarToken) },
        { "sidecarTokenPresent", !sidecarToken.empty() },
        { "matched", StageEvidenceSourceIdentityMatches(source, stageEvidence) },
        { "requiredWhenPresent", true }
    };
}

std::vector<std::string> SourceAnnotationKeys(const Stack::RawWorkspace::SourceRecord& source) {
    std::vector<std::string> keys;
    AddAnnotationKey(keys, source.relativePathKey);
    AddAnnotationKey(keys, source.relativePath.generic_string());
    AddAnnotationKey(keys, source.absolutePath.string());
    AddAnnotationKey(keys, source.fileName);
    AddAnnotationKey(keys, source.stem);
    AddAnnotationKey(keys, source.fingerprint);
    AddAnnotationKey(keys, SourceIdentityToken(source));
    return keys;
}

const RawStartingPointAnnotationEntry* FindAnnotationForSource(
    const std::vector<RawStartingPointAnnotationEntry>& entries,
    const Stack::RawWorkspace::SourceRecord& source,
    int& outIndex,
    std::string& outMatchedKey) {
    outIndex = -1;
    outMatchedKey.clear();
    if (entries.empty()) {
        return nullptr;
    }

    const std::vector<std::string> sourceKeys = SourceAnnotationKeys(source);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const RawStartingPointAnnotationEntry& entry = entries[i];
        for (const std::string& sourceKey : sourceKeys) {
            if (std::find(entry.keys.begin(), entry.keys.end(), sourceKey) != entry.keys.end()) {
                outIndex = static_cast<int>(i);
                outMatchedKey = sourceKey;
                return &entry;
            }
        }
    }
    return nullptr;
}

std::vector<std::string> RecommendedValidationCategories() {
    return {
        "normal daylight",
        "high-key snow/beach/interior white room",
        "low-key night or stage",
        "backlit person/object",
        "bright sky landscape",
        "interior with bright window",
        "high ISO shadow lift",
        "mixed/artificial light",
        "intentionally warm or cool scene",
        "flat overcast / low contrast",
        "clipped specular highlights",
        "camera JPEG with strong embedded look"
    };
}

nlohmann::json BuildHumanReviewFieldGuide() {
    return nlohmann::json::array({
        {
            { "field", "tooDark" },
            { "type", "boolean" },
            { "detail", "True when the reviewed starting point feels globally too dark." }
        },
        {
            { "field", "tooBright" },
            { "type", "boolean" },
            { "detail", "True when the reviewed starting point feels globally too bright." }
        },
        {
            { "field", "tooLocal" },
            { "type", "boolean" },
            { "detail", "True when Local Range edits feel too opinionated, visible, or misplaced." }
        },
        {
            { "field", "tooFlat" },
            { "type", "boolean" },
            { "detail", "True when the reviewed starting point needs more contrast or tone shape." }
        },
        {
            { "field", "tooFinished" },
            { "type", "boolean" },
            { "detail", "True when the reviewed starting point feels like a finished edit instead of a place to begin." }
        },
        {
            { "field", "nextManualControl" },
            { "type", "string" },
            { "detail", "Name the next manual control a reviewer would naturally touch." }
        },
        {
            { "field", "notes" },
            { "type", "string" },
            { "detail", "Briefly describe the review reason or failure mode." }
        }
    });
}

nlohmann::json BuildAnnotationReadinessContract() {
    return {
        { "requiresRealRawSources", true },
        { "requiresCategoryTagsPerRecord", true },
        { "requiresEveryRecommendedCategoryAcrossSet", true },
        { "requiresCompleteHumanReviewPerRecord", true },
        { "requiredHumanReviewFields", JsonStringVector(RequiredHumanReviewFields()) },
        { "humanReviewFieldGuide", BuildHumanReviewFieldGuide() },
        { "readyWhen",
          "Every scanned source has a matching annotation entry, imageCategoryTags is non-empty, every recommended category is represented by the set, and every humanReview field is complete." },
        { "preflightCommand",
          "--check-raw-starting-point-annotations <workspace-folder> <annotations.json> --require-ready" },
        { "preflightWithRepairCommand",
          "--check-raw-starting-point-annotations <workspace-folder> <annotations.json> --require-ready --repair-out <annotations-repair.json>" },
        { "repairOutputPurpose",
          "When preflight is blocked, --repair-out writes source-keyed annotation records that can be copied back into the annotations sidecar." }
    };
}

nlohmann::json SerializeSourceIdentity(const Stack::RawWorkspace::SourceRecord& source) {
    return {
        { "id", source.relativePathKey },
        { "identityToken", SourceIdentityToken(source) },
        { "relativePath", source.relativePath.generic_string() },
        { "absolutePath", source.absolutePath.string() },
        { "fileName", source.fileName },
        { "stem", source.stem },
        { "extension", source.extension },
        { "fileSizeBytes", source.fileSizeBytes },
        { "modifiedTimeTicks", source.modifiedTimeTicks },
        { "fingerprint", source.fingerprint }
    };
}

nlohmann::json BuildValidationWorkflowChecklist(
    const std::filesystem::path& workspaceRoot,
    const std::filesystem::path& annotationTemplatePath,
    const std::filesystem::path& annotationPath,
    const std::filesystem::path& stageEvidenceTemplatePath,
    const std::filesystem::path& stageEvidencePath,
    const std::filesystem::path& recordsPath);

nlohmann::json BuildAnnotationTemplateRecord(const Stack::RawWorkspace::SourceRecord& source) {
    return {
        { "source", SerializeSourceIdentity(source) },
        { "imageCategoryTags", nlohmann::json::array() },
        { "humanReview", BuildHumanReviewTemplate() }
    };
}

nlohmann::json BuildAnnotationTemplateReport(
    const RawStartingPointRecordOptions& options,
    const std::vector<Stack::RawWorkspace::SourceRecord>& sources) {
    nlohmann::json records = nlohmann::json::object();
    for (const Stack::RawWorkspace::SourceRecord& source : sources) {
        records[source.relativePathKey] = BuildAnnotationTemplateRecord(source);
    }

    return {
        { "schema", "stack.raw-starting-point.validation-annotations-template" },
        { "version", kAnnotationTemplateSchemaVersion },
        { "workspaceRoot", options.workspaceRoot.string() },
        { "sourceCount", sources.size() },
        { "instructions",
          "Fill imageCategoryTags with one or more recommended categories and complete humanReview, then rerun the record command with --annotations pointing at this file." },
        { "annotationReadinessContract", BuildAnnotationReadinessContract() },
        { "humanReviewFieldGuide", BuildHumanReviewFieldGuide() },
        { "requiredHumanReviewFields", JsonStringVector(RequiredHumanReviewFields()) },
        { "recommendedValidationCategories", JsonStringVector(RecommendedValidationCategories()) },
        { "validationWorkflow", BuildValidationWorkflowChecklist(
            options.workspaceRoot,
            options.annotationTemplateOutputPath,
            options.annotationPath,
            options.stageEvidenceTemplateOutputPath,
            options.stageEvidencePath,
            std::filesystem::path()) },
        { "records", std::move(records) }
    };
}

void IncrementJsonCount(nlohmann::json& counts, const std::string& key) {
    if (!counts.is_object()) {
        counts = nlohmann::json::object();
    }
    const std::string safeKey = key.empty() ? "(empty)" : key;
    const int current = counts.value(safeKey, 0);
    counts[safeKey] = current + 1;
}

bool HumanReviewFieldComplete(const std::string& field, const nlohmann::json& value) {
    if (value.is_null()) {
        return false;
    }
    if (field == "tooDark" ||
        field == "tooBright" ||
        field == "tooLocal" ||
        field == "tooFlat" ||
        field == "tooFinished") {
        return value.is_boolean();
    }
    if (field == "nextManualControl" || field == "notes") {
        return value.is_string() && !value.get<std::string>().empty();
    }
    return true;
}

nlohmann::json MissingHumanReviewFields(const nlohmann::json& humanReview) {
    nlohmann::json missing = nlohmann::json::array();
    for (const std::string& field : RequiredHumanReviewFields()) {
        const nlohmann::json value =
            humanReview.is_object() && humanReview.contains(field)
                ? humanReview[field]
                : nlohmann::json();
        if (!HumanReviewFieldComplete(field, value)) {
            missing.push_back(field);
        }
    }
    return missing;
}

nlohmann::json RecommendedCategoryCoverageFromCounts(
    const nlohmann::json& categoryTagCounts,
    const std::vector<std::string>& recommendedCategories,
    nlohmann::json& outMissingRecommendedCategories,
    int& outRecommendedCategoriesPresent) {
    nlohmann::json recommendedCategoryCoverage = nlohmann::json::array();
    outMissingRecommendedCategories = nlohmann::json::array();
    outRecommendedCategoriesPresent = 0;
    for (const std::string& category : recommendedCategories) {
        const int count = categoryTagCounts.value(category, 0);
        if (count > 0) {
            ++outRecommendedCategoriesPresent;
        } else {
            outMissingRecommendedCategories.push_back(category);
        }
        recommendedCategoryCoverage.push_back({
            { "category", category },
            { "recordCount", count }
        });
    }
    return recommendedCategoryCoverage;
}

struct RequiredStageEvidenceSpec {
    const char* id;
    const char* label;
    const char* validationGap;
    const char* evidenceLayer;
    const char* sampleAfter;
    const char* sampleBefore;
    const char* purpose;
};

std::vector<RequiredStageEvidenceSpec> RequiredStageEvidenceSpecs() {
    return {
        {
            "neutral-scene",
            "Neutral Scene",
            "neutral scene stats",
            "scene appearance",
            "Demosaic, white-balance policy, and camera-to-working transform",
            "RAW Exposure, Local Range, Finish Tone, and Display Fit",
            "Measure the unstyled scene key and color baseline before user-facing edits."
        },
        {
            "raw-placement",
            "Raw Placement",
            "raw placement stats",
            "scene appearance constrained by raw safety",
            "Proposed RAW Exposure and white-balance policy",
            "Local Range, Finish Tone, and Display Fit",
            "Check whether global scene placement is sensible before local or tone stages can hide issues."
        },
        {
            "local-candidate",
            "Local Candidate",
            "local candidate stats",
            "scene appearance",
            "Proposed RAW Exposure, white balance, and Local Range",
            "Finish Tone and Display Fit",
            "Evaluate regional corrections before global tone or display mapping compresses them."
        },
        {
            "finish-tone-candidate",
            "Finish Tone Candidate",
            "finish tone candidate stats",
            "pre-display tone",
            "Proposed RAW Exposure, white balance, Local Range, and Finish Tone",
            "Display Fit",
            "Judge global tone shape before View Transform can mask weak or excessive contrast."
        },
        {
            "display-candidate",
            "Display Candidate",
            "display candidate stats",
            "final display",
            "All proposed visible controls including Display Fit",
            "None",
            "Verify final screen readability while keeping upstream stage evidence separate."
        }
    };
}

nlohmann::json RequiredStageMetricGroups(const std::string& stageId) {
    if (stageId == "display-candidate") {
        return nlohmann::json::array({
            "transferFamily",
            "display percentiles",
            "display clipping fractions",
            "display spread",
            "readability score"
        });
    }
    if (stageId == "local-candidate") {
        return nlohmann::json::array({
            "valid pixel fraction",
            "luma percentiles",
            "EV percentiles",
            "regional EV summaries",
            "local conflict score",
            "local mask halo risk"
        });
    }
    if (stageId == "finish-tone-candidate") {
        return nlohmann::json::array({
            "valid pixel fraction",
            "luma percentiles",
            "EV percentiles",
            "mid/wide spread EV",
            "endpoint pressure",
            "tone shape score"
        });
    }
    return nlohmann::json::array({
        "valid pixel fraction",
        "luma percentiles",
        "EV percentiles",
        "log-average luma",
        "shadow/highlight mass",
        "scene warnings"
    });
}

nlohmann::json BuildStageEvidenceGuide() {
    nlohmann::json stages = nlohmann::json::array();
    for (const RequiredStageEvidenceSpec& stage : RequiredStageEvidenceSpecs()) {
        stages.push_back({
            { "stage", stage.id },
            { "label", stage.label },
            { "validationGap", stage.validationGap },
            { "evidenceLayer", stage.evidenceLayer },
            { "sampleAfter", stage.sampleAfter },
            { "sampleBefore", stage.sampleBefore },
            { "purpose", stage.purpose },
            { "requiredMetricGroups", RequiredStageMetricGroups(stage.id) },
            { "requiredBeforeTuning", true }
        });
    }
    return stages;
}

nlohmann::json BuildRealRawSourceCapturePlan(
    const Stack::RawWorkspace::SourceRecord& source) {
    nlohmann::json requiredStages = nlohmann::json::array();
    for (const RequiredStageEvidenceSpec& stage : RequiredStageEvidenceSpecs()) {
        requiredStages.push_back({
            { "stage", stage.id },
            { "label", stage.label },
            { "requiredMetricGroups", RequiredStageMetricGroups(stage.id) },
            { "completeStatusRequired", "complete" }
        });
    }

    return {
        { "schema", "stack.raw-starting-point.real-raw-source-capture-plan" },
        { "version", 1 },
        { "source", SerializeSourceIdentity(source) },
        { "sidecarRecordKey", source.relativePathKey },
        { "requiresRealRawSource", true },
        { "requiresSameSourceIdentityToken", true },
        { "requiresManualDiagnosticsCapture", true },
        { "mutatesSidecarsAutomatically", false },
        { "tunesConstants", false },
        { "requiredStageCount", requiredStages.size() },
        { "requiredStages", std::move(requiredStages) },
        { "captureSteps", nlohmann::json::array({
            "Open this RAW source in the RAW tab.",
            "Run Analyze if diagnostics are stale, then inspect Build Starting Point diagnostics.",
            "Capture the versioned startingPointDiagnostics payload for this exact source identity token.",
            "Copy the diagnostics into the matching stage-evidence sidecar record.",
            "Run the stage-evidence preflight before merging validation records."
        }) },
        { "preflightCommands", nlohmann::json::array({
            "--check-raw-starting-point-stage-evidence <workspace-folder> <stage-evidence.json> --out <stage-evidence-check.json>",
            "--validate-raw-starting-point-records <workspace-folder> --load-raw-safety --annotations <annotations.json> --stage-evidence <stage-evidence.json> --require-ready-sidecars"
        }) },
        { "readyWhen",
          "The sidecar record has this source identity token, versioned startingPointDiagnostics, candidate diagnostics, a selected candidate, and complete required named stage diagnostics." }
    };
}

nlohmann::json BuildStageEvidenceCaptureContract() {
    return {
        { "requiresRealRawSources", true },
        { "requiresCandidateDiagnostics", true },
        { "requiresSelectedCandidate", true },
        { "requiresEveryRequiredStage", true },
        { "requiredStartingPointDiagnosticsFields", {
            "version",
            "hasSelectedCandidate",
            "selectedCandidateId",
            "candidates",
            "stageDiagnostics"
        } },
        { "requiredStages", BuildStageEvidenceGuide() },
        { "readyWhen",
          "Every scanned source has versioned startingPointDiagnostics, non-empty candidates, selected-candidate evidence, and complete diagnostics for every required named stage." },
        { "preflightCommand",
          "--check-raw-starting-point-stage-evidence <workspace-folder> <stage-evidence.json>" },
        { "preflightWithRepairCommand",
          "--check-raw-starting-point-stage-evidence <workspace-folder> <stage-evidence.json> --repair-out <stage-evidence-repair.json>" },
        { "repairOutputPurpose",
          "When preflight is blocked, --repair-out writes source-keyed stage-evidence records that can be filled with captured Starting Point diagnostics." }
    };
}

nlohmann::json BuildStageEvidenceTemplateRecord(
    const Stack::RawWorkspace::SourceRecord& source) {
    nlohmann::json requiredStageDiagnostics = nlohmann::json::array();
    for (const RequiredStageEvidenceSpec& stage : RequiredStageEvidenceSpecs()) {
        requiredStageDiagnostics.push_back({
            { "stage", stage.id },
            { "label", stage.label },
            { "status", "missing" },
            { "evidenceLayer", stage.evidenceLayer },
            { "sampleAfter", stage.sampleAfter },
            { "sampleBefore", stage.sampleBefore },
            { "purpose", stage.purpose },
            { "requiredMetricGroups", RequiredStageMetricGroups(stage.id) },
            { "stats", nlohmann::json::object() }
        });
    }

    return {
        { "source", SerializeSourceIdentity(source) },
        { "realRawCapturePlan", BuildRealRawSourceCapturePlan(source) },
        { "startingPointDiagnostics", {
            { "captureRequired", true },
            { "instructions",
              "Replace this placeholder with the versioned Starting Point diagnostics captured for this source before running --check-raw-starting-point-stage-evidence." },
            { "expectedFields", {
                "version",
                "hasSelectedCandidate",
                "selectedCandidateId",
                "candidates",
                "stageDiagnostics"
            } },
            { "requiredCandidateEvidence", {
                { "hasCandidateDiagnostics", true },
                { "hasSelectedCandidate", true }
            } },
            { "captureContract", BuildStageEvidenceCaptureContract() },
            { "requiredStageDiagnostics", std::move(requiredStageDiagnostics) }
        } }
    };
}

nlohmann::json BuildStageEvidenceTemplateReport(
    const RawStartingPointRecordOptions& options,
    const std::vector<Stack::RawWorkspace::SourceRecord>& sources) {
    nlohmann::json records = nlohmann::json::object();
    for (const Stack::RawWorkspace::SourceRecord& source : sources) {
        records[source.relativePathKey] = BuildStageEvidenceTemplateRecord(source);
    }

    nlohmann::json requiredStages = nlohmann::json::array();
    for (const RequiredStageEvidenceSpec& stage : RequiredStageEvidenceSpecs()) {
        requiredStages.push_back({
            { "stage", stage.id },
            { "label", stage.label },
            { "validationGap", stage.validationGap },
            { "evidenceLayer", stage.evidenceLayer },
            { "sampleAfter", stage.sampleAfter },
            { "sampleBefore", stage.sampleBefore },
            { "purpose", stage.purpose },
            { "requiredMetricGroups", RequiredStageMetricGroups(stage.id) }
        });
    }

    return {
        { "schema", "stack.raw-starting-point.validation-stage-evidence-template" },
        { "version", kStageEvidenceTemplateSchemaVersion },
        { "workspaceRoot", options.workspaceRoot.string() },
        { "sourceCount", sources.size() },
        { "instructions",
          "Replace each startingPointDiagnostics placeholder with captured versioned diagnostics, then run --check-raw-starting-point-stage-evidence before merging with --stage-evidence." },
        { "diagnosticCaptureContract", BuildStageEvidenceCaptureContract() },
        { "requiredStages", std::move(requiredStages) },
        { "validationWorkflow", BuildValidationWorkflowChecklist(
            options.workspaceRoot,
            options.annotationTemplateOutputPath,
            options.annotationPath,
            options.stageEvidenceTemplateOutputPath,
            options.stageEvidencePath,
            std::filesystem::path()) },
        { "records", std::move(records) }
    };
}

nlohmann::json BuildTemplateGenerationReport(
    const RawStartingPointRecordOptions& options,
    const std::vector<Stack::RawWorkspace::SourceRecord>& sources) {
    return {
        { "schema", "stack.raw-starting-point.validation-template-generation" },
        { "version", kTemplateGenerationSchemaVersion },
        { "workspaceRoot", options.workspaceRoot.string() },
        { "sourceCount", sources.size() },
        { "templatesOnly", true },
        { "recordsGenerated", false },
        { "metadataLoaded", false },
        { "rawSafetyLoaded", false },
        { "behaviorChanged", false },
        { "annotationTemplateSchemaVersion", kAnnotationTemplateSchemaVersion },
        { "annotationTemplateFile", options.annotationTemplateOutputPath.string() },
        { "annotationTemplateRecordCount",
          options.annotationTemplateOutputPath.empty() ? 0 : sources.size() },
        { "stageEvidenceTemplateSchemaVersion", kStageEvidenceTemplateSchemaVersion },
        { "stageEvidenceTemplateFile", options.stageEvidenceTemplateOutputPath.string() },
        { "stageEvidenceTemplateRecordCount",
          options.stageEvidenceTemplateOutputPath.empty() ? 0 : sources.size() },
        { "validationWorkflowSchemaVersion", kValidationWorkflowSchemaVersion },
        { "nextAction",
          "Fill generated annotation and stage-evidence templates, preflight both sidecars with repair outputs when blocked, then run validation records with real RAW sources and --load-raw-safety." },
        { "validationWorkflow", BuildValidationWorkflowChecklist(
            options.workspaceRoot,
            options.annotationTemplateOutputPath,
            options.annotationPath,
            options.stageEvidenceTemplateOutputPath,
            options.stageEvidencePath,
            std::filesystem::path()) }
    };
}

bool StageArrayHasCompleteStage(const nlohmann::json& stages, const std::string& stageId) {
    if (!stages.is_array()) {
        return false;
    }
    for (const nlohmann::json& stage : stages) {
        if (!stage.is_object()) {
            continue;
        }
        if (stage.value("stage", "") == stageId &&
            stage.value("status", "") == "complete") {
            return true;
        }
    }
    return false;
}

std::string NormalizeStageStatusForAccounting(std::string status) {
    if (status == "complete" ||
        status == "projected" ||
        status == "pending" ||
        status == "fallback" ||
        status == "unavailable" ||
        status == "missing") {
        return status;
    }
    return status.empty() ? std::string("missing") : std::string("unknown");
}

std::string StageArrayStatusForAccounting(
    const nlohmann::json& stages,
    const std::string& stageId) {
    if (!stages.is_array()) {
        return "missing";
    }
    for (const nlohmann::json& stage : stages) {
        if (!stage.is_object()) {
            continue;
        }
        if (stage.value("stage", std::string()) == stageId) {
            return NormalizeStageStatusForAccounting(
                stage.value("status", std::string()));
        }
    }
    return "missing";
}

const nlohmann::json* FindCandidateByKind(
    const nlohmann::json& candidates,
    const std::string& kind) {
    if (!candidates.is_array()) {
        return nullptr;
    }
    for (const nlohmann::json& candidate : candidates) {
        if (candidate.is_object() &&
            candidate.value("kind", std::string()) == kind) {
            return &candidate;
        }
    }
    return nullptr;
}

nlohmann::json FindSelectedCandidateValue(
    const nlohmann::json& diagnostics) {
    if (!diagnostics.is_object()) {
        return nlohmann::json::object();
    }
    const nlohmann::json candidates =
        diagnostics.value("candidates", nlohmann::json::array());
    if (!candidates.is_array()) {
        return nlohmann::json::object();
    }

    const int selectedIndex =
        diagnostics.value("selectedCandidateIndex", -1);
    if (selectedIndex >= 0 &&
        selectedIndex < static_cast<int>(candidates.size()) &&
        candidates[static_cast<std::size_t>(selectedIndex)].is_object()) {
        return candidates[static_cast<std::size_t>(selectedIndex)];
    }

    const std::string selectedKind =
        diagnostics.value("selectedCandidateKind", std::string());
    if (!selectedKind.empty()) {
        const nlohmann::json* candidate = FindCandidateByKind(candidates, selectedKind);
        return candidate == nullptr ? nlohmann::json::object() : *candidate;
    }
    return nlohmann::json::object();
}

std::string VisibleRecipeFieldForControlId(const std::string& controlId) {
    if (controlId == "raw-exposure") {
        return "preToneExposureEv";
    }
    if (controlId == "white-balance") {
        return "whiteBalance";
    }
    if (controlId == "local-range") {
        return "localRange";
    }
    if (controlId == "finish-tone") {
        return "finishTone.layerJson";
    }
    if (controlId == "display-fit") {
        return "viewTransform.layerJson";
    }
    return {};
}

std::string VisibleRecipeEditSectionForControlId(const std::string& controlId) {
    if (controlId == "raw-exposure") {
        return "rawExposure";
    }
    if (controlId == "white-balance") {
        return "whiteBalance";
    }
    if (controlId == "local-range") {
        return "localRange";
    }
    if (controlId == "finish-tone") {
        return "finishTone";
    }
    if (controlId == "display-fit") {
        return "displayFit";
    }
    return {};
}

nlohmann::json BuildVisibleRecipeProposedValue(
    const std::string& controlId,
    const nlohmann::json& visibleEdits) {
    const std::string section = VisibleRecipeEditSectionForControlId(controlId);
    nlohmann::json proposedValue = {
        { "available", false },
        { "sourceSection", JsonStringOrNull(section) }
    };
    if (section.empty()) {
        proposedValue["unavailableReason"] = "unknown visible control id";
        return proposedValue;
    }
    const nlohmann::json value =
        visibleEdits.value(section, nlohmann::json::object());
    if (!value.is_object()) {
        proposedValue["unavailableReason"] =
            "selected candidate visibleEdits section missing";
        return proposedValue;
    }

    proposedValue = value;
    proposedValue["available"] = true;
    proposedValue["sourceSection"] = section;
    return proposedValue;
}

nlohmann::json BuildVisibleRecipeWriteAudit(
    const nlohmann::json& diagnostics) {
    nlohmann::json audit = {
        { "schema", "stack.raw-starting-point.visible-recipe-write-audit" },
        { "version", 2 },
        { "available", false },
        { "source", "startingPointDiagnostics.selectedCandidate.visibleEdits" },
        { "validationCommandMutatesRecipe", false },
        { "hiddenOutputPass", false },
        { "writesVisibleManualControlsOnly", false },
        { "selectedCandidateKind", nullptr },
        { "selectedCandidateId", nullptr },
        { "selectedCandidateSummary", nullptr },
        { "controlCount", 0 },
        { "controlsWithProposedValues", 0 },
        { "controls", nlohmann::json::array() },
        { "recipeFields", nlohmann::json::array() },
        { "missingProposedValueControls", nlohmann::json::array() }
    };
    if (!diagnostics.is_object()) {
        audit["unavailableReason"] = "startingPointDiagnostics missing or invalid";
        return audit;
    }

    const nlohmann::json selectedCandidate =
        FindSelectedCandidateValue(diagnostics);
    if (!selectedCandidate.is_object() || selectedCandidate.empty()) {
        audit["unavailableReason"] = "selected candidate missing";
        return audit;
    }

    const nlohmann::json visibleEdits =
        selectedCandidate.value("visibleEdits", nlohmann::json::object());
    if (!visibleEdits.is_object()) {
        audit["unavailableReason"] = "selected candidate visibleEdits missing";
        return audit;
    }

    audit["available"] = true;
    audit["selectedCandidateKind"] =
        JsonStringOrNull(selectedCandidate.value("kind", std::string()));
    audit["selectedCandidateId"] =
        JsonStringOrNull(selectedCandidate.value("id", std::string()));
    audit["selectedCandidateSummary"] =
        JsonStringOrNull(selectedCandidate.value("summary", std::string()));

    nlohmann::json controls = nlohmann::json::array();
    nlohmann::json missingProposedValueControls = nlohmann::json::array();
    std::vector<std::string> recipeFields;
    int controlsWithProposedValues = 0;
    const nlohmann::json touchedControls =
        visibleEdits.value("touchedControls", nlohmann::json::array());
    if (touchedControls.is_array()) {
        for (const nlohmann::json& control : touchedControls) {
            if (!control.is_object()) {
                continue;
            }
            const std::string controlId = control.value("id", std::string());
            const std::string recipeField =
                VisibleRecipeFieldForControlId(controlId);
            if (controlId.empty() || recipeField.empty()) {
                continue;
            }
            AddUniqueString(recipeFields, recipeField);
            nlohmann::json proposedValue =
                BuildVisibleRecipeProposedValue(controlId, visibleEdits);
            const bool hasProposedValue =
                proposedValue.value("available", false) &&
                proposedValue.value("valid", true);
            if (hasProposedValue) {
                ++controlsWithProposedValues;
            } else {
                missingProposedValueControls.push_back(controlId);
            }
            controls.push_back({
                { "id", controlId },
                { "label", JsonStringOrNull(control.value("label", std::string())) },
                { "recipeField", recipeField },
                { "visibleManualControl", true },
                { "hasProposedValue", hasProposedValue },
                { "proposedValue", std::move(proposedValue) }
            });
        }
    }

    audit["controlCount"] = controls.size();
    audit["controlsWithProposedValues"] = controlsWithProposedValues;
    audit["controls"] = std::move(controls);
    audit["recipeFields"] = JsonStringVector(recipeFields);
    audit["missingProposedValueControls"] =
        std::move(missingProposedValueControls);
    audit["writesVisibleManualControlsOnly"] =
        !audit["controls"].empty() &&
        !audit.value("hiddenOutputPass", true);
    audit["reviewNote"] = audit.value("writesVisibleManualControlsOnly", false)
        ? "Selected candidate proposes editable recipe fields; the validation command records them without applying the recipe."
        : "Selected candidate did not expose touched visible controls in diagnostics.";
    return audit;
}

std::string CandidateStageStatusForAccounting(
    const nlohmann::json* candidate,
    const std::string& stageId) {
    if (candidate == nullptr || !candidate->is_object()) {
        return "missing";
    }
    return StageArrayStatusForAccounting(
        candidate->value("stageDiagnostics", nlohmann::json::array()),
        stageId);
}

bool DiagnosticsHasCompleteStageDiagnostics(
    const nlohmann::json& diagnostics,
    const std::string& stageId) {
    if (!diagnostics.is_object()) {
        return false;
    }
    if (StageArrayHasCompleteStage(
            diagnostics.value("stageDiagnostics", nlohmann::json::array()),
            stageId)) {
        return true;
    }
    const nlohmann::json candidates =
        diagnostics.value("candidates", nlohmann::json::array());
    if (!candidates.is_array()) {
        return false;
    }
    for (const nlohmann::json& candidate : candidates) {
        if (!candidate.is_object()) {
            continue;
        }
        if (StageArrayHasCompleteStage(
                candidate.value("stageDiagnostics", nlohmann::json::array()),
                stageId)) {
            return true;
        }
    }
    return false;
}

bool DiagnosticsHasCandidateDiagnostics(const nlohmann::json& diagnostics) {
    if (!diagnostics.is_object() || diagnostics.value("version", 0) <= 0) {
        return false;
    }
    const nlohmann::json candidates =
        diagnostics.value("candidates", nlohmann::json::array());
    return candidates.is_array() && !candidates.empty();
}

bool DiagnosticsHasSelectedCandidate(const nlohmann::json& diagnostics) {
    return diagnostics.is_object() &&
        diagnostics.value("version", 0) > 0 &&
        diagnostics.value("hasSelectedCandidate", false);
}

std::vector<std::string> RequiredActionReadinessDiagnosticLabels() {
    return {
        "Build Starting Point action",
        "Add Local Range action",
        "Add Mild Tone action"
    };
}

std::vector<ActionReadinessDetailGuardSpec>
RequiredActionReadinessDetailGuards() {
    return {
        {
            "Build Starting Point action",
            {
                "safe visible controls including RAW Exposure and Display Fit"
            }
        },
        {
            "Add Local Range action",
            {
                "Explicit action writes visible Local Range points only",
                "Add Local Range should remain unavailable until a visible Local Range candidate exists"
            }
        },
        {
            "Add Mild Tone action",
            {
                "Explicit action writes visible Finish Tone graph points only",
                "Add Mild Tone should remain unavailable until a visible Finish Tone candidate exists"
            }
        }
    };
}

bool DetailContainsAnyFragment(
    const std::string& detail,
    const std::vector<std::string>& acceptedFragments) {
    for (const std::string& fragment : acceptedFragments) {
        if (!fragment.empty() && detail.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> ExpectedStartingPointCandidateKinds() {
    return {
        "current-fit",
        "base",
        "balanced"
    };
}

std::string StartingPointCandidateKindLabel(const std::string& kind) {
    if (kind == "current-fit") {
        return "Current Fit";
    }
    if (kind == "balanced") {
        return "Balanced";
    }
    if (kind == "farther") {
        return "Farther";
    }
    return "Base";
}

std::string CandidateStageDiagnosticCoverageKey(
    const std::string& kind,
    const std::string& stageId) {
    return kind + "/" + stageId;
}

std::string CandidateStageDiagnosticCoverageLabel(
    const std::string& kind,
    const std::string& stageLabel) {
    return StartingPointCandidateKindLabel(kind) + " " + stageLabel;
}

std::vector<ExpectedStartingPointCandidateUiLineSpec>
ExpectedStartingPointCandidateVisibleControlLines() {
    return {
        { "current-fit", "CurrentFit visible controls" },
        { "base", "Base visible controls" },
        { "balanced", "Balanced Local/Tone visible controls" }
    };
}

std::vector<TrackedStartingPointCandidateControlValueUiLineSpec>
TrackedStartingPointCandidateControlValueLines() {
    return {
        { "current-fit-raw-exposure", "current-fit", "RAW Exposure", "CurrentFit RAW Exposure", "RawDevelopmentRecipe::preToneExposureEv" },
        { "current-fit-display-fit", "current-fit", "Display Fit", "CurrentFit Display Fit", "Not applied." },
        { "current-fit-local-range", "current-fit", "Local Range", "CurrentFit Local Range", "Dry-run report does not apply recipe values." },
        { "current-fit-finish-tone", "current-fit", "Finish Tone", "CurrentFit Finish Tone", "Dry-run report does not apply recipe values." },
        { "base-raw-exposure", "base", "RAW Exposure", "Base RAW Exposure", "RawDevelopmentRecipe::preToneExposureEv" },
        { "base-display-fit", "base", "Display Fit", "Base Display Fit", "Not applied." },
        { "base-local-range", "base", "Local Range", "Base Local Range", "Dry-run report does not apply recipe values." },
        { "base-finish-tone", "base", "Finish Tone", "Base Finish Tone", "Dry-run report does not apply recipe values." },
        { "balanced-raw-exposure", "balanced", "RAW Exposure", "Balanced Local/Tone RAW Exposure", "RawDevelopmentRecipe::preToneExposureEv" },
        { "balanced-local-range", "balanced", "Local Range", "Balanced Local/Tone Local Range", "Dry-run report does not apply recipe values." },
        { "balanced-finish-tone", "balanced", "Finish Tone", "Balanced Local/Tone Finish Tone", "Dry-run report does not apply recipe values." }
    };
}

std::vector<ExpectedStartingPointCandidateUiLineSpec>
ExpectedStartingPointCandidateScoreComponentLines() {
    return {
        { "current-fit", "CurrentFit score components" },
        { "base", "Base score components" },
        { "balanced", "Balanced Local/Tone score components" }
    };
}

std::vector<ExpectedStartingPointCandidateUiLineSpec>
ExpectedStartingPointCandidateWarningLines() {
    return {
        { "current-fit", "CurrentFit warnings" },
        { "base", "Base warnings" },
        { "balanced", "Balanced Local/Tone warnings" }
    };
}

bool CandidateHasValidTotalScore(const nlohmann::json& candidate, double* totalScore) {
    if (totalScore != nullptr) {
        *totalScore = 0.0;
    }
    if (!candidate.is_object()) {
        return false;
    }
    const nlohmann::json score = candidate.value("score", nlohmann::json::object());
    if (!score.is_object() || !score.value("valid", false)) {
        return false;
    }
    if (!score.contains("totalScore") || !score["totalScore"].is_number()) {
        return false;
    }
    const double value = score["totalScore"].get<double>();
    if (!std::isfinite(value)) {
        return false;
    }
    if (totalScore != nullptr) {
        *totalScore = value;
    }
    return true;
}

std::string DiagnosticsUiViewLineValue(
    const nlohmann::json& diagnostics,
    const std::string& label,
    bool* found) {
    if (found != nullptr) {
        *found = false;
    }
    if (!diagnostics.is_object()) {
        return std::string();
    }
    const nlohmann::json uiView =
        diagnostics.value("uiView", nlohmann::json::object());
    if (!uiView.is_object()) {
        return std::string();
    }
    const nlohmann::json lines =
        uiView.value("lines", nlohmann::json::array());
    if (!lines.is_array()) {
        return std::string();
    }
    for (const nlohmann::json& line : lines) {
        if (line.is_object() && line.value("label", std::string()) == label) {
            if (found != nullptr) {
                *found = true;
            }
            const nlohmann::json value =
                line.value("value", nlohmann::json());
            if (value.is_string()) {
                return value.get<std::string>();
            }
            if (!value.is_null()) {
                return value.dump();
            }
            return std::string();
        }
    }
    return std::string();
}

std::string DiagnosticsUiViewLineDetail(
    const nlohmann::json& diagnostics,
    const std::string& label,
    bool* found) {
    if (found != nullptr) {
        *found = false;
    }
    if (!diagnostics.is_object()) {
        return std::string();
    }
    const nlohmann::json uiView =
        diagnostics.value("uiView", nlohmann::json::object());
    if (!uiView.is_object()) {
        return std::string();
    }
    const nlohmann::json lines =
        uiView.value("lines", nlohmann::json::array());
    if (!lines.is_array()) {
        return std::string();
    }
    for (const nlohmann::json& line : lines) {
        if (line.is_object() && line.value("label", std::string()) == label) {
            if (found != nullptr) {
                *found = true;
            }
            const nlohmann::json detail =
                line.value("detail", nlohmann::json());
            if (detail.is_string()) {
                return detail.get<std::string>();
            }
            if (!detail.is_null()) {
                return detail.dump();
            }
            return std::string();
        }
    }
    return std::string();
}

int CountDiagnosticsUiViewLines(
    const nlohmann::json& diagnostics,
    const std::string& label,
    nlohmann::json* valueCounts) {
    if (!diagnostics.is_object()) {
        return 0;
    }
    const nlohmann::json uiView =
        diagnostics.value("uiView", nlohmann::json::object());
    if (!uiView.is_object()) {
        return 0;
    }
    const nlohmann::json lines =
        uiView.value("lines", nlohmann::json::array());
    if (!lines.is_array()) {
        return 0;
    }
    int count = 0;
    for (const nlohmann::json& line : lines) {
        if (!line.is_object() || line.value("label", std::string()) != label) {
            continue;
        }
        ++count;
        if (valueCounts == nullptr) {
            continue;
        }
        const nlohmann::json value = line.value("value", nlohmann::json());
        if (value.is_string()) {
            const std::string text = value.get<std::string>();
            IncrementJsonCount(*valueCounts, text.empty() ? "(empty)" : text);
        } else if (!value.is_null()) {
            const std::string text = value.dump();
            IncrementJsonCount(*valueCounts, text.empty() ? "(empty)" : text);
        } else {
            IncrementJsonCount(*valueCounts, "(empty)");
        }
    }
    return count;
}

int DiagnosticsWarningCount(const nlohmann::json& diagnostics) {
    if (!diagnostics.is_object()) {
        return 0;
    }
    const nlohmann::json warnings =
        diagnostics.value("warnings", nlohmann::json::array());
    if (!warnings.is_array()) {
        return 0;
    }
    int count = 0;
    for (const nlohmann::json& warning : warnings) {
        if (warning.is_string() && !warning.get<std::string>().empty()) {
            ++count;
        }
    }
    return count;
}

std::string NormalizedDiagnosticsLineValue(const std::string& value) {
    if (value.empty()) {
        return "(empty)";
    }
    return value;
}

std::string NormalizedActionReadinessValue(const std::string& value) {
    return NormalizedDiagnosticsLineValue(value);
}

bool RecordHasCompleteStageDiagnostics(
    const nlohmann::json& record,
    const std::string& stageId) {
    if (!record.is_object()) {
        return false;
    }
    const nlohmann::json diagnostics =
        record.value("startingPointDiagnostics", nlohmann::json::object());
    return DiagnosticsHasCompleteStageDiagnostics(diagnostics, stageId);
}

nlohmann::json ExtractStageEvidenceDiagnostics(
    const RawStartingPointAnnotationEntry* stageEvidence) {
    if (stageEvidence == nullptr || !stageEvidence->value.is_object()) {
        return nlohmann::json::object();
    }

    static constexpr const char* wrapperFields[] = {
        "startingPointDiagnostics",
        "diagnostics"
    };
    for (const char* field : wrapperFields) {
        if (stageEvidence->value.contains(field) &&
            stageEvidence->value[field].is_object()) {
            return stageEvidence->value[field];
        }
    }

    if (stageEvidence->value.contains("stageDiagnostics") ||
        stageEvidence->value.contains("candidates") ||
        stageEvidence->value.contains("hasSelectedCandidate") ||
        stageEvidence->value.contains("selectedCandidateId") ||
        (stageEvidence->value.contains("version") &&
            stageEvidence->value["version"].is_number_integer() &&
            stageEvidence->value["version"].get<int>() > 0)) {
        return stageEvidence->value;
    }

    return nlohmann::json::object();
}

void RemoveSatisfiedStageValidationGaps(nlohmann::json& record) {
    if (!record.is_object() ||
        !record.contains("validationGaps") ||
        !record["validationGaps"].is_array()) {
        return;
    }

    nlohmann::json keptGaps = nlohmann::json::array();
    for (const nlohmann::json& gap : record["validationGaps"]) {
        if (!gap.is_string()) {
            keptGaps.push_back(gap);
            continue;
        }

        const std::string gapText = gap.get<std::string>();
        bool satisfiedStageGap = false;
        for (const RequiredStageEvidenceSpec& stage : RequiredStageEvidenceSpecs()) {
            if (gapText == stage.validationGap &&
                RecordHasCompleteStageDiagnostics(record, stage.id)) {
                satisfiedStageGap = true;
                break;
            }
        }
        if (!satisfiedStageGap) {
            keptGaps.push_back(gap);
        }
    }
    record["validationGaps"] = std::move(keptGaps);
}

bool MergeStageEvidenceDiagnostics(
    nlohmann::json& record,
    const RawStartingPointAnnotationEntry* stageEvidence) {
    nlohmann::json diagnostics = ExtractStageEvidenceDiagnostics(stageEvidence);
    if (!diagnostics.is_object() || diagnostics.empty()) {
        return false;
    }

    record["startingPointDiagnostics"] = std::move(diagnostics);
    RemoveSatisfiedStageValidationGaps(record);
    return true;
}

nlohmann::json BuildEvidenceChecklistItem(
    const std::string& id,
    const std::string& label,
    bool complete,
    int completeRecordCount,
    int expectedRecordCount,
    bool requiredBeforeTuning,
    const std::string& detail) {
    return {
        { "id", id },
        { "label", label },
        { "complete", complete },
        { "completeRecordCount", completeRecordCount },
        { "expectedRecordCount", expectedRecordCount },
        { "missingRecordCount", std::max(0, expectedRecordCount - completeRecordCount) },
        { "requiredBeforeTuning", requiredBeforeTuning },
        { "detail", detail }
    };
}

struct TuningConstantEvidenceSpec {
    std::string id;
    std::string group;
    std::string path;
    std::string label;
    nlohmann::json currentEngineeringDefault;
    std::string ownerControl;
    std::string evidenceFocus;
    std::vector<std::string> requiredEvidenceItems;
    std::vector<std::string> requiredCategories;
    std::vector<std::string> requiredStages;
    std::vector<std::string> humanReviewSignals;
    std::string tuningQuestion;
};

std::vector<TuningConstantEvidenceSpec> TuningConstantEvidenceSpecs() {
    return {
        {
            "raw-exposure-target-median-relative-to-white",
            "rawExposure",
            "rawExposure.targetMedianRelativeToWhiteEv",
            "RAW Exposure target key",
            -2.70,
            "RAW Exposure",
            "Scene key placement constrained by raw highlight safety.",
            {
                "metadata",
                "raw-buffer-safety",
                "representative-categories",
                "human-review",
                "candidate-diagnostics",
                "stage-diagnostics"
            },
            {
                "normal daylight",
                "high-key snow/beach/interior white room",
                "low-key night or stage",
                "backlit person/object",
                "bright sky landscape"
            },
            {
                "neutral-scene",
                "raw-placement",
                "display-candidate"
            },
            {
                "tooDark",
                "tooBright",
                "nextManualControl"
            },
            "Do reviewed Base candidates land the scene key close enough without forcing high-key or low-key images toward a normal histogram?"
        },
        {
            "raw-exposure-delta-clamp-min",
            "rawExposure",
            "rawExposure.deltaClampMinEv",
            "RAW Exposure negative clamp",
            -0.50,
            "RAW Exposure",
            "Maximum automatic darkening from the global scene placement control.",
            {
                "metadata",
                "raw-buffer-safety",
                "representative-categories",
                "human-review",
                "candidate-diagnostics",
                "stage-diagnostics"
            },
            {
                "high-key snow/beach/interior white room",
                "clipped specular highlights",
                "camera JPEG with strong embedded look"
            },
            {
                "neutral-scene",
                "raw-placement",
                "display-candidate"
            },
            {
                "tooDark",
                "tooFinished",
                "nextManualControl"
            },
            "Do automatic negative moves protect highlights without making bright-intent files feel under-placed or over-edited?"
        },
        {
            "raw-exposure-delta-clamp-max",
            "rawExposure",
            "rawExposure.deltaClampMaxEv",
            "RAW Exposure positive clamp",
            1.00,
            "RAW Exposure",
            "Maximum automatic brightening allowed before Local Range or Display Fit should carry the rest.",
            {
                "metadata",
                "raw-buffer-safety",
                "representative-categories",
                "human-review",
                "candidate-diagnostics",
                "stage-diagnostics"
            },
            {
                "low-key night or stage",
                "backlit person/object",
                "interior with bright window",
                "high ISO shadow lift",
                "clipped specular highlights"
            },
            {
                "raw-placement",
                "local-candidate",
                "display-candidate"
            },
            {
                "tooBright",
                "tooLocal",
                "nextManualControl"
            },
            "Do positive RAW Exposure moves stop before highlight/headroom or high-ISO shadow-lift risk should move the problem downstream?"
        },
        {
            "raw-exposure-auto-apply-confidence-min",
            "rawExposure",
            "rawExposure.autoApplyConfidenceMin",
            "RAW Exposure auto-apply confidence",
            0.85,
            "RAW Exposure",
            "Minimum confidence before a visible RAW Exposure suggestion is safe to apply.",
            {
                "metadata",
                "raw-buffer-safety",
                "representative-categories",
                "human-review",
                "candidate-diagnostics",
                "stage-diagnostics",
                "visible-recipe-writes"
            },
            {
                "mixed/artificial light",
                "intentionally warm or cool scene",
                "camera JPEG with strong embedded look",
                "normal daylight"
            },
            {
                "neutral-scene",
                "raw-placement",
                "display-candidate"
            },
            {
                "tooDark",
                "tooBright",
                "tooFinished",
                "nextManualControl"
            },
            "Does the confidence gate prevent applying global placement when color, scene key, or intent evidence is ambiguous?"
        },
        {
            "raw-exposure-auto-apply-max-abs-delta",
            "rawExposure",
            "rawExposure.autoApplyMaxAbsDeltaEv",
            "RAW Exposure auto-apply delta cap",
            0.50,
            "RAW Exposure",
            "Largest one-click RAW Exposure movement allowed while still feeling like a starting point.",
            {
                "metadata",
                "raw-buffer-safety",
                "representative-categories",
                "human-review",
                "candidate-diagnostics",
                "stage-diagnostics",
                "visible-recipe-writes"
            },
            {
                "normal daylight",
                "low-key night or stage",
                "high-key snow/beach/interior white room",
                "interior with bright window"
            },
            {
                "raw-placement",
                "display-candidate"
            },
            {
                "tooDark",
                "tooBright",
                "tooFinished",
                "nextManualControl"
            },
            "Do applied global exposure moves stay modest enough that reviewers still regard the result as a beginning, not a finished edit?"
        },
        {
            "balanced-local-min-confidence",
            "balancedLocal",
            "balancedLocal.minConfidence",
            "Balanced Local Range confidence",
            0.70,
            "Local Range",
            "Minimum evidence before Balanced authors visible Local Range graph points.",
            {
                "metadata",
                "raw-buffer-safety",
                "representative-categories",
                "human-review",
                "candidate-diagnostics",
                "stage-diagnostics",
                "visible-recipe-writes"
            },
            {
                "backlit person/object",
                "bright sky landscape",
                "interior with bright window",
                "high ISO shadow lift",
                "flat overcast / low contrast"
            },
            {
                "raw-placement",
                "local-candidate",
                "display-candidate"
            },
            {
                "tooLocal",
                "tooFinished",
                "nextManualControl"
            },
            "Do Local Range edits appear only when a real regional conflict remains after global placement?"
        },
        {
            "balanced-local-max-abs-delta",
            "balancedLocal",
            "balancedLocal.maxAbsDeltaEv",
            "Balanced Local Range delta cap",
            1.00,
            "Local Range",
            "Maximum EV lift or hold for automatic Balanced Local Range points.",
            {
                "metadata",
                "raw-buffer-safety",
                "representative-categories",
                "human-review",
                "candidate-diagnostics",
                "stage-diagnostics",
                "visible-recipe-writes"
            },
            {
                "backlit person/object",
                "bright sky landscape",
                "interior with bright window",
                "high ISO shadow lift",
                "clipped specular highlights"
            },
            {
                "raw-placement",
                "local-candidate",
                "display-candidate"
            },
            {
                "tooLocal",
                "tooBright",
                "tooFinished",
                "nextManualControl"
            },
            "Do local lifts and holds remain visible, conservative, and noise/headroom-safe across representative conflict scenes?"
        },
        {
            "balanced-local-max-adjustment-points",
            "balancedLocal",
            "balancedLocal.maxAdjustmentPoints",
            "Balanced Local Range point budget",
            2,
            "Local Range",
            "Maximum number of automatic Local Range adjustment points.",
            {
                "representative-categories",
                "human-review",
                "candidate-diagnostics",
                "stage-diagnostics",
                "visible-recipe-writes"
            },
            {
                "backlit person/object",
                "bright sky landscape",
                "interior with bright window",
                "flat overcast / low contrast"
            },
            {
                "local-candidate",
                "display-candidate"
            },
            {
                "tooLocal",
                "tooFinished",
                "nextManualControl"
            },
            "Does the point budget solve one or two clear conflicts without making the Local Range graph feel pre-edited?"
        },
        {
            "balanced-local-max-color-targeted-points",
            "balancedLocal",
            "balancedLocal.maxColorTargetedPoints",
            "Balanced color-targeted point budget",
            1,
            "Local Range",
            "Maximum number of automatic color-qualified Local Range points.",
            {
                "metadata",
                "representative-categories",
                "human-review",
                "candidate-diagnostics",
                "stage-diagnostics",
                "visible-recipe-writes"
            },
            {
                "bright sky landscape",
                "mixed/artificial light",
                "intentionally warm or cool scene",
                "camera JPEG with strong embedded look"
            },
            {
                "raw-placement",
                "local-candidate",
                "display-candidate"
            },
            {
                "tooLocal",
                "tooFinished",
                "nextManualControl"
            },
            "Do color-qualified points stay limited to clear sky/color conflicts without treating stylized or mixed light as a correction target?"
        },
        {
            "mild-finish-tone-max-strength",
            "mildFinishTone",
            "mildFinishTone.maxStrength",
            "Mild Finish Tone strength cap",
            0.25,
            "Finish Tone",
            "Maximum automatic S-curve strength for Balanced mild tone.",
            {
                "representative-categories",
                "human-review",
                "candidate-diagnostics",
                "stage-diagnostics",
                "visible-recipe-writes"
            },
            {
                "flat overcast / low contrast",
                "normal daylight",
                "high-key snow/beach/interior white room",
                "low-key night or stage",
                "camera JPEG with strong embedded look"
            },
            {
                "finish-tone-candidate",
                "display-candidate"
            },
            {
                "tooFlat",
                "tooFinished",
                "nextManualControl"
            },
            "Does mild tone add useful global contrast without making the result feel finished or hiding upstream placement issues?"
        },
        {
            "mild-finish-tone-min-apply-strength",
            "mildFinishTone",
            "mildFinishTone.minApplyStrength",
            "Mild Finish Tone apply threshold",
            0.035,
            "Finish Tone",
            "Smallest tone strength worth authoring as visible graph points.",
            {
                "representative-categories",
                "human-review",
                "candidate-diagnostics",
                "stage-diagnostics",
                "visible-recipe-writes"
            },
            {
                "flat overcast / low contrast",
                "normal daylight"
            },
            {
                "finish-tone-candidate",
                "display-candidate"
            },
            {
                "tooFlat",
                "tooFinished",
                "nextManualControl"
            },
            "Is the minimum tone edit visibly useful enough to justify authoring graph points instead of leaving Finish Tone neutral?"
        },
        {
            "mild-finish-tone-mid-spread-good",
            "mildFinishTone",
            "mildFinishTone.midSpreadGoodEv",
            "Mild Finish Tone good mid-spread",
            1.00,
            "Finish Tone",
            "Lower midtone spread bound treated as already contrast-safe.",
            {
                "representative-categories",
                "human-review",
                "candidate-diagnostics",
                "stage-diagnostics"
            },
            {
                "flat overcast / low contrast",
                "normal daylight",
                "high-key snow/beach/interior white room"
            },
            {
                "finish-tone-candidate",
                "display-candidate"
            },
            {
                "tooFlat",
                "nextManualControl"
            },
            "Do midtone-spread thresholds match reviewer reports about flatness before Display Fit can mask the issue?"
        },
        {
            "mild-finish-tone-mid-spread-flat-limit",
            "mildFinishTone",
            "mildFinishTone.midSpreadFlatLimitEv",
            "Mild Finish Tone flat mid-spread limit",
            2.40,
            "Finish Tone",
            "Midtone spread where mild tone should taper toward neutral.",
            {
                "representative-categories",
                "human-review",
                "candidate-diagnostics",
                "stage-diagnostics"
            },
            {
                "flat overcast / low contrast",
                "normal daylight",
                "camera JPEG with strong embedded look"
            },
            {
                "finish-tone-candidate",
                "display-candidate"
            },
            {
                "tooFlat",
                "tooFinished",
                "nextManualControl"
            },
            "Does the flatness taper avoid tone edits when pre-display contrast is already adequate?"
        },
        {
            "mild-finish-tone-wide-spread-good",
            "mildFinishTone",
            "mildFinishTone.wideSpreadGoodEv",
            "Mild Finish Tone wide-spread good bound",
            5.50,
            "Finish Tone",
            "Wide tonal range bound used to keep mild tone from over-compressing broad scenes.",
            {
                "metadata",
                "raw-buffer-safety",
                "representative-categories",
                "human-review",
                "candidate-diagnostics",
                "stage-diagnostics"
            },
            {
                "bright sky landscape",
                "interior with bright window",
                "clipped specular highlights",
                "low-key night or stage"
            },
            {
                "finish-tone-candidate",
                "display-candidate"
            },
            {
                "tooFlat",
                "tooFinished",
                "nextManualControl"
            },
            "Do wide-spread thresholds keep Finish Tone from doing display-mapping work that belongs to Display Fit?"
        },
        {
            "mild-finish-tone-wide-spread-limit",
            "mildFinishTone",
            "mildFinishTone.wideSpreadLimitEv",
            "Mild Finish Tone wide-spread limit",
            8.00,
            "Finish Tone",
            "Wide tonal range hard limit where automatic mild tone should become very cautious.",
            {
                "metadata",
                "raw-buffer-safety",
                "representative-categories",
                "human-review",
                "candidate-diagnostics",
                "stage-diagnostics"
            },
            {
                "bright sky landscape",
                "interior with bright window",
                "clipped specular highlights",
                "low-key night or stage"
            },
            {
                "finish-tone-candidate",
                "display-candidate"
            },
            {
                "tooFinished",
                "nextManualControl"
            },
            "Do very broad-range scenes stay protected from automatic tone shaping that should remain a manual choice?"
        },
        {
            "mild-finish-tone-point-y-delta-per-strength",
            "mildFinishTone",
            "mildFinishTone.pointYDeltaPerStrength",
            "Mild Finish Tone point delta scale",
            0.07,
            "Finish Tone",
            "Graph-point movement per unit of mild tone strength.",
            {
                "representative-categories",
                "human-review",
                "candidate-diagnostics",
                "stage-diagnostics",
                "visible-recipe-writes"
            },
            {
                "flat overcast / low contrast",
                "normal daylight",
                "high-key snow/beach/interior white room",
                "low-key night or stage"
            },
            {
                "finish-tone-candidate",
                "display-candidate"
            },
            {
                "tooFlat",
                "tooFinished",
                "nextManualControl"
            },
            "Do the visible graph points move enough to explain the contrast change while remaining easy to edit manually?"
        }
    };
}

nlohmann::json BuildTuningConstantEvidenceGuide() {
    nlohmann::json constants = nlohmann::json::array();
    for (const TuningConstantEvidenceSpec& spec : TuningConstantEvidenceSpecs()) {
        constants.push_back({
            { "id", spec.id },
            { "group", spec.group },
            { "path", spec.path },
            { "label", spec.label },
            { "currentEngineeringDefault", spec.currentEngineeringDefault },
            { "ownerControl", spec.ownerControl },
            { "evidenceFocus", spec.evidenceFocus },
            { "requiredEvidenceItems", JsonStringVector(spec.requiredEvidenceItems) },
            { "requiredCategories", JsonStringVector(spec.requiredCategories) },
            { "requiredStages", JsonStringVector(spec.requiredStages) },
            { "humanReviewSignals", JsonStringVector(spec.humanReviewSignals) },
            { "tuningQuestion", spec.tuningQuestion }
        });
    }

    return {
        { "schema", "stack.raw-starting-point.constant-tuning-evidence-guide" },
        { "version", kTuningConstantEvidenceGuideSchemaVersion },
        { "constantsTunedByGuide", false },
        { "requiresRepresentativeRealRawRecords", true },
        { "instructions",
          "Use this guide to decide what evidence a reviewer must cite before changing an engineering default. It is not tuning evidence by itself." },
        { "constants", std::move(constants) }
    };
}

nlohmann::json BuildConstantTuningEvidenceStatus(
    const nlohmann::json& categoryTagCounts,
    const nlohmann::json& completeStageRecordCounts,
    int recordCount,
    bool allRequiredEvidenceComplete) {
    nlohmann::json constants = nlohmann::json::array();
    int readyConstantCount = 0;

    for (const TuningConstantEvidenceSpec& spec : TuningConstantEvidenceSpecs()) {
        nlohmann::json requiredCategoryCounts = nlohmann::json::object();
        nlohmann::json missingCategories = nlohmann::json::array();
        for (const std::string& category : spec.requiredCategories) {
            const int count = categoryTagCounts.value(category, 0);
            requiredCategoryCounts[category] = count;
            if (count <= 0) {
                missingCategories.push_back(category);
            }
        }

        nlohmann::json requiredStageRecordCounts = nlohmann::json::object();
        nlohmann::json missingStages = nlohmann::json::array();
        int leastCompleteStageRecordCount = recordCount;
        for (const std::string& stage : spec.requiredStages) {
            const int completeCount = completeStageRecordCounts.value(stage, 0);
            requiredStageRecordCounts[stage] = completeCount;
            leastCompleteStageRecordCount =
                std::min(leastCompleteStageRecordCount, completeCount);
            if (recordCount <= 0 || completeCount < recordCount) {
                missingStages.push_back(stage);
            }
        }

        const bool categoriesComplete =
            recordCount > 0 && missingCategories.empty();
        const bool stagesComplete =
            recordCount > 0 && missingStages.empty();
        const bool readyForTuningReview =
            allRequiredEvidenceComplete && categoriesComplete && stagesComplete;
        if (readyForTuningReview) {
            ++readyConstantCount;
        }

        constants.push_back({
            { "id", spec.id },
            { "group", spec.group },
            { "path", spec.path },
            { "label", spec.label },
            { "currentEngineeringDefault", spec.currentEngineeringDefault },
            { "ownerControl", spec.ownerControl },
            { "evidenceFocus", spec.evidenceFocus },
            { "requiredEvidenceItems", JsonStringVector(spec.requiredEvidenceItems) },
            { "requiredCategories", JsonStringVector(spec.requiredCategories) },
            { "requiredCategoryCounts", std::move(requiredCategoryCounts) },
            { "missingRequiredCategories", std::move(missingCategories) },
            { "requiredStages", JsonStringVector(spec.requiredStages) },
            { "requiredStageCompleteRecordCounts", std::move(requiredStageRecordCounts) },
            { "leastCompleteStageRecordCount", leastCompleteStageRecordCount },
            { "missingRequiredStages", std::move(missingStages) },
            { "humanReviewSignals", JsonStringVector(spec.humanReviewSignals) },
            { "tuningQuestion", spec.tuningQuestion },
            { "readyForTuningReview", readyForTuningReview },
            { "tuningState", readyForTuningReview
                ? "evidence-ready-for-human-review"
                : "blocked-pending-validation-evidence" },
            { "tuningInstruction", readyForTuningReview
                ? "A human reviewer may compare record IDs and propose a constant change; this report still does not tune it automatically."
                : "Keep the engineering default unchanged until missing categories, stages, and global readiness blockers are resolved." }
        });
    }

    const int constantCount =
        static_cast<int>(TuningConstantEvidenceSpecs().size());
    return {
        { "schema", "stack.raw-starting-point.constant-tuning-evidence-status" },
        { "version", kTuningConstantEvidenceStatusSchemaVersion },
        { "recordCount", recordCount },
        { "constantCount", constantCount },
        { "readyConstantCount", readyConstantCount },
        { "allRequiredEvidenceComplete", allRequiredEvidenceComplete },
        { "readyForFullTuningReview",
          constantCount > 0 && readyConstantCount == constantCount },
        { "constantsTunedByThisReport", false },
        { "instructions",
          "This checklist explains the evidence required before changing constants. It does not change constants, recipes, render output, or RAW buffers." },
        { "constants", std::move(constants) }
    };
}

nlohmann::json BuildConstantReviewBlankReview() {
    return {
        { "decision", "" },
        { "decisionOptions", nlohmann::json::array({ "keep", "change", "defer" }) },
        { "reviewer", "" },
        { "reviewDate", "" },
        { "proposedValue", nlohmann::json() },
        { "rationale", "" },
        { "evidenceRecordIds", nlohmann::json::array() },
        { "counterexampleRecordIds", nlohmann::json::array() },
        { "categoryEvidenceReviewed", nlohmann::json::array() },
        { "stageEvidenceReviewed", nlohmann::json::array() },
        { "humanReviewSignalSummary", "" },
        { "observedFailureModes", nlohmann::json::array() },
        { "visibleControlImpact", "" },
        { "safetyNotes", "" },
        { "readyToApplyInFuturePass", false }
    };
}

nlohmann::json BuildConstantReviewTemplateRecord(
    const nlohmann::json& constantStatus,
    const nlohmann::json& globalBlockingReasons) {
    const bool readyForTuningReview =
        constantStatus.is_object() &&
        constantStatus.value("readyForTuningReview", false);

    return {
        { "id", constantStatus.value("id", "") },
        { "group", constantStatus.value("group", "") },
        { "path", constantStatus.value("path", "") },
        { "label", constantStatus.value("label", "") },
        { "currentEngineeringDefault", constantStatus.value("currentEngineeringDefault", nlohmann::json()) },
        { "ownerControl", constantStatus.value("ownerControl", "") },
        { "evidenceFocus", constantStatus.value("evidenceFocus", "") },
        { "requiredEvidenceItems", constantStatus.value("requiredEvidenceItems", nlohmann::json::array()) },
        { "requiredCategories", constantStatus.value("requiredCategories", nlohmann::json::array()) },
        { "missingRequiredCategories", constantStatus.value("missingRequiredCategories", nlohmann::json::array()) },
        { "requiredStages", constantStatus.value("requiredStages", nlohmann::json::array()) },
        { "missingRequiredStages", constantStatus.value("missingRequiredStages", nlohmann::json::array()) },
        { "humanReviewSignals", constantStatus.value("humanReviewSignals", nlohmann::json::array()) },
        { "tuningQuestion", constantStatus.value("tuningQuestion", "") },
        { "readyForTuningReview", readyForTuningReview },
        { "tuningState", constantStatus.value("tuningState", "blocked-pending-validation-evidence") },
        { "globalBlockingReasons", globalBlockingReasons },
        { "review", BuildConstantReviewBlankReview() },
        { "reviewInstructions", readyForTuningReview
            ? "Fill review fields with record IDs and rationale before any later pass changes this constant."
            : "Leave decision empty or defer; resolve missing evidence and global blockers before proposing a constant change." }
    };
}

nlohmann::json BuildConstantReviewTemplateReport(
    const nlohmann::json& summaryReport,
    const std::filesystem::path& templateOutputPath) {
    const nlohmann::json summary =
        summaryReport.is_object()
            ? summaryReport.value("validationSetSummary", nlohmann::json::object())
            : nlohmann::json::object();
    const nlohmann::json readiness =
        summary.value("tuningReadiness", nlohmann::json::object());
    const nlohmann::json constantEvidence =
        summary.value("constantTuningEvidence", nlohmann::json::object());
    const nlohmann::json constants =
        constantEvidence.value("constants", nlohmann::json::array());
    const nlohmann::json blockingReasons =
        readiness.value("blockingReasons", nlohmann::json::array());

    nlohmann::json records = nlohmann::json::object();
    if (constants.is_array()) {
        for (const nlohmann::json& constantStatus : constants) {
            if (!constantStatus.is_object()) {
                continue;
            }
            const std::string id = constantStatus.value("id", "");
            if (id.empty()) {
                continue;
            }
            records[id] =
                BuildConstantReviewTemplateRecord(constantStatus, blockingReasons);
        }
    }

    return {
        { "schema", "stack.raw-starting-point.constant-review-template" },
        { "version", kConstantReviewTemplateSchemaVersion },
        { "sourceSummaryReportPath", summaryReport.value("sourceReportPath", "") },
        { "templateOutputFile", templateOutputPath.string() },
        { "sourceSummarySchemaVersion", summaryReport.value("version", 0) },
        { "validationSetSummarySchemaVersion", summary.value("version", 0) },
        { "constantEvidenceStatusVersion", constantEvidence.value("version", 0) },
        { "recordCount", summary.value("recordCount", 0) },
        { "constantCount", constantEvidence.value("constantCount", 0) },
        { "readyConstantCount", constantEvidence.value("readyConstantCount", 0) },
        { "readyForFullTuningReview", constantEvidence.value("readyForFullTuningReview", false) },
        { "mechanicalInputsComplete", readiness.value("mechanicalInputsComplete", false) },
        { "blockingReasons", blockingReasons },
        { "constantsTunedByTemplate", false },
        { "behaviorChanged", false },
        { "reviewContract", {
            { "requiresRepresentativeRealRawRecords", true },
            { "requiresReadySummary", true },
            { "requiresRecordIdsForEveryChange", true },
            { "requiresHumanReviewer", true },
            { "requiresMechanicalCheckBeforeTuning", true },
            { "allowedDecisions", nlohmann::json::array({ "keep", "change", "defer" }) },
            { "reviewCheckCommand",
              "--check-raw-starting-point-constant-review <records-summary.json> <constant-review-template.json> --require-ready --out <constant-review-check.json>" },
            { "reviewCheckWithRepairCommand",
              "--check-raw-starting-point-constant-review <records-summary.json> <constant-review-template.json> --require-ready --out <constant-review-check.json> --repair-out <constant-review-repair.json>" },
            { "reviewCheckWithRepairAndPatchBundleCommand",
              "--check-raw-starting-point-constant-review <records-summary.json> <constant-review-template.json> --require-ready --out <constant-review-check.json> --repair-out <constant-review-repair.json> --suggested-review-patch-bundle-out <constant-review-patch-bundle.json>" },
            { "repairOutputPurpose",
              "When the mechanical check is blocked, --repair-out writes constant-keyed review records that can be repaired and copied back into the template." },
            { "patchBundleOutputPurpose",
              "When evidence-only suggested patches are available, --suggested-review-patch-bundle-out writes the advisory bundle as a standalone helper file without mutating the review template." },
            { "readyWhen",
              "Every changed constant has a reviewer, date, rationale, proposedValue, evidenceRecordIds, and no unresolved global or per-constant evidence blockers, then the constant-review checker passes." }
        } },
        { "instructions",
          "Fill this template only after validation summaries are ready. It is a review artifact for a later tuning pass and does not change constants, recipes, render output, or RAW buffers." },
        { "records", std::move(records) }
    };
}

bool JsonHasNonEmptyString(const nlohmann::json& object, const char* field) {
    return object.is_object() &&
        object.contains(field) &&
        object[field].is_string() &&
        !object[field].get<std::string>().empty();
}

bool JsonArrayHasNonEmptyString(const nlohmann::json& value) {
    if (!value.is_array()) {
        return false;
    }
    for (const nlohmann::json& item : value) {
        if (item.is_string() && !item.get<std::string>().empty()) {
            return true;
        }
    }
    return false;
}

void AddRecordIdAlias(std::set<std::string>& ids, const std::string& value) {
    if (value.empty()) {
        return;
    }
    ids.insert(value);
    ids.insert(NormalizeAnnotationKey(value));
}

void AddRecordIdAliasesFromObject(
    std::set<std::string>& ids,
    const nlohmann::json& object,
    const std::vector<const char*>& fields) {
    if (!object.is_object()) {
        return;
    }
    for (const char* field : fields) {
        AddRecordIdAlias(ids, JsonOptionalString(object, field));
    }
}

void AddValidationRecordAliases(
    std::set<std::string>& ids,
    const nlohmann::json& record,
    int recordIndex) {
    AddRecordIdAlias(ids, RecordSourceId(record, recordIndex));
    AddRecordIdAliasesFromObject(
        ids,
        record,
        { "id", "sourceId", "relativePath", "path", "fileName", "stem", "absolutePath", "fingerprint", "identityToken", "sourceIdentityToken" });
    const nlohmann::json source =
        record.value("source", nlohmann::json::object());
    AddRecordIdAliasesFromObject(
        ids,
        source,
        { "id", "relativePath", "path", "fileName", "stem", "absolutePath", "fingerprint", "identityToken", "sourceIdentityToken" });
}

std::set<std::string> BuildValidationRecordAliases(
    const nlohmann::json& record,
    int recordIndex) {
    std::set<std::string> ids;
    AddValidationRecordAliases(ids, record, recordIndex);
    return ids;
}

nlohmann::json StringSetToJsonArray(const std::set<std::string>& values) {
    nlohmann::json result = nlohmann::json::array();
    for (const std::string& value : values) {
        result.push_back(value);
    }
    return result;
}

bool TryLoadRecordsForConstantReview(
    const nlohmann::json& summaryReport,
    ConstantReviewEvidenceIndex& outIndex) {
    outIndex = ConstantReviewEvidenceIndex();
    if (!summaryReport.is_object()) {
        outIndex.unavailableReason = "summary report is not a JSON object";
        return false;
    }
    outIndex.sourceReportPath = summaryReport.value("sourceReportPath", "");
    if (outIndex.sourceReportPath.empty()) {
        outIndex.unavailableReason = "summary report has no sourceReportPath";
        return false;
    }

    std::error_code ec;
    const std::filesystem::path sourcePath =
        std::filesystem::absolute(std::filesystem::path(outIndex.sourceReportPath), ec).lexically_normal();
    if (ec ||
        !std::filesystem::exists(sourcePath, ec) || ec ||
        !std::filesystem::is_regular_file(sourcePath, ec) || ec) {
        outIndex.unavailableReason =
            "source validation records file is unavailable: " + outIndex.sourceReportPath;
        return false;
    }
    outIndex.sourceReportPath = sourcePath.string();

    std::ifstream in(sourcePath, std::ios::binary);
    if (!in) {
        outIndex.unavailableReason =
            "could not open source validation records file: " + sourcePath.string();
        return false;
    }

    nlohmann::json sourceReport;
    try {
        in >> sourceReport;
    } catch (const std::exception& ex) {
        outIndex.unavailableReason =
            std::string("invalid JSON in source validation records file: ") + ex.what();
        return false;
    }

    const nlohmann::json records =
        sourceReport.value("records", nlohmann::json::array());
    if (!records.is_array()) {
        outIndex.unavailableReason =
            "source validation records file has no records array";
        return false;
    }

    for (std::size_t i = 0; i < records.size(); ++i) {
        ConstantReviewValidationRecordEvidence recordEvidence;
        recordEvidence.canonicalId =
            RecordSourceId(records[i], static_cast<int>(i));
        recordEvidence.record = records[i];
        recordEvidence.aliases =
            BuildValidationRecordAliases(records[i], static_cast<int>(i));
        const std::size_t recordSlot = outIndex.records.size();
        for (const std::string& alias : recordEvidence.aliases) {
            outIndex.knownRecordIds.insert(alias);
            outIndex.knownRecordIds.insert(NormalizeAnnotationKey(alias));
            outIndex.aliasToRecord[alias] = recordSlot;
            outIndex.aliasToRecord[NormalizeAnnotationKey(alias)] = recordSlot;
        }
        outIndex.records.push_back(std::move(recordEvidence));
    }
    outIndex.available = true;
    return true;
}

std::set<std::string> JsonStringSet(const nlohmann::json& value) {
    std::set<std::string> result;
    for (const std::string& item : JsonStringList(value)) {
        if (!item.empty()) {
            result.insert(item);
        }
    }
    return result;
}

const ConstantReviewValidationRecordEvidence* FindConstantReviewEvidenceRecord(
    const ConstantReviewEvidenceIndex& evidenceIndex,
    const std::string& citedId) {
    if (!evidenceIndex.available || citedId.empty()) {
        return nullptr;
    }

    const auto direct = evidenceIndex.aliasToRecord.find(citedId);
    if (direct != evidenceIndex.aliasToRecord.end() &&
        direct->second < evidenceIndex.records.size()) {
        return &evidenceIndex.records[direct->second];
    }

    const std::string normalized = NormalizeAnnotationKey(citedId);
    const auto normalizedIt = evidenceIndex.aliasToRecord.find(normalized);
    if (normalizedIt != evidenceIndex.aliasToRecord.end() &&
        normalizedIt->second < evidenceIndex.records.size()) {
        return &evidenceIndex.records[normalizedIt->second];
    }

    return nullptr;
}

bool RecordHasCompleteHumanReviewSignal(
    const nlohmann::json& record,
    const std::string& signal) {
    if (!record.is_object() || signal.empty()) {
        return false;
    }
    const nlohmann::json humanReview =
        record.value("humanReview", nlohmann::json::object());
    const nlohmann::json value =
        humanReview.is_object() && humanReview.contains(signal)
            ? humanReview[signal]
            : nlohmann::json();
    return HumanReviewFieldComplete(signal, value);
}

nlohmann::json BuildConstantReviewEvidenceRecordCatalog(
    const ConstantReviewEvidenceIndex& evidenceIndex) {
    nlohmann::json records = nlohmann::json::array();
    if (evidenceIndex.available) {
        for (const ConstantReviewValidationRecordEvidence& recordEvidence :
             evidenceIndex.records) {
            nlohmann::json completeStages = nlohmann::json::array();
            for (const RequiredStageEvidenceSpec& stage : RequiredStageEvidenceSpecs()) {
                if (RecordHasCompleteStageDiagnostics(recordEvidence.record, stage.id)) {
                    completeStages.push_back(stage.id);
                }
            }

            nlohmann::json completeHumanReviewSignals = nlohmann::json::array();
            nlohmann::json missingHumanReviewSignals = nlohmann::json::array();
            for (const std::string& field : RequiredHumanReviewFields()) {
                if (RecordHasCompleteHumanReviewSignal(recordEvidence.record, field)) {
                    completeHumanReviewSignals.push_back(field);
                } else {
                    missingHumanReviewSignals.push_back(field);
                }
            }

            nlohmann::json source = nlohmann::json::object();
            if (recordEvidence.record.is_object() &&
                recordEvidence.record.contains("source") &&
                recordEvidence.record["source"].is_object()) {
                source = recordEvidence.record["source"];
            }

            nlohmann::json entry = {
                { "recordId", recordEvidence.canonicalId },
                { "aliases", StringSetToJsonArray(recordEvidence.aliases) },
                { "source", std::move(source) },
                { "recordStatus", recordEvidence.record.value("recordStatus", "") },
                { "imageCategoryTags",
                  recordEvidence.record.value("imageCategoryTags", nlohmann::json::array()) },
                { "completeStages", std::move(completeStages) },
                { "completeHumanReviewSignals", std::move(completeHumanReviewSignals) },
                { "missingHumanReviewSignals", std::move(missingHumanReviewSignals) },
                { "validationGaps",
                  recordEvidence.record.value("validationGaps", nlohmann::json::array()) }
            };

            const nlohmann::json humanReview =
                recordEvidence.record.value("humanReview", nlohmann::json::object());
            if (humanReview.is_object()) {
                entry["humanReviewSummary"] = {
                    { "nextManualControl",
                      JsonOptionalString(humanReview, "nextManualControl") },
                    { "tooDark", humanReview.value("tooDark", nlohmann::json()) },
                    { "tooBright", humanReview.value("tooBright", nlohmann::json()) },
                    { "tooLocal", humanReview.value("tooLocal", nlohmann::json()) },
                    { "tooFlat", humanReview.value("tooFlat", nlohmann::json()) },
                    { "tooFinished", humanReview.value("tooFinished", nlohmann::json()) }
                };
            }

            records.push_back(std::move(entry));
        }
    }

    return {
        { "available", evidenceIndex.available },
        { "sourceValidationRecordsFile", evidenceIndex.sourceReportPath },
        { "unavailableReason", evidenceIndex.unavailableReason },
        { "recordCount", evidenceIndex.records.size() },
        { "aliasCount", evidenceIndex.knownRecordIds.size() },
        { "instructions",
          "Use recordId or any alias from this catalog in constant-review evidenceRecordIds/counterexampleRecordIds, choosing records whose categories, completeStages, and completeHumanReviewSignals cover the constant's requirements." },
        { "records", std::move(records) }
    };
}

struct ConstantReviewEvidenceRecordSuggestion {
    std::string recordId;
    int matchedRequirementCount = 0;
    int matchingCategoryCount = 0;
    int matchingStageCount = 0;
    int matchingHumanReviewSignalCount = 0;
    std::set<std::string> matchingCategories;
    std::set<std::string> matchingStages;
    std::set<std::string> matchingHumanReviewSignals;
    nlohmann::json entry = nlohmann::json::object();
};

void AddMissingStrings(
    const std::set<std::string>& required,
    const std::set<std::string>& covered,
    nlohmann::json& outMissing) {
    outMissing = nlohmann::json::array();
    for (const std::string& item : required) {
        if (covered.find(item) == covered.end()) {
            outMissing.push_back(item);
        }
    }
}

std::vector<ConstantReviewEvidenceRecordSuggestion>
BuildConstantReviewEvidenceRecordSuggestionCandidates(
    const ConstantReviewEvidenceIndex& evidenceIndex,
    const std::set<std::string>& requiredCategories,
    const std::set<std::string>& requiredStages,
    const std::set<std::string>& requiredHumanReviewSignals) {
    const int totalRequiredRequirementCount =
        static_cast<int>(
            requiredCategories.size() +
            requiredStages.size() +
            requiredHumanReviewSignals.size());

    std::vector<ConstantReviewEvidenceRecordSuggestion> candidates;
    if (evidenceIndex.available) {
        for (const ConstantReviewValidationRecordEvidence& recordEvidence :
             evidenceIndex.records) {
            std::set<std::string> matchingCategories;
            std::set<std::string> matchingStages;
            std::set<std::string> matchingHumanReviewSignals;

            const nlohmann::json tags =
                recordEvidence.record.value("imageCategoryTags", nlohmann::json::array());
            if (tags.is_array()) {
                for (const std::string& tag : JsonStringList(tags)) {
                    if (requiredCategories.find(tag) != requiredCategories.end()) {
                        matchingCategories.insert(tag);
                    }
                }
            }

            for (const RequiredStageEvidenceSpec& stage : RequiredStageEvidenceSpecs()) {
                if (requiredStages.find(stage.id) != requiredStages.end() &&
                    RecordHasCompleteStageDiagnostics(recordEvidence.record, stage.id)) {
                    matchingStages.insert(stage.id);
                }
            }

            for (const std::string& signal : requiredHumanReviewSignals) {
                if (RecordHasCompleteHumanReviewSignal(recordEvidence.record, signal)) {
                    matchingHumanReviewSignals.insert(signal);
                }
            }

            ConstantReviewEvidenceRecordSuggestion candidate;
            candidate.recordId = recordEvidence.canonicalId;
            candidate.matchingCategories = std::move(matchingCategories);
            candidate.matchingStages = std::move(matchingStages);
            candidate.matchingHumanReviewSignals =
                std::move(matchingHumanReviewSignals);
            candidate.matchingCategoryCount =
                static_cast<int>(candidate.matchingCategories.size());
            candidate.matchingStageCount =
                static_cast<int>(candidate.matchingStages.size());
            candidate.matchingHumanReviewSignalCount =
                static_cast<int>(candidate.matchingHumanReviewSignals.size());
            candidate.matchedRequirementCount =
                candidate.matchingCategoryCount +
                candidate.matchingStageCount +
                candidate.matchingHumanReviewSignalCount;
            if (candidate.matchedRequirementCount <= 0) {
                continue;
            }

            const double coverageScore01 =
                totalRequiredRequirementCount > 0
                    ? static_cast<double>(candidate.matchedRequirementCount) /
                          static_cast<double>(totalRequiredRequirementCount)
                    : 0.0;
            const bool coversAllCategories =
                candidate.matchingCategories.size() == requiredCategories.size();
            const bool coversAllStages =
                candidate.matchingStages.size() == requiredStages.size();
            const bool coversAllHumanReviewSignals =
                candidate.matchingHumanReviewSignals.size() ==
                    requiredHumanReviewSignals.size();

            candidate.entry = {
                { "recordId", candidate.recordId },
                { "aliases", StringSetToJsonArray(recordEvidence.aliases) },
                { "recordStatus", recordEvidence.record.value("recordStatus", "") },
                { "matchedRequirementCount", candidate.matchedRequirementCount },
                { "coverageScore01", coverageScore01 },
                { "matchingCategoryCount", candidate.matchingCategoryCount },
                { "matchingStageCount", candidate.matchingStageCount },
                { "matchingHumanReviewSignalCount",
                  candidate.matchingHumanReviewSignalCount },
                { "matchingRequiredCategories",
                  StringSetToJsonArray(candidate.matchingCategories) },
                { "matchingRequiredStages",
                  StringSetToJsonArray(candidate.matchingStages) },
                { "matchingHumanReviewSignals",
                  StringSetToJsonArray(candidate.matchingHumanReviewSignals) },
                { "coversAllRequiredCategories", coversAllCategories },
                { "coversAllRequiredStages", coversAllStages },
                { "coversAllRequiredHumanReviewSignals",
                  coversAllHumanReviewSignals },
                { "completeCoverageForThisConstant",
                  coversAllCategories &&
                      coversAllStages &&
                      coversAllHumanReviewSignals }
            };
            candidates.push_back(std::move(candidate));
        }
    }

    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const ConstantReviewEvidenceRecordSuggestion& lhs,
           const ConstantReviewEvidenceRecordSuggestion& rhs) {
            if (lhs.matchedRequirementCount != rhs.matchedRequirementCount) {
                return lhs.matchedRequirementCount > rhs.matchedRequirementCount;
            }
            if (lhs.matchingStageCount != rhs.matchingStageCount) {
                return lhs.matchingStageCount > rhs.matchingStageCount;
            }
            if (lhs.matchingCategoryCount != rhs.matchingCategoryCount) {
                return lhs.matchingCategoryCount > rhs.matchingCategoryCount;
            }
            if (lhs.matchingHumanReviewSignalCount !=
                rhs.matchingHumanReviewSignalCount) {
                return lhs.matchingHumanReviewSignalCount >
                    rhs.matchingHumanReviewSignalCount;
            }
            return lhs.recordId < rhs.recordId;
        });

    return candidates;
}

int CountNewEvidenceMatches(
    const std::set<std::string>& matching,
    const std::set<std::string>& covered) {
    int count = 0;
    for (const std::string& item : matching) {
        if (covered.find(item) == covered.end()) {
            ++count;
        }
    }
    return count;
}

std::set<std::string> NewEvidenceMatches(
    const std::set<std::string>& matching,
    const std::set<std::string>& covered) {
    std::set<std::string> result;
    for (const std::string& item : matching) {
        if (covered.find(item) == covered.end()) {
            result.insert(item);
        }
    }
    return result;
}

void MergeEvidenceMatches(
    std::set<std::string>& covered,
    const std::set<std::string>& matching) {
    covered.insert(matching.begin(), matching.end());
}

nlohmann::json BuildConstantReviewEvidenceRecordSuggestions(
    const nlohmann::json& constantStatus,
    const ConstantReviewEvidenceIndex& evidenceIndex) {
    constexpr int kSuggestionLimit = 25;

    const std::set<std::string> requiredCategories =
        JsonStringSet(constantStatus.value("requiredCategories", nlohmann::json::array()));
    const std::set<std::string> requiredStages =
        JsonStringSet(constantStatus.value("requiredStages", nlohmann::json::array()));
    const std::set<std::string> requiredHumanReviewSignals =
        JsonStringSet(constantStatus.value("humanReviewSignals", nlohmann::json::array()));
    std::vector<ConstantReviewEvidenceRecordSuggestion> candidates =
        BuildConstantReviewEvidenceRecordSuggestionCandidates(
            evidenceIndex,
            requiredCategories,
            requiredStages,
            requiredHumanReviewSignals);

    nlohmann::json records = nlohmann::json::array();
    const std::size_t shownCount =
        std::min<std::size_t>(
            static_cast<std::size_t>(kSuggestionLimit),
            candidates.size());
    for (std::size_t i = 0; i < shownCount; ++i) {
        records.push_back(std::move(candidates[i].entry));
    }

    return {
        { "available", evidenceIndex.available },
        { "sourceValidationRecordsFile", evidenceIndex.sourceReportPath },
        { "unavailableReason", evidenceIndex.unavailableReason },
        { "knownRecordCount", evidenceIndex.records.size() },
        { "suggestedRecordCount", candidates.size() },
        { "shownRecordCount", records.size() },
        { "suggestionLimit", kSuggestionLimit },
        { "requiredCategories", StringSetToJsonArray(requiredCategories) },
        { "requiredStages", StringSetToJsonArray(requiredStages) },
        { "requiredHumanReviewSignals",
          StringSetToJsonArray(requiredHumanReviewSignals) },
        { "instructions",
          "Use these records as starting suggestions for evidenceRecordIds or counterexampleRecordIds. A changed constant may need multiple records; the checker still requires full category, stage, and human-review coverage before tuning." },
        { "records", std::move(records) }
    };
}

nlohmann::json BuildConstantReviewEvidenceCoveragePlan(
    const nlohmann::json& constantStatus,
    const ConstantReviewEvidenceIndex& evidenceIndex) {
    constexpr int kPlanRecordLimit = 8;

    const std::set<std::string> requiredCategories =
        JsonStringSet(constantStatus.value("requiredCategories", nlohmann::json::array()));
    const std::set<std::string> requiredStages =
        JsonStringSet(constantStatus.value("requiredStages", nlohmann::json::array()));
    const std::set<std::string> requiredHumanReviewSignals =
        JsonStringSet(constantStatus.value("humanReviewSignals", nlohmann::json::array()));
    std::vector<ConstantReviewEvidenceRecordSuggestion> candidates =
        BuildConstantReviewEvidenceRecordSuggestionCandidates(
            evidenceIndex,
            requiredCategories,
            requiredStages,
            requiredHumanReviewSignals);

    std::set<std::string> coveredCategories;
    std::set<std::string> coveredStages;
    std::set<std::string> coveredHumanReviewSignals;
    nlohmann::json selectedRecordIds = nlohmann::json::array();
    nlohmann::json selectedRecords = nlohmann::json::array();
    std::vector<bool> selected(candidates.size(), false);

    for (int step = 0; step < kPlanRecordLimit; ++step) {
        std::size_t bestIndex = candidates.size();
        int bestNewMatchCount = 0;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (selected[i]) {
                continue;
            }
            const ConstantReviewEvidenceRecordSuggestion& candidate = candidates[i];
            const int newMatchCount =
                CountNewEvidenceMatches(candidate.matchingCategories, coveredCategories) +
                CountNewEvidenceMatches(candidate.matchingStages, coveredStages) +
                CountNewEvidenceMatches(
                    candidate.matchingHumanReviewSignals,
                    coveredHumanReviewSignals);
            if (newMatchCount <= 0) {
                continue;
            }
            if (bestIndex == candidates.size() ||
                newMatchCount > bestNewMatchCount ||
                (newMatchCount == bestNewMatchCount &&
                    candidate.matchedRequirementCount >
                        candidates[bestIndex].matchedRequirementCount)) {
                bestIndex = i;
                bestNewMatchCount = newMatchCount;
            }
        }

        if (bestIndex == candidates.size()) {
            break;
        }

        selected[bestIndex] = true;
        const ConstantReviewEvidenceRecordSuggestion& candidate = candidates[bestIndex];
        const std::set<std::string> newCategories =
            NewEvidenceMatches(candidate.matchingCategories, coveredCategories);
        const std::set<std::string> newStages =
            NewEvidenceMatches(candidate.matchingStages, coveredStages);
        const std::set<std::string> newHumanReviewSignals =
            NewEvidenceMatches(
                candidate.matchingHumanReviewSignals,
                coveredHumanReviewSignals);

        MergeEvidenceMatches(coveredCategories, candidate.matchingCategories);
        MergeEvidenceMatches(coveredStages, candidate.matchingStages);
        MergeEvidenceMatches(
            coveredHumanReviewSignals,
            candidate.matchingHumanReviewSignals);

        selectedRecordIds.push_back(candidate.recordId);
        selectedRecords.push_back({
            { "recordId", candidate.recordId },
            { "aliases", candidate.entry.value("aliases", nlohmann::json::array()) },
            { "recordStatus", candidate.entry.value("recordStatus", "") },
            { "newRequirementCount", bestNewMatchCount },
            { "newRequiredCategories", StringSetToJsonArray(newCategories) },
            { "newRequiredStages", StringSetToJsonArray(newStages) },
            { "newHumanReviewSignals", StringSetToJsonArray(newHumanReviewSignals) },
            { "matchingRequiredCategories",
              StringSetToJsonArray(candidate.matchingCategories) },
            { "matchingRequiredStages",
              StringSetToJsonArray(candidate.matchingStages) },
            { "matchingHumanReviewSignals",
              StringSetToJsonArray(candidate.matchingHumanReviewSignals) }
        });
    }

    nlohmann::json missingCategories;
    nlohmann::json missingStages;
    nlohmann::json missingHumanReviewSignals;
    AddMissingStrings(requiredCategories, coveredCategories, missingCategories);
    AddMissingStrings(requiredStages, coveredStages, missingStages);
    AddMissingStrings(
        requiredHumanReviewSignals,
        coveredHumanReviewSignals,
        missingHumanReviewSignals);

    const bool complete =
        evidenceIndex.available &&
        missingCategories.empty() &&
        missingStages.empty() &&
        missingHumanReviewSignals.empty();

    return {
        { "available", evidenceIndex.available },
        { "sourceValidationRecordsFile", evidenceIndex.sourceReportPath },
        { "unavailableReason", evidenceIndex.unavailableReason },
        { "knownRecordCount", evidenceIndex.records.size() },
        { "candidateRecordCount", candidates.size() },
        { "selectedRecordCount", selectedRecords.size() },
        { "recordLimit", kPlanRecordLimit },
        { "recommendedEvidenceRecordIds", std::move(selectedRecordIds) },
        { "coveredCategories", StringSetToJsonArray(coveredCategories) },
        { "missingRequiredCategories", std::move(missingCategories) },
        { "coveredStages", StringSetToJsonArray(coveredStages) },
        { "missingRequiredStages", std::move(missingStages) },
        { "coveredHumanReviewSignals",
          StringSetToJsonArray(coveredHumanReviewSignals) },
        { "missingHumanReviewSignals", std::move(missingHumanReviewSignals) },
        { "completeIfRecommendedRecordsCited", complete },
        { "advisoryOnly", true },
        { "selectionStrategy",
          "Greedy set cover over known validation records, choosing records that add the most new required category, stage, and human-review coverage at each step." },
        { "instructions",
          "Use recommendedEvidenceRecordIds as a starting set for review.evidenceRecordIds, then rerun the checker. This plan is advisory and does not replace the mechanical coverage gate." },
        { "records", std::move(selectedRecords) }
    };
}

nlohmann::json JsonArrayOrEmpty(const nlohmann::json& value) {
    return value.is_array() ? value : nlohmann::json::array();
}

std::string EscapeJsonPointerToken(std::string value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char c : value) {
        if (c == '~') {
            escaped += "~0";
        } else if (c == '/') {
            escaped += "~1";
        } else {
            escaped.push_back(c);
        }
    }
    return escaped;
}

nlohmann::json BuildConstantReviewSuggestedEvidencePatch(
    const nlohmann::json& constantStatus,
    const nlohmann::json& evidenceRecordCoveragePlan) {
    const nlohmann::json recommendedRecordIds =
        JsonArrayOrEmpty(
            evidenceRecordCoveragePlan.value(
                "recommendedEvidenceRecordIds",
                nlohmann::json::array()));
    const nlohmann::json coveredCategories =
        JsonArrayOrEmpty(
            evidenceRecordCoveragePlan.value(
                "coveredCategories",
                nlohmann::json::array()));
    const nlohmann::json coveredStages =
        JsonArrayOrEmpty(
            evidenceRecordCoveragePlan.value(
                "coveredStages",
                nlohmann::json::array()));
    const nlohmann::json coveredHumanReviewSignals =
        JsonArrayOrEmpty(
            evidenceRecordCoveragePlan.value(
                "coveredHumanReviewSignals",
                nlohmann::json::array()));
    const nlohmann::json missingCategories =
        JsonArrayOrEmpty(
            evidenceRecordCoveragePlan.value(
                "missingRequiredCategories",
                nlohmann::json::array()));
    const nlohmann::json missingStages =
        JsonArrayOrEmpty(
            evidenceRecordCoveragePlan.value(
                "missingRequiredStages",
                nlohmann::json::array()));
    const nlohmann::json missingHumanReviewSignals =
        JsonArrayOrEmpty(
            evidenceRecordCoveragePlan.value(
                "missingHumanReviewSignals",
                nlohmann::json::array()));

    const bool available =
        evidenceRecordCoveragePlan.value("available", false);
    const bool complete =
        evidenceRecordCoveragePlan.value(
            "completeIfRecommendedRecordsCited",
            false);
    const bool readyForCopy =
        available &&
        complete &&
        recommendedRecordIds.is_array() &&
        !recommendedRecordIds.empty();

    nlohmann::json blockers = nlohmann::json::array();
    if (!available) {
        const std::string unavailableReason =
            evidenceRecordCoveragePlan.value("unavailableReason", "");
        blockers.push_back(
            unavailableReason.empty()
                ? "Validation records are unavailable; evidence fields cannot be suggested."
                : unavailableReason);
    }
    if (recommendedRecordIds.empty()) {
        blockers.push_back("No recommended validation record IDs are available.");
    }
    if (!complete) {
        blockers.push_back(
            "Recommended records do not yet cover every required category, stage, and human-review signal.");
    }

    const bool hasHumanReviewSignals =
        coveredHumanReviewSignals.is_array() &&
        !coveredHumanReviewSignals.empty();
    const std::string humanReviewSignalSummary =
        hasHumanReviewSignals
            ? "Copy after a reviewer confirms the cited records cover the required human-review signals listed in coveredHumanReviewSignals."
            : "Copy only after a reviewer confirms the cited records cover this constant's human-review requirements.";

    const std::string constantId = constantStatus.value("id", "");
    const std::string constantPath = constantStatus.value("path", "");
    const std::string reviewPointerBase =
        "/records/" + EscapeJsonPointerToken(constantId) + "/review";
    const nlohmann::json reviewFields = {
        { "evidenceRecordIds", recommendedRecordIds },
        { "categoryEvidenceReviewed", coveredCategories },
        { "stageEvidenceReviewed", coveredStages },
        { "humanReviewSignalSummary", humanReviewSignalSummary }
    };
    nlohmann::json templateJsonPatch = nlohmann::json::array({
        {
            { "op", "replace" },
            { "path", reviewPointerBase + "/evidenceRecordIds" },
            { "value", reviewFields["evidenceRecordIds"] }
        },
        {
            { "op", "replace" },
            { "path", reviewPointerBase + "/categoryEvidenceReviewed" },
            { "value", reviewFields["categoryEvidenceReviewed"] }
        },
        {
            { "op", "replace" },
            { "path", reviewPointerBase + "/stageEvidenceReviewed" },
            { "value", reviewFields["stageEvidenceReviewed"] }
        },
        {
            { "op", "replace" },
            { "path", reviewPointerBase + "/humanReviewSignalSummary" },
            { "value", reviewFields["humanReviewSignalSummary"] }
        }
    });

    return {
        { "available", available },
        { "readyForCopy", readyForCopy },
        { "advisoryOnly", true },
        { "source", "evidenceRecordCoveragePlan" },
        { "targetReviewObject", "review" },
        { "mutatesTemplateAutomatically", false },
        { "patchAppliesAutomatically", false },
        { "constantId", constantId },
        { "constantPath", constantPath },
        { "reviewFields", reviewFields },
        { "jsonPatchFormat", "RFC 6902-compatible advisory operations" },
        { "jsonPointerBase", reviewPointerBase },
        { "templateJsonPatchOperationCount", templateJsonPatch.size() },
        { "templateJsonPatch", std::move(templateJsonPatch) },
        { "coveredHumanReviewSignals", coveredHumanReviewSignals },
        { "missingBeforeCopy", {
            { "categories", missingCategories },
            { "stages", missingStages },
            { "humanReviewSignals", missingHumanReviewSignals }
        } },
        { "fieldsIntentionallyNotIncluded", nlohmann::json::array({
            "review.decision",
            "review.reviewer",
            "review.reviewDate",
            "review.proposedValue",
            "review.rationale",
            "review.visibleControlImpact",
            "review.readyToApplyInFuturePass"
        }) },
        { "blockingReasons", std::move(blockers) },
        { "instructions",
          "This is a copyable evidence-only patch. Copy reviewFields or templateJsonPatch into the constant-review template only after confirming the recommended records against real validation evidence, then fill reviewer, rationale, proposedValue, decision, visibleControlImpact, and readyToApplyInFuturePass manually and rerun the checker. The templateJsonPatch is advisory and is never applied automatically by this command." }
    };
}

nlohmann::json BuildConstantReviewSuggestedPatchSummary(
    const nlohmann::json& recordReports) {
    int constantCount = 0;
    int patchAvailableCount = 0;
    int patchReadyForCopyCount = 0;
    int patchBlockedCount = 0;
    int patchUnavailableCount = 0;
    int patchMissingCount = 0;
    int patchAutoMutatingCount = 0;
    int patchAppliesAutomaticallyCount = 0;

    nlohmann::json readyForCopyConstantIds = nlohmann::json::array();
    nlohmann::json blockedConstantIds = nlohmann::json::array();
    nlohmann::json unavailableConstantIds = nlohmann::json::array();
    nlohmann::json missingPatchConstantIds = nlohmann::json::array();
    nlohmann::json autoMutatingConstantIds = nlohmann::json::array();
    nlohmann::json autoApplyingConstantIds = nlohmann::json::array();

    if (recordReports.is_array()) {
        for (const nlohmann::json& record : recordReports) {
            if (!record.is_object()) {
                continue;
            }
            ++constantCount;
            const std::string id = record.value("id", "");
            const nlohmann::json patch =
                record.value(
                    "suggestedReviewEvidencePatch",
                    nlohmann::json::object());
            if (!patch.is_object() || patch.empty()) {
                ++patchMissingCount;
                missingPatchConstantIds.push_back(id);
                continue;
            }

            const bool available = patch.value("available", false);
            const bool readyForCopy = patch.value("readyForCopy", false);
            const bool mutatesAutomatically =
                patch.value("mutatesTemplateAutomatically", true);
            const bool patchAppliesAutomatically =
                patch.value("patchAppliesAutomatically", true);

            if (available) {
                ++patchAvailableCount;
            } else {
                ++patchUnavailableCount;
                unavailableConstantIds.push_back(id);
            }
            if (readyForCopy) {
                ++patchReadyForCopyCount;
                readyForCopyConstantIds.push_back(id);
            } else {
                ++patchBlockedCount;
                blockedConstantIds.push_back(id);
            }
            if (mutatesAutomatically) {
                ++patchAutoMutatingCount;
                autoMutatingConstantIds.push_back(id);
            }
            if (patchAppliesAutomatically) {
                ++patchAppliesAutomaticallyCount;
                autoApplyingConstantIds.push_back(id);
            }
        }
    }

    return {
        { "constantCount", constantCount },
        { "patchAvailableCount", patchAvailableCount },
        { "patchReadyForCopyCount", patchReadyForCopyCount },
        { "patchBlockedCount", patchBlockedCount },
        { "patchUnavailableCount", patchUnavailableCount },
        { "patchMissingCount", patchMissingCount },
        { "patchAutoMutatingCount", patchAutoMutatingCount },
        { "patchAppliesAutomaticallyCount", patchAppliesAutomaticallyCount },
        { "readyForCopyConstantIds", std::move(readyForCopyConstantIds) },
        { "blockedConstantIds", std::move(blockedConstantIds) },
        { "unavailableConstantIds", std::move(unavailableConstantIds) },
        { "missingPatchConstantIds", std::move(missingPatchConstantIds) },
        { "autoMutatingConstantIds", std::move(autoMutatingConstantIds) },
        { "autoApplyingConstantIds", std::move(autoApplyingConstantIds) },
        { "allPatchesAvailable", constantCount > 0 && patchMissingCount == 0 },
        { "allPatchesReadyForCopy",
          constantCount > 0 &&
              patchReadyForCopyCount == constantCount &&
              patchAutoMutatingCount == 0 &&
              patchAppliesAutomaticallyCount == 0 },
        { "advisoryOnly", true },
        { "instructions",
          "Use readyForCopyConstantIds to find constants whose suggestedReviewEvidencePatch.reviewFields or templateJsonPatch can be copied after human confirmation. This summary is advisory and does not tune constants, mutate review templates, or apply templateJsonPatch automatically." }
    };
}

nlohmann::json BuildConstantReviewSuggestedPatchBundle(
    const nlohmann::json& recordReports,
    const std::filesystem::path& reviewTemplatePath) {
    int constantCount = 0;
    int bundledConstantCount = 0;
    int bundledOperationCount = 0;
    int blockedPatchCount = 0;
    int unavailablePatchCount = 0;
    int missingPatchCount = 0;
    int unsafePatchCount = 0;
    int missingOperationPatchCount = 0;

    nlohmann::json readyForCopyConstantIds = nlohmann::json::array();
    nlohmann::json bundledConstantIds = nlohmann::json::array();
    nlohmann::json blockedConstantIds = nlohmann::json::array();
    nlohmann::json unavailableConstantIds = nlohmann::json::array();
    nlohmann::json missingPatchConstantIds = nlohmann::json::array();
    nlohmann::json unsafePatchConstantIds = nlohmann::json::array();
    nlohmann::json missingOperationConstantIds = nlohmann::json::array();
    nlohmann::json bundledPatchRecords = nlohmann::json::array();
    nlohmann::json templateJsonPatch = nlohmann::json::array();

    if (recordReports.is_array()) {
        for (const nlohmann::json& record : recordReports) {
            if (!record.is_object()) {
                continue;
            }
            ++constantCount;
            const std::string id = record.value("id", "");
            const nlohmann::json patch =
                record.value(
                    "suggestedReviewEvidencePatch",
                    nlohmann::json::object());
            if (!patch.is_object() || patch.empty()) {
                ++missingPatchCount;
                missingPatchConstantIds.push_back(id);
                continue;
            }

            const bool available = patch.value("available", false);
            const bool readyForCopy = patch.value("readyForCopy", false);
            const bool mutatesAutomatically =
                patch.value("mutatesTemplateAutomatically", true);
            const bool patchAppliesAutomatically =
                patch.value("patchAppliesAutomatically", true);
            if (!available) {
                ++unavailablePatchCount;
                unavailableConstantIds.push_back(id);
                continue;
            }
            if (!readyForCopy) {
                ++blockedPatchCount;
                blockedConstantIds.push_back(id);
                continue;
            }
            readyForCopyConstantIds.push_back(id);
            if (mutatesAutomatically || patchAppliesAutomatically) {
                ++unsafePatchCount;
                unsafePatchConstantIds.push_back(id);
                continue;
            }

            const nlohmann::json operations =
                JsonArrayOrEmpty(
                    patch.value(
                        "templateJsonPatch",
                        nlohmann::json::array()));
            if (operations.empty()) {
                ++missingOperationPatchCount;
                missingOperationConstantIds.push_back(id);
                continue;
            }

            const int operationStart = bundledOperationCount;
            for (const nlohmann::json& operation : operations) {
                templateJsonPatch.push_back(operation);
                ++bundledOperationCount;
            }
            ++bundledConstantCount;
            bundledConstantIds.push_back(id);
            bundledPatchRecords.push_back({
                { "constantId", id },
                { "constantPath", patch.value("constantPath", "") },
                { "jsonPointerBase", patch.value("jsonPointerBase", "") },
                { "operationStartIndex", operationStart },
                { "operationCount", operations.size() },
                { "evidenceRecordIds",
                  patch.value("reviewFields", nlohmann::json::object())
                      .value("evidenceRecordIds", nlohmann::json::array()) }
            });
        }
    }

    const bool available = bundledOperationCount > 0;
    const bool readyForPartialCopy =
        available && unsafePatchCount == 0 && missingOperationPatchCount == 0;
    const bool completeForAllConstants =
        constantCount > 0 &&
        bundledConstantCount == constantCount &&
        blockedPatchCount == 0 &&
        unavailablePatchCount == 0 &&
        missingPatchCount == 0 &&
        unsafePatchCount == 0 &&
        missingOperationPatchCount == 0;

    return {
        { "available", available },
        { "readyForPartialCopy", readyForPartialCopy },
        { "completeForAllConstants", completeForAllConstants },
        { "advisoryOnly", true },
        { "source", "records[].suggestedReviewEvidencePatch.templateJsonPatch" },
        { "targetReviewObject", "records.*.review" },
        { "targetReviewTemplateFile", reviewTemplatePath.string() },
        { "jsonPatchFormat", "RFC 6902-compatible advisory operations" },
        { "mutatesTemplateAutomatically", false },
        { "patchAppliesAutomatically", false },
        { "constantCount", constantCount },
        { "bundledConstantCount", bundledConstantCount },
        { "bundledOperationCount", bundledOperationCount },
        { "blockedPatchCount", blockedPatchCount },
        { "unavailablePatchCount", unavailablePatchCount },
        { "missingPatchCount", missingPatchCount },
        { "unsafePatchCount", unsafePatchCount },
        { "missingOperationPatchCount", missingOperationPatchCount },
        { "readyForCopyConstantIds", std::move(readyForCopyConstantIds) },
        { "bundledConstantIds", std::move(bundledConstantIds) },
        { "blockedConstantIds", std::move(blockedConstantIds) },
        { "unavailableConstantIds", std::move(unavailableConstantIds) },
        { "missingPatchConstantIds", std::move(missingPatchConstantIds) },
        { "unsafePatchConstantIds", std::move(unsafePatchConstantIds) },
        { "missingOperationConstantIds",
          std::move(missingOperationConstantIds) },
        { "fieldsIncluded", nlohmann::json::array({
            "review.evidenceRecordIds",
            "review.categoryEvidenceReviewed",
            "review.stageEvidenceReviewed",
            "review.humanReviewSignalSummary"
        }) },
        { "fieldsIntentionallyNotIncluded", nlohmann::json::array({
            "review.decision",
            "review.reviewer",
            "review.reviewDate",
            "review.proposedValue",
            "review.rationale",
            "review.visibleControlImpact",
            "review.readyToApplyInFuturePass"
        }) },
        { "templateJsonPatch", std::move(templateJsonPatch) },
        { "patchRecords", std::move(bundledPatchRecords) },
        { "instructions",
          "This bundle concatenates ready evidence-only templateJsonPatch operations for reviewer convenience. It is advisory, may be partial when some constants are blocked, and is never applied automatically by this command. After manually copying operations into a review template, fill reviewer, rationale, proposedValue, decision, visibleControlImpact, and readyToApplyInFuturePass manually, then rerun the checker with --require-ready before any tuning pass." }
    };
}

nlohmann::json BuildConstantReviewEvidenceCoverage(
    const nlohmann::json& constantStatus,
    const nlohmann::json& evidenceRecordIds,
    const ConstantReviewEvidenceIndex& evidenceIndex) {
    const std::set<std::string> requiredCategories =
        JsonStringSet(constantStatus.value("requiredCategories", nlohmann::json::array()));
    const std::set<std::string> requiredStages =
        JsonStringSet(constantStatus.value("requiredStages", nlohmann::json::array()));
    const std::set<std::string> requiredHumanReviewSignals =
        JsonStringSet(constantStatus.value("humanReviewSignals", nlohmann::json::array()));

    std::set<std::string> coveredCategories;
    std::set<std::string> coveredStages;
    std::set<std::string> coveredHumanReviewSignals;
    nlohmann::json citedRecords = nlohmann::json::array();
    int citedKnownRecordCount = 0;

    if (evidenceRecordIds.is_array() && evidenceIndex.available) {
        for (const nlohmann::json& item : evidenceRecordIds) {
            if (!item.is_string()) {
                continue;
            }
            const std::string citedId = item.get<std::string>();
            const ConstantReviewValidationRecordEvidence* recordEvidence =
                FindConstantReviewEvidenceRecord(evidenceIndex, citedId);
            if (recordEvidence == nullptr) {
                continue;
            }
            ++citedKnownRecordCount;

            nlohmann::json recordCategories = nlohmann::json::array();
            const nlohmann::json tags =
                recordEvidence->record.value("imageCategoryTags", nlohmann::json::array());
            if (tags.is_array()) {
                for (const nlohmann::json& tag : tags) {
                    if (tag.is_string() && !tag.get<std::string>().empty()) {
                        const std::string category = tag.get<std::string>();
                        coveredCategories.insert(category);
                        recordCategories.push_back(category);
                    }
                }
            }

            nlohmann::json recordStages = nlohmann::json::array();
            for (const RequiredStageEvidenceSpec& stage : RequiredStageEvidenceSpecs()) {
                if (RecordHasCompleteStageDiagnostics(recordEvidence->record, stage.id)) {
                    coveredStages.insert(stage.id);
                    recordStages.push_back(stage.id);
                }
            }

            nlohmann::json recordHumanReviewSignals = nlohmann::json::array();
            for (const std::string& signal : requiredHumanReviewSignals) {
                if (RecordHasCompleteHumanReviewSignal(recordEvidence->record, signal)) {
                    coveredHumanReviewSignals.insert(signal);
                    recordHumanReviewSignals.push_back(signal);
                }
            }

            citedRecords.push_back({
                { "citedId", citedId },
                { "recordId", recordEvidence->canonicalId },
                { "imageCategoryTags", std::move(recordCategories) },
                { "completeStages", std::move(recordStages) },
                { "completeHumanReviewSignals", std::move(recordHumanReviewSignals) }
            });
        }
    }

    nlohmann::json missingCategories;
    nlohmann::json missingStages;
    nlohmann::json missingHumanReviewSignals;
    AddMissingStrings(requiredCategories, coveredCategories, missingCategories);
    AddMissingStrings(requiredStages, coveredStages, missingStages);
    AddMissingStrings(
        requiredHumanReviewSignals,
        coveredHumanReviewSignals,
        missingHumanReviewSignals);

    const bool categoryCoverageComplete = missingCategories.empty();
    const bool stageCoverageComplete = missingStages.empty();
    const bool humanReviewSignalCoverageComplete =
        missingHumanReviewSignals.empty();

    return {
        { "available", evidenceIndex.available },
        { "citedKnownRecordCount", citedKnownRecordCount },
        { "requiredCategories", StringSetToJsonArray(requiredCategories) },
        { "coveredCategories", StringSetToJsonArray(coveredCategories) },
        { "missingRequiredCategories", std::move(missingCategories) },
        { "requiredStages", StringSetToJsonArray(requiredStages) },
        { "coveredStages", StringSetToJsonArray(coveredStages) },
        { "missingRequiredStages", std::move(missingStages) },
        { "requiredHumanReviewSignals", StringSetToJsonArray(requiredHumanReviewSignals) },
        { "coveredHumanReviewSignals", StringSetToJsonArray(coveredHumanReviewSignals) },
        { "missingHumanReviewSignals", std::move(missingHumanReviewSignals) },
        { "categoryCoverageComplete", categoryCoverageComplete },
        { "stageCoverageComplete", stageCoverageComplete },
        { "humanReviewSignalCoverageComplete", humanReviewSignalCoverageComplete },
        { "complete",
          evidenceIndex.available &&
              citedKnownRecordCount > 0 &&
              categoryCoverageComplete &&
              stageCoverageComplete &&
              humanReviewSignalCoverageComplete },
        { "citedRecords", std::move(citedRecords) }
    };
}

nlohmann::json BuildConstantReviewCheckReport(
    const nlohmann::json& summaryReport,
    const nlohmann::json& reviewTemplate,
    const RawStartingPointConstantReviewCheckOptions& options,
    const ConstantReviewEvidenceIndex& evidenceIndex) {
    const nlohmann::json summaryCandidate =
        summaryReport.is_object()
            ? summaryReport.value("validationSetSummary", nlohmann::json::object())
            : nlohmann::json::object();
    const nlohmann::json summary =
        summaryCandidate.is_object() ? summaryCandidate : nlohmann::json::object();
    const nlohmann::json readinessCandidate =
        summary.value("tuningReadiness", nlohmann::json::object());
    const nlohmann::json readiness =
        readinessCandidate.is_object() ? readinessCandidate : nlohmann::json::object();
    const nlohmann::json constantEvidenceCandidate =
        summary.value("constantTuningEvidence", nlohmann::json::object());
    const nlohmann::json constantEvidence =
        constantEvidenceCandidate.is_object()
            ? constantEvidenceCandidate
            : nlohmann::json::object();
    const nlohmann::json expectedConstantsCandidate =
        constantEvidence.value("constants", nlohmann::json::array());
    const bool expectedConstantsArray = expectedConstantsCandidate.is_array();
    const nlohmann::json expectedConstants =
        expectedConstantsArray
            ? expectedConstantsCandidate
            : nlohmann::json::array();
    const nlohmann::json reviewRecordsCandidate =
        reviewTemplate.is_object()
            ? reviewTemplate.value("records", nlohmann::json::object())
            : nlohmann::json::object();
    const bool reviewRecordsObject = reviewRecordsCandidate.is_object();
    const nlohmann::json reviewRecords =
        reviewRecordsObject
            ? reviewRecordsCandidate
            : nlohmann::json::object();
    const std::string summarySchema =
        summaryReport.is_object() ? summaryReport.value("schema", "") : "";
    const int summaryVersion =
        summaryReport.is_object() ? summaryReport.value("version", 0) : 0;
    const std::string summarySourcePath =
        summaryReport.is_object() ? summaryReport.value("sourceReportPath", "") : "";
    const std::string reviewSchema =
        reviewTemplate.is_object() ? reviewTemplate.value("schema", "") : "";
    const int reviewVersion =
        reviewTemplate.is_object() ? reviewTemplate.value("version", 0) : 0;
    const std::string reviewSourcePath =
        reviewTemplate.is_object()
            ? reviewTemplate.value("sourceSummaryReportPath", "")
            : "";

    std::set<std::string> expectedConstantIds;
    if (expectedConstants.is_array()) {
        for (const nlohmann::json& constant : expectedConstants) {
            const std::string id = constant.value("id", "");
            if (!id.empty()) {
                expectedConstantIds.insert(id);
            }
        }
    }

    nlohmann::json blockers = nlohmann::json::array();
    if (summarySchema != "stack.raw-starting-point.validation-summary-report") {
        blockers.push_back("Summary file is not a RAW Starting Point validation summary report.");
    }
    if (summaryVersion < 14) {
        blockers.push_back("Summary report schema is older than the constant-review checker workflow.");
    }
    if (reviewSchema != "stack.raw-starting-point.constant-review-template") {
        blockers.push_back("Review file is not a RAW Starting Point constant-review template.");
    }
    if (reviewVersion < 2) {
        blockers.push_back("Constant-review template schema is older than the checker workflow.");
    }
    if (!expectedConstantsArray) {
        blockers.push_back("Summary constant evidence has no constants array.");
    }
    if (expectedConstants.empty()) {
        blockers.push_back("Summary constant evidence contains no expected constants.");
    }
    if (!reviewRecordsObject) {
        blockers.push_back("Constant-review template records must be an object keyed by constant id.");
    }

    if (!reviewSourcePath.empty() &&
        !summarySourcePath.empty() &&
        reviewSourcePath != summarySourcePath) {
        blockers.push_back("Review template was generated from a different validation records source than the summary.");
    }

    const bool mechanicalInputsComplete =
        readiness.value("mechanicalInputsComplete", false);
    const bool readyForFullTuningReview =
        constantEvidence.value("readyForFullTuningReview", false);
    if (!mechanicalInputsComplete) {
        blockers.push_back("Validation summary mechanical inputs are not ready.");
    }
    if (!readyForFullTuningReview) {
        blockers.push_back("Validation summary constant evidence is not ready for full tuning review.");
    }
    if (!evidenceIndex.available) {
        blockers.push_back("Validation record IDs could not be loaded; changed constants cannot be mechanically tied to record evidence.");
    }

    nlohmann::json recordReports = nlohmann::json::array();
    nlohmann::json missingExpectedConstants = nlohmann::json::array();
    nlohmann::json extraReviewConstants = nlohmann::json::array();
    nlohmann::json decisionCounts = nlohmann::json::object();
    std::set<std::string> unknownRecordIds;

    int reviewedConstantCount = 0;
    int changedConstantCount = 0;
    int keptConstantCount = 0;
    int deferredConstantCount = 0;
    int incompleteConstantCount = 0;
    int invalidDecisionCount = 0;
    int readyChangeCount = 0;
    int coverageBlockedChangeCount = 0;

    if (reviewRecordsObject) {
        for (auto it = reviewRecords.begin(); it != reviewRecords.end(); ++it) {
            if (expectedConstantIds.find(it.key()) == expectedConstantIds.end()) {
                extraReviewConstants.push_back(it.key());
            }
        }
    }

    if (!extraReviewConstants.empty()) {
        blockers.push_back("Constant-review template contains unknown constant IDs.");
    }

    if (expectedConstantsArray) {
        for (const nlohmann::json& constantStatus : expectedConstants) {
            if (!constantStatus.is_object()) {
                continue;
            }
            const std::string id = constantStatus.value("id", "");
            if (id.empty()) {
                continue;
            }
            nlohmann::json evidenceRecordSuggestions =
                BuildConstantReviewEvidenceRecordSuggestions(
                    constantStatus,
                    evidenceIndex);
            nlohmann::json evidenceRecordCoveragePlan =
                BuildConstantReviewEvidenceCoveragePlan(
                    constantStatus,
                    evidenceIndex);
            nlohmann::json suggestedReviewEvidencePatch =
                BuildConstantReviewSuggestedEvidencePatch(
                    constantStatus,
                    evidenceRecordCoveragePlan);

            const bool hasReviewRecord =
                reviewRecords.is_object() &&
                reviewRecords.contains(id) &&
                reviewRecords[id].is_object();
            if (!hasReviewRecord) {
                missingExpectedConstants.push_back(id);
                ++incompleteConstantCount;
                recordReports.push_back({
                    { "id", id },
                    { "status", "missing-review-record" },
                    { "readyForFutureTuningPass", false },
                    { "evidenceRecordSuggestions", std::move(evidenceRecordSuggestions) },
                    { "evidenceRecordCoveragePlan", std::move(evidenceRecordCoveragePlan) },
                    { "suggestedReviewEvidencePatch", std::move(suggestedReviewEvidencePatch) },
                    { "blockingReasons", nlohmann::json::array({
                        "Missing review record for expected constant."
                    }) }
                });
                continue;
            }

            const nlohmann::json& record = reviewRecords[id];
            const nlohmann::json reviewCandidate =
                record.value("review", nlohmann::json::object());
            const nlohmann::json review =
                reviewCandidate.is_object()
                    ? reviewCandidate
                    : nlohmann::json::object();
            const std::string decision = review.value("decision", "");
            nlohmann::json missingReviewFields = nlohmann::json::array();
            nlohmann::json recordBlockers = nlohmann::json::array();
            nlohmann::json unknownEvidenceIds = nlohmann::json::array();
            nlohmann::json unknownCounterexampleIds = nlohmann::json::array();
            nlohmann::json evidenceRecordCoverage =
                BuildConstantReviewEvidenceCoverage(
                    constantStatus,
                    review.value("evidenceRecordIds", nlohmann::json::array()),
                    evidenceIndex);

            const bool decisionEmpty = decision.empty();
            const bool decisionAllowed =
                decision == "keep" || decision == "change" || decision == "defer";
            if (decisionEmpty) {
                missingReviewFields.push_back("review.decision");
                recordBlockers.push_back("Review decision is empty.");
            } else if (!decisionAllowed) {
                ++invalidDecisionCount;
                recordBlockers.push_back("Review decision must be keep, change, or defer.");
            } else {
                ++reviewedConstantCount;
                IncrementJsonCount(decisionCounts, decision);
                if (decision == "change") {
                    ++changedConstantCount;
                } else if (decision == "keep") {
                    ++keptConstantCount;
                } else if (decision == "defer") {
                    ++deferredConstantCount;
                    recordBlockers.push_back("Review decision deferred this constant; leave it unchanged until evidence is ready.");
                }
            }

            auto requireReviewString = [&](const char* field) {
                if (!JsonHasNonEmptyString(review, field)) {
                    missingReviewFields.push_back(std::string("review.") + field);
                }
            };
            if (!decisionEmpty && decisionAllowed) {
                requireReviewString("reviewer");
                requireReviewString("reviewDate");
                requireReviewString("rationale");
            }

            const bool readyForTuningReview =
                constantStatus.value("readyForTuningReview", false);
            const bool isChange = decision == "change";
            if (isChange) {
                if (!mechanicalInputsComplete || !readyForFullTuningReview) {
                    recordBlockers.push_back("Summary readiness is blocked; do not change this constant yet.");
                }
                if (!readyForTuningReview) {
                    recordBlockers.push_back("Per-constant evidence is not ready for tuning review.");
                }
                if (!review.contains("proposedValue") ||
                    review["proposedValue"].is_null() ||
                    !review["proposedValue"].is_number()) {
                    missingReviewFields.push_back("review.proposedValue");
                }
                if (!review.value("readyToApplyInFuturePass", false)) {
                    missingReviewFields.push_back("review.readyToApplyInFuturePass");
                }
                requireReviewString("visibleControlImpact");
                if (!JsonArrayHasNonEmptyString(review.value("evidenceRecordIds", nlohmann::json::array()))) {
                    missingReviewFields.push_back("review.evidenceRecordIds");
                }
                if (!JsonArrayHasNonEmptyString(review.value("categoryEvidenceReviewed", nlohmann::json::array()))) {
                    missingReviewFields.push_back("review.categoryEvidenceReviewed");
                }
                if (!JsonArrayHasNonEmptyString(review.value("stageEvidenceReviewed", nlohmann::json::array()))) {
                    missingReviewFields.push_back("review.stageEvidenceReviewed");
                }
            }

            auto validateRecordIds = [&](
                const nlohmann::json& ids,
                nlohmann::json& unknownIds) {
                if (!ids.is_array() || !evidenceIndex.available) {
                    return;
                }
                for (const nlohmann::json& item : ids) {
                    if (!item.is_string()) {
                        continue;
                    }
                    const std::string citedId = item.get<std::string>();
                    if (citedId.empty()) {
                        continue;
                    }
                    if (evidenceIndex.knownRecordIds.find(citedId) ==
                            evidenceIndex.knownRecordIds.end() &&
                        evidenceIndex.knownRecordIds.find(NormalizeAnnotationKey(citedId)) ==
                            evidenceIndex.knownRecordIds.end()) {
                        unknownIds.push_back(citedId);
                        unknownRecordIds.insert(citedId);
                    }
                }
            };
            validateRecordIds(
                review.value("evidenceRecordIds", nlohmann::json::array()),
                unknownEvidenceIds);
            validateRecordIds(
                review.value("counterexampleRecordIds", nlohmann::json::array()),
                unknownCounterexampleIds);
            if (isChange && !unknownEvidenceIds.empty()) {
                recordBlockers.push_back("Changed constant cites unknown evidence record IDs.");
            }
            if (!unknownCounterexampleIds.empty()) {
                recordBlockers.push_back("Review cites unknown counterexample record IDs.");
            }
            if (isChange && !evidenceIndex.available) {
                recordBlockers.push_back("Changed constant requires source validation records so evidence IDs can be checked.");
            }
            if (isChange && evidenceIndex.available) {
                if (evidenceRecordCoverage.value("citedKnownRecordCount", 0) <= 0) {
                    recordBlockers.push_back("Changed constant cites no known validation records.");
                    ++coverageBlockedChangeCount;
                } else if (!evidenceRecordCoverage.value("complete", false)) {
                    recordBlockers.push_back("Changed constant cited records do not cover every required category, stage, and human-review signal.");
                    ++coverageBlockedChangeCount;
                }
            }

            if (!missingReviewFields.empty()) {
                recordBlockers.push_back("Review fields are incomplete.");
            }

            const bool recordReady =
                decisionAllowed &&
                !decisionEmpty &&
                recordBlockers.empty();
            if (!recordReady) {
                ++incompleteConstantCount;
            }
            if (isChange && recordReady) {
                ++readyChangeCount;
            }

            recordReports.push_back({
                { "id", id },
                { "label", constantStatus.value("label", "") },
                { "path", constantStatus.value("path", "") },
                { "ownerControl", constantStatus.value("ownerControl", "") },
                { "decision", decision },
                { "status", recordReady ? "review-ready" : "review-blocked" },
                { "readyForTuningReview", readyForTuningReview },
                { "readyForFutureTuningPass", recordReady },
                { "missingReviewFields", std::move(missingReviewFields) },
                { "unknownEvidenceRecordIds", std::move(unknownEvidenceIds) },
                { "unknownCounterexampleRecordIds", std::move(unknownCounterexampleIds) },
                { "evidenceRecordCoverage", std::move(evidenceRecordCoverage) },
                { "evidenceRecordSuggestions", std::move(evidenceRecordSuggestions) },
                { "evidenceRecordCoveragePlan", std::move(evidenceRecordCoveragePlan) },
                { "suggestedReviewEvidencePatch", std::move(suggestedReviewEvidencePatch) },
                { "blockingReasons", std::move(recordBlockers) }
            });
        }
    }

    if (!missingExpectedConstants.empty()) {
        blockers.push_back("Constant-review template is missing expected constant IDs.");
    }
    if (incompleteConstantCount > 0) {
        blockers.push_back("Some constant reviews are incomplete or blocked.");
    }
    if (!unknownRecordIds.empty()) {
        blockers.push_back("Some cited record IDs do not exist in the source validation records.");
    }
    if (coverageBlockedChangeCount > 0) {
        blockers.push_back("Some changed constants cite records without required category, stage, or human-review coverage.");
    }

    const bool readyForTuningPass =
        blockers.empty() &&
        reviewedConstantCount == static_cast<int>(expectedConstantIds.size()) &&
        deferredConstantCount == 0;
    const nlohmann::json suggestedReviewEvidencePatchSummary =
        BuildConstantReviewSuggestedPatchSummary(recordReports);
    const nlohmann::json suggestedReviewEvidencePatchBundle =
        BuildConstantReviewSuggestedPatchBundle(
            recordReports,
            options.reviewTemplatePath);

    return {
        { "schema", "stack.raw-starting-point.constant-review-check" },
        { "version", kConstantReviewCheckSchemaVersion },
        { "summaryReportFile", options.summaryPath.string() },
        { "constantReviewTemplateFile", options.reviewTemplatePath.string() },
        { "outputFile", options.outputPath.string() },
        { "repairOutputFile", options.repairOutputPath.string() },
        { "suggestedReviewPatchBundleOutputFile",
          options.suggestedReviewPatchBundleOutputPath.string() },
        { "sourceValidationRecordsFile", evidenceIndex.sourceReportPath },
        { "summarySchemaVersion", summaryVersion },
        { "validationSetSummarySchemaVersion", summary.value("version", 0) },
        { "constantReviewTemplateSchemaVersion", reviewVersion },
        { "requireReady", options.requireReady },
        { "behaviorChanged", false },
        { "constantsTunedByCheck", false },
        { "recordIdValidation", {
            { "available", evidenceIndex.available },
            { "knownRecordCount", evidenceIndex.records.size() },
            { "knownRecordIdAliasCount", evidenceIndex.knownRecordIds.size() },
            { "unavailableReason", evidenceIndex.unavailableReason },
            { "unknownRecordIds", StringSetToJsonArray(unknownRecordIds) }
        } },
        { "evidenceRecordCatalog",
          BuildConstantReviewEvidenceRecordCatalog(evidenceIndex) },
        { "summaryReadiness", {
            { "mechanicalInputsComplete", mechanicalInputsComplete },
            { "readyForFullTuningReview", readyForFullTuningReview },
            { "summaryBlockingReasons",
              readiness.value("blockingReasons", nlohmann::json::array()) }
        } },
        { "constantCoverage", {
            { "expectedConstantCount", expectedConstantIds.size() },
            { "reviewRecordCount", reviewRecords.is_object() ? reviewRecords.size() : 0 },
            { "missingExpectedConstants", missingExpectedConstants },
            { "extraReviewConstants", extraReviewConstants }
        } },
        { "reviewSummary", {
            { "reviewedConstantCount", reviewedConstantCount },
            { "changedConstantCount", changedConstantCount },
            { "readyChangeCount", readyChangeCount },
            { "keptConstantCount", keptConstantCount },
            { "deferredConstantCount", deferredConstantCount },
            { "incompleteConstantCount", incompleteConstantCount },
            { "invalidDecisionCount", invalidDecisionCount },
            { "coverageBlockedChangeCount", coverageBlockedChangeCount },
            { "decisionCounts", decisionCounts }
        } },
        { "suggestedReviewEvidencePatchSummary",
          suggestedReviewEvidencePatchSummary },
        { "suggestedReviewEvidencePatchBundle",
          suggestedReviewEvidencePatchBundle },
        { "readiness", {
            { "readyForTuningPass", readyForTuningPass },
            { "blockingReasons", blockers },
            { "nextAction", readyForTuningPass
                ? "A later tuning pass may review changed constants and apply source changes deliberately; this check has not changed them."
                : "Resolve blockingReasons before any tuning pass changes constants." }
        } },
        { "instructions",
          "This report validates the filled constant-review template mechanically, suggests known records, advisory coverage plans, copyable per-constant evidence-only patches, an advisory bundled JSON Patch for ready evidence-only fields, and aggregate suggested-patch readiness for each constant, and checks that changed constants cite records covering required categories, stages, and human-review signals. When requested, the same advisory bundle is also saved as a standalone helper file. This command does not tune constants, change recipes, touch render output, load RAW buffers, or mutate review templates automatically." },
        { "records", std::move(recordReports) }
    };
}

nlohmann::json BuildConstantReviewRepairRecord(
    const nlohmann::json& checkRecord,
    const nlohmann::json& constantStatus,
    const nlohmann::json& templateRecord) {
    auto firstString = [&](
        const char* field,
        const char* fallback = "") -> std::string {
        const std::string fromCheck = JsonOptionalString(checkRecord, field);
        if (!fromCheck.empty()) {
            return fromCheck;
        }
        const std::string fromTemplate = JsonOptionalString(templateRecord, field);
        if (!fromTemplate.empty()) {
            return fromTemplate;
        }
        const std::string fromConstant = JsonOptionalString(constantStatus, field);
        if (!fromConstant.empty()) {
            return fromConstant;
        }
        return fallback != nullptr ? std::string(fallback) : std::string();
    };
    auto firstJson = [&](
        const char* field,
        nlohmann::json fallback = nlohmann::json()) -> nlohmann::json {
        if (templateRecord.is_object() && templateRecord.contains(field)) {
            return templateRecord[field];
        }
        if (constantStatus.is_object() && constantStatus.contains(field)) {
            return constantStatus[field];
        }
        return fallback;
    };

    const nlohmann::json reviewCandidate =
        templateRecord.is_object()
            ? templateRecord.value("review", nlohmann::json::object())
            : nlohmann::json::object();
    const nlohmann::json review =
        reviewCandidate.is_object()
            ? reviewCandidate
            : BuildConstantReviewBlankReview();
    const std::string status = firstString("status", "review-blocked");
    const bool readyForTuningReview =
        checkRecord.is_object() && checkRecord.contains("readyForTuningReview")
            ? checkRecord.value("readyForTuningReview", false)
            : constantStatus.value("readyForTuningReview", false);

    return {
        { "id", firstString("id") },
        { "group", firstString("group") },
        { "path", firstString("path") },
        { "label", firstString("label") },
        { "currentEngineeringDefault", firstJson("currentEngineeringDefault") },
        { "ownerControl", firstString("ownerControl") },
        { "evidenceFocus", firstString("evidenceFocus") },
        { "requiredEvidenceItems", firstJson("requiredEvidenceItems", nlohmann::json::array()) },
        { "requiredCategories", firstJson("requiredCategories", nlohmann::json::array()) },
        { "missingRequiredCategories", firstJson("missingRequiredCategories", nlohmann::json::array()) },
        { "requiredStages", firstJson("requiredStages", nlohmann::json::array()) },
        { "missingRequiredStages", firstJson("missingRequiredStages", nlohmann::json::array()) },
        { "humanReviewSignals", firstJson("humanReviewSignals", nlohmann::json::array()) },
        { "tuningQuestion", firstString("tuningQuestion") },
        { "readyForTuningReview", readyForTuningReview },
        { "tuningState", firstString("tuningState", "blocked-pending-validation-evidence") },
        { "globalBlockingReasons", firstJson("globalBlockingReasons", nlohmann::json::array()) },
        { "status", status },
        { "repairNeeded", true },
        { "readyForFutureTuningPass", checkRecord.value("readyForFutureTuningPass", false) },
        { "check", {
            { "decision", checkRecord.value("decision", "") },
            { "status", status },
            { "missingReviewFields",
              checkRecord.value("missingReviewFields", nlohmann::json::array()) },
            { "unknownEvidenceRecordIds",
              checkRecord.value("unknownEvidenceRecordIds", nlohmann::json::array()) },
            { "unknownCounterexampleRecordIds",
              checkRecord.value("unknownCounterexampleRecordIds", nlohmann::json::array()) },
            { "evidenceRecordCoverage",
              checkRecord.value("evidenceRecordCoverage", nlohmann::json::object()) },
            { "evidenceRecordSuggestions",
              checkRecord.value("evidenceRecordSuggestions", nlohmann::json::object()) },
            { "evidenceRecordCoveragePlan",
              checkRecord.value("evidenceRecordCoveragePlan", nlohmann::json::object()) },
            { "suggestedReviewEvidencePatch",
              checkRecord.value("suggestedReviewEvidencePatch", nlohmann::json::object()) },
            { "blockingReasons",
              checkRecord.value("blockingReasons", nlohmann::json::array()) }
        } },
        { "evidenceRecordSuggestions",
          checkRecord.value("evidenceRecordSuggestions", nlohmann::json::object()) },
        { "evidenceRecordCoveragePlan",
          checkRecord.value("evidenceRecordCoveragePlan", nlohmann::json::object()) },
        { "suggestedReviewEvidencePatch",
          checkRecord.value("suggestedReviewEvidencePatch", nlohmann::json::object()) },
        { "review", review },
        { "reviewInstructions", templateRecord.value(
            "reviewInstructions",
            std::string("Fill or repair review fields with known validation record IDs, then rerun the constant-review checker.")) },
        { "repairInstructions",
          "Copy this record into the constant-review template record for this constant, fill missing review fields, replace unknown record IDs with known validation record IDs, use suggestedReviewEvidencePatch.reviewFields as an evidence-only patch when it matches real reviewed records, then rerun --check-raw-starting-point-constant-review --require-ready." }
    };
}

nlohmann::json BuildConstantReviewRepairOutputReport(
    const RawStartingPointConstantReviewCheckOptions& options,
    const nlohmann::json& summaryReport,
    const nlohmann::json& reviewTemplate,
    const nlohmann::json& checkReport) {
    const nlohmann::json summary =
        summaryReport.is_object()
            ? summaryReport.value("validationSetSummary", nlohmann::json::object())
            : nlohmann::json::object();
    const nlohmann::json constantEvidence =
        summary.value("constantTuningEvidence", nlohmann::json::object());
    const nlohmann::json constants =
        constantEvidence.value("constants", nlohmann::json::array());
    const nlohmann::json templateRecordsCandidate =
        reviewTemplate.is_object()
            ? reviewTemplate.value("records", nlohmann::json::object())
            : nlohmann::json::object();
    const nlohmann::json templateRecords =
        templateRecordsCandidate.is_object()
            ? templateRecordsCandidate
            : nlohmann::json::object();
    const nlohmann::json checkRecords =
        checkReport.value("records", nlohmann::json::array());

    nlohmann::json constantStatusById = nlohmann::json::object();
    if (constants.is_array()) {
        for (const nlohmann::json& constant : constants) {
            if (!constant.is_object()) {
                continue;
            }
            const std::string id = constant.value("id", "");
            if (!id.empty()) {
                constantStatusById[id] = constant;
            }
        }
    }

    nlohmann::json repairRecords = nlohmann::json::object();
    if (checkRecords.is_array()) {
        for (const nlohmann::json& checkRecord : checkRecords) {
            if (!checkRecord.is_object()) {
                continue;
            }
            const std::string id = checkRecord.value("id", "");
            if (id.empty() || checkRecord.value("readyForFutureTuningPass", false)) {
                continue;
            }
            const nlohmann::json constantStatus =
                constantStatusById.contains(id) && constantStatusById[id].is_object()
                    ? constantStatusById[id]
                    : nlohmann::json::object();
            const nlohmann::json templateRecord =
                templateRecords.contains(id) && templateRecords[id].is_object()
                    ? templateRecords[id]
                    : nlohmann::json::object();
            repairRecords[id] =
                BuildConstantReviewRepairRecord(
                    checkRecord,
                    constantStatus,
                    templateRecord);
        }
    }

    const nlohmann::json readiness =
        checkReport.value("readiness", nlohmann::json::object());
    const nlohmann::json reviewSummary =
        checkReport.value("reviewSummary", nlohmann::json::object());

    return {
        { "schema", "stack.raw-starting-point.constant-review-repair" },
        { "version", kConstantReviewRepairSchemaVersion },
        { "summaryReportFile", options.summaryPath.string() },
        { "constantReviewTemplateFile", options.reviewTemplatePath.string() },
        { "constantReviewCheckFile", options.outputPath.string() },
        { "repairOutputFile", options.repairOutputPath.string() },
        { "suggestedReviewPatchBundleOutputFile",
          options.suggestedReviewPatchBundleOutputPath.string() },
        { "checkReportSchemaVersion", checkReport.value("version", 0) },
        { "summaryReportSchemaVersion", checkReport.value("summarySchemaVersion", 0) },
        { "validationSetSummarySchemaVersion",
          checkReport.value("validationSetSummarySchemaVersion", 0) },
        { "constantReviewTemplateSchemaVersion",
          checkReport.value("constantReviewTemplateSchemaVersion", 0) },
        { "readyForTuningPass", readiness.value("readyForTuningPass", false) },
        { "constantsTunedByRepair", false },
        { "behaviorChanged", false },
        { "repairAvailable", !repairRecords.empty() },
        { "repairRecordCount", repairRecords.size() },
        { "changedConstantCount", reviewSummary.value("changedConstantCount", 0) },
        { "incompleteConstantCount", reviewSummary.value("incompleteConstantCount", 0) },
        { "coverageBlockedChangeCount", reviewSummary.value("coverageBlockedChangeCount", 0) },
        { "blockingReasons", readiness.value("blockingReasons", nlohmann::json::array()) },
        { "recordIdValidation",
          checkReport.value("recordIdValidation", nlohmann::json::object()) },
        { "suggestedReviewEvidencePatchSummary",
          checkReport.value(
              "suggestedReviewEvidencePatchSummary",
              nlohmann::json::object()) },
        { "suggestedReviewEvidencePatchBundle",
          checkReport.value(
              "suggestedReviewEvidencePatchBundle",
              nlohmann::json::object()) },
        { "evidenceRecordCatalog",
          checkReport.value("evidenceRecordCatalog", nlohmann::json::object()) },
        { "repairContract", {
            { "copyRecordsIntoConstantReviewTemplate", true },
            { "requiresRerunWithRequireReady", true },
            { "doesNotTuneConstants", true },
            { "doesNotChangeRecipesOrRenderOutput", true },
            { "readyWhen",
              "Every repaired record passes --check-raw-starting-point-constant-review --require-ready and the checker report says readyForTuningPass=true." }
        } },
        { "instructions",
          "Repair records are helper copies for blocked constant reviews. Use evidenceRecordSuggestions, evidenceRecordCoveragePlan, suggestedReviewEvidencePatch, the advisory suggestedReviewEvidencePatchBundle, or the optional standalone patch-bundle output to choose and copy evidence-only fields from source records, copy each repaired record into the constant-review template, rerun the checker with --require-ready, and do not change constants until the check passes." },
        { "records", std::move(repairRecords) }
    };
}

nlohmann::json BuildConstantReviewPatchBundleOutputReport(
    const RawStartingPointConstantReviewCheckOptions& options,
    const nlohmann::json& checkReport) {
    const nlohmann::json readiness =
        checkReport.value("readiness", nlohmann::json::object());
    const nlohmann::json patchSummary =
        checkReport.value(
            "suggestedReviewEvidencePatchSummary",
            nlohmann::json::object());
    const nlohmann::json bundleCandidate =
        checkReport.value(
            "suggestedReviewEvidencePatchBundle",
            nlohmann::json::object());
    const nlohmann::json bundle =
        bundleCandidate.is_object()
            ? bundleCandidate
            : nlohmann::json::object();

    return {
        { "schema", "stack.raw-starting-point.constant-review-patch-bundle" },
        { "version", kConstantReviewPatchBundleSchemaVersion },
        { "summaryReportFile", options.summaryPath.string() },
        { "constantReviewTemplateFile", options.reviewTemplatePath.string() },
        { "constantReviewCheckFile", options.outputPath.string() },
        { "constantReviewRepairFile", options.repairOutputPath.string() },
        { "patchBundleOutputFile",
          options.suggestedReviewPatchBundleOutputPath.string() },
        { "checkReportSchemaVersion", checkReport.value("version", 0) },
        { "requireReady", options.requireReady },
        { "readyForTuningPass",
          readiness.value("readyForTuningPass", false) },
        { "blockingReasons",
          readiness.value("blockingReasons", nlohmann::json::array()) },
        { "behaviorChanged", false },
        { "constantsTunedByPatchBundle", false },
        { "reviewTemplateMutatedByPatchBundle", false },
        { "recipesChangedByPatchBundle", false },
        { "renderOutputChangedByPatchBundle", false },
        { "rawBuffersLoadedByPatchBundle", false },
        { "bundleAvailable", bundle.value("available", false) },
        { "readyForPartialCopy",
          bundle.value("readyForPartialCopy", false) },
        { "completeForAllConstants",
          bundle.value("completeForAllConstants", false) },
        { "bundledConstantCount",
          bundle.value("bundledConstantCount", 0) },
        { "bundledOperationCount",
          bundle.value("bundledOperationCount", 0) },
        { "advisoryOnly", true },
        { "mutatesTemplateAutomatically", false },
        { "patchAppliesAutomatically", false },
        { "suggestedReviewEvidencePatchSummary", patchSummary },
        { "bundle", bundle },
        { "instructions",
          "This standalone file saves the advisory suggestedReviewEvidencePatchBundle from the constant-review check report for reviewer convenience. It does not apply JSON Patch operations, mutate the constant-review template, tune constants, change recipes, touch render output, or load RAW buffers. Reviewers must manually copy approved evidence-only operations into the template, fill the remaining review fields, and rerun --check-raw-starting-point-constant-review --require-ready before any later tuning pass changes constants." }
    };
}

std::string WorkflowPathString(
    const std::filesystem::path& path,
    const char* placeholder) {
    if (!path.empty()) {
        return path.string();
    }
    return placeholder != nullptr ? std::string(placeholder) : std::string();
}

nlohmann::json BuildWorkflowStep(
    const char* id,
    const char* label,
    const char* kind,
    const char* detail,
    const std::vector<std::string>& validationArguments) {
    nlohmann::json step = {
        { "id", id },
        { "label", label },
        { "kind", kind },
        { "detail", detail },
        { "requiredBeforeTuning", true }
    };
    if (!validationArguments.empty()) {
        step["validationArguments"] = JsonStringVector(validationArguments);
    }
    return step;
}

nlohmann::json BuildValidationWorkflowChecklist(
    const std::filesystem::path& workspaceRoot,
    const std::filesystem::path& annotationTemplatePath,
    const std::filesystem::path& annotationPath,
    const std::filesystem::path& stageEvidenceTemplatePath,
    const std::filesystem::path& stageEvidencePath,
    const std::filesystem::path& recordsPath) {
    const std::string workspace =
        WorkflowPathString(workspaceRoot, "<workspace-folder>");
    const std::string annotationTemplate =
        WorkflowPathString(annotationTemplatePath, "<annotations-template.json>");
    const std::string annotations =
        WorkflowPathString(
            annotationPath.empty() ? annotationTemplatePath : annotationPath,
            "<annotations.json>");
    const std::string stageEvidenceTemplate =
        WorkflowPathString(stageEvidenceTemplatePath, "<stage-evidence-template.json>");
    const std::string stageEvidence =
        WorkflowPathString(
            stageEvidencePath.empty() ? stageEvidenceTemplatePath : stageEvidencePath,
            "<stage-evidence.json>");
    const std::string records =
        WorkflowPathString(recordsPath, "<records.json>");
    const std::string annotationRepair = "<annotations-repair.json>";
    const std::string stageEvidenceRepair = "<stage-evidence-repair.json>";
    const std::string sidecarPreflight = "<sidecar-preflight.json>";
    const std::string recordsSummary = "<records-summary.json>";
    const std::string constantReviewTemplate = "<constant-review-template.json>";
    const std::string constantReviewCheck = "<constant-review-check.json>";
    const std::string constantReviewRepair = "<constant-review-repair.json>";
    const std::string constantReviewPatchBundle =
        "<constant-review-patch-bundle.json>";
    const std::string validationGateStatus =
        "<validation-gate-status.json>";

    nlohmann::json steps = nlohmann::json::array();
    steps.push_back(BuildWorkflowStep(
        "generate-sidecar-templates",
        "Generate sidecar templates",
        "command",
        "Scan representative RAW sources and write fillable annotation and stage-evidence sidecars.",
        {
            "--validate-raw-starting-point-records",
            workspace,
            "--templates-only",
            "--annotation-template-out",
            annotationTemplate,
            "--stage-evidence-template-out",
            stageEvidenceTemplate,
            "--out",
            "<template-generation-report.json>"
        }));
    steps.push_back(BuildWorkflowStep(
        "fill-annotations",
        "Fill annotations",
        "manual",
        "Add imageCategoryTags and complete every humanReview field for every scanned RAW source.",
        {}));
    steps.push_back(BuildWorkflowStep(
        "preflight-annotations",
        "Preflight annotations",
        "command",
        "The annotation sidecar must match scanned sources, cover every recommended category, and pass --require-ready; use --repair-out to write source-keyed patch records when blocked.",
        {
            "--check-raw-starting-point-annotations",
            workspace,
            annotations,
            "--require-ready",
            "--out",
            "<annotations-check.json>",
            "--repair-out",
            annotationRepair
        }));
    steps.push_back(BuildWorkflowStep(
        "fill-stage-evidence",
        "Fill stage evidence",
        "manual",
        "Replace placeholders with versioned Starting Point diagnostics, including candidate diagnostics and every required named stage.",
        {}));
    steps.push_back(BuildWorkflowStep(
        "preflight-stage-evidence",
        "Preflight stage evidence",
        "command",
        "The stage-evidence sidecar must match scanned sources and contain complete candidate and named-stage diagnostics; use --repair-out to write source-keyed capture placeholders when blocked.",
        {
            "--check-raw-starting-point-stage-evidence",
            workspace,
            stageEvidence,
            "--out",
            "<stage-evidence-check.json>",
            "--repair-out",
            stageEvidenceRepair
        }));
    steps.push_back(BuildWorkflowStep(
        "generate-ready-records",
        "Generate ready records",
        "command",
        "Only generate tuning records after sidecars are ready; include raw-buffer safety and require ready sidecars.",
        {
            "--validate-raw-starting-point-records",
            workspace,
            "--out",
            records,
            "--load-raw-safety",
            "--annotations",
            annotations,
            "--stage-evidence",
            stageEvidence,
            "--require-ready-sidecars",
            "--sidecar-preflight-out",
            sidecarPreflight
        }));
    steps.push_back(BuildWorkflowStep(
        "summarize-ready-records",
        "Summarize ready records",
        "command",
        "--require-ready must pass before constants are tuned from the record set.",
        {
            "--summarize-raw-starting-point-records",
            records,
            "--require-ready",
            "--out",
            recordsSummary,
            "--constant-review-template-out",
            constantReviewTemplate
        }));
    steps.push_back(BuildWorkflowStep(
        "fill-constant-review-template",
        "Fill constant review template",
        "manual",
        "After --require-ready passes, fill any proposed constant changes with reviewer, rationale, and evidence record IDs. This does not tune constants by itself.",
        {}));
    steps.push_back(BuildWorkflowStep(
        "check-constant-review-template",
        "Check constant review template",
        "command",
        "The filled review template must pass mechanical checks before any later pass changes constants; use --repair-out to write patchable review records when blocked and --suggested-review-patch-bundle-out to save the advisory evidence-only bundle separately.",
        {
            "--check-raw-starting-point-constant-review",
            recordsSummary,
            constantReviewTemplate,
            "--require-ready",
            "--out",
            constantReviewCheck,
            "--repair-out",
            constantReviewRepair,
            "--suggested-review-patch-bundle-out",
            constantReviewPatchBundle
        }));
    steps.push_back(BuildWorkflowStep(
        "check-validation-gates",
        "Check validation gates",
        "command",
        "Aggregate the existing gate artifacts into one read-only gate-status report. This does not scan RAW files, repair sidecars, mutate review templates, apply patches, or tune constants.",
        {
            "--check-raw-starting-point-validation-gates",
            "--annotation-check",
            "<annotations-check.json>",
            "--stage-evidence-check",
            "<stage-evidence-check.json>",
            "--sidecar-preflight",
            sidecarPreflight,
            "--records-summary",
            recordsSummary,
            "--constant-review-check",
            constantReviewCheck,
            "--require-ready",
            "--out",
            validationGateStatus
        }));

    return {
        { "schema", "stack.raw-starting-point.validation-workflow" },
        { "version", kValidationWorkflowSchemaVersion },
        { "behaviorChanged", false },
        { "requiresRepresentativeRealRawSources", true },
        { "constantsTunedByWorkflow", false },
        { "repairOutputs", {
            { "annotationRepair", annotationRepair },
            { "stageEvidenceRepair", stageEvidenceRepair },
            { "constantReviewRepair", constantReviewRepair },
            { "requiredBeforeTuning", false },
            { "purpose",
              "Repair outputs are helper files for blocked sidecar preflights or constant-review checks; they do not make a validation set ready until reviewers merge and rerun the checks." }
        } },
        { "recordPreflightOutput", {
            { "sidecarPreflight", sidecarPreflight },
            { "requiredBeforeTuning", true },
            { "purpose",
              "The record command can save this preflight report before RAW metadata/loading so blocked sidecars remain inspectable by automation." }
        } },
        { "constantReviewOutput", {
            { "constantReviewTemplate", constantReviewTemplate },
            { "constantReviewCheck", constantReviewCheck },
            { "constantReviewRepair", constantReviewRepair },
            { "constantReviewPatchBundle", constantReviewPatchBundle },
            { "requiredBeforeTuning", true },
            { "purpose",
              "The summary command can save a fillable reviewer template, the checker validates the filled template, --repair-out can save patchable blocked records, and --suggested-review-patch-bundle-out can save an advisory evidence-only patch bundle before any later pass changes constants." }
        } },
        { "validationGateStatusOutput", {
            { "validationGateStatus", validationGateStatus },
            { "requiredBeforeTuning", true },
            { "purpose",
              "The gate-status checker reads existing gate JSON artifacts and reports which required validation gates are missing, stale, blocked, or ready. It is read-only and cannot make a blocked validation set ready by itself." }
        } },
        { "steps", std::move(steps) }
    };
}

nlohmann::json BuildValidationArtifactSchemaVersions() {
    return {
        { "annotationTemplate", kAnnotationTemplateSchemaVersion },
        { "stageEvidenceTemplate", kStageEvidenceTemplateSchemaVersion },
        { "templateGeneration", kTemplateGenerationSchemaVersion },
        { "tuningConstantEvidenceGuide", kTuningConstantEvidenceGuideSchemaVersion },
        { "tuningConstantEvidenceStatus", kTuningConstantEvidenceStatusSchemaVersion },
        { "constantReviewTemplate", kConstantReviewTemplateSchemaVersion },
        { "constantReviewCheck", kConstantReviewCheckSchemaVersion },
        { "constantReviewRepair", kConstantReviewRepairSchemaVersion },
        { "constantReviewPatchBundle", kConstantReviewPatchBundleSchemaVersion },
        { "readinessGateCatalog", kReadinessGateCatalogSchemaVersion },
        { "validationGateStatus", kValidationGateStatusSchemaVersion },
        { "validationEvidencePackageManifest", kValidationEvidencePackageManifestSchemaVersion },
        { "validationWorkflow", kValidationWorkflowSchemaVersion },
        { "validationWorkflowReport", kValidationWorkflowReportSchemaVersion },
        { "annotationRepair", kAnnotationRepairSchemaVersion },
        { "stageEvidenceRepair", kStageEvidenceRepairSchemaVersion },
        { "annotationCheck", kAnnotationCheckSchemaVersion },
        { "stageEvidenceCheck", kStageEvidenceCheckSchemaVersion },
        { "recordSidecarPreflight", kRecordSidecarPreflightSchemaVersion },
        { "validationSetSummary", kValidationSetSummarySchemaVersion },
        { "validationRecords", kValidationRecordsSchemaVersion },
        { "validationSummaryReport", kValidationSummaryReportSchemaVersion }
    };
}

nlohmann::json BuildValidationWorkflowContractBundle() {
    return {
        { "requiresRepresentativeRealRawSources", true },
        { "annotationReadinessContract", BuildAnnotationReadinessContract() },
        { "recommendedValidationCategories", JsonStringVector(RecommendedValidationCategories()) },
        { "requiredHumanReviewFields", JsonStringVector(RequiredHumanReviewFields()) },
        { "humanReviewFieldGuide", BuildHumanReviewFieldGuide() },
        { "stageEvidenceCaptureContract", BuildStageEvidenceCaptureContract() },
        { "requiredStageEvidence", BuildStageEvidenceGuide() },
        { "constantTuningEvidenceGuide", BuildTuningConstantEvidenceGuide() },
        { "readyForTuningWhen",
          "Representative real RAW records cover every recommended category, every record has complete human review, raw-buffer safety, candidate diagnostics, every expected candidate/stage combination is complete, every required named stage is complete, and the filled constant-review template passes --check-raw-starting-point-constant-review --require-ready." }
    };
}

nlohmann::json BuildValidationReadinessGateCatalog() {
    nlohmann::json gates = nlohmann::json::array({
        {
            { "gateId", "annotation-sidecar-ready" },
            { "label", "Annotation sidecar ready" },
            { "command", "--check-raw-starting-point-annotations" },
            { "outputArtifact", "annotationCheck" },
            { "expectedSchema", "stack.raw-starting-point.validation-annotation-check" },
            { "schemaVersion", kAnnotationCheckSchemaVersion },
            { "gateStatusInputArgument", "--annotation-check" },
            { "requiredReadyJsonPointer", "/readiness/readyForRecordMerge" },
            { "requiredArguments", nlohmann::json::array({ "--require-ready" }) },
            { "manualRepairAids", nlohmann::json::array({
                "--repair-out",
                "/sidecarRepair/suggestedSidecarRepairPatch/sidecarJsonPatch"
            }) },
            { "failureExitCode", 11 },
            { "requiredBeforeTuning", true }
        },
        {
            { "gateId", "stage-evidence-sidecar-ready" },
            { "label", "Stage evidence sidecar ready" },
            { "command", "--check-raw-starting-point-stage-evidence" },
            { "outputArtifact", "stageEvidenceCheck" },
            { "expectedSchema", "stack.raw-starting-point.validation-stage-evidence-check" },
            { "schemaVersion", kStageEvidenceCheckSchemaVersion },
            { "gateStatusInputArgument", "--stage-evidence-check" },
            { "requiredReadyJsonPointer", "/readiness/readyForRecordMerge" },
            { "requiredArguments", nlohmann::json::array() },
            { "manualRepairAids", nlohmann::json::array({
                "--repair-out",
                "/sidecarRepair/suggestedSidecarRepairPatch/sidecarJsonPatch"
            }) },
            { "failureExitCode", 12 },
            { "requiredBeforeTuning", true }
        },
        {
            { "gateId", "record-sidecar-preflight-ready" },
            { "label", "Record sidecar preflight ready" },
            { "command", "--validate-raw-starting-point-records" },
            { "outputArtifact", "recordSidecarPreflight" },
            { "expectedSchema", "stack.raw-starting-point.validation-record-sidecar-preflight" },
            { "schemaVersion", kRecordSidecarPreflightSchemaVersion },
            { "gateStatusInputArgument", "--sidecar-preflight" },
            { "requiredReadyJsonPointer", "/readyForRecordGeneration" },
            { "requiredArguments", nlohmann::json::array({
                "--require-ready-sidecars",
                "--sidecar-preflight-out"
            }) },
            { "manualRepairAids", nlohmann::json::array({
                "/sidecarRepairPatchSummary",
                "/annotationCheck/sidecarRepair/suggestedSidecarRepairPatch/sidecarJsonPatch",
                "/stageEvidenceCheck/sidecarRepair/suggestedSidecarRepairPatch/sidecarJsonPatch"
            }) },
            { "failureExitCode", 9 },
            { "requiredBeforeTuning", true },
            { "preventsRecordGenerationWhenBlocked", true }
        },
        {
            { "gateId", "validation-record-summary-ready" },
            { "label", "Validation record summary ready" },
            { "command", "--summarize-raw-starting-point-records" },
            { "outputArtifact", "validationSummaryReport" },
            { "expectedSchema", "stack.raw-starting-point.validation-summary-report" },
            { "schemaVersion", kValidationSummaryReportSchemaVersion },
            { "gateStatusInputArgument", "--records-summary" },
            { "requiredReadyJsonPointer", "/validationSetSummary/tuningReadiness/mechanicalInputsComplete" },
            { "requiredArguments", nlohmann::json::array({ "--require-ready" }) },
            { "manualRepairAids", nlohmann::json::array({
                "--constant-review-template-out",
                "/validationSetSummary/evidenceChecklist"
            }) },
            { "failureExitCode", 10 },
            { "requiredBeforeTuning", true }
        },
        {
            { "gateId", "constant-review-ready" },
            { "label", "Constant review ready" },
            { "command", "--check-raw-starting-point-constant-review" },
            { "outputArtifact", "constantReviewCheck" },
            { "expectedSchema", "stack.raw-starting-point.constant-review-check" },
            { "schemaVersion", kConstantReviewCheckSchemaVersion },
            { "gateStatusInputArgument", "--constant-review-check" },
            { "requiredReadyJsonPointer", "/readiness/readyForTuningPass" },
            { "requiredArguments", nlohmann::json::array({ "--require-ready" }) },
            { "manualRepairAids", nlohmann::json::array({
                "--repair-out",
                "--suggested-review-patch-bundle-out",
                "/suggestedReviewEvidencePatchBundle/templateJsonPatch"
            }) },
            { "failureExitCode", 13 },
            { "requiredBeforeTuning", true }
        }
    });

    return {
        { "schema", "stack.raw-starting-point.validation-readiness-gate-catalog" },
        { "version", kReadinessGateCatalogSchemaVersion },
        { "advisoryOnly", true },
        { "requiresRepresentativeRealRawSources", true },
        { "behaviorChanged", false },
        { "recipesChanged", false },
        { "renderOutputChanged", false },
        { "rawBuffersLoadedByCatalog", false },
        { "templatesMutatedByCatalog", false },
        { "sidecarsMutatedByCatalog", false },
        { "constantsTunedByCatalog", false },
        { "manualRepairOnly", true },
        { "patchesApplyAutomatically", false },
        { "gateCount", gates.size() },
        { "instructions",
          "Every gate listed here must pass before constants are tuned. Manual repair aids identify copyable helper paths only; the workflow report never applies patches, mutates sidecars or review templates, loads RAW buffers, changes recipes, changes render output, or tunes constants." },
        { "gates", std::move(gates) }
    };
}

nlohmann::json BuildValidationEvidencePackageManifest(
    const RawStartingPointValidationWorkflowOptions& options) {
    const std::string annotationTemplate =
        WorkflowPathString(options.annotationTemplatePath, "<annotations-template.json>");
    const std::string annotations =
        WorkflowPathString(
            options.annotationPath.empty() ? options.annotationTemplatePath : options.annotationPath,
            "<annotations.json>");
    const std::string stageEvidenceTemplate =
        WorkflowPathString(options.stageEvidenceTemplatePath, "<stage-evidence-template.json>");
    const std::string stageEvidence =
        WorkflowPathString(
            options.stageEvidencePath.empty() ? options.stageEvidenceTemplatePath : options.stageEvidencePath,
            "<stage-evidence.json>");
    const std::string records =
        WorkflowPathString(options.recordsPath, "<records.json>");

    const std::string templateGenerationReport = "<template-generation-report.json>";
    const std::string annotationCheck = "<annotations-check.json>";
    const std::string annotationRepair = "<annotations-repair.json>";
    const std::string stageEvidenceCheck = "<stage-evidence-check.json>";
    const std::string stageEvidenceRepair = "<stage-evidence-repair.json>";
    const std::string sidecarPreflight = "<sidecar-preflight.json>";
    const std::string recordsSummary = "<records-summary.json>";
    const std::string constantReviewTemplate = "<constant-review-template.json>";
    const std::string constantReviewCheck = "<constant-review-check.json>";
    const std::string constantReviewRepair = "<constant-review-repair.json>";
    const std::string constantReviewPatchBundle =
        "<constant-review-patch-bundle.json>";
    const std::string validationGateStatus =
        "<validation-gate-status.json>";

    auto artifact = [](
        const char* id,
        const char* label,
        const char* kind,
        const std::string& path,
        const char* producerStep,
        nlohmann::json consumers,
        bool requiredBeforeTuning,
        bool manualReviewRequired,
        bool repairOnly,
        const char* schema,
        int schemaVersion,
        const char* readinessGateId = nullptr,
        const char* gateStatusInputArgument = nullptr,
        const char* requiredReadyJsonPointer = nullptr) {
        nlohmann::json result = {
            { "artifactId", id },
            { "label", label },
            { "kind", kind },
            { "path", path },
            { "producerStep", producerStep },
            { "consumers", std::move(consumers) },
            { "requiredBeforeTuning", requiredBeforeTuning },
            { "manualReviewRequired", manualReviewRequired },
            { "repairOnly", repairOnly }
        };
        if (schema != nullptr && schema[0] != '\0') {
            result["schema"] = schema;
            result["schemaVersion"] = schemaVersion;
        }
        if (readinessGateId != nullptr && readinessGateId[0] != '\0') {
            result["readinessGateId"] = readinessGateId;
        }
        if (gateStatusInputArgument != nullptr && gateStatusInputArgument[0] != '\0') {
            result["gateStatusInputArgument"] = gateStatusInputArgument;
        }
        if (requiredReadyJsonPointer != nullptr && requiredReadyJsonPointer[0] != '\0') {
            result["requiredReadyJsonPointer"] = requiredReadyJsonPointer;
        }
        return result;
    };

    nlohmann::json artifacts = nlohmann::json::array({
        artifact(
            "annotation-template",
            "Annotation template",
            "fillable-template",
            annotationTemplate,
            "generate-sidecar-templates",
            nlohmann::json::array({ "fill-annotations", "preflight-annotations" }),
            false,
            true,
            false,
            "stack.raw-starting-point.validation-annotations-template",
            kAnnotationTemplateSchemaVersion),
        artifact(
            "stage-evidence-template",
            "Stage evidence template",
            "fillable-template",
            stageEvidenceTemplate,
            "generate-sidecar-templates",
            nlohmann::json::array({ "fill-stage-evidence", "preflight-stage-evidence" }),
            false,
            true,
            false,
            "stack.raw-starting-point.validation-stage-evidence-template",
            kStageEvidenceTemplateSchemaVersion),
        artifact(
            "template-generation-report",
            "Template generation report",
            "report",
            templateGenerationReport,
            "generate-sidecar-templates",
            nlohmann::json::array({ "human-review-handoff" }),
            false,
            false,
            false,
            "stack.raw-starting-point.validation-template-generation",
            kTemplateGenerationSchemaVersion),
        artifact(
            "annotations-sidecar",
            "Filled annotations sidecar",
            "manual-sidecar",
            annotations,
            "fill-annotations",
            nlohmann::json::array({ "preflight-annotations", "generate-ready-records" }),
            true,
            true,
            false,
            "stack.raw-starting-point.validation-annotations-template",
            kAnnotationTemplateSchemaVersion),
        artifact(
            "stage-evidence-sidecar",
            "Filled stage evidence sidecar",
            "manual-sidecar",
            stageEvidence,
            "fill-stage-evidence",
            nlohmann::json::array({ "preflight-stage-evidence", "generate-ready-records" }),
            true,
            true,
            false,
            "stack.raw-starting-point.validation-stage-evidence-template",
            kStageEvidenceTemplateSchemaVersion),
        artifact(
            "annotation-check",
            "Annotation readiness check",
            "gate-artifact",
            annotationCheck,
            "preflight-annotations",
            nlohmann::json::array({ "generate-ready-records", "check-validation-gates" }),
            true,
            false,
            false,
            "stack.raw-starting-point.validation-annotation-check",
            kAnnotationCheckSchemaVersion,
            "annotation-sidecar-ready",
            "--annotation-check",
            "/readiness/readyForRecordMerge"),
        artifact(
            "annotation-repair",
            "Annotation repair helper",
            "manual-repair-helper",
            annotationRepair,
            "preflight-annotations",
            nlohmann::json::array({ "fill-annotations" }),
            false,
            true,
            true,
            "stack.raw-starting-point.validation-annotations-repair",
            kAnnotationRepairSchemaVersion),
        artifact(
            "stage-evidence-check",
            "Stage evidence readiness check",
            "gate-artifact",
            stageEvidenceCheck,
            "preflight-stage-evidence",
            nlohmann::json::array({ "generate-ready-records", "check-validation-gates" }),
            true,
            false,
            false,
            "stack.raw-starting-point.validation-stage-evidence-check",
            kStageEvidenceCheckSchemaVersion,
            "stage-evidence-sidecar-ready",
            "--stage-evidence-check",
            "/readiness/readyForRecordMerge"),
        artifact(
            "stage-evidence-repair",
            "Stage evidence repair helper",
            "manual-repair-helper",
            stageEvidenceRepair,
            "preflight-stage-evidence",
            nlohmann::json::array({ "fill-stage-evidence" }),
            false,
            true,
            true,
            "stack.raw-starting-point.validation-stage-evidence-repair",
            kStageEvidenceRepairSchemaVersion),
        artifact(
            "record-sidecar-preflight",
            "Record sidecar preflight",
            "gate-artifact",
            sidecarPreflight,
            "generate-ready-records",
            nlohmann::json::array({ "check-validation-gates" }),
            true,
            false,
            false,
            "stack.raw-starting-point.validation-record-sidecar-preflight",
            kRecordSidecarPreflightSchemaVersion,
            "record-sidecar-preflight-ready",
            "--sidecar-preflight",
            "/readyForRecordGeneration"),
        artifact(
            "validation-records",
            "Validation records",
            "records",
            records,
            "generate-ready-records",
            nlohmann::json::array({ "summarize-ready-records" }),
            true,
            false,
            false,
            "stack.raw-starting-point.validation-records",
            kValidationRecordsSchemaVersion),
        artifact(
            "validation-summary-report",
            "Validation summary report",
            "gate-artifact",
            recordsSummary,
            "summarize-ready-records",
            nlohmann::json::array({ "fill-constant-review-template", "check-validation-gates" }),
            true,
            false,
            false,
            "stack.raw-starting-point.validation-summary-report",
            kValidationSummaryReportSchemaVersion,
            "validation-record-summary-ready",
            "--records-summary",
            "/validationSetSummary/tuningReadiness/mechanicalInputsComplete"),
        artifact(
            "constant-review-template",
            "Constant review template",
            "fillable-template",
            constantReviewTemplate,
            "summarize-ready-records",
            nlohmann::json::array({ "fill-constant-review-template", "check-constant-review-template" }),
            true,
            true,
            false,
            "stack.raw-starting-point.constant-review-template",
            kConstantReviewTemplateSchemaVersion),
        artifact(
            "constant-review-check",
            "Constant review check",
            "gate-artifact",
            constantReviewCheck,
            "check-constant-review-template",
            nlohmann::json::array({ "check-validation-gates" }),
            true,
            false,
            false,
            "stack.raw-starting-point.constant-review-check",
            kConstantReviewCheckSchemaVersion,
            "constant-review-ready",
            "--constant-review-check",
            "/readiness/readyForTuningPass"),
        artifact(
            "constant-review-repair",
            "Constant review repair helper",
            "manual-repair-helper",
            constantReviewRepair,
            "check-constant-review-template",
            nlohmann::json::array({ "fill-constant-review-template" }),
            false,
            true,
            true,
            "stack.raw-starting-point.constant-review-repair",
            kConstantReviewRepairSchemaVersion),
        artifact(
            "constant-review-patch-bundle",
            "Constant review evidence patch bundle",
            "advisory-patch-helper",
            constantReviewPatchBundle,
            "check-constant-review-template",
            nlohmann::json::array({ "fill-constant-review-template" }),
            false,
            true,
            true,
            "stack.raw-starting-point.constant-review-patch-bundle",
            kConstantReviewPatchBundleSchemaVersion),
        artifact(
            "validation-gate-status",
            "Aggregate validation gate status",
            "gate-summary",
            validationGateStatus,
            "check-validation-gates",
            nlohmann::json::array({ "constant-tuning-pass" }),
            true,
            false,
            false,
            "stack.raw-starting-point.validation-readiness-gate-status",
            kValidationGateStatusSchemaVersion)
    });

    const nlohmann::json catalog = BuildValidationReadinessGateCatalog();
    nlohmann::json gateInputs = nlohmann::json::array();
    for (const nlohmann::json& gate : catalog.value("gates", nlohmann::json::array())) {
        const std::string outputArtifact =
            gate.value("outputArtifact", std::string());
        std::string artifactPath;
        if (outputArtifact == "annotationCheck") {
            artifactPath = annotationCheck;
        } else if (outputArtifact == "stageEvidenceCheck") {
            artifactPath = stageEvidenceCheck;
        } else if (outputArtifact == "recordSidecarPreflight") {
            artifactPath = sidecarPreflight;
        } else if (outputArtifact == "validationSummaryReport") {
            artifactPath = recordsSummary;
        } else if (outputArtifact == "constantReviewCheck") {
            artifactPath = constantReviewCheck;
        }

        gateInputs.push_back({
            { "gateId", gate.value("gateId", std::string()) },
            { "label", gate.value("label", std::string()) },
            { "producerCommand", gate.value("command", std::string()) },
            { "outputArtifact", outputArtifact },
            { "artifactPath", artifactPath },
            { "gateStatusInputArgument",
              gate.value("gateStatusInputArgument", std::string()) },
            { "expectedSchema", gate.value("expectedSchema", std::string()) },
            { "expectedSchemaVersion", gate.value("schemaVersion", 0) },
            { "requiredReadyJsonPointer",
              gate.value("requiredReadyJsonPointer", std::string()) },
            { "requiredArguments",
              gate.value("requiredArguments", nlohmann::json::array()) },
            { "manualRepairAids",
              gate.value("manualRepairAids", nlohmann::json::array()) }
        });
    }

    return {
        { "schema", "stack.raw-starting-point.validation-evidence-package-manifest" },
        { "version", kValidationEvidencePackageManifestSchemaVersion },
        { "behaviorChanged", false },
        { "recipesChanged", false },
        { "renderOutputChanged", false },
        { "rawBuffersLoaded", false },
        { "templatesMutated", false },
        { "sidecarsMutated", false },
        { "reviewTemplatesMutated", false },
        { "patchesApplied", false },
        { "constantsTuned", false },
        { "requiresRepresentativeRealRawSources", true },
        { "artifactCount", artifacts.size() },
        { "gateInputCount", gateInputs.size() },
        { "artifacts", std::move(artifacts) },
        { "gateStatusInputs", std::move(gateInputs) },
        { "instructions",
          "Use this manifest as a handoff map for real RAW validation artifacts. It is embedded in the workflow report only; it does not create artifacts, scan RAW files, mutate sidecars or review templates, apply JSON Patch operations, change recipes, touch render output, or tune constants." }
    };
}

nlohmann::json BuildValidationWorkflowReport(
    const RawStartingPointValidationWorkflowOptions& options) {
    const nlohmann::json workflow = BuildValidationWorkflowChecklist(
        options.workspaceRoot,
        options.annotationTemplatePath,
        options.annotationPath,
        options.stageEvidenceTemplatePath,
        options.stageEvidencePath,
        options.recordsPath);

    return {
        { "schema", "stack.raw-starting-point.validation-workflow-report" },
        { "version", kValidationWorkflowReportSchemaVersion },
        { "behaviorChanged", false },
        { "recipesChanged", false },
        { "renderOutputChanged", false },
        { "rawBuffersLoaded", false },
        { "templatesMutated", false },
        { "constantsTuned", false },
        { "requiresRepresentativeRealRawSources", true },
        { "validationWorkflowSchemaVersion", kValidationWorkflowSchemaVersion },
        { "artifactSchemaVersions", BuildValidationArtifactSchemaVersions() },
        { "validationContracts", BuildValidationWorkflowContractBundle() },
        { "readinessGateCatalog", BuildValidationReadinessGateCatalog() },
        { "evidencePackageManifest",
          BuildValidationEvidencePackageManifest(options) },
        { "paths", {
            { "workspaceRoot", WorkflowPathString(options.workspaceRoot, "<workspace-folder>") },
            { "annotationTemplate", WorkflowPathString(options.annotationTemplatePath, "<annotations-template.json>") },
            { "annotations", WorkflowPathString(options.annotationPath, "<annotations.json>") },
            { "stageEvidenceTemplate", WorkflowPathString(options.stageEvidenceTemplatePath, "<stage-evidence-template.json>") },
            { "stageEvidence", WorkflowPathString(options.stageEvidencePath, "<stage-evidence.json>") },
            { "records", WorkflowPathString(options.recordsPath, "<records.json>") },
            { "evidencePackageManifestOutput",
              WorkflowPathString(options.evidenceManifestOutputPath, "<embedded-only>") },
            { "outputFile", WorkflowPathString(options.outputPath, "<stdout>") }
        } },
        { "workflow", workflow },
        { "instructions",
          "This standalone report is an inert planning artifact for RAW Starting Point validation. It does not scan RAW files, load RAW buffers, mutate sidecars or review templates, tune constants, change recipes, or touch render output. Use the workflow steps with representative real RAW sources, complete human review and stage-evidence sidecars, pass readiness gates, then tune constants only in a later evidence-backed pass." }
    };
}

std::filesystem::path GateStatusInputPath(
    const RawStartingPointValidationGateStatusOptions& options,
    const std::string& outputArtifact) {
    if (outputArtifact == "annotationCheck") {
        return options.annotationCheckPath;
    }
    if (outputArtifact == "stageEvidenceCheck") {
        return options.stageEvidenceCheckPath;
    }
    if (outputArtifact == "recordSidecarPreflight") {
        return options.recordSidecarPreflightPath;
    }
    if (outputArtifact == "validationSummaryReport") {
        return options.validationSummaryPath;
    }
    if (outputArtifact == "constantReviewCheck") {
        return options.constantReviewCheckPath;
    }
    return {};
}

nlohmann::json JsonPointerValueOrNull(
    const nlohmann::json& object,
    const std::string& pointer,
    bool* found = nullptr) {
    if (found != nullptr) {
        *found = false;
    }
    if (pointer.empty()) {
        if (found != nullptr) {
            *found = true;
        }
        return object;
    }
    try {
        const nlohmann::json& value =
            object.at(nlohmann::json::json_pointer(pointer));
        if (found != nullptr) {
            *found = true;
        }
        return value;
    } catch (...) {
        return nlohmann::json();
    }
}

std::string BlockingReasonsPointerForReadyPointer(const std::string& readyPointer) {
    if (readyPointer.empty() || readyPointer[0] != '/') {
        return "/blockingReasons";
    }
    const std::size_t slash = readyPointer.find_last_of('/');
    if (slash == std::string::npos || slash == 0) {
        return "/blockingReasons";
    }
    return readyPointer.substr(0, slash) + "/blockingReasons";
}

void AppendBlockingReasons(
    nlohmann::json& target,
    const nlohmann::json& reasons) {
    if (!target.is_array()) {
        target = nlohmann::json::array();
    }
    if (reasons.is_string()) {
        target.push_back(reasons.get<std::string>());
        return;
    }
    if (!reasons.is_array()) {
        return;
    }
    for (const nlohmann::json& reason : reasons) {
        if (reason.is_string()) {
            target.push_back(reason.get<std::string>());
        }
    }
}

void AddBooleanAdvisoryCheck(
    nlohmann::json& checks,
    const std::string& checkId,
    const std::string& label,
    const std::string& jsonPointer,
    const std::string& detailJsonPointer,
    const nlohmann::json& artifact,
    const std::string& instructions) {
    bool found = false;
    const nlohmann::json value =
        JsonPointerValueOrNull(artifact, jsonPointer, &found);
    const bool valueIsBoolean = value.is_boolean();
    const bool complete = valueIsBoolean && value.get<bool>();
    std::string status = "missing";
    if (found && !valueIsBoolean) {
        status = "invalid";
    } else if (complete) {
        status = "complete";
    } else if (found) {
        status = "incomplete";
    }

    checks.push_back({
        { "checkId", checkId },
        { "label", label },
        { "jsonPointer", jsonPointer },
        { "detailJsonPointer", detailJsonPointer },
        { "present", found },
        { "valueIsBoolean", valueIsBoolean },
        { "complete", complete },
        { "status", status },
        { "isGating", false },
        { "instructions", instructions }
    });
}

nlohmann::json BuildGateArtifactAdvisoryChecks(
    const std::string& outputArtifact,
    const nlohmann::json& artifact,
    bool artifactLoaded,
    bool schemaMatches,
    bool versionMatches) {
    nlohmann::json checks = nlohmann::json::array();
    if (!artifactLoaded || !schemaMatches || !versionMatches) {
        return checks;
    }

    if (outputArtifact == "stageEvidenceCheck") {
        AddBooleanAdvisoryCheck(
            checks,
            "candidate-stage-diagnostics",
            "Candidate-stage diagnostics coverage",
            "/candidateStageDiagnostics/coverageComplete",
            "/candidateStageDiagnostics/requiredCandidateStageCoverage",
            artifact,
            "Review missing candidate-stage rows before tuning; this advisory does not affect readyForRecordMerge.");
    } else if (outputArtifact == "validationSummaryReport") {
        AddBooleanAdvisoryCheck(
            checks,
            "candidate-stage-diagnostics",
            "Candidate-stage diagnostics coverage",
            "/validationSetSummary/startingPointDiagnostics/candidateStageDiagnosticCoverageComplete",
            "/validationSetSummary/startingPointDiagnostics/candidateStageDiagnosticCoverage",
            artifact,
            "Review missing validation-summary candidate-stage rows before tuning; this advisory does not affect mechanicalInputsComplete.");
    }

    return checks;
}

nlohmann::json BuildValidationGateStatusReport(
    const RawStartingPointValidationGateStatusOptions& options) {
    const nlohmann::json catalog = BuildValidationReadinessGateCatalog();
    const nlohmann::json gates =
        catalog.value("gates", nlohmann::json::array());

    nlohmann::json gateStatuses = nlohmann::json::array();
    nlohmann::json blockingReasons = nlohmann::json::array();
    nlohmann::json nonGatingAdvisoryChecks = nlohmann::json::array();
    nlohmann::json nextRequiredGate = {
        { "available", false },
        { "reason", "All required gate artifacts are ready." }
    };
    std::map<std::string, int> gateStatusCounts;
    std::map<std::string, int> advisoryStatusCounts;
    int requiredGateCount = 0;
    int readyGateCount = 0;
    int missingArtifactGateCount = 0;
    int invalidArtifactGateCount = 0;
    int schemaMismatchGateCount = 0;
    int versionMismatchGateCount = 0;
    int readyFieldMissingGateCount = 0;
    int notReadyGateCount = 0;
    int advisoryCheckCount = 0;
    int incompleteAdvisoryCheckCount = 0;

    for (const nlohmann::json& gate : gates) {
        const std::string gateId = gate.value("gateId", std::string());
        const std::string label = gate.value("label", std::string());
        const std::string outputArtifact =
            gate.value("outputArtifact", std::string());
        const std::string expectedSchema =
            gate.value("expectedSchema", std::string());
        const int expectedVersion = gate.value("schemaVersion", 0);
        const std::string readyPointer =
            gate.value("requiredReadyJsonPointer", std::string());
        const bool requiredBeforeTuning =
            gate.value("requiredBeforeTuning", true);
        if (requiredBeforeTuning) {
            ++requiredGateCount;
        }

        const std::filesystem::path inputPath =
            GateStatusInputPath(options, outputArtifact);
        const bool artifactProvided = !inputPath.empty();
        bool artifactExists = false;
        bool artifactRegularFile = false;
        bool artifactLoaded = false;
        bool schemaMatches = false;
        bool versionMatches = false;
        bool readyFieldPresent = false;
        bool readyFieldBoolean = false;
        bool ready = false;
        std::string actualSchema;
        int actualVersion = 0;
        std::string status = "missing-artifact";
        nlohmann::json artifact = nlohmann::json::object();
        nlohmann::json gateBlockers = nlohmann::json::array();

        if (!artifactProvided) {
            gateBlockers.push_back(
                "No artifact path was provided for this gate.");
            ++missingArtifactGateCount;
        } else {
            std::error_code ec;
            artifactExists = std::filesystem::exists(inputPath, ec) && !ec;
            artifactRegularFile =
                artifactExists && std::filesystem::is_regular_file(inputPath, ec) && !ec;
            if (!artifactExists || !artifactRegularFile) {
                gateBlockers.push_back(
                    "The artifact path is missing or is not a regular file.");
                ++missingArtifactGateCount;
            } else {
                std::ifstream in(inputPath, std::ios::binary);
                try {
                    in >> artifact;
                    artifactLoaded = true;
                } catch (const std::exception& ex) {
                    gateBlockers.push_back(
                        std::string("The artifact is not valid JSON: ") + ex.what());
                    ++invalidArtifactGateCount;
                    status = "invalid-json";
                }
            }
        }

        if (artifactLoaded && !artifact.is_object()) {
            status = "invalid-json-shape";
            gateBlockers.push_back(
                "The artifact JSON root must be an object.");
            ++invalidArtifactGateCount;
            artifactLoaded = false;
        }

        if (artifactLoaded) {
            actualSchema = artifact.value("schema", std::string());
            actualVersion = artifact.value("version", 0);
            schemaMatches =
                expectedSchema.empty() || actualSchema == expectedSchema;
            versionMatches =
                expectedVersion <= 0 || actualVersion == expectedVersion;

            if (!schemaMatches) {
                status = "schema-mismatch";
                gateBlockers.push_back(
                    "The artifact schema does not match the readiness gate catalog.");
                ++schemaMismatchGateCount;
            } else if (!versionMatches) {
                status = "schema-version-mismatch";
                gateBlockers.push_back(
                    "The artifact schema version does not match the readiness gate catalog.");
                ++versionMismatchGateCount;
            } else {
                bool found = false;
                const nlohmann::json readyValue =
                    JsonPointerValueOrNull(artifact, readyPointer, &found);
                readyFieldPresent = found;
                readyFieldBoolean = readyValue.is_boolean();
                ready = readyFieldBoolean && readyValue.get<bool>();
                if (!readyFieldPresent) {
                    status = "ready-field-missing";
                    gateBlockers.push_back(
                        "The artifact is missing the required readiness field.");
                    ++readyFieldMissingGateCount;
                } else if (!readyFieldBoolean) {
                    status = "ready-field-not-boolean";
                    gateBlockers.push_back(
                        "The required readiness field is present but is not a boolean.");
                    ++readyFieldMissingGateCount;
                } else if (!ready) {
                    status = "not-ready";
                    AppendBlockingReasons(
                        gateBlockers,
                        JsonPointerValueOrNull(
                            artifact,
                            BlockingReasonsPointerForReadyPointer(readyPointer)));
                    if (gateBlockers.empty()) {
                        gateBlockers.push_back(
                            "The required readiness field is false.");
                    }
                    ++notReadyGateCount;
                } else {
                    status = "ready";
                    ++readyGateCount;
                }
            }
        }

        ++gateStatusCounts[status];

        nlohmann::json artifactAdvisoryChecks =
            BuildGateArtifactAdvisoryChecks(
                outputArtifact,
                artifact,
                artifactLoaded,
                schemaMatches,
                versionMatches);
        for (const nlohmann::json& check : artifactAdvisoryChecks) {
            ++advisoryCheckCount;
            const std::string advisoryStatus =
                check.value("status", std::string("unknown"));
            ++advisoryStatusCounts[advisoryStatus];
            if (advisoryStatus != "complete") {
                ++incompleteAdvisoryCheckCount;
            }
            nlohmann::json flattened = check;
            flattened["gateId"] = gateId;
            flattened["outputArtifact"] = outputArtifact;
            flattened["gateStatusInputArgument"] =
                gate.value("gateStatusInputArgument", std::string());
            flattened["inputFile"] = inputPath.string();
            nonGatingAdvisoryChecks.push_back(std::move(flattened));
        }

        if (status != "ready" && requiredBeforeTuning) {
            const std::string prefix =
                gateId.empty() ? outputArtifact : gateId;
            if (gateBlockers.empty()) {
                blockingReasons.push_back(prefix + ": gate is not ready.");
            } else {
                for (const nlohmann::json& blocker : gateBlockers) {
                    if (blocker.is_string()) {
                        blockingReasons.push_back(
                            prefix + ": " + blocker.get<std::string>());
                    }
                }
            }

            if (!nextRequiredGate.value("available", false)) {
                nextRequiredGate = {
                    { "available", true },
                    { "gateId", gateId },
                    { "label", label },
                    { "status", status },
                    { "command", gate.value("command", std::string()) },
                    { "outputArtifact", outputArtifact },
                    { "gateStatusInputArgument",
                      gate.value("gateStatusInputArgument", std::string()) },
                    { "inputFile", inputPath.string() },
                    { "artifactProvided", artifactProvided },
                    { "artifactExists", artifactExists },
                    { "expectedSchema", expectedSchema },
                    { "expectedSchemaVersion", expectedVersion },
                    { "requiredReadyJsonPointer", readyPointer },
                    { "requiredArguments",
                      gate.value("requiredArguments", nlohmann::json::array()) },
                    { "manualRepairAids",
                      gate.value("manualRepairAids", nlohmann::json::array()) },
                    { "failureExitCode", gate.value("failureExitCode", 0) },
                    { "blockingReasons", gateBlockers },
                    { "instructions",
                      "Create or repair this gate artifact with the listed command, provide it to --check-raw-starting-point-validation-gates using gateStatusInputArgument, and rerun the aggregate check before tuning constants." }
                };
            }
        }

        gateStatuses.push_back({
            { "gateId", gateId },
            { "label", label },
            { "command", gate.value("command", std::string()) },
            { "outputArtifact", outputArtifact },
            { "gateStatusInputArgument",
              gate.value("gateStatusInputArgument", std::string()) },
            { "inputFile", inputPath.string() },
            { "artifactProvided", artifactProvided },
            { "artifactExists", artifactExists },
            { "artifactRegularFile", artifactRegularFile },
            { "artifactLoaded", artifactLoaded },
            { "expectedSchema", expectedSchema },
            { "expectedSchemaVersion", expectedVersion },
            { "artifactSchema", actualSchema },
            { "artifactSchemaVersion", actualVersion },
            { "schemaMatches", schemaMatches },
            { "schemaVersionMatches", versionMatches },
            { "requiredReadyJsonPointer", readyPointer },
            { "readyFieldPresent", readyFieldPresent },
            { "readyFieldBoolean", readyFieldBoolean },
            { "ready", ready },
            { "status", status },
            { "requiredBeforeTuning", requiredBeforeTuning },
            { "nonGatingAdvisoryChecks", artifactAdvisoryChecks },
            { "blockingReasons", std::move(gateBlockers) }
        });
    }

    const bool allRequiredGatesReady =
        requiredGateCount > 0 && readyGateCount == requiredGateCount;
    nlohmann::json gateStatusSummaryByStatus = nlohmann::json::object();
    for (const auto& [status, count] : gateStatusCounts) {
        gateStatusSummaryByStatus[status] = count;
    }
    nlohmann::json advisoryStatusSummaryByStatus = nlohmann::json::object();
    for (const auto& [status, count] : advisoryStatusCounts) {
        advisoryStatusSummaryByStatus[status] = count;
    }
    return {
        { "schema", "stack.raw-starting-point.validation-readiness-gate-status" },
        { "version", kValidationGateStatusSchemaVersion },
        { "behaviorChanged", false },
        { "recipesChanged", false },
        { "renderOutputChanged", false },
        { "rawBuffersLoaded", false },
        { "sidecarsMutated", false },
        { "reviewTemplatesMutated", false },
        { "patchesApplied", false },
        { "constantsTuned", false },
        { "requiresRepresentativeRealRawSources", true },
        { "requireReady", options.requireReady },
        { "readinessGateCatalogSchemaVersion",
          catalog.value("version", 0) },
        { "artifactSchemaVersions", BuildValidationArtifactSchemaVersions() },
        { "inputFiles", {
            { "annotationCheck", options.annotationCheckPath.string() },
            { "stageEvidenceCheck", options.stageEvidenceCheckPath.string() },
            { "recordSidecarPreflight", options.recordSidecarPreflightPath.string() },
            { "validationSummaryReport", options.validationSummaryPath.string() },
            { "constantReviewCheck", options.constantReviewCheckPath.string() },
            { "outputFile", options.outputPath.string() }
        } },
        { "gateStatus", std::move(gateStatuses) },
        { "nonGatingAdvisories", {
            { "checkCount", advisoryCheckCount },
            { "completeCheckCount", advisoryCheckCount - incompleteAdvisoryCheckCount },
            { "incompleteCheckCount", incompleteAdvisoryCheckCount },
            { "statusSummaryByStatus", advisoryStatusSummaryByStatus },
            { "checks", std::move(nonGatingAdvisoryChecks) },
            { "affectsRequiredReadiness", false }
        } },
        { "readiness", {
            { "allRequiredGatesReady", allRequiredGatesReady },
            { "requiredGateCount", requiredGateCount },
            { "readyGateCount", readyGateCount },
            { "blockedGateCount", requiredGateCount - readyGateCount },
            { "missingArtifactGateCount", missingArtifactGateCount },
            { "invalidArtifactGateCount", invalidArtifactGateCount },
            { "schemaMismatchGateCount", schemaMismatchGateCount },
            { "schemaVersionMismatchGateCount", versionMismatchGateCount },
            { "readyFieldMissingGateCount", readyFieldMissingGateCount },
            { "notReadyGateCount", notReadyGateCount },
            { "gateStatusSummaryByStatus", gateStatusSummaryByStatus },
            { "nextRequiredGate", nextRequiredGate },
            { "blockingReasons", blockingReasons },
            { "nextAction", allRequiredGatesReady
                ? "Every required gate artifact is ready; constant changes still need an evidence-backed tuning pass."
                : "Resolve blockingReasons, rerun the underlying gate commands, then rerun this aggregate gate checker." }
        } },
        { "instructions",
          "This aggregate gate-status report only reads existing JSON artifacts. It does not scan RAW files, load RAW buffers, mutate sidecars or review templates, apply JSON Patch operations, change recipes, touch render output, or tune constants." }
    };
}

nlohmann::json BuildAnnotationRepairRecord(
    const Stack::RawWorkspace::SourceRecord& source,
    const std::vector<std::string>& tags,
    const nlohmann::json& humanReview,
    const nlohmann::json& missingHumanReview,
    const std::string& status) {
    return {
        { "source", SerializeSourceIdentity(source) },
        { "status", status },
        { "repairNeeded", true },
        { "imageCategoryTags", JsonStringVector(tags) },
        { "humanReview", humanReview },
        { "missingHumanReviewFields", missingHumanReview },
        { "repairInstructions",
          "Copy this record into the annotations sidecar entry for this source, fill imageCategoryTags, and complete every humanReview field before rerunning --check-raw-starting-point-annotations --require-ready." }
    };
}

nlohmann::json BuildStageEvidenceRepairRecord(
    const Stack::RawWorkspace::SourceRecord& source,
    const std::string& status,
    bool hasDiagnostics,
    bool hasCandidates,
    bool hasSelectedCandidate,
    const nlohmann::json& missingStages) {
    nlohmann::json repair = BuildStageEvidenceTemplateRecord(source);
    repair["status"] = status;
    repair["repairNeeded"] = true;
    repair["diagnostics"] = {
        { "present", hasDiagnostics },
        { "hasCandidateDiagnostics", hasCandidates },
        { "hasSelectedCandidate", hasSelectedCandidate }
    };
    repair["missingStages"] = missingStages;
    repair["repairInstructions"] =
        "Copy this record into the stage-evidence sidecar entry for this source, replace placeholders with captured Starting Point diagnostics, and mark every required stage complete before rerunning --check-raw-starting-point-stage-evidence.";
    return repair;
}

nlohmann::json BuildSidecarRepairPatch(
    const std::filesystem::path& sidecarPath,
    const nlohmann::json& repairRecords,
    const std::string& sidecarKind,
    const std::string& copyInstructions) {
    nlohmann::json sidecarJsonPatch = nlohmann::json::array();
    nlohmann::json operationMetadata = nlohmann::json::array();
    nlohmann::json recordIds = nlohmann::json::array();

    if (repairRecords.is_object()) {
        for (auto record = repairRecords.begin();
             record != repairRecords.end();
             ++record) {
            const std::string pointerPath =
                "/records/" + EscapeJsonPointerToken(record.key());
            sidecarJsonPatch.push_back({
                { "op", "add" },
                { "path", pointerPath },
                { "value", record.value() }
            });
            operationMetadata.push_back({
                { "recordId", record.key() },
                { "op", "add" },
                { "path", pointerPath }
            });
            recordIds.push_back(record.key());
        }
    }

    const bool hasOperations = !sidecarJsonPatch.empty();
    nlohmann::json blockingReasons = nlohmann::json::array();
    if (!hasOperations) {
        blockingReasons.push_back(
            "No repair records are available for this sidecar report.");
    }

    return {
        { "available", hasOperations },
        { "readyForManualCopy", hasOperations },
        { "advisoryOnly", true },
        { "sidecarKind", sidecarKind },
        { "source", "sidecarRepair.records" },
        { "targetSidecarPath", sidecarPath.string() },
        { "targetRecordsObjectPath", "/records" },
        { "requiresCanonicalRecordsObject", true },
        { "mutatesSidecarAutomatically", false },
        { "patchAppliesAutomatically", false },
        { "jsonPatchFormat", "RFC 6902-compatible advisory operations" },
        { "jsonPointerBase", "/records" },
        { "sidecarJsonPatchOperationCount", sidecarJsonPatch.size() },
        { "sidecarJsonPatch", std::move(sidecarJsonPatch) },
        { "recordIds", std::move(recordIds) },
        { "operationMetadata", std::move(operationMetadata) },
        { "blockingReasons", std::move(blockingReasons) },
        { "instructions", copyInstructions }
    };
}

nlohmann::json BuildAnnotationRepairOutputReport(
    const RawStartingPointAnnotationCheckOptions& options,
    const nlohmann::json& checkReport) {
    const nlohmann::json sidecarRepair =
        checkReport.value("sidecarRepair", nlohmann::json::object());
    const nlohmann::json readiness =
        checkReport.value("readiness", nlohmann::json::object());
    const nlohmann::json repairRecords =
        sidecarRepair.value("records", nlohmann::json::object());
    const std::string repairInstructions =
        sidecarRepair.value(
            "instructions",
            std::string("Copy records from this object into the annotations sidecar, then rerun the annotation check with --require-ready."));

    return {
        { "schema", "stack.raw-starting-point.validation-annotations-repair" },
        { "version", kAnnotationRepairSchemaVersion },
        { "workspaceRoot", options.workspaceRoot.string() },
        { "annotationFile", options.annotationPath.string() },
        { "repairOutputFile", options.repairOutputPath.string() },
        { "sourceCount", checkReport.value("sourceCount", 0) },
        { "checkReportSchemaVersion", checkReport.value("version", 0) },
        { "readyForRecordMerge", readiness.value("readyForRecordMerge", false) },
        { "repairAvailable", sidecarRepair.value("available", false) },
        { "repairRecordCount", sidecarRepair.value("repairRecordCount", 0) },
        { "instructions", repairInstructions },
        { "annotationReadinessContract", sidecarRepair.value(
            "annotationReadinessContract",
            BuildAnnotationReadinessContract()) },
        { "suggestedSidecarRepairPatch",
          BuildSidecarRepairPatch(
              options.annotationPath,
              repairRecords,
              "annotations",
              repairInstructions +
                  " The sidecarJsonPatch operations are advisory only and are never applied automatically by this command.") },
        { "records", repairRecords }
    };
}

nlohmann::json BuildStageEvidenceRepairOutputReport(
    const RawStartingPointStageEvidenceCheckOptions& options,
    const nlohmann::json& checkReport) {
    const nlohmann::json sidecarRepair =
        checkReport.value("sidecarRepair", nlohmann::json::object());
    const nlohmann::json readiness =
        checkReport.value("readiness", nlohmann::json::object());
    const nlohmann::json repairRecords =
        sidecarRepair.value("records", nlohmann::json::object());
    const std::string repairInstructions =
        sidecarRepair.value(
            "instructions",
            std::string("Copy records from this object into the stage-evidence sidecar, replace placeholders with captured diagnostics, then rerun the stage-evidence check."));

    return {
        { "schema", "stack.raw-starting-point.validation-stage-evidence-repair" },
        { "version", kStageEvidenceRepairSchemaVersion },
        { "workspaceRoot", options.workspaceRoot.string() },
        { "stageEvidenceFile", options.stageEvidencePath.string() },
        { "repairOutputFile", options.repairOutputPath.string() },
        { "sourceCount", checkReport.value("sourceCount", 0) },
        { "checkReportSchemaVersion", checkReport.value("version", 0) },
        { "readyForRecordMerge", readiness.value("readyForRecordMerge", false) },
        { "repairAvailable", sidecarRepair.value("available", false) },
        { "repairRecordCount", sidecarRepair.value("repairRecordCount", 0) },
        { "instructions", repairInstructions },
        { "diagnosticCaptureContract", sidecarRepair.value(
            "diagnosticCaptureContract",
            BuildStageEvidenceCaptureContract()) },
        { "suggestedSidecarRepairPatch",
          BuildSidecarRepairPatch(
              options.stageEvidencePath,
              repairRecords,
              "stage-evidence",
              repairInstructions +
                  " The sidecarJsonPatch operations are advisory only, placeholders still require real captured diagnostics, and the command never applies them automatically.") },
        { "records", repairRecords }
    };
}

nlohmann::json BuildAnnotationCheckReport(
    const RawStartingPointAnnotationCheckOptions& options,
    const std::vector<Stack::RawWorkspace::SourceRecord>& sources) {
    nlohmann::json records = nlohmann::json::array();
    nlohmann::json repairRecords = nlohmann::json::object();
    nlohmann::json sourceStatusCounts = nlohmann::json::object();
    nlohmann::json categoryTagCounts = nlohmann::json::object();
    nlohmann::json missingHumanReviewFieldCounts = nlohmann::json::object();
    nlohmann::json humanReviewBoolCounts = nlohmann::json::object();
    nlohmann::json nextManualControlCounts = nlohmann::json::object();
    nlohmann::json unmatchedSourceIds = nlohmann::json::array();
    nlohmann::json missingCategorySourceIds = nlohmann::json::array();
    nlohmann::json incompleteHumanReviewSourceIds = nlohmann::json::array();

    std::set<int> matchedAnnotationIndices;
    int matchedSourceCount = 0;
    int recordsWithCategoryTags = 0;
    int completeHumanReviewCount = 0;
    int readySourceCount = 0;

    for (const Stack::RawWorkspace::SourceRecord& source : sources) {
        int annotationIndex = -1;
        std::string annotationMatchedKey;
        const RawStartingPointAnnotationEntry* annotation =
            FindAnnotationForSource(
                options.annotationEntries,
                source,
                annotationIndex,
                annotationMatchedKey);
        if (annotation != nullptr) {
            ++matchedSourceCount;
            matchedAnnotationIndices.insert(annotationIndex);
        }

        std::vector<std::string> tags;
        MergeAnnotationTags(tags, annotation);
        nlohmann::json humanReview = BuildHumanReviewTemplate();
        MergeAnnotationHumanReview(humanReview, annotation);
        const nlohmann::json missingHumanReview = MissingHumanReviewFields(humanReview);

        const bool hasCategoryTags = !tags.empty();
        const bool humanReviewComplete = annotation != nullptr && missingHumanReview.empty();
        const bool ready = annotation != nullptr && hasCategoryTags && humanReviewComplete;
        std::string status = "review-complete";
        if (annotation == nullptr) {
            status = "missing-annotation";
            unmatchedSourceIds.push_back(source.relativePathKey);
        } else if (!hasCategoryTags || !humanReviewComplete) {
            status = "review-incomplete";
            if (!hasCategoryTags) {
                missingCategorySourceIds.push_back(source.relativePathKey);
            }
            if (!humanReviewComplete) {
                incompleteHumanReviewSourceIds.push_back(source.relativePathKey);
            }
        }
        IncrementJsonCount(sourceStatusCounts, status);

        if (hasCategoryTags) {
            ++recordsWithCategoryTags;
            for (const std::string& tag : tags) {
                IncrementJsonCount(categoryTagCounts, tag);
            }
        }
        if (annotation != nullptr) {
            for (const nlohmann::json& field : missingHumanReview) {
                if (field.is_string()) {
                    IncrementJsonCount(missingHumanReviewFieldCounts, field.get<std::string>());
                }
            }
            for (const std::string& field : RequiredHumanReviewFields()) {
                const nlohmann::json value =
                    humanReview.is_object() && humanReview.contains(field)
                        ? humanReview[field]
                        : nlohmann::json();
                if (value.is_boolean()) {
                    IncrementJsonCount(
                        humanReviewBoolCounts[field],
                        value.get<bool>() ? "true" : "false");
                }
                if (field == "nextManualControl" &&
                    value.is_string() &&
                    !value.get<std::string>().empty()) {
                    IncrementJsonCount(nextManualControlCounts, value.get<std::string>());
                }
            }
        }
        if (humanReviewComplete) {
            ++completeHumanReviewCount;
        }
        if (ready) {
            ++readySourceCount;
        } else {
            repairRecords[source.relativePathKey] =
                BuildAnnotationRepairRecord(
                    source,
                    tags,
                    humanReview,
                    missingHumanReview,
                    status);
        }

        records.push_back({
            { "source", SerializeSourceIdentity(source) },
            { "annotation", SerializeAnnotationMatch(options.annotationPath, annotation, annotationMatchedKey) },
            { "status", status },
            { "readyForRecordMerge", ready },
            { "imageCategoryTags", JsonStringVector(tags) },
            { "requiresHumanCategoryTags", !hasCategoryTags },
            { "humanReview", humanReview },
            { "missingHumanReviewFields", missingHumanReview }
        });
    }

    nlohmann::json unmatchedAnnotationKeys = nlohmann::json::array();
    for (std::size_t i = 0; i < options.annotationEntries.size(); ++i) {
        if (matchedAnnotationIndices.find(static_cast<int>(i)) == matchedAnnotationIndices.end()) {
            unmatchedAnnotationKeys.push_back(options.annotationEntries[i].primaryKey);
        }
    }

    const std::vector<std::string> recommendedCategories = RecommendedValidationCategories();
    nlohmann::json missingRecommendedCategories = nlohmann::json::array();
    int recommendedCategoriesPresent = 0;
    const nlohmann::json recommendedCategoryCoverage =
        RecommendedCategoryCoverageFromCounts(
            categoryTagCounts,
            recommendedCategories,
            missingRecommendedCategories,
            recommendedCategoriesPresent);

    nlohmann::json blockers = nlohmann::json::array();
    if (sources.empty()) {
        blockers.push_back("No RAW sources were scanned.");
    }
    if (!unmatchedSourceIds.empty()) {
        blockers.push_back("Some scanned sources have no matching annotation entry.");
    }
    if (!unmatchedAnnotationKeys.empty()) {
        blockers.push_back("Some annotation entries did not match scanned sources.");
    }
    if (!missingCategorySourceIds.empty()) {
        blockers.push_back("Add representative image category tags to every matched annotation.");
    }
    if (!missingRecommendedCategories.empty()) {
        blockers.push_back(
            "Add annotation category coverage for every recommended validation category before generating tuning records.");
    }
    if (!incompleteHumanReviewSourceIds.empty()) {
        blockers.push_back("Complete required human review fields for every matched annotation.");
    }
    const bool hasRepairRecords = !repairRecords.empty();

    return {
        { "schema", "stack.raw-starting-point.validation-annotation-check" },
        { "version", kAnnotationCheckSchemaVersion },
        { "workspaceRoot", options.workspaceRoot.string() },
        { "annotationFile", options.annotationPath.string() },
        { "repairOutputFile", options.repairOutputPath.string() },
        { "sourceCount", sources.size() },
        { "annotationEntryCount", options.annotationEntries.size() },
        { "requireReady", options.requireReady },
        { "requiredHumanReviewFields", JsonStringVector(RequiredHumanReviewFields()) },
        { "recommendedValidationCategories", JsonStringVector(RecommendedValidationCategories()) },
        { "sourceCoverage", {
            { "matchedSourceCount", matchedSourceCount },
            { "unmatchedSourceCount", sources.size() - matchedSourceCount },
            { "unmatchedSourceIds", unmatchedSourceIds }
        } },
        { "annotationCoverage", {
            { "matchedEntryCount", matchedAnnotationIndices.size() },
            { "unmatchedEntryCount", options.annotationEntries.size() - matchedAnnotationIndices.size() },
            { "unmatchedAnnotationKeys", unmatchedAnnotationKeys }
        } },
        { "categoryCoverage", {
            { "recordsWithCategoryTags", recordsWithCategoryTags },
            { "recordsMissingCategoryTags", sources.size() - recordsWithCategoryTags },
            { "missingCategorySourceIds", missingCategorySourceIds },
            { "categoryTagCounts", categoryTagCounts },
            { "recommendedCategoriesPresent", recommendedCategoriesPresent },
            { "recommendedCategoriesTotal", static_cast<int>(recommendedCategories.size()) },
            { "missingRecommendedCategories", missingRecommendedCategories },
            { "representativeCoverageComplete", missingRecommendedCategories.empty() },
            { "recommendedCategoryCoverage", recommendedCategoryCoverage }
        } },
        { "humanReview", {
            { "completeRecordCount", completeHumanReviewCount },
            { "missingRecordCount", sources.size() - completeHumanReviewCount },
            { "incompleteSourceIds", incompleteHumanReviewSourceIds },
            { "missingFieldCounts", missingHumanReviewFieldCounts },
            { "booleanFieldCounts", humanReviewBoolCounts },
            { "nextManualControlCounts", nextManualControlCounts }
        } },
        { "sourceStatusCounts", sourceStatusCounts },
        { "readiness", {
            { "readyForRecordMerge", blockers.empty() },
            { "readySourceCount", readySourceCount },
            { "blockingReasons", blockers },
            { "nextAction", blockers.empty()
                ? "Run --validate-raw-starting-point-records with --load-raw-safety and --annotations on this workspace."
                : "Resolve blockingReasons before using these annotations for validation records." }
        } },
        { "sidecarRepair", {
            { "available", hasRepairRecords },
            { "repairRecordCount", repairRecords.size() },
            { "instructions",
              "Copy records from this object into the annotations sidecar, then rerun the annotation check with --require-ready." },
            { "annotationReadinessContract", BuildAnnotationReadinessContract() },
            { "suggestedSidecarRepairPatch",
              BuildSidecarRepairPatch(
                  options.annotationPath,
                  repairRecords,
                  "annotations",
                  "Copy approved sidecarJsonPatch operations into the annotations sidecar records object manually, then rerun the annotation check with --require-ready. The check report never applies these operations automatically.") },
            { "records", std::move(repairRecords) }
        } },
        { "records", std::move(records) }
    };
}

nlohmann::json BuildStageEvidenceCheckReport(
    const RawStartingPointStageEvidenceCheckOptions& options,
    const std::vector<Stack::RawWorkspace::SourceRecord>& sources) {
    const std::vector<RequiredStageEvidenceSpec> requiredStages =
        RequiredStageEvidenceSpecs();
    const std::vector<std::string> expectedCandidateKinds =
        ExpectedStartingPointCandidateKinds();

    nlohmann::json records = nlohmann::json::array();
    nlohmann::json repairRecords = nlohmann::json::object();
    nlohmann::json sourceStatusCounts = nlohmann::json::object();
    nlohmann::json requiredStageDescriptions = nlohmann::json::array();
    nlohmann::json stageCoverage = nlohmann::json::array();
    nlohmann::json stageCoverageById = nlohmann::json::object();
    nlohmann::json candidateStageCoverage = nlohmann::json::array();
    nlohmann::json candidateStageCoverageByKey = nlohmann::json::object();
    for (const RequiredStageEvidenceSpec& stage : requiredStages) {
        requiredStageDescriptions.push_back({
            { "stage", stage.id },
            { "label", stage.label },
            { "validationGap", stage.validationGap }
        });
        stageCoverageById[stage.id] = {
            { "stage", stage.id },
            { "label", stage.label },
            { "validationGap", stage.validationGap },
            { "completeRecordCount", 0 },
            { "missingSourceIds", nlohmann::json::array() }
        };
    }
    for (const std::string& kind : expectedCandidateKinds) {
        for (const RequiredStageEvidenceSpec& stage : requiredStages) {
            const std::string key =
                CandidateStageDiagnosticCoverageKey(kind, stage.id);
            candidateStageCoverageByKey[key] = {
                { "key", key },
                { "kind", kind },
                { "label", StartingPointCandidateKindLabel(kind) },
                { "stage", stage.id },
                { "stageLabel", stage.label },
                { "validationGap", stage.validationGap },
                { "completeRecordCount", 0 },
                { "missingSourceIds", nlohmann::json::array() }
            };
        }
    }

    nlohmann::json unmatchedSourceIds = nlohmann::json::array();
    nlohmann::json sourceIdentityMismatchSourceIds = nlohmann::json::array();
    nlohmann::json missingDiagnosticsSourceIds = nlohmann::json::array();
    nlohmann::json incompleteCandidateSourceIds = nlohmann::json::array();
    nlohmann::json incompleteStageSourceIds = nlohmann::json::array();
    std::set<int> matchedStageEvidenceIndices;

    int matchedSourceCount = 0;
    int matchedSourceIdentityCount = 0;
    int recordsWithSourceIdentityToken = 0;
    int recordsWithDiagnostics = 0;
    int recordsWithCandidates = 0;
    int recordsWithSelectedCandidate = 0;
    int recordsWithExpectedCandidateStageDiagnostics = 0;
    int readySourceCount = 0;

    for (const Stack::RawWorkspace::SourceRecord& source : sources) {
        int stageEvidenceIndex = -1;
        std::string stageEvidenceMatchedKey;
        const RawStartingPointAnnotationEntry* stageEvidence =
            FindAnnotationForSource(
                options.stageEvidenceEntries,
                source,
                stageEvidenceIndex,
                stageEvidenceMatchedKey);
        if (stageEvidence != nullptr) {
            ++matchedSourceCount;
            matchedStageEvidenceIndices.insert(stageEvidenceIndex);
        }
        const nlohmann::json sourceIdentityCheck =
            SerializeStageEvidenceSourceIdentityCheck(source, stageEvidence);
        const bool sourceIdentityMatches =
            sourceIdentityCheck.value("matched", true);
        if (stageEvidence != nullptr &&
            sourceIdentityCheck.value("sidecarTokenPresent", false)) {
            ++recordsWithSourceIdentityToken;
        }
        if (stageEvidence != nullptr && sourceIdentityMatches) {
            ++matchedSourceIdentityCount;
        }

        const nlohmann::json diagnostics =
            sourceIdentityMatches
                ? ExtractStageEvidenceDiagnostics(stageEvidence)
                : nlohmann::json::object();
        const bool hasDiagnostics = diagnostics.is_object() &&
            diagnostics.value("version", 0) > 0;
        const bool hasCandidates = DiagnosticsHasCandidateDiagnostics(diagnostics);
        const bool hasSelectedCandidate = DiagnosticsHasSelectedCandidate(diagnostics);
        if (hasDiagnostics) {
            ++recordsWithDiagnostics;
        }
        if (hasCandidates) {
            ++recordsWithCandidates;
        }
        if (hasSelectedCandidate) {
            ++recordsWithSelectedCandidate;
        }

        std::set<std::string> candidateStageDiagnosticsInSource;
        const nlohmann::json candidates =
            diagnostics.value("candidates", nlohmann::json::array());
        if (candidates.is_array()) {
            for (const nlohmann::json& candidate : candidates) {
                if (!candidate.is_object()) {
                    continue;
                }
                const std::string candidateKind =
                    candidate.value("kind", std::string());
                if (candidateKind.empty()) {
                    continue;
                }
                for (const RequiredStageEvidenceSpec& stage : requiredStages) {
                    if (StageArrayHasCompleteStage(
                            candidate.value("stageDiagnostics", nlohmann::json::array()),
                            stage.id)) {
                        candidateStageDiagnosticsInSource.insert(
                            CandidateStageDiagnosticCoverageKey(candidateKind, stage.id));
                    }
                }
            }
        }

        nlohmann::json perSourceStageCoverage = nlohmann::json::array();
        nlohmann::json missingStages = nlohmann::json::array();
        bool allRequiredStagesComplete = !requiredStages.empty();
        for (const RequiredStageEvidenceSpec& stage : requiredStages) {
            const bool complete =
                DiagnosticsHasCompleteStageDiagnostics(diagnostics, stage.id);
            if (complete) {
                const int current =
                    stageCoverageById[stage.id].value("completeRecordCount", 0);
                stageCoverageById[stage.id]["completeRecordCount"] = current + 1;
            } else {
                allRequiredStagesComplete = false;
                missingStages.push_back(stage.id);
                stageCoverageById[stage.id]["missingSourceIds"].push_back(source.relativePathKey);
            }
            perSourceStageCoverage.push_back({
                { "stage", stage.id },
                { "label", stage.label },
                { "validationGap", stage.validationGap },
                { "complete", complete }
            });
        }

        nlohmann::json perSourceCandidateStageCoverage = nlohmann::json::array();
        nlohmann::json missingCandidateStageDiagnostics = nlohmann::json::array();
        bool allExpectedCandidateStagesComplete =
            hasCandidates && !expectedCandidateKinds.empty() && !requiredStages.empty();
        for (const std::string& kind : expectedCandidateKinds) {
            for (const RequiredStageEvidenceSpec& stage : requiredStages) {
                const std::string key =
                    CandidateStageDiagnosticCoverageKey(kind, stage.id);
                const bool complete =
                    candidateStageDiagnosticsInSource.find(key) !=
                    candidateStageDiagnosticsInSource.end();
                if (complete) {
                    const int current =
                        candidateStageCoverageByKey[key].value("completeRecordCount", 0);
                    candidateStageCoverageByKey[key]["completeRecordCount"] =
                        current + 1;
                } else {
                    allExpectedCandidateStagesComplete = false;
                    missingCandidateStageDiagnostics.push_back(key);
                    candidateStageCoverageByKey[key]["missingSourceIds"]
                        .push_back(source.relativePathKey);
                }
                perSourceCandidateStageCoverage.push_back({
                    { "key", key },
                    { "kind", kind },
                    { "label", StartingPointCandidateKindLabel(kind) },
                    { "stage", stage.id },
                    { "stageLabel", stage.label },
                    { "validationGap", stage.validationGap },
                    { "complete", complete }
                });
            }
        }
        if (allExpectedCandidateStagesComplete) {
            ++recordsWithExpectedCandidateStageDiagnostics;
        }

        std::string status = "evidence-complete";
        if (stageEvidence == nullptr) {
            status = "missing-stage-evidence";
            unmatchedSourceIds.push_back(source.relativePathKey);
        } else if (!sourceIdentityMatches) {
            status = "source-identity-mismatch";
            sourceIdentityMismatchSourceIds.push_back(source.relativePathKey);
        } else if (!hasDiagnostics) {
            status = "diagnostics-incomplete";
            missingDiagnosticsSourceIds.push_back(source.relativePathKey);
        } else if (!hasCandidates || !hasSelectedCandidate) {
            status = "candidate-diagnostics-incomplete";
            incompleteCandidateSourceIds.push_back(source.relativePathKey);
        } else if (!allRequiredStagesComplete) {
            status = "stage-diagnostics-incomplete";
            incompleteStageSourceIds.push_back(source.relativePathKey);
        }
        const bool ready = status == "evidence-complete";
        if (ready) {
            ++readySourceCount;
        } else {
            repairRecords[source.relativePathKey] =
                BuildStageEvidenceRepairRecord(
                    source,
                    status,
                    hasDiagnostics,
                    hasCandidates,
                    hasSelectedCandidate,
                    missingStages);
        }
        IncrementJsonCount(sourceStatusCounts, status);

        records.push_back({
            { "source", SerializeSourceIdentity(source) },
            { "stageEvidence", SerializeAnnotationMatch(
                options.stageEvidencePath,
                stageEvidence,
                stageEvidenceMatchedKey) },
            { "sourceIdentity", sourceIdentityCheck },
            { "status", status },
            { "readyForRecordMerge", ready },
            { "diagnostics", {
                { "present", hasDiagnostics },
                { "version", hasDiagnostics
                    ? diagnostics.value("version", 0)
                    : 0 },
                { "hasCandidateDiagnostics", hasCandidates },
                { "hasSelectedCandidate", hasSelectedCandidate }
            } },
            { "stageCoverage", std::move(perSourceStageCoverage) },
            { "missingStages", std::move(missingStages) },
            { "candidateStageCoverage", std::move(perSourceCandidateStageCoverage) },
            { "missingCandidateStageDiagnostics", std::move(missingCandidateStageDiagnostics) }
        });
    }

    nlohmann::json unmatchedStageEvidenceKeys = nlohmann::json::array();
    for (std::size_t i = 0; i < options.stageEvidenceEntries.size(); ++i) {
        if (matchedStageEvidenceIndices.find(static_cast<int>(i)) ==
            matchedStageEvidenceIndices.end()) {
            unmatchedStageEvidenceKeys.push_back(options.stageEvidenceEntries[i].primaryKey);
        }
    }

    for (const RequiredStageEvidenceSpec& stage : requiredStages) {
        nlohmann::json coverage = stageCoverageById[stage.id];
        const int completeCount = coverage.value("completeRecordCount", 0);
        coverage["missingRecordCount"] =
            std::max(0, static_cast<int>(sources.size()) - completeCount);
        coverage["complete"] =
            !sources.empty() && completeCount == static_cast<int>(sources.size());
        stageCoverage.push_back(std::move(coverage));
    }
    for (const std::string& kind : expectedCandidateKinds) {
        for (const RequiredStageEvidenceSpec& stage : requiredStages) {
            const std::string key =
                CandidateStageDiagnosticCoverageKey(kind, stage.id);
            nlohmann::json coverage = candidateStageCoverageByKey[key];
            const int completeCount = coverage.value("completeRecordCount", 0);
            coverage["missingRecordCount"] =
                std::max(0, static_cast<int>(sources.size()) - completeCount);
            coverage["complete"] =
                !sources.empty() && completeCount == static_cast<int>(sources.size());
            candidateStageCoverage.push_back(std::move(coverage));
        }
    }
    const bool allRequiredStagesComplete =
        !sources.empty() && incompleteStageSourceIds.empty();
    const bool candidateStageDiagnosticCoverageComplete =
        !sources.empty() &&
        recordsWithExpectedCandidateStageDiagnostics ==
            static_cast<int>(sources.size());

    nlohmann::json blockers = nlohmann::json::array();
    if (sources.empty()) {
        blockers.push_back("No RAW sources were scanned.");
    }
    if (!unmatchedSourceIds.empty()) {
        blockers.push_back("Some scanned sources have no matching stage evidence entry.");
    }
    if (!unmatchedStageEvidenceKeys.empty()) {
        blockers.push_back("Some stage evidence entries did not match scanned sources.");
    }
    if (!sourceIdentityMismatchSourceIds.empty()) {
        blockers.push_back("Some stage evidence entries matched by key but declare a different source identity token.");
    }
    if (!missingDiagnosticsSourceIds.empty()) {
        blockers.push_back("Some matched stage evidence entries do not contain versioned startingPointDiagnostics.");
    }
    if (!incompleteCandidateSourceIds.empty()) {
        blockers.push_back("Some stage evidence entries are missing candidate diagnostics or selected-candidate evidence.");
    }
    if (!incompleteStageSourceIds.empty()) {
        blockers.push_back("Some stage evidence entries are missing complete required named stage diagnostics.");
    }
    const bool hasRepairRecords = !repairRecords.empty();

    return {
        { "schema", "stack.raw-starting-point.validation-stage-evidence-check" },
        { "version", kStageEvidenceCheckSchemaVersion },
        { "workspaceRoot", options.workspaceRoot.string() },
        { "stageEvidenceFile", options.stageEvidencePath.string() },
        { "repairOutputFile", options.repairOutputPath.string() },
        { "sourceCount", sources.size() },
        { "stageEvidenceEntryCount", options.stageEvidenceEntries.size() },
        { "requiredStages", requiredStageDescriptions },
        { "sourceCoverage", {
            { "matchedSourceCount", matchedSourceCount },
            { "unmatchedSourceCount", sources.size() - matchedSourceCount },
            { "unmatchedSourceIds", unmatchedSourceIds },
            { "recordsWithSourceIdentityToken", recordsWithSourceIdentityToken },
            { "matchedSourceIdentityCount", matchedSourceIdentityCount },
            { "sourceIdentityMismatchSourceIds", sourceIdentityMismatchSourceIds }
        } },
        { "stageEvidenceCoverage", {
            { "matchedEntryCount", matchedStageEvidenceIndices.size() },
            { "unmatchedEntryCount", options.stageEvidenceEntries.size() - matchedStageEvidenceIndices.size() },
            { "unmatchedStageEvidenceKeys", unmatchedStageEvidenceKeys }
        } },
        { "candidateDiagnostics", {
            { "recordsWithDiagnostics", recordsWithDiagnostics },
            { "recordsWithCandidates", recordsWithCandidates },
            { "recordsWithSelectedCandidate", recordsWithSelectedCandidate },
            { "recordsWithExpectedCandidateStageDiagnostics", recordsWithExpectedCandidateStageDiagnostics },
            { "completeRecordCount", std::min(
                recordsWithDiagnostics,
                std::min(recordsWithCandidates, recordsWithSelectedCandidate)) },
            { "missingDiagnosticsSourceIds", missingDiagnosticsSourceIds },
            { "incompleteCandidateSourceIds", incompleteCandidateSourceIds }
        } },
        { "stageDiagnostics", {
            { "allRequiredStagesComplete", allRequiredStagesComplete },
            { "incompleteStageSourceIds", incompleteStageSourceIds },
            { "requiredStageCoverage", stageCoverage }
        } },
        { "candidateStageDiagnostics", {
            { "coverageComplete", candidateStageDiagnosticCoverageComplete },
            { "coverageIsGating", false },
            { "recordsWithExpectedCandidateStageDiagnostics", recordsWithExpectedCandidateStageDiagnostics },
            { "requiredCandidateStageCoverage", candidateStageCoverage }
        } },
        { "sourceStatusCounts", sourceStatusCounts },
        { "readiness", {
            { "readyForRecordMerge", blockers.empty() },
            { "readySourceCount", readySourceCount },
            { "blockingReasons", blockers },
            { "nextAction", blockers.empty()
                ? "Run --validate-raw-starting-point-records with --load-raw-safety, --annotations, and --stage-evidence on this workspace."
                : "Resolve blockingReasons before using this stage evidence for validation records." }
        } },
        { "sidecarRepair", {
            { "available", hasRepairRecords },
            { "repairRecordCount", repairRecords.size() },
            { "instructions",
              "Copy records from this object into the stage-evidence sidecar, replace placeholders with captured diagnostics, then rerun the stage-evidence check." },
            { "diagnosticCaptureContract", BuildStageEvidenceCaptureContract() },
            { "suggestedSidecarRepairPatch",
              BuildSidecarRepairPatch(
                  options.stageEvidencePath,
                  repairRecords,
                  "stage-evidence",
                  "Copy approved sidecarJsonPatch operations into the stage-evidence sidecar records object manually, replace placeholders with real captured diagnostics, then rerun the stage-evidence check. The check report never applies these operations automatically.") },
            { "records", std::move(repairRecords) }
        } },
        { "records", std::move(records) }
    };
}

bool ReadinessReportReadyForMerge(const nlohmann::json& report) {
    return report.is_object() &&
        report.value("readiness", nlohmann::json::object())
            .value("readyForRecordMerge", false);
}

nlohmann::json BuildSidecarRepairPatchSummary(
    const nlohmann::json& annotationCheck,
    const nlohmann::json& stageEvidenceCheck) {
    nlohmann::json entries = nlohmann::json::array();
    nlohmann::json blockers = nlohmann::json::array();
    int patchPreviewCount = 0;
    int availablePatchSourceCount = 0;
    int totalOperationCount = 0;

    auto addEntry = [&](const char* sidecarKind, const nlohmann::json& check) {
        const nlohmann::json sidecarRepair = check.is_object()
            ? check.value("sidecarRepair", nlohmann::json::object())
            : nlohmann::json::object();
        const nlohmann::json patch =
            sidecarRepair.value(
                "suggestedSidecarRepairPatch",
                nlohmann::json::object());
        const bool patchPresent = patch.is_object() && !patch.empty();
        const bool available =
            patchPresent && patch.value("available", false);
        const bool readyForManualCopy =
            patchPresent && patch.value("readyForManualCopy", false);
        const bool advisoryOnly =
            patchPresent && patch.value("advisoryOnly", false);
        const bool patchAppliesAutomatically =
            patchPresent &&
            patch.value("patchAppliesAutomatically", true);
        const bool mutatesSidecarAutomatically =
            patchPresent &&
            patch.value("mutatesSidecarAutomatically", true);
        const int operationCount = patchPresent
            ? patch.value("sidecarJsonPatchOperationCount", 0)
            : 0;

        if (patchPresent) {
            ++patchPreviewCount;
            totalOperationCount += std::max(0, operationCount);
            if (available) {
                ++availablePatchSourceCount;
            }
            if (!advisoryOnly) {
                blockers.push_back(
                    std::string(sidecarKind) +
                    " patch preview is not advisory-only.");
            }
            if (patchAppliesAutomatically) {
                blockers.push_back(
                    std::string(sidecarKind) +
                    " patch preview is marked as automatically applied.");
            }
            if (mutatesSidecarAutomatically) {
                blockers.push_back(
                    std::string(sidecarKind) +
                    " patch preview is marked as mutating the sidecar automatically.");
            }
            if (available && operationCount <= 0) {
                blockers.push_back(
                    std::string(sidecarKind) +
                    " patch preview is available but has no operations.");
            }
        }

        entries.push_back({
            { "sidecarKind", sidecarKind },
            { "patchPresent", patchPresent },
            { "available", available },
            { "readyForManualCopy", readyForManualCopy },
            { "advisoryOnly", advisoryOnly },
            { "patchAppliesAutomatically", patchAppliesAutomatically },
            { "mutatesSidecarAutomatically", mutatesSidecarAutomatically },
            { "sidecarJsonPatchOperationCount", operationCount },
            { "targetSidecarPath", patchPresent
                ? patch.value("targetSidecarPath", std::string())
                : std::string() },
            { "targetRecordsObjectPath", patchPresent
                ? patch.value("targetRecordsObjectPath", std::string())
                : std::string() },
            { "requiresCanonicalRecordsObject", patchPresent
                ? patch.value("requiresCanonicalRecordsObject", false)
                : false },
            { "recordIds", patchPresent
                ? patch.value("recordIds", nlohmann::json::array())
                : nlohmann::json::array() }
        });
    };

    addEntry("annotations", annotationCheck);
    addEntry("stage-evidence", stageEvidenceCheck);

    const bool hasOperations = totalOperationCount > 0;
    const bool readyForManualCopy =
        hasOperations && blockers.empty();
    return {
        { "available", hasOperations },
        { "readyForManualCopy", readyForManualCopy },
        { "advisoryOnly", true },
        { "manualCopyOnly", true },
        { "patchesApplyAutomatically", false },
        { "mutatesSidecarsAutomatically", false },
        { "patchPreviewCount", patchPreviewCount },
        { "availablePatchSourceCount", availablePatchSourceCount },
        { "sidecarJsonPatchOperationCount", totalOperationCount },
        { "blockingReasons", std::move(blockers) },
        { "entries", std::move(entries) },
        { "instructions",
          "Use nested sidecarRepair.suggestedSidecarRepairPatch.sidecarJsonPatch operations only as manual copy aids for sidecar records, then rerun the sidecar checks. The preflight report never applies patches or mutates sidecars automatically." }
    };
}

nlohmann::json BuildRecordSidecarPreflightReport(
    const RawStartingPointRecordOptions& options,
    const std::vector<Stack::RawWorkspace::SourceRecord>& sources) {
    nlohmann::json blockers = nlohmann::json::array();
    nlohmann::json annotationCheck;
    nlohmann::json stageEvidenceCheck;

    const bool annotationsPresent = !options.annotationPath.empty();
    const bool stageEvidencePresent = !options.stageEvidencePath.empty();
    if (!annotationsPresent) {
        blockers.push_back("--require-ready-sidecars requires --annotations.");
    } else {
        RawStartingPointAnnotationCheckOptions annotationOptions;
        annotationOptions.workspaceRoot = options.workspaceRoot;
        annotationOptions.annotationPath = options.annotationPath;
        annotationOptions.expectMinSources = options.expectMinSources;
        annotationOptions.maxSources = options.maxSources;
        annotationOptions.requireReady = true;
        annotationOptions.annotationEntries = options.annotationEntries;
        annotationCheck = BuildAnnotationCheckReport(annotationOptions, sources);
        if (!ReadinessReportReadyForMerge(annotationCheck)) {
            blockers.push_back("Annotations are not ready for record merge.");
        }
    }

    if (!stageEvidencePresent) {
        blockers.push_back("--require-ready-sidecars requires --stage-evidence.");
    } else {
        RawStartingPointStageEvidenceCheckOptions stageOptions;
        stageOptions.workspaceRoot = options.workspaceRoot;
        stageOptions.stageEvidencePath = options.stageEvidencePath;
        stageOptions.expectMinSources = options.expectMinSources;
        stageOptions.maxSources = options.maxSources;
        stageOptions.stageEvidenceEntries = options.stageEvidenceEntries;
        stageEvidenceCheck = BuildStageEvidenceCheckReport(stageOptions, sources);
        if (!ReadinessReportReadyForMerge(stageEvidenceCheck)) {
            blockers.push_back("Stage evidence is not ready for record merge.");
        }
    }

    const bool ready = blockers.empty();
    return {
        { "schema", "stack.raw-starting-point.validation-record-sidecar-preflight" },
        { "version", kRecordSidecarPreflightSchemaVersion },
        { "workspaceRoot", options.workspaceRoot.string() },
        { "sidecarPreflightOutputFile", options.sidecarPreflightOutputPath.string() },
        { "sourceCount", sources.size() },
        { "requireReadySidecars", true },
        { "annotationsRequired", true },
        { "annotationsPresent", annotationsPresent },
        { "stageEvidenceRequired", true },
        { "stageEvidencePresent", stageEvidencePresent },
        { "sidecarRepairPatchSummary",
          BuildSidecarRepairPatchSummary(annotationCheck, stageEvidenceCheck) },
        { "annotationCheck", annotationCheck.is_null()
            ? nlohmann::json::object()
            : annotationCheck },
        { "stageEvidenceCheck", stageEvidenceCheck.is_null()
            ? nlohmann::json::object()
            : stageEvidenceCheck },
        { "readyForRecordGeneration", ready },
        { "blockingReasons", blockers },
        { "nextAction", ready
            ? "Generate validation records with --load-raw-safety using the ready sidecars."
            : "Resolve blockingReasons before generating validation records." },
        { "validationWorkflow", BuildValidationWorkflowChecklist(
            options.workspaceRoot,
            options.annotationTemplateOutputPath,
            options.annotationPath,
            options.stageEvidenceTemplateOutputPath,
            options.stageEvidencePath,
            options.outputPath) }
    };
}

void PrintRecordSidecarPreflightFailure(const nlohmann::json& preflight) {
    std::cerr << "RAW Starting Point record validation failed: "
              << "--require-ready-sidecars blocked record generation.\n";
    const nlohmann::json blockers =
        preflight.value("blockingReasons", nlohmann::json::array());
    if (!blockers.is_array()) {
        return;
    }
    for (const nlohmann::json& blocker : blockers) {
        if (blocker.is_string()) {
            std::cerr << "  - " << blocker.get<std::string>() << "\n";
        }
    }
}

nlohmann::json BuildValidationSetSummary(
    const nlohmann::json& report,
    const std::filesystem::path& recordsPath = std::filesystem::path()) {
    const bool reportObject = report.is_object();
    const nlohmann::json emptyRecords = nlohmann::json::array();
    const nlohmann::json& records =
        reportObject && report.contains("records") && report["records"].is_array()
            ? report["records"]
            : emptyRecords;
    const std::vector<std::string> requiredHumanReviewFields = RequiredHumanReviewFields();
    const std::vector<std::string> recommendedCategories = RecommendedValidationCategories();
    const std::vector<RequiredStageEvidenceSpec> requiredStageEvidence =
        RequiredStageEvidenceSpecs();
    const std::vector<std::string> requiredActionReadinessLabels =
        RequiredActionReadinessDiagnosticLabels();
    const std::vector<ActionReadinessDetailGuardSpec> requiredActionReadinessDetailGuards =
        RequiredActionReadinessDetailGuards();
    const std::vector<std::string> expectedCandidateKinds =
        ExpectedStartingPointCandidateKinds();
    const std::vector<ExpectedStartingPointCandidateUiLineSpec> expectedVisibleControlLines =
        ExpectedStartingPointCandidateVisibleControlLines();
    const std::vector<TrackedStartingPointCandidateControlValueUiLineSpec> trackedControlValueLines =
        TrackedStartingPointCandidateControlValueLines();
    const std::vector<ExpectedStartingPointCandidateUiLineSpec> expectedScoreComponentLines =
        ExpectedStartingPointCandidateScoreComponentLines();
    const std::vector<ExpectedStartingPointCandidateUiLineSpec> expectedWarningLines =
        ExpectedStartingPointCandidateWarningLines();
    std::vector<std::string> expectedVisibleControlLineLabels;
    std::vector<std::string> trackedControlValueLineLabels;
    std::vector<std::string> expectedScoreComponentLineLabels;
    std::vector<std::string> expectedWarningLineLabels;
    std::vector<std::string> expectedCandidateStageDiagnosticLabels;
    for (const ExpectedStartingPointCandidateUiLineSpec& spec : expectedVisibleControlLines) {
        expectedVisibleControlLineLabels.push_back(spec.lineLabel);
    }
    for (const TrackedStartingPointCandidateControlValueUiLineSpec& spec : trackedControlValueLines) {
        trackedControlValueLineLabels.push_back(spec.lineLabel);
    }
    for (const ExpectedStartingPointCandidateUiLineSpec& spec : expectedScoreComponentLines) {
        expectedScoreComponentLineLabels.push_back(spec.lineLabel);
    }
    for (const ExpectedStartingPointCandidateUiLineSpec& spec : expectedWarningLines) {
        expectedWarningLineLabels.push_back(spec.lineLabel);
    }
    for (const std::string& kind : expectedCandidateKinds) {
        for (const RequiredStageEvidenceSpec& stage : requiredStageEvidence) {
            expectedCandidateStageDiagnosticLabels.push_back(
                CandidateStageDiagnosticCoverageLabel(kind, stage.label));
        }
    }

    nlohmann::json recordStatusCounts = nlohmann::json::object();
    nlohmann::json categoryTagCounts = nlohmann::json::object();
    nlohmann::json validationGapCounts = nlohmann::json::object();
    nlohmann::json missingHumanReviewFieldCounts = nlohmann::json::object();
    nlohmann::json humanReviewBoolCounts = nlohmann::json::object();
    nlohmann::json nextManualControlCounts = nlohmann::json::object();
    nlohmann::json completeStageRecordCounts = nlohmann::json::object();
    nlohmann::json missingStageRecordIdsById = nlohmann::json::object();
    nlohmann::json candidateKindRecordCounts = nlohmann::json::object();
    nlohmann::json selectedCandidateKindCounts = nlohmann::json::object();
    nlohmann::json missingCandidateKindRecordIdsByKind = nlohmann::json::object();
    nlohmann::json missingCandidateScoreRecordIdsByKind = nlohmann::json::object();
    nlohmann::json candidateVisibleControlLineRecordCounts = nlohmann::json::object();
    nlohmann::json candidateVisibleControlValueCountsByKind = nlohmann::json::object();
    nlohmann::json missingCandidateVisibleControlLineRecordIdsByKind = nlohmann::json::object();
    nlohmann::json candidateControlValueLineRecordCounts = nlohmann::json::object();
    nlohmann::json candidateControlValueLineDetailRecordCounts = nlohmann::json::object();
    nlohmann::json candidateControlValueCountsByKey = nlohmann::json::object();
    nlohmann::json missingCandidateControlValueLineRecordIdsByKey = nlohmann::json::object();
    nlohmann::json missingCandidateControlValueLineDetailRecordIdsByKey = nlohmann::json::object();
    nlohmann::json candidateScoreComponentLineRecordCounts = nlohmann::json::object();
    nlohmann::json candidateScoreComponentValueCountsByKind = nlohmann::json::object();
    nlohmann::json missingCandidateScoreComponentLineRecordIdsByKind = nlohmann::json::object();
    nlohmann::json candidateWarningLineRecordCounts = nlohmann::json::object();
    nlohmann::json candidateWarningValueCountsByKind = nlohmann::json::object();
    nlohmann::json missingCandidateWarningLineRecordIdsByKind = nlohmann::json::object();
    nlohmann::json candidateStageDiagnosticRecordCounts = nlohmann::json::object();
    nlohmann::json missingCandidateStageDiagnosticRecordIdsByKey = nlohmann::json::object();
    nlohmann::json candidateStageEvidenceStatusCountsByKey = nlohmann::json::object();
    nlohmann::json candidateStageEvidenceNonCompleteRecordIdsByKey = nlohmann::json::object();
    nlohmann::json sourceLineValueCounts = nlohmann::json::object();
    nlohmann::json missingSourceLineRecordIds = nlohmann::json::array();
    nlohmann::json missingSourceScopeDetailRecordIds = nlohmann::json::array();
    nlohmann::json selectedCandidateDetailValueCounts = nlohmann::json::object();
    nlohmann::json missingSelectedCandidateDetailLineRecordIds = nlohmann::json::array();
    nlohmann::json missingSelectedCandidateVisibleControlDetailRecordIds = nlohmann::json::array();
    nlohmann::json candidateScoreOrderValueCounts = nlohmann::json::object();
    nlohmann::json missingCandidateScoreOrderLineRecordIds = nlohmann::json::array();
    nlohmann::json missingCandidateScoreOrderGuardrailDetailRecordIds = nlohmann::json::array();
    nlohmann::json visibleActionScopeValueCounts = nlohmann::json::object();
    nlohmann::json missingVisibleActionScopeLineRecordIds = nlohmann::json::array();
    nlohmann::json missingVisibleActionScopeGuardrailDetailRecordIds = nlohmann::json::array();
    nlohmann::json dryRunValueCounts = nlohmann::json::object();
    nlohmann::json missingDryRunLineRecordIds = nlohmann::json::array();
    nlohmann::json missingDryRunReadOnlyDetailRecordIds = nlohmann::json::array();
    nlohmann::json recipeWritesValueCounts = nlohmann::json::object();
    nlohmann::json missingRecipeWritesLineRecordIds = nlohmann::json::array();
    nlohmann::json missingRecipeWritesExplicitActionDetailRecordIds = nlohmann::json::array();
    nlohmann::json stageEvidenceValueCounts = nlohmann::json::object();
    nlohmann::json missingStageEvidenceLineRecordIds = nlohmann::json::array();
    nlohmann::json missingStageEvidenceSourceDetailRecordIds = nlohmann::json::array();
    nlohmann::json partialEvidenceValueCounts = nlohmann::json::object();
    nlohmann::json missingPartialEvidenceUiLineRecordIds = nlohmann::json::array();
    nlohmann::json partialEvidenceLineCountMismatchRecordIds = nlohmann::json::array();
    nlohmann::json partialEvidenceLineWithoutWarningRecordIds = nlohmann::json::array();
    nlohmann::json actionReadinessLineRecordCounts = nlohmann::json::object();
    nlohmann::json actionReadinessValueCountsByLabel = nlohmann::json::object();
    nlohmann::json missingActionReadinessRecordIdsByLabel = nlohmann::json::object();
    nlohmann::json actionReadinessGuardrailDetailRecordCounts = nlohmann::json::object();
    nlohmann::json missingActionReadinessGuardrailDetailRecordIdsByLabel = nlohmann::json::object();
    nlohmann::json recordDiagnosticCompleteness = nlohmann::json::array();
    std::map<std::string, int> candidateScoreRecordCounts;
    std::map<std::string, double> candidateScoreSums;
    std::map<std::string, double> candidateScoreMins;
    std::map<std::string, double> candidateScoreMaxes;
    for (const RequiredStageEvidenceSpec& stage : requiredStageEvidence) {
        completeStageRecordCounts[stage.id] = 0;
        missingStageRecordIdsById[stage.id] = nlohmann::json::array();
    }
    for (const std::string& kind : expectedCandidateKinds) {
        candidateKindRecordCounts[kind] = 0;
        selectedCandidateKindCounts[kind] = 0;
        missingCandidateKindRecordIdsByKind[kind] = nlohmann::json::array();
        missingCandidateScoreRecordIdsByKind[kind] = nlohmann::json::array();
        candidateScoreRecordCounts[kind] = 0;
        candidateScoreSums[kind] = 0.0;
        candidateScoreMins[kind] = 0.0;
        candidateScoreMaxes[kind] = 0.0;
    }
    for (const ExpectedStartingPointCandidateUiLineSpec& spec : expectedVisibleControlLines) {
        candidateVisibleControlLineRecordCounts[spec.kind] = 0;
        candidateVisibleControlValueCountsByKind[spec.kind] = nlohmann::json::object();
        missingCandidateVisibleControlLineRecordIdsByKind[spec.kind] =
            nlohmann::json::array();
    }
    for (const TrackedStartingPointCandidateControlValueUiLineSpec& spec : trackedControlValueLines) {
        candidateControlValueLineRecordCounts[spec.key] = 0;
        candidateControlValueLineDetailRecordCounts[spec.key] = 0;
        candidateControlValueCountsByKey[spec.key] = nlohmann::json::object();
        missingCandidateControlValueLineRecordIdsByKey[spec.key] =
            nlohmann::json::array();
        missingCandidateControlValueLineDetailRecordIdsByKey[spec.key] =
            nlohmann::json::array();
    }
    for (const ExpectedStartingPointCandidateUiLineSpec& spec : expectedScoreComponentLines) {
        candidateScoreComponentLineRecordCounts[spec.kind] = 0;
        candidateScoreComponentValueCountsByKind[spec.kind] = nlohmann::json::object();
        missingCandidateScoreComponentLineRecordIdsByKind[spec.kind] =
            nlohmann::json::array();
    }
    for (const ExpectedStartingPointCandidateUiLineSpec& spec : expectedWarningLines) {
        candidateWarningLineRecordCounts[spec.kind] = 0;
        candidateWarningValueCountsByKind[spec.kind] = nlohmann::json::object();
        missingCandidateWarningLineRecordIdsByKind[spec.kind] =
            nlohmann::json::array();
    }
    for (const std::string& kind : expectedCandidateKinds) {
        for (const RequiredStageEvidenceSpec& stage : requiredStageEvidence) {
            const std::string key =
                CandidateStageDiagnosticCoverageKey(kind, stage.id);
            candidateStageDiagnosticRecordCounts[key] = 0;
            missingCandidateStageDiagnosticRecordIdsByKey[key] =
                nlohmann::json::array();
            candidateStageEvidenceStatusCountsByKey[key] =
                nlohmann::json::object();
            candidateStageEvidenceNonCompleteRecordIdsByKey[key] =
                nlohmann::json::array();
        }
    }
    for (const std::string& label : requiredActionReadinessLabels) {
        actionReadinessLineRecordCounts[label] = 0;
        actionReadinessValueCountsByLabel[label] = nlohmann::json::object();
        missingActionReadinessRecordIdsByLabel[label] = nlohmann::json::array();
    }
    for (const ActionReadinessDetailGuardSpec& spec : requiredActionReadinessDetailGuards) {
        actionReadinessGuardrailDetailRecordCounts[spec.label] = 0;
        missingActionReadinessGuardrailDetailRecordIdsByLabel[spec.label] =
            nlohmann::json::array();
    }

    nlohmann::json invalidRecordIds = nlohmann::json::array();
    nlohmann::json metadataMissingRecordIds = nlohmann::json::array();
    nlohmann::json rawSafetyNotRequestedRecordIds = nlohmann::json::array();
    nlohmann::json rawSafetyUnavailableRecordIds = nlohmann::json::array();
    nlohmann::json missingCategoryRecordIds = nlohmann::json::array();
    nlohmann::json incompleteHumanReviewRecordIds = nlohmann::json::array();
    nlohmann::json missingCandidateDiagnosticsRecordIds = nlohmann::json::array();
    nlohmann::json missingSelectedCandidateRecordIds = nlohmann::json::array();
    nlohmann::json missingVisibleRecipeWriteAuditRecordIds = nlohmann::json::array();
    nlohmann::json noVisibleRecipeWriteAuditControlsRecordIds = nlohmann::json::array();
    nlohmann::json missingVisibleRecipeWriteAuditValueRecordIds = nlohmann::json::array();
    nlohmann::json missingActionReadinessRecordIds = nlohmann::json::array();
    nlohmann::json visibleFieldChangeRecordIds = nlohmann::json::array();
    nlohmann::json visibleRecipeWriteAuditControlCounts = nlohmann::json::object();
    nlohmann::json visibleRecipeWriteAuditRecipeFieldCounts = nlohmann::json::object();
    nlohmann::json visibleRecipeWriteAuditValueControlCounts = nlohmann::json::object();

    int metadataLoadedCount = 0;
    int rawSafetyLoadedCount = 0;
    int recordsWithRawSafetyRequested = 0;
    int annotationMatchedRecordCount = 0;
    int stageEvidenceMatchedRecordCount = 0;
    int stageEvidenceAppliedRecordCount = 0;
    int recordsRequiringCategoryTags = 0;
    int recordsWithCategoryTags = 0;
    int humanReviewCompleteCount = 0;
    int recordsWithVisibleFieldChanges = 0;
    int recordsWithVisibleRecipeWriteAudit = 0;
    int recordsWithVisibleRecipeWriteAuditControls = 0;
    int totalAuditedVisibleRecipeControlWrites = 0;
    int recordsWithVisibleRecipeWriteAuditValues = 0;
    int totalAuditedVisibleRecipeControlValues = 0;
    int recordsWithStartingPointDiagnostics = 0;
    int recordsWithCandidateDiagnostics = 0;
    int recordsWithExpectedCandidateKinds = 0;
    int recordsWithExpectedCandidateScores = 0;
    int recordsWithExpectedVisibleControlLines = 0;
    int recordsWithTrackedCandidateControlValueLines = 0;
    int recordsWithTrackedCandidateControlValueLineDetails = 0;
    int recordsWithExpectedScoreComponentLines = 0;
    int recordsWithExpectedWarningLines = 0;
    int recordsWithExpectedCandidateStageDiagnostics = 0;
    int recordsWithSourceLine = 0;
    int recordsWithSourceScopeDetail = 0;
    int recordsWithSelectedCandidate = 0;
    int recordsWithSelectedCandidateDetailLine = 0;
    int recordsWithSelectedCandidateVisibleControlDetail = 0;
    int recordsWithCandidateScoreOrderLine = 0;
    int recordsWithCandidateScoreOrderGuardrailDetail = 0;
    int recordsWithVisibleActionScopeLine = 0;
    int recordsWithVisibleActionScopeGuardrailDetail = 0;
    int recordsWithDryRunLine = 0;
    int recordsWithDryRunReadOnlyDetail = 0;
    int recordsWithRecipeWritesLine = 0;
    int recordsWithRecipeWritesExplicitActionDetail = 0;
    int recordsWithStageEvidenceLine = 0;
    int recordsWithStageEvidenceSourceDetail = 0;
    int recordsWithPartialEvidenceWarnings = 0;
    int recordsWithPartialEvidenceUiLines = 0;
    int totalPartialEvidenceWarnings = 0;
    int totalPartialEvidenceUiLines = 0;
    int recordsWithActionReadinessDiagnostics = 0;
    int recordsWithActionReadinessGuardrailDetails = 0;
    int recordsWithCompleteUiDiagnosticSet = 0;

    for (std::size_t recordIndex = 0; recordIndex < records.size(); ++recordIndex) {
        const nlohmann::json& record = records[recordIndex];
        const std::string recordId =
            RecordSourceId(record, static_cast<int>(recordIndex));
        if (!record.is_object()) {
            IncrementJsonCount(recordStatusCounts, "invalid-record");
            invalidRecordIds.push_back(recordId);
            recordDiagnosticCompleteness.push_back(
                BuildStartingPointDiagnosticCompletenessRow(
                    recordId,
                    static_cast<int>(recordIndex),
                    false,
                    "invalid-record",
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    false,
                    0,
                    0,
                    expectedCandidateKinds,
                    expectedCandidateKinds,
                    expectedVisibleControlLineLabels,
                    trackedControlValueLineLabels,
                    trackedControlValueLineLabels,
                    expectedScoreComponentLineLabels,
                    expectedWarningLineLabels,
                    expectedCandidateStageDiagnosticLabels,
                    requiredActionReadinessLabels,
                    requiredActionReadinessLabels));
            continue;
        }

        const std::string recordStatus = record.value("recordStatus", "missing");
        IncrementJsonCount(recordStatusCounts, recordStatus);

        const nlohmann::json metadata = record.value("metadata", nlohmann::json::object());
        if (metadata.is_object() && metadata.value("loaded", false)) {
            ++metadataLoadedCount;
        } else {
            metadataMissingRecordIds.push_back(recordId);
        }

        const nlohmann::json rawBufferSafety = record.value("rawBufferSafety", nlohmann::json::object());
        const bool rawSafetyRequested =
            rawBufferSafety.is_object() && rawBufferSafety.value("requested", false);
        const bool rawSafetyLoaded =
            rawBufferSafety.is_object() && rawBufferSafety.value("loaded", false);
        if (rawSafetyRequested) {
            ++recordsWithRawSafetyRequested;
        } else {
            rawSafetyNotRequestedRecordIds.push_back(recordId);
        }
        if (rawSafetyLoaded) {
            ++rawSafetyLoadedCount;
        } else if (rawSafetyRequested) {
            rawSafetyUnavailableRecordIds.push_back(recordId);
        }

        const nlohmann::json annotation = record.value("annotation", nlohmann::json::object());
        if (annotation.is_object() && annotation.value("matched", false)) {
            ++annotationMatchedRecordCount;
        }

        const nlohmann::json stageEvidence = record.value("stageEvidence", nlohmann::json::object());
        if (stageEvidence.is_object()) {
            if (stageEvidence.value("matched", false)) {
                ++stageEvidenceMatchedRecordCount;
            }
            if (stageEvidence.value("appliedStartingPointDiagnostics", false)) {
                ++stageEvidenceAppliedRecordCount;
            }
        }

        if (record.value("requiresHumanCategoryTags", false)) {
            ++recordsRequiringCategoryTags;
        }

        const nlohmann::json tags = record.value("imageCategoryTags", nlohmann::json::array());
        if (tags.is_array() && !tags.empty()) {
            ++recordsWithCategoryTags;
            for (const nlohmann::json& tag : tags) {
                if (tag.is_string()) {
                    IncrementJsonCount(categoryTagCounts, tag.get<std::string>());
                }
            }
        } else {
            missingCategoryRecordIds.push_back(recordId);
        }

        const nlohmann::json gaps = record.value("validationGaps", nlohmann::json::array());
        if (gaps.is_array()) {
            for (const nlohmann::json& gap : gaps) {
                if (gap.is_string()) {
                    IncrementJsonCount(validationGapCounts, gap.get<std::string>());
                }
            }
        }

        const nlohmann::json visibleFieldsChanged =
            record.value("visibleFieldsChanged", nlohmann::json::array());
        if (visibleFieldsChanged.is_array() && !visibleFieldsChanged.empty()) {
            ++recordsWithVisibleFieldChanges;
            visibleFieldChangeRecordIds.push_back(recordId);
        }

        const nlohmann::json visibleRecipeWriteAudit =
            record.value("visibleRecipeWriteAudit", nlohmann::json::object());
        if (visibleRecipeWriteAudit.is_object() &&
            visibleRecipeWriteAudit.value("available", false)) {
            ++recordsWithVisibleRecipeWriteAudit;
            const nlohmann::json auditControls =
                visibleRecipeWriteAudit.value("controls", nlohmann::json::array());
            if (auditControls.is_array() && !auditControls.empty()) {
                ++recordsWithVisibleRecipeWriteAuditControls;
                totalAuditedVisibleRecipeControlWrites +=
                    static_cast<int>(auditControls.size());
                bool allControlsHaveProposedValues = true;
                for (const nlohmann::json& control : auditControls) {
                    if (!control.is_object()) {
                        allControlsHaveProposedValues = false;
                        continue;
                    }
                    const std::string controlId =
                        control.value("id", std::string());
                    if (!controlId.empty()) {
                        IncrementJsonCount(
                            visibleRecipeWriteAuditControlCounts,
                            controlId);
                    }
                    const std::string recipeField =
                        control.value("recipeField", std::string());
                    if (!recipeField.empty()) {
                        IncrementJsonCount(
                            visibleRecipeWriteAuditRecipeFieldCounts,
                            recipeField);
                    }
                    if (control.value("hasProposedValue", false)) {
                        ++totalAuditedVisibleRecipeControlValues;
                        if (!controlId.empty()) {
                            IncrementJsonCount(
                                visibleRecipeWriteAuditValueControlCounts,
                                controlId);
                        }
                    } else {
                        allControlsHaveProposedValues = false;
                    }
                }
                if (allControlsHaveProposedValues) {
                    ++recordsWithVisibleRecipeWriteAuditValues;
                } else {
                    missingVisibleRecipeWriteAuditValueRecordIds.push_back(recordId);
                }
            } else {
                noVisibleRecipeWriteAuditControlsRecordIds.push_back(recordId);
                missingVisibleRecipeWriteAuditValueRecordIds.push_back(recordId);
            }
        } else {
            missingVisibleRecipeWriteAuditRecordIds.push_back(recordId);
            missingVisibleRecipeWriteAuditValueRecordIds.push_back(recordId);
        }

        const nlohmann::json startingPointDiagnostics =
            record.value("startingPointDiagnostics", nlohmann::json::object());
        bool hasStartingPointDiagnostics = false;
        bool hasCandidateDiagnostics = false;
        bool hasSourceLine = false;
        bool hasSourceScopeDetail = false;
        bool hasSelectedCandidate = false;
        bool hasSelectedCandidateDetailLine = false;
        bool hasSelectedCandidateVisibleControlDetail = false;
        bool hasCandidateScoreOrderLine = false;
        bool hasCandidateScoreOrderGuardrailDetail = false;
        bool hasVisibleActionScopeLine = false;
        bool hasVisibleActionScopeGuardrailDetail = false;
        bool hasDryRunLine = false;
        bool hasDryRunReadOnlyDetail = false;
        bool hasRecipeWritesLine = false;
        bool hasRecipeWritesExplicitActionDetail = false;
        bool hasStageEvidenceLine = false;
        bool hasStageEvidenceSourceDetail = false;
        int partialEvidenceWarningCount = 0;
        int partialEvidenceUiLineCount = 0;
        std::vector<std::string> missingCandidateKindsInRecord;
        std::vector<std::string> missingCandidateScoresInRecord;
        std::vector<std::string> missingVisibleControlLinesInRecord;
        std::vector<std::string> missingCandidateControlValueLinesInRecord;
        std::vector<std::string> missingCandidateControlValueLineDetailsInRecord;
        std::vector<std::string> missingScoreComponentLinesInRecord;
        std::vector<std::string> missingWarningLinesInRecord;
        std::vector<std::string> missingCandidateStageDiagnosticsInRecord;
        std::vector<std::string> missingActionReadinessLinesInRecord;
        std::vector<std::string> missingActionReadinessGuardrailDetailsInRecord;
        std::set<std::string> candidateKindsInRecord;
        std::set<std::string> candidateStageDiagnosticsInRecord;
        std::map<std::string, double> candidateScoresByKindInRecord;
        bool candidateStageEvidenceAccountingRecorded = false;
        if (startingPointDiagnostics.is_object() &&
            startingPointDiagnostics.value("version", 0) > 0) {
            hasStartingPointDiagnostics = true;
            ++recordsWithStartingPointDiagnostics;
            const nlohmann::json candidates =
                startingPointDiagnostics.value("candidates", nlohmann::json::array());
            if (candidates.is_array() && !candidates.empty()) {
                hasCandidateDiagnostics = true;
                ++recordsWithCandidateDiagnostics;
                for (const nlohmann::json& candidate : candidates) {
                    if (!candidate.is_object()) {
                        continue;
                    }
                    const std::string candidateKind =
                        candidate.value("kind", std::string());
                    if (!candidateKind.empty()) {
                        candidateKindsInRecord.insert(candidateKind);
                        double totalScore = 0.0;
                        if (CandidateHasValidTotalScore(candidate, &totalScore)) {
                            candidateScoresByKindInRecord[candidateKind] = totalScore;
                        }
                        for (const RequiredStageEvidenceSpec& stage : requiredStageEvidence) {
                            if (StageArrayHasCompleteStage(
                                    candidate.value("stageDiagnostics", nlohmann::json::array()),
                                    stage.id)) {
                                candidateStageDiagnosticsInRecord.insert(
                                    CandidateStageDiagnosticCoverageKey(candidateKind, stage.id));
                            }
                        }
                    }
                }
            }
            for (const std::string& kind : expectedCandidateKinds) {
                const nlohmann::json* candidate =
                    FindCandidateByKind(candidates, kind);
                for (const RequiredStageEvidenceSpec& stage : requiredStageEvidence) {
                    const std::string key =
                        CandidateStageDiagnosticCoverageKey(kind, stage.id);
                    const std::string status =
                        CandidateStageStatusForAccounting(candidate, stage.id);
                    IncrementJsonCount(
                        candidateStageEvidenceStatusCountsByKey[key],
                        status);
                    if (status != "complete") {
                        candidateStageEvidenceNonCompleteRecordIdsByKey[key]
                            .push_back(recordId);
                    }
                }
            }
            candidateStageEvidenceAccountingRecorded = true;
            bool foundSourceLine = false;
            const std::string sourceLineValue =
                DiagnosticsUiViewLineValue(
                    startingPointDiagnostics,
                    "Source",
                    &foundSourceLine);
            bool foundSourceLineDetail = false;
            const std::string sourceLineDetail =
                DiagnosticsUiViewLineDetail(
                    startingPointDiagnostics,
                    "Source",
                    &foundSourceLineDetail);
            if (foundSourceLine) {
                hasSourceLine = true;
                ++recordsWithSourceLine;
                IncrementJsonCount(
                    sourceLineValueCounts,
                    NormalizedDiagnosticsLineValue(sourceLineValue));
            }
            if (foundSourceLineDetail &&
                sourceLineDetail.find("scoped to this RAW source") != std::string::npos) {
                hasSourceScopeDetail = true;
                ++recordsWithSourceScopeDetail;
            }
            if (startingPointDiagnostics.value("hasSelectedCandidate", false)) {
                hasSelectedCandidate = true;
                ++recordsWithSelectedCandidate;
                std::string selectedKind =
                    startingPointDiagnostics.value("selectedCandidateKind", std::string());
                if (selectedKind.empty() && candidates.is_array()) {
                    const int selectedIndex =
                        startingPointDiagnostics.value("selectedCandidateIndex", -1);
                    if (selectedIndex >= 0 &&
                        selectedIndex < static_cast<int>(candidates.size()) &&
                        candidates[static_cast<std::size_t>(selectedIndex)].is_object()) {
                        selectedKind =
                            candidates[static_cast<std::size_t>(selectedIndex)]
                                .value("kind", std::string());
                    }
                }
                if (!selectedKind.empty()) {
                    IncrementJsonCount(selectedCandidateKindCounts, selectedKind);
                }
            }
            bool foundSelectedCandidateDetailLine = false;
            const std::string selectedCandidateLineValue =
                DiagnosticsUiViewLineValue(
                    startingPointDiagnostics,
                    "Diagnostic selection",
                    &foundSelectedCandidateDetailLine);
            bool foundSelectedCandidateLineDetail = false;
            const std::string selectedCandidateLineDetail =
                DiagnosticsUiViewLineDetail(
                    startingPointDiagnostics,
                    "Diagnostic selection",
                    &foundSelectedCandidateLineDetail);
            if (foundSelectedCandidateDetailLine) {
                hasSelectedCandidateDetailLine = true;
                ++recordsWithSelectedCandidateDetailLine;
                IncrementJsonCount(
                    selectedCandidateDetailValueCounts,
                    NormalizedDiagnosticsLineValue(selectedCandidateLineValue));
            }
            if (foundSelectedCandidateLineDetail &&
                selectedCandidateLineDetail.find("Visible controls:") != std::string::npos) {
                hasSelectedCandidateVisibleControlDetail = true;
                ++recordsWithSelectedCandidateVisibleControlDetail;
            }
            bool foundCandidateScoreOrderLine = false;
            const std::string candidateScoreOrderValue =
                DiagnosticsUiViewLineValue(
                    startingPointDiagnostics,
                    "Candidate score order",
                    &foundCandidateScoreOrderLine);
            bool foundCandidateScoreOrderDetail = false;
            const std::string candidateScoreOrderDetail =
                DiagnosticsUiViewLineDetail(
                    startingPointDiagnostics,
                    "Candidate score order",
                    &foundCandidateScoreOrderDetail);
            if (foundCandidateScoreOrderLine) {
                hasCandidateScoreOrderLine = true;
                ++recordsWithCandidateScoreOrderLine;
                IncrementJsonCount(
                    candidateScoreOrderValueCounts,
                    NormalizedDiagnosticsLineValue(candidateScoreOrderValue));
            }
            if (foundCandidateScoreOrderDetail &&
                candidateScoreOrderDetail.find("does not change candidate scoring") !=
                    std::string::npos) {
                hasCandidateScoreOrderGuardrailDetail = true;
                ++recordsWithCandidateScoreOrderGuardrailDetail;
            }
            bool foundVisibleActionScopeLine = false;
            const std::string visibleActionScopeValue =
                DiagnosticsUiViewLineValue(
                    startingPointDiagnostics,
                    "Visible action scope",
                    &foundVisibleActionScopeLine);
            bool foundVisibleActionScopeDetail = false;
            const std::string visibleActionScopeDetail =
                DiagnosticsUiViewLineDetail(
                    startingPointDiagnostics,
                    "Visible action scope",
                    &foundVisibleActionScopeDetail);
            if (foundVisibleActionScopeLine) {
                hasVisibleActionScopeLine = true;
                ++recordsWithVisibleActionScopeLine;
                IncrementJsonCount(
                    visibleActionScopeValueCounts,
                    NormalizedDiagnosticsLineValue(visibleActionScopeValue));
            }
            if (foundVisibleActionScopeDetail &&
                visibleActionScopeDetail.find(
                    "Build Starting Point writes safe visible controls") !=
                    std::string::npos &&
                visibleActionScopeDetail.find("does not apply recipe values") !=
                    std::string::npos) {
                hasVisibleActionScopeGuardrailDetail = true;
                ++recordsWithVisibleActionScopeGuardrailDetail;
            }
            bool foundDryRunLine = false;
            const std::string dryRunValue =
                DiagnosticsUiViewLineValue(
                    startingPointDiagnostics,
                    "Dry run",
                    &foundDryRunLine);
            bool foundDryRunDetail = false;
            const std::string dryRunDetail =
                DiagnosticsUiViewLineDetail(
                    startingPointDiagnostics,
                    "Dry run",
                    &foundDryRunDetail);
            if (foundDryRunLine) {
                hasDryRunLine = true;
                ++recordsWithDryRunLine;
                IncrementJsonCount(
                    dryRunValueCounts,
                    NormalizedDiagnosticsLineValue(dryRunValue));
            }
            if (foundDryRunDetail &&
                dryRunDetail.find("does not write recipes") != std::string::npos) {
                hasDryRunReadOnlyDetail = true;
                ++recordsWithDryRunReadOnlyDetail;
            }
            bool foundRecipeWritesLine = false;
            const std::string recipeWritesValue =
                DiagnosticsUiViewLineValue(
                    startingPointDiagnostics,
                    "Recipe writes",
                    &foundRecipeWritesLine);
            bool foundRecipeWritesDetail = false;
            const std::string recipeWritesDetail =
                DiagnosticsUiViewLineDetail(
                    startingPointDiagnostics,
                    "Recipe writes",
                    &foundRecipeWritesDetail);
            if (foundRecipeWritesLine) {
                hasRecipeWritesLine = true;
                ++recordsWithRecipeWritesLine;
                IncrementJsonCount(
                    recipeWritesValueCounts,
                    NormalizedDiagnosticsLineValue(recipeWritesValue));
            }
            if (foundRecipeWritesDetail &&
                recipeWritesDetail.find("through explicit UI actions") != std::string::npos) {
                hasRecipeWritesExplicitActionDetail = true;
                ++recordsWithRecipeWritesExplicitActionDetail;
            }
            bool foundStageEvidenceLine = false;
            const std::string stageEvidenceValue =
                DiagnosticsUiViewLineValue(
                    startingPointDiagnostics,
                    "Stage evidence",
                    &foundStageEvidenceLine);
            bool foundStageEvidenceDetail = false;
            const std::string stageEvidenceDetail =
                DiagnosticsUiViewLineDetail(
                    startingPointDiagnostics,
                    "Stage evidence",
                    &foundStageEvidenceDetail);
            if (foundStageEvidenceLine) {
                hasStageEvidenceLine = true;
                ++recordsWithStageEvidenceLine;
                IncrementJsonCount(
                    stageEvidenceValueCounts,
                    NormalizedDiagnosticsLineValue(stageEvidenceValue));
            }
            if (foundStageEvidenceDetail &&
                stageEvidenceDetail.find("Score terms name whether they used") !=
                    std::string::npos) {
                hasStageEvidenceSourceDetail = true;
                ++recordsWithStageEvidenceSourceDetail;
            }
            partialEvidenceWarningCount = DiagnosticsWarningCount(startingPointDiagnostics);
            partialEvidenceUiLineCount = CountDiagnosticsUiViewLines(
                startingPointDiagnostics,
                "Partial evidence",
                &partialEvidenceValueCounts);
            totalPartialEvidenceWarnings += partialEvidenceWarningCount;
            totalPartialEvidenceUiLines += partialEvidenceUiLineCount;
            if (partialEvidenceWarningCount > 0) {
                ++recordsWithPartialEvidenceWarnings;
            }
            if (partialEvidenceUiLineCount > 0) {
                ++recordsWithPartialEvidenceUiLines;
            }
        }
        if (!candidateStageEvidenceAccountingRecorded) {
            for (const std::string& kind : expectedCandidateKinds) {
                for (const RequiredStageEvidenceSpec& stage : requiredStageEvidence) {
                    const std::string key =
                        CandidateStageDiagnosticCoverageKey(kind, stage.id);
                    IncrementJsonCount(
                        candidateStageEvidenceStatusCountsByKey[key],
                        "missing");
                    candidateStageEvidenceNonCompleteRecordIdsByKey[key]
                        .push_back(recordId);
                }
            }
        }
        if (!hasSelectedCandidateDetailLine) {
            missingSelectedCandidateDetailLineRecordIds.push_back(recordId);
        }
        if (!hasSelectedCandidateVisibleControlDetail) {
            missingSelectedCandidateVisibleControlDetailRecordIds.push_back(recordId);
        }
        if (!hasSourceLine) {
            missingSourceLineRecordIds.push_back(recordId);
        }
        if (!hasSourceScopeDetail) {
            missingSourceScopeDetailRecordIds.push_back(recordId);
        }
        if (!hasCandidateScoreOrderLine) {
            missingCandidateScoreOrderLineRecordIds.push_back(recordId);
        }
        if (!hasCandidateScoreOrderGuardrailDetail) {
            missingCandidateScoreOrderGuardrailDetailRecordIds.push_back(recordId);
        }
        if (!hasVisibleActionScopeLine) {
            missingVisibleActionScopeLineRecordIds.push_back(recordId);
        }
        if (!hasVisibleActionScopeGuardrailDetail) {
            missingVisibleActionScopeGuardrailDetailRecordIds.push_back(recordId);
        }
        if (!hasDryRunLine) {
            missingDryRunLineRecordIds.push_back(recordId);
        }
        if (!hasDryRunReadOnlyDetail) {
            missingDryRunReadOnlyDetailRecordIds.push_back(recordId);
        }
        if (!hasRecipeWritesLine) {
            missingRecipeWritesLineRecordIds.push_back(recordId);
        }
        if (!hasRecipeWritesExplicitActionDetail) {
            missingRecipeWritesExplicitActionDetailRecordIds.push_back(recordId);
        }
        if (!hasStageEvidenceLine) {
            missingStageEvidenceLineRecordIds.push_back(recordId);
        }
        if (!hasStageEvidenceSourceDetail) {
            missingStageEvidenceSourceDetailRecordIds.push_back(recordId);
        }
        if (partialEvidenceWarningCount > 0 && partialEvidenceUiLineCount <= 0) {
            missingPartialEvidenceUiLineRecordIds.push_back(recordId);
        }
        if (partialEvidenceWarningCount != partialEvidenceUiLineCount) {
            partialEvidenceLineCountMismatchRecordIds.push_back(recordId);
        }
        if (partialEvidenceWarningCount <= 0 && partialEvidenceUiLineCount > 0) {
            partialEvidenceLineWithoutWarningRecordIds.push_back(recordId);
        }
        bool hasAllExpectedCandidateKinds = hasCandidateDiagnostics;
        for (const std::string& kind : expectedCandidateKinds) {
            if (candidateKindsInRecord.find(kind) != candidateKindsInRecord.end()) {
                const int current = candidateKindRecordCounts.value(kind, 0);
                candidateKindRecordCounts[kind] = current + 1;
            } else {
                hasAllExpectedCandidateKinds = false;
                missingCandidateKindRecordIdsByKind[kind].push_back(recordId);
                missingCandidateKindsInRecord.push_back(kind);
            }
        }
        if (hasAllExpectedCandidateKinds) {
            ++recordsWithExpectedCandidateKinds;
        }
        bool hasAllExpectedCandidateScores = hasCandidateDiagnostics;
        for (const std::string& kind : expectedCandidateKinds) {
            const auto scoreIt = candidateScoresByKindInRecord.find(kind);
            if (scoreIt != candidateScoresByKindInRecord.end()) {
                const int current = candidateScoreRecordCounts[kind];
                candidateScoreRecordCounts[kind] = current + 1;
                candidateScoreSums[kind] += scoreIt->second;
                if (current == 0) {
                    candidateScoreMins[kind] = scoreIt->second;
                    candidateScoreMaxes[kind] = scoreIt->second;
                } else {
                    candidateScoreMins[kind] =
                        std::min(candidateScoreMins[kind], scoreIt->second);
                    candidateScoreMaxes[kind] =
                        std::max(candidateScoreMaxes[kind], scoreIt->second);
                }
            } else {
                hasAllExpectedCandidateScores = false;
                missingCandidateScoreRecordIdsByKind[kind].push_back(recordId);
                missingCandidateScoresInRecord.push_back(kind);
            }
        }
        if (hasAllExpectedCandidateScores) {
            ++recordsWithExpectedCandidateScores;
        }
        bool hasAllExpectedVisibleControlLines = hasStartingPointDiagnostics;
        for (const ExpectedStartingPointCandidateUiLineSpec& spec : expectedVisibleControlLines) {
            bool hasVisibleControlLine = false;
            const std::string visibleControlValue =
                DiagnosticsUiViewLineValue(
                    startingPointDiagnostics,
                    spec.lineLabel,
                    &hasVisibleControlLine);
            if (hasVisibleControlLine) {
                const int current =
                    candidateVisibleControlLineRecordCounts.value(spec.kind, 0);
                candidateVisibleControlLineRecordCounts[spec.kind] = current + 1;
                IncrementJsonCount(
                    candidateVisibleControlValueCountsByKind[spec.kind],
                    NormalizedDiagnosticsLineValue(visibleControlValue));
            } else {
                hasAllExpectedVisibleControlLines = false;
                missingCandidateVisibleControlLineRecordIdsByKind[spec.kind]
                    .push_back(recordId);
                missingVisibleControlLinesInRecord.push_back(spec.lineLabel);
            }
        }
        if (hasAllExpectedVisibleControlLines) {
            ++recordsWithExpectedVisibleControlLines;
        }
        bool hasAllTrackedCandidateControlValueLines = hasStartingPointDiagnostics;
        bool hasAllTrackedCandidateControlValueLineDetails = hasStartingPointDiagnostics;
        for (const TrackedStartingPointCandidateControlValueUiLineSpec& spec : trackedControlValueLines) {
            bool hasControlValueLine = false;
            const std::string controlValue =
                DiagnosticsUiViewLineValue(
                    startingPointDiagnostics,
                    spec.lineLabel,
                    &hasControlValueLine);
            bool hasControlValueLineDetail = false;
            const std::string controlValueDetail =
                DiagnosticsUiViewLineDetail(
                    startingPointDiagnostics,
                    spec.lineLabel,
                    &hasControlValueLineDetail);
            if (hasControlValueLine) {
                const int current =
                    candidateControlValueLineRecordCounts.value(spec.key, 0);
                candidateControlValueLineRecordCounts[spec.key] = current + 1;
                IncrementJsonCount(
                    candidateControlValueCountsByKey[spec.key],
                    NormalizedDiagnosticsLineValue(controlValue));
            } else {
                hasAllTrackedCandidateControlValueLines = false;
                missingCandidateControlValueLineRecordIdsByKey[spec.key]
                    .push_back(recordId);
                missingCandidateControlValueLinesInRecord.push_back(spec.lineLabel);
            }
            if (hasControlValueLineDetail &&
                controlValueDetail.find(spec.requiredDetailFragment) != std::string::npos) {
                const int current =
                    candidateControlValueLineDetailRecordCounts.value(spec.key, 0);
                candidateControlValueLineDetailRecordCounts[spec.key] = current + 1;
            } else {
                hasAllTrackedCandidateControlValueLineDetails = false;
                missingCandidateControlValueLineDetailRecordIdsByKey[spec.key]
                    .push_back(recordId);
                missingCandidateControlValueLineDetailsInRecord.push_back(spec.lineLabel);
            }
        }
        if (hasAllTrackedCandidateControlValueLines) {
            ++recordsWithTrackedCandidateControlValueLines;
        }
        if (hasAllTrackedCandidateControlValueLineDetails) {
            ++recordsWithTrackedCandidateControlValueLineDetails;
        }
        bool hasAllExpectedScoreComponentLines = hasStartingPointDiagnostics;
        for (const ExpectedStartingPointCandidateUiLineSpec& spec : expectedScoreComponentLines) {
            bool hasScoreComponentLine = false;
            const std::string scoreComponentValue =
                DiagnosticsUiViewLineValue(
                    startingPointDiagnostics,
                    spec.lineLabel,
                    &hasScoreComponentLine);
            if (hasScoreComponentLine) {
                const int current =
                    candidateScoreComponentLineRecordCounts.value(spec.kind, 0);
                candidateScoreComponentLineRecordCounts[spec.kind] = current + 1;
                IncrementJsonCount(
                    candidateScoreComponentValueCountsByKind[spec.kind],
                    NormalizedDiagnosticsLineValue(scoreComponentValue));
            } else {
                hasAllExpectedScoreComponentLines = false;
                missingCandidateScoreComponentLineRecordIdsByKind[spec.kind]
                    .push_back(recordId);
                missingScoreComponentLinesInRecord.push_back(spec.lineLabel);
            }
        }
        if (hasAllExpectedScoreComponentLines) {
            ++recordsWithExpectedScoreComponentLines;
        }
        bool hasAllExpectedWarningLines = hasStartingPointDiagnostics;
        for (const ExpectedStartingPointCandidateUiLineSpec& spec : expectedWarningLines) {
            bool hasWarningLine = false;
            const std::string warningValue =
                DiagnosticsUiViewLineValue(
                    startingPointDiagnostics,
                    spec.lineLabel,
                    &hasWarningLine);
            if (hasWarningLine) {
                const int current =
                    candidateWarningLineRecordCounts.value(spec.kind, 0);
                candidateWarningLineRecordCounts[spec.kind] = current + 1;
                IncrementJsonCount(
                    candidateWarningValueCountsByKind[spec.kind],
                    NormalizedDiagnosticsLineValue(warningValue));
            } else {
                hasAllExpectedWarningLines = false;
                missingCandidateWarningLineRecordIdsByKind[spec.kind]
                    .push_back(recordId);
                missingWarningLinesInRecord.push_back(spec.lineLabel);
            }
        }
        if (hasAllExpectedWarningLines) {
            ++recordsWithExpectedWarningLines;
        }
        bool hasAllExpectedCandidateStageDiagnostics = hasCandidateDiagnostics;
        for (const std::string& kind : expectedCandidateKinds) {
            for (const RequiredStageEvidenceSpec& stage : requiredStageEvidence) {
                const std::string key =
                    CandidateStageDiagnosticCoverageKey(kind, stage.id);
                if (candidateStageDiagnosticsInRecord.find(key) !=
                    candidateStageDiagnosticsInRecord.end()) {
                    const int current =
                        candidateStageDiagnosticRecordCounts.value(key, 0);
                    candidateStageDiagnosticRecordCounts[key] = current + 1;
                } else {
                    hasAllExpectedCandidateStageDiagnostics = false;
                    missingCandidateStageDiagnosticRecordIdsByKey[key]
                        .push_back(recordId);
                    missingCandidateStageDiagnosticsInRecord.push_back(
                        CandidateStageDiagnosticCoverageLabel(kind, stage.label));
                }
            }
        }
        if (hasAllExpectedCandidateStageDiagnostics) {
            ++recordsWithExpectedCandidateStageDiagnostics;
        }
        if (!hasStartingPointDiagnostics || !hasCandidateDiagnostics) {
            missingCandidateDiagnosticsRecordIds.push_back(recordId);
        }
        if (!hasSelectedCandidate) {
            missingSelectedCandidateRecordIds.push_back(recordId);
        }
        bool hasAllActionReadinessLines = hasStartingPointDiagnostics;
        for (const std::string& label : requiredActionReadinessLabels) {
            bool hasActionReadinessLine = false;
            const std::string actionReadinessValue =
                DiagnosticsUiViewLineValue(
                    startingPointDiagnostics,
                    label,
                    &hasActionReadinessLine);
            if (hasActionReadinessLine) {
                const int current = actionReadinessLineRecordCounts.value(label, 0);
                actionReadinessLineRecordCounts[label] = current + 1;
                IncrementJsonCount(
                    actionReadinessValueCountsByLabel[label],
                    NormalizedActionReadinessValue(actionReadinessValue));
            } else {
                hasAllActionReadinessLines = false;
                missingActionReadinessRecordIdsByLabel[label].push_back(recordId);
                missingActionReadinessLinesInRecord.push_back(label);
            }
        }
        if (hasAllActionReadinessLines) {
            ++recordsWithActionReadinessDiagnostics;
        } else {
            missingActionReadinessRecordIds.push_back(recordId);
        }
        bool hasAllActionReadinessGuardrailDetails = hasStartingPointDiagnostics;
        for (const ActionReadinessDetailGuardSpec& spec : requiredActionReadinessDetailGuards) {
            bool hasActionReadinessDetail = false;
            const std::string actionReadinessDetail =
                DiagnosticsUiViewLineDetail(
                    startingPointDiagnostics,
                    spec.label,
                    &hasActionReadinessDetail);
            if (hasActionReadinessDetail &&
                DetailContainsAnyFragment(actionReadinessDetail, spec.acceptedDetailFragments)) {
                const int current =
                    actionReadinessGuardrailDetailRecordCounts.value(spec.label, 0);
                actionReadinessGuardrailDetailRecordCounts[spec.label] = current + 1;
            } else {
                hasAllActionReadinessGuardrailDetails = false;
                missingActionReadinessGuardrailDetailRecordIdsByLabel[spec.label]
                    .push_back(recordId);
                missingActionReadinessGuardrailDetailsInRecord.push_back(spec.label);
            }
        }
        if (hasAllActionReadinessGuardrailDetails) {
            ++recordsWithActionReadinessGuardrailDetails;
        }
        const bool hasCompleteUiDiagnosticSet =
            hasStartingPointDiagnostics &&
            hasCandidateDiagnostics &&
            hasSelectedCandidate &&
            hasAllExpectedCandidateKinds &&
            hasAllExpectedCandidateScores &&
            hasAllExpectedVisibleControlLines &&
            hasAllExpectedScoreComponentLines &&
            hasAllExpectedWarningLines &&
            hasAllActionReadinessLines;
        if (hasCompleteUiDiagnosticSet) {
            ++recordsWithCompleteUiDiagnosticSet;
        }
        recordDiagnosticCompleteness.push_back(
            BuildStartingPointDiagnosticCompletenessRow(
                recordId,
                static_cast<int>(recordIndex),
                true,
                recordStatus,
                hasStartingPointDiagnostics,
                hasCandidateDiagnostics,
                hasSourceLine,
                hasSourceScopeDetail,
                hasSelectedCandidate,
                hasAllExpectedCandidateKinds,
                hasAllExpectedCandidateScores,
                hasAllExpectedVisibleControlLines,
                hasAllTrackedCandidateControlValueLines,
                hasAllTrackedCandidateControlValueLineDetails,
                hasAllExpectedScoreComponentLines,
                hasAllExpectedWarningLines,
                hasAllExpectedCandidateStageDiagnostics,
                hasAllActionReadinessLines,
                hasAllActionReadinessGuardrailDetails,
                hasSelectedCandidateDetailLine,
                hasSelectedCandidateVisibleControlDetail,
                hasCandidateScoreOrderLine,
                hasCandidateScoreOrderGuardrailDetail,
                hasVisibleActionScopeLine,
                hasVisibleActionScopeGuardrailDetail,
                hasDryRunLine,
                hasDryRunReadOnlyDetail,
                hasRecipeWritesLine,
                hasRecipeWritesExplicitActionDetail,
                hasStageEvidenceLine,
                hasStageEvidenceSourceDetail,
                partialEvidenceWarningCount,
                partialEvidenceUiLineCount,
                missingCandidateKindsInRecord,
                missingCandidateScoresInRecord,
                missingVisibleControlLinesInRecord,
                missingCandidateControlValueLinesInRecord,
                missingCandidateControlValueLineDetailsInRecord,
                missingScoreComponentLinesInRecord,
                missingWarningLinesInRecord,
                missingCandidateStageDiagnosticsInRecord,
                missingActionReadinessLinesInRecord,
                missingActionReadinessGuardrailDetailsInRecord));
        for (const RequiredStageEvidenceSpec& stage : requiredStageEvidence) {
            if (RecordHasCompleteStageDiagnostics(record, stage.id)) {
                const int current = completeStageRecordCounts.value(stage.id, 0);
                completeStageRecordCounts[stage.id] = current + 1;
            } else {
                missingStageRecordIdsById[stage.id].push_back(recordId);
            }
        }

        const nlohmann::json humanReview = record.value("humanReview", nlohmann::json::object());
        bool humanReviewComplete = humanReview.is_object();
        for (const std::string& field : requiredHumanReviewFields) {
            const nlohmann::json value =
                humanReview.is_object() && humanReview.contains(field)
                    ? humanReview[field]
                    : nlohmann::json();
            if (!HumanReviewFieldComplete(field, value)) {
                humanReviewComplete = false;
                IncrementJsonCount(missingHumanReviewFieldCounts, field);
            }
            if (value.is_boolean()) {
                IncrementJsonCount(
                    humanReviewBoolCounts[field],
                    value.get<bool>() ? "true" : "false");
            }
            if (field == "nextManualControl" && value.is_string() && !value.get<std::string>().empty()) {
                IncrementJsonCount(nextManualControlCounts, value.get<std::string>());
            }
        }
        if (humanReviewComplete) {
            ++humanReviewCompleteCount;
        } else {
            incompleteHumanReviewRecordIds.push_back(recordId);
        }
    }

    nlohmann::json missingRecommendedCategories = nlohmann::json::array();
    int recommendedCategoriesPresent = 0;
    const nlohmann::json recommendedCategoryCoverage =
        RecommendedCategoryCoverageFromCounts(
            categoryTagCounts,
            recommendedCategories,
            missingRecommendedCategories,
            recommendedCategoriesPresent);

    nlohmann::json blockers = nlohmann::json::array();
    const int recordCount = static_cast<int>(records.size());
    nlohmann::json stageEvidenceCoverage = nlohmann::json::array();
    bool requiredStageEvidenceComplete = recordCount > 0;
    int leastCoveredRequiredStageRecordCount = recordCount;
    for (const RequiredStageEvidenceSpec& stage : requiredStageEvidence) {
        const int completeCount = completeStageRecordCounts.value(stage.id, 0);
        const int missingCount = std::max(0, recordCount - completeCount);
        leastCoveredRequiredStageRecordCount =
            std::min(leastCoveredRequiredStageRecordCount, completeCount);
        if (missingCount > 0) {
            requiredStageEvidenceComplete = false;
        }
        stageEvidenceCoverage.push_back({
            { "stage", stage.id },
            { "label", stage.label },
            { "validationGap", stage.validationGap },
            { "completeRecordCount", completeCount },
            { "missingRecordCount", missingCount },
            { "missingRecordIds", missingStageRecordIdsById.value(stage.id, nlohmann::json::array()) },
            { "complete", recordCount > 0 && missingCount == 0 }
        });
    }
    const bool recordsPresent = recordCount > 0;
    nlohmann::json candidateKindCoverage = nlohmann::json::array();
    for (const std::string& kind : expectedCandidateKinds) {
        const int completeCount = candidateKindRecordCounts.value(kind, 0);
        const int missingCount = std::max(0, recordCount - completeCount);
        candidateKindCoverage.push_back({
            { "kind", kind },
            { "label", StartingPointCandidateKindLabel(kind) },
            { "candidateRecordCount", completeCount },
            { "selectedRecordCount", selectedCandidateKindCounts.value(kind, 0) },
            { "missingRecordCount", missingCount },
            { "missingRecordIds", missingCandidateKindRecordIdsByKind.value(kind, nlohmann::json::array()) },
            { "complete", recordsPresent && missingCount == 0 }
        });
    }
    const bool candidateKindCoverageComplete =
        recordsPresent && recordsWithExpectedCandidateKinds == recordCount;
    nlohmann::json candidateScoreCoverage = nlohmann::json::array();
    for (const std::string& kind : expectedCandidateKinds) {
        const int validScoreCount = candidateScoreRecordCounts[kind];
        const int missingCount = std::max(0, recordCount - validScoreCount);
        candidateScoreCoverage.push_back({
            { "kind", kind },
            { "label", StartingPointCandidateKindLabel(kind) },
            { "validScoreRecordCount", validScoreCount },
            { "missingScoreRecordCount", missingCount },
            { "missingScoreRecordIds", missingCandidateScoreRecordIdsByKind.value(kind, nlohmann::json::array()) },
            { "minTotalScore", validScoreCount > 0 ? nlohmann::json(candidateScoreMins[kind]) : nlohmann::json() },
            { "maxTotalScore", validScoreCount > 0 ? nlohmann::json(candidateScoreMaxes[kind]) : nlohmann::json() },
            { "averageTotalScore", validScoreCount > 0 ? nlohmann::json(candidateScoreSums[kind] / validScoreCount) : nlohmann::json() },
            { "complete", recordsPresent && missingCount == 0 }
        });
    }
    const bool candidateScoreCoverageComplete =
        recordsPresent && recordsWithExpectedCandidateScores == recordCount;
    nlohmann::json candidateVisibleControlLineCoverage = nlohmann::json::array();
    for (const ExpectedStartingPointCandidateUiLineSpec& spec : expectedVisibleControlLines) {
        const int completeCount =
            candidateVisibleControlLineRecordCounts.value(spec.kind, 0);
        const int missingCount = std::max(0, recordCount - completeCount);
        candidateVisibleControlLineCoverage.push_back({
            { "kind", spec.kind },
            { "label", StartingPointCandidateKindLabel(spec.kind) },
            { "lineLabel", spec.lineLabel },
            { "completeRecordCount", completeCount },
            { "missingRecordCount", missingCount },
            { "missingRecordIds", missingCandidateVisibleControlLineRecordIdsByKind.value(spec.kind, nlohmann::json::array()) },
            { "valueCounts", candidateVisibleControlValueCountsByKind.value(spec.kind, nlohmann::json::object()) },
            { "complete", recordsPresent && missingCount == 0 }
        });
    }
    const bool candidateVisibleControlLineCoverageComplete =
        recordsPresent && recordsWithExpectedVisibleControlLines == recordCount;
    nlohmann::json candidateControlValueLineCoverage = nlohmann::json::array();
    for (const TrackedStartingPointCandidateControlValueUiLineSpec& spec : trackedControlValueLines) {
        const int lineCount =
            candidateControlValueLineRecordCounts.value(spec.key, 0);
        const int detailCount =
            candidateControlValueLineDetailRecordCounts.value(spec.key, 0);
        const int missingLineCount = std::max(0, recordCount - lineCount);
        const int missingDetailCount = std::max(0, recordCount - detailCount);
        candidateControlValueLineCoverage.push_back({
            { "key", spec.key },
            { "kind", spec.kind },
            { "label", StartingPointCandidateKindLabel(spec.kind) },
            { "control", spec.control },
            { "lineLabel", spec.lineLabel },
            { "requiredDetailFragment", spec.requiredDetailFragment },
            { "lineRecordCount", lineCount },
            { "detailRecordCount", detailCount },
            { "missingLineRecordCount", missingLineCount },
            { "missingLineRecordIds", missingCandidateControlValueLineRecordIdsByKey.value(spec.key, nlohmann::json::array()) },
            { "missingDetailRecordCount", missingDetailCount },
            { "missingDetailRecordIds", missingCandidateControlValueLineDetailRecordIdsByKey.value(spec.key, nlohmann::json::array()) },
            { "valueCounts", candidateControlValueCountsByKey.value(spec.key, nlohmann::json::object()) },
            { "complete", recordsPresent && missingLineCount == 0 && missingDetailCount == 0 }
        });
    }
    const bool candidateControlValueLineCoverageComplete =
        recordsPresent &&
        recordsWithTrackedCandidateControlValueLines == recordCount &&
        recordsWithTrackedCandidateControlValueLineDetails == recordCount;
    nlohmann::json candidateScoreComponentLineCoverage = nlohmann::json::array();
    for (const ExpectedStartingPointCandidateUiLineSpec& spec : expectedScoreComponentLines) {
        const int completeCount =
            candidateScoreComponentLineRecordCounts.value(spec.kind, 0);
        const int missingCount = std::max(0, recordCount - completeCount);
        candidateScoreComponentLineCoverage.push_back({
            { "kind", spec.kind },
            { "label", StartingPointCandidateKindLabel(spec.kind) },
            { "lineLabel", spec.lineLabel },
            { "completeRecordCount", completeCount },
            { "missingRecordCount", missingCount },
            { "missingRecordIds", missingCandidateScoreComponentLineRecordIdsByKind.value(spec.kind, nlohmann::json::array()) },
            { "valueCounts", candidateScoreComponentValueCountsByKind.value(spec.kind, nlohmann::json::object()) },
            { "complete", recordsPresent && missingCount == 0 }
        });
    }
    const bool candidateScoreComponentLineCoverageComplete =
        recordsPresent && recordsWithExpectedScoreComponentLines == recordCount;
    nlohmann::json candidateWarningLineCoverage = nlohmann::json::array();
    for (const ExpectedStartingPointCandidateUiLineSpec& spec : expectedWarningLines) {
        const int completeCount =
            candidateWarningLineRecordCounts.value(spec.kind, 0);
        const int missingCount = std::max(0, recordCount - completeCount);
        candidateWarningLineCoverage.push_back({
            { "kind", spec.kind },
            { "label", StartingPointCandidateKindLabel(spec.kind) },
            { "lineLabel", spec.lineLabel },
            { "completeRecordCount", completeCount },
            { "missingRecordCount", missingCount },
            { "missingRecordIds", missingCandidateWarningLineRecordIdsByKind.value(spec.kind, nlohmann::json::array()) },
            { "valueCounts", candidateWarningValueCountsByKind.value(spec.kind, nlohmann::json::object()) },
            { "complete", recordsPresent && missingCount == 0 }
        });
    }
    const bool candidateWarningLineCoverageComplete =
        recordsPresent && recordsWithExpectedWarningLines == recordCount;
    nlohmann::json candidateStageDiagnosticCoverage = nlohmann::json::array();
    for (const std::string& kind : expectedCandidateKinds) {
        for (const RequiredStageEvidenceSpec& stage : requiredStageEvidence) {
            const std::string key =
                CandidateStageDiagnosticCoverageKey(kind, stage.id);
            const int completeCount =
                candidateStageDiagnosticRecordCounts.value(key, 0);
            const int missingCount = std::max(0, recordCount - completeCount);
            candidateStageDiagnosticCoverage.push_back({
                { "key", key },
                { "kind", kind },
                { "label", StartingPointCandidateKindLabel(kind) },
                { "stage", stage.id },
                { "stageLabel", stage.label },
                { "validationGap", stage.validationGap },
                { "completeRecordCount", completeCount },
                { "missingRecordCount", missingCount },
                { "missingRecordIds", missingCandidateStageDiagnosticRecordIdsByKey.value(key, nlohmann::json::array()) },
                { "complete", recordsPresent && missingCount == 0 }
            });
        }
    }
    const bool candidateStageDiagnosticCoverageComplete =
        recordsPresent &&
        recordsWithExpectedCandidateStageDiagnostics == recordCount;
    nlohmann::json candidateStageEvidenceAccounting = nlohmann::json::array();
    bool candidateStageEvidenceAccountingComplete = recordsPresent;
    int leastCompleteCandidateStageEvidenceRecordCount = recordCount;
    int candidateStageEvidenceGateBlockedCombinationCount = 0;
    for (const std::string& kind : expectedCandidateKinds) {
        for (const RequiredStageEvidenceSpec& stage : requiredStageEvidence) {
            const std::string key =
                CandidateStageDiagnosticCoverageKey(kind, stage.id);
            const nlohmann::json statusCounts =
                candidateStageEvidenceStatusCountsByKey.value(
                    key,
                    nlohmann::json::object());
            const int completeCount = statusCounts.value("complete", 0);
            const int projectedCount = statusCounts.value("projected", 0);
            const int pendingCount = statusCounts.value("pending", 0);
            const int fallbackCount = statusCounts.value("fallback", 0);
            const int unavailableCount = statusCounts.value("unavailable", 0);
            const int missingCount = statusCounts.value("missing", 0);
            const int unknownCount = statusCounts.value("unknown", 0);
            const int nonCompleteCount = std::max(0, recordCount - completeCount);
            const bool complete = recordsPresent && nonCompleteCount == 0;
            leastCompleteCandidateStageEvidenceRecordCount =
                std::min(leastCompleteCandidateStageEvidenceRecordCount, completeCount);
            if (!complete) {
                candidateStageEvidenceAccountingComplete = false;
                ++candidateStageEvidenceGateBlockedCombinationCount;
            }
            candidateStageEvidenceAccounting.push_back({
                { "key", key },
                { "kind", kind },
                { "label", StartingPointCandidateKindLabel(kind) },
                { "stage", stage.id },
                { "stageLabel", stage.label },
                { "validationGap", stage.validationGap },
                { "statusCounts", statusCounts },
                { "completeRecordCount", completeCount },
                { "projectedRecordCount", projectedCount },
                { "pendingRecordCount", pendingCount },
                { "fallbackRecordCount", fallbackCount },
                { "unavailableRecordCount", unavailableCount },
                { "missingRecordCount", missingCount },
                { "unknownRecordCount", unknownCount },
                { "nonCompleteRecordCount", nonCompleteCount },
                { "nonCompleteRecordIds", candidateStageEvidenceNonCompleteRecordIdsByKey.value(key, nlohmann::json::array()) },
                { "complete", complete },
                { "gating", true }
            });
        }
    }
    const bool sourceAttributionCoverageComplete =
        recordsPresent &&
        recordsWithSourceLine == recordCount &&
        recordsWithSourceScopeDetail == recordCount;
    const nlohmann::json sourceAttributionCoverage = {
        { "lineLabel", "Source" },
        { "requiredDetailFragment", "scoped to this RAW source" },
        { "lineRecordCount", recordsWithSourceLine },
        { "scopeDetailRecordCount", recordsWithSourceScopeDetail },
        { "missingLineRecordCount", std::max(0, recordCount - recordsWithSourceLine) },
        { "missingLineRecordIds", missingSourceLineRecordIds },
        { "missingScopeDetailRecordCount", std::max(0, recordCount - recordsWithSourceScopeDetail) },
        { "missingScopeDetailRecordIds", missingSourceScopeDetailRecordIds },
        { "lineValueCounts", sourceLineValueCounts },
        { "complete", sourceAttributionCoverageComplete }
    };
    const bool selectedCandidateDetailCoverageComplete =
        recordsPresent &&
        recordsWithSelectedCandidateVisibleControlDetail == recordCount;
    const nlohmann::json selectedCandidateDetailCoverage = {
        { "lineLabel", "Diagnostic selection" },
        { "requiredDetailFragment", "Visible controls:" },
        { "lineRecordCount", recordsWithSelectedCandidateDetailLine },
        { "visibleControlDetailRecordCount", recordsWithSelectedCandidateVisibleControlDetail },
        { "missingLineRecordCount", std::max(0, recordCount - recordsWithSelectedCandidateDetailLine) },
        { "missingLineRecordIds", missingSelectedCandidateDetailLineRecordIds },
        { "missingVisibleControlDetailRecordCount", std::max(0, recordCount - recordsWithSelectedCandidateVisibleControlDetail) },
        { "missingVisibleControlDetailRecordIds", missingSelectedCandidateVisibleControlDetailRecordIds },
        { "lineValueCounts", selectedCandidateDetailValueCounts },
        { "complete", selectedCandidateDetailCoverageComplete }
    };
    const bool candidateScoreOrderCoverageComplete =
        recordsPresent &&
        recordsWithCandidateScoreOrderLine == recordCount &&
        recordsWithCandidateScoreOrderGuardrailDetail == recordCount;
    const nlohmann::json candidateScoreOrderCoverage = {
        { "lineLabel", "Candidate score order" },
        { "requiredDetailFragment", "does not change candidate scoring" },
        { "lineRecordCount", recordsWithCandidateScoreOrderLine },
        { "guardrailDetailRecordCount", recordsWithCandidateScoreOrderGuardrailDetail },
        { "missingLineRecordCount", std::max(0, recordCount - recordsWithCandidateScoreOrderLine) },
        { "missingLineRecordIds", missingCandidateScoreOrderLineRecordIds },
        { "missingGuardrailDetailRecordCount", std::max(0, recordCount - recordsWithCandidateScoreOrderGuardrailDetail) },
        { "missingGuardrailDetailRecordIds", missingCandidateScoreOrderGuardrailDetailRecordIds },
        { "lineValueCounts", candidateScoreOrderValueCounts },
        { "complete", candidateScoreOrderCoverageComplete }
    };
    const bool visibleActionScopeCoverageComplete =
        recordsPresent &&
        recordsWithVisibleActionScopeLine == recordCount &&
        recordsWithVisibleActionScopeGuardrailDetail == recordCount;
    const nlohmann::json visibleActionScopeCoverage = {
        { "lineLabel", "Visible action scope" },
        { "requiredDetailFragments", nlohmann::json::array({
            "Build Starting Point writes safe visible controls",
            "does not apply recipe values"
        }) },
        { "lineRecordCount", recordsWithVisibleActionScopeLine },
        { "guardrailDetailRecordCount", recordsWithVisibleActionScopeGuardrailDetail },
        { "missingLineRecordCount", std::max(0, recordCount - recordsWithVisibleActionScopeLine) },
        { "missingLineRecordIds", missingVisibleActionScopeLineRecordIds },
        { "missingGuardrailDetailRecordCount", std::max(0, recordCount - recordsWithVisibleActionScopeGuardrailDetail) },
        { "missingGuardrailDetailRecordIds", missingVisibleActionScopeGuardrailDetailRecordIds },
        { "lineValueCounts", visibleActionScopeValueCounts },
        { "complete", visibleActionScopeCoverageComplete }
    };
    const bool dryRunGuardrailCoverageComplete =
        recordsPresent &&
        recordsWithDryRunLine == recordCount &&
        recordsWithDryRunReadOnlyDetail == recordCount;
    const nlohmann::json dryRunGuardrailCoverage = {
        { "lineLabel", "Dry run" },
        { "requiredDetailFragment", "does not write recipes" },
        { "lineRecordCount", recordsWithDryRunLine },
        { "readOnlyDetailRecordCount", recordsWithDryRunReadOnlyDetail },
        { "missingLineRecordCount", std::max(0, recordCount - recordsWithDryRunLine) },
        { "missingLineRecordIds", missingDryRunLineRecordIds },
        { "missingReadOnlyDetailRecordCount", std::max(0, recordCount - recordsWithDryRunReadOnlyDetail) },
        { "missingReadOnlyDetailRecordIds", missingDryRunReadOnlyDetailRecordIds },
        { "lineValueCounts", dryRunValueCounts },
        { "complete", dryRunGuardrailCoverageComplete }
    };
    const bool recipeWritesGuardrailCoverageComplete =
        recordsPresent &&
        recordsWithRecipeWritesLine == recordCount &&
        recordsWithRecipeWritesExplicitActionDetail == recordCount;
    const nlohmann::json recipeWritesGuardrailCoverage = {
        { "lineLabel", "Recipe writes" },
        { "requiredDetailFragment", "through explicit UI actions" },
        { "lineRecordCount", recordsWithRecipeWritesLine },
        { "explicitActionDetailRecordCount", recordsWithRecipeWritesExplicitActionDetail },
        { "missingLineRecordCount", std::max(0, recordCount - recordsWithRecipeWritesLine) },
        { "missingLineRecordIds", missingRecipeWritesLineRecordIds },
        { "missingExplicitActionDetailRecordCount", std::max(0, recordCount - recordsWithRecipeWritesExplicitActionDetail) },
        { "missingExplicitActionDetailRecordIds", missingRecipeWritesExplicitActionDetailRecordIds },
        { "lineValueCounts", recipeWritesValueCounts },
        { "complete", recipeWritesGuardrailCoverageComplete }
    };
    const bool stageEvidenceUiLineCoverageComplete =
        recordsPresent &&
        recordsWithStageEvidenceLine == recordCount &&
        recordsWithStageEvidenceSourceDetail == recordCount;
    const nlohmann::json stageEvidenceUiLineCoverage = {
        { "lineLabel", "Stage evidence" },
        { "requiredDetailFragment", "Score terms name whether they used" },
        { "lineRecordCount", recordsWithStageEvidenceLine },
        { "sourceDetailRecordCount", recordsWithStageEvidenceSourceDetail },
        { "missingLineRecordCount", std::max(0, recordCount - recordsWithStageEvidenceLine) },
        { "missingLineRecordIds", missingStageEvidenceLineRecordIds },
        { "missingSourceDetailRecordCount", std::max(0, recordCount - recordsWithStageEvidenceSourceDetail) },
        { "missingSourceDetailRecordIds", missingStageEvidenceSourceDetailRecordIds },
        { "lineValueCounts", stageEvidenceValueCounts },
        { "complete", stageEvidenceUiLineCoverageComplete }
    };
    const bool partialEvidenceUiCoverageComplete =
        recordsPresent &&
        missingPartialEvidenceUiLineRecordIds.empty() &&
        partialEvidenceLineCountMismatchRecordIds.empty();
    const nlohmann::json partialEvidenceUiCoverage = {
        { "lineLabel", "Partial evidence" },
        { "expectedLineValue", "Fallback" },
        { "recordsWithWarnings", recordsWithPartialEvidenceWarnings },
        { "recordsWithUiLine", recordsWithPartialEvidenceUiLines },
        { "totalWarningCount", totalPartialEvidenceWarnings },
        { "totalUiLineCount", totalPartialEvidenceUiLines },
        { "missingUiLineRecordCount", static_cast<int>(missingPartialEvidenceUiLineRecordIds.size()) },
        { "missingUiLineRecordIds", missingPartialEvidenceUiLineRecordIds },
        { "lineCountMismatchRecordCount", static_cast<int>(partialEvidenceLineCountMismatchRecordIds.size()) },
        { "lineCountMismatchRecordIds", partialEvidenceLineCountMismatchRecordIds },
        { "lineWithoutWarningRecordIds", partialEvidenceLineWithoutWarningRecordIds },
        { "lineValueCounts", partialEvidenceValueCounts },
        { "complete", partialEvidenceUiCoverageComplete }
    };
    nlohmann::json actionReadinessLineCoverage = nlohmann::json::array();
    for (const std::string& label : requiredActionReadinessLabels) {
        const int completeCount = actionReadinessLineRecordCounts.value(label, 0);
        const int missingCount = std::max(0, recordCount - completeCount);
        actionReadinessLineCoverage.push_back({
            { "label", label },
            { "completeRecordCount", completeCount },
            { "missingRecordCount", missingCount },
            { "missingRecordIds", missingActionReadinessRecordIdsByLabel.value(label, nlohmann::json::array()) },
            { "valueCounts", actionReadinessValueCountsByLabel.value(label, nlohmann::json::object()) },
            { "complete", recordsPresent && missingCount == 0 }
        });
    }
    nlohmann::json actionReadinessGuardrailDetailCoverage = nlohmann::json::array();
    for (const ActionReadinessDetailGuardSpec& spec : requiredActionReadinessDetailGuards) {
        const int detailCount =
            actionReadinessGuardrailDetailRecordCounts.value(spec.label, 0);
        const int missingCount = std::max(0, recordCount - detailCount);
        actionReadinessGuardrailDetailCoverage.push_back({
            { "label", spec.label },
            { "acceptedDetailFragments", JsonStringVector(spec.acceptedDetailFragments) },
            { "detailRecordCount", detailCount },
            { "missingDetailRecordCount", missingCount },
            { "missingDetailRecordIds", missingActionReadinessGuardrailDetailRecordIdsByLabel.value(spec.label, nlohmann::json::array()) },
            { "complete", recordsPresent && missingCount == 0 }
        });
    }
    const bool actionReadinessDiagnosticsComplete =
        recordsPresent && recordsWithActionReadinessDiagnostics == recordCount;
    const bool actionReadinessGuardrailDetailCoverageComplete =
        recordsPresent && recordsWithActionReadinessGuardrailDetails == recordCount;
    const bool recordDiagnosticCompletenessComplete =
        recordsPresent && recordsWithCompleteUiDiagnosticSet == recordCount;
    const bool metadataEvidenceComplete = recordsPresent && metadataLoadedCount == recordCount;
    const bool rawSafetyEvidenceComplete = recordsPresent &&
        recordsWithRawSafetyRequested == recordCount &&
        rawSafetyLoadedCount == recordCount;
    const bool categoryEvidenceComplete = recordsPresent &&
        recordsWithCategoryTags == recordCount &&
        missingRecommendedCategories.empty();
    const bool humanReviewEvidenceComplete =
        recordsPresent && humanReviewCompleteCount == recordCount;
    const bool candidateEvidenceComplete = recordsPresent &&
        recordsWithStartingPointDiagnostics == recordCount &&
        recordsWithCandidateDiagnostics == recordCount &&
        recordsWithSelectedCandidate == recordCount;
    const bool visibleRecipeWriteAuditComplete = recordsPresent &&
        recordsWithVisibleRecipeWriteAudit == recordCount;
    const bool visibleRecipeWriteAuditValuesComplete = recordsPresent &&
        recordsWithVisibleRecipeWriteAuditValues == recordCount;
    const bool visibleWriteInvariantHeld = recordsWithVisibleFieldChanges == 0;
    nlohmann::json evidenceChecklistItems = nlohmann::json::array({
        BuildEvidenceChecklistItem(
            "records",
            "Validation records present",
            recordsPresent,
            recordCount,
            std::max(1, recordCount),
            true,
            "At least one reviewed RAW validation record is required before tuning constants."),
        BuildEvidenceChecklistItem(
            "metadata",
            "Metadata loaded",
            metadataEvidenceComplete,
            metadataLoadedCount,
            recordCount,
            true,
            "Every record must include source metadata before comparing RAW behavior."),
        BuildEvidenceChecklistItem(
            "raw-buffer-safety",
            "Raw buffer safety sampled",
            rawSafetyEvidenceComplete,
            rawSafetyLoadedCount,
            recordCount,
            true,
            "Run records with --load-raw-safety so raw clipping and headroom are sampled."),
        BuildEvidenceChecklistItem(
            "representative-categories",
            "Representative categories covered",
            categoryEvidenceComplete,
            recordsWithCategoryTags,
            recordCount,
            true,
            "Every record needs category tags, and the set must cover every recommended validation category."),
        BuildEvidenceChecklistItem(
            "human-review",
            "Human review complete",
            humanReviewEvidenceComplete,
            humanReviewCompleteCount,
            recordCount,
            true,
            "Every record needs complete review fields before constants can be tied to observed results."),
        BuildEvidenceChecklistItem(
            "candidate-diagnostics",
            "Candidate diagnostics present",
            candidateEvidenceComplete,
            std::min(
                recordsWithStartingPointDiagnostics,
                std::min(recordsWithCandidateDiagnostics, recordsWithSelectedCandidate)),
            recordCount,
            true,
            "Every record must include Starting Point candidate diagnostics and a selected dry-run candidate."),
        BuildEvidenceChecklistItem(
            "candidate-stage-evidence",
            "Candidate stage evidence complete",
            candidateStageEvidenceAccountingComplete,
            leastCompleteCandidateStageEvidenceRecordCount,
            recordCount,
            true,
            "Every expected Starting Point candidate/stage combination must be complete before constants or wider one-click apply behavior are reviewed."),
        BuildEvidenceChecklistItem(
            "stage-diagnostics",
            "Required stage diagnostics complete",
            requiredStageEvidenceComplete,
            leastCoveredRequiredStageRecordCount,
            recordCount,
            true,
            "Every record must include complete named stage diagnostics for Neutral Scene, Raw Placement, Local Candidate, Finish Tone Candidate, and Display Candidate."),
        BuildEvidenceChecklistItem(
            "visible-recipe-writes",
            "No validation-record recipe writes",
            visibleWriteInvariantHeld,
            recordCount - recordsWithVisibleFieldChanges,
            recordCount,
            false,
            "The validation record command should preserve existing recipes; applied UI actions are validated separately."),
        BuildEvidenceChecklistItem(
            "visible-recipe-write-audit",
            "Selected candidate visible write audit",
            visibleRecipeWriteAuditComplete,
            recordsWithVisibleRecipeWriteAudit,
            recordCount,
            false,
            "Records should list which editable recipe fields the selected Starting Point candidate would write without applying them."),
        BuildEvidenceChecklistItem(
            "visible-recipe-write-values",
            "Selected candidate visible write values",
            visibleRecipeWriteAuditValuesComplete,
            recordsWithVisibleRecipeWriteAuditValues,
            recordCount,
            false,
            "Records should include the proposed visible recipe values or summaries for each selected-candidate write.")
    });
    const bool allRequiredEvidenceComplete = recordsPresent &&
        metadataEvidenceComplete &&
        rawSafetyEvidenceComplete &&
        categoryEvidenceComplete &&
        humanReviewEvidenceComplete &&
        candidateEvidenceComplete &&
        candidateStageEvidenceAccountingComplete &&
        requiredStageEvidenceComplete;
    const nlohmann::json constantTuningEvidence =
        BuildConstantTuningEvidenceStatus(
            categoryTagCounts,
            completeStageRecordCounts,
            recordCount,
            allRequiredEvidenceComplete);

    if (recordCount <= 0) {
        blockers.push_back("No RAW validation records are present.");
    }
    if (metadataLoadedCount < recordCount) {
        blockers.push_back("Some records failed metadata loading.");
    }
    if (recordsWithRawSafetyRequested < recordCount || rawSafetyLoadedCount < recordCount) {
        blockers.push_back("Run records with --load-raw-safety and resolve any raw safety load failures.");
    }
    if (recordsRequiringCategoryTags > 0 || recordsWithCategoryTags < recordCount) {
        blockers.push_back("Add representative image category tags to every record.");
    }
    if (!missingRecommendedCategories.empty()) {
        blockers.push_back(
            "Add representative RAW records covering every recommended image category before tuning constants.");
    }
    if (humanReviewCompleteCount < recordCount) {
        blockers.push_back("Complete required human review fields for every record.");
    }
    if (recordCount > 0 && !candidateEvidenceComplete) {
        blockers.push_back("Collect Starting Point candidate diagnostics and selected-candidate evidence for every record.");
    }
    if (recordCount > 0 && !candidateStageEvidenceAccountingComplete) {
        blockers.push_back(
            "Collect complete candidate-stage evidence for every expected Starting Point candidate/stage combination before tuning constants or widening one-click apply behavior.");
    }
    bool hasCriticalStageGapCounts = false;
    for (const RequiredStageEvidenceSpec& stage : requiredStageEvidence) {
        if (validationGapCounts.value(stage.validationGap, 0) > 0) {
            hasCriticalStageGapCounts = true;
            blockers.push_back(std::string("Resolve validation gap: ") + stage.validationGap + ".");
        }
    }
    if (recordCount > 0 && !requiredStageEvidenceComplete && !hasCriticalStageGapCounts) {
        blockers.push_back("Collect complete named stage diagnostics for every required Starting Point stage before tuning constants.");
    }

    return {
        { "schema", "stack.raw-starting-point.validation-set-summary" },
        { "version", kValidationSetSummarySchemaVersion },
        { "recordCount", recordCount },
        { "sourceCount", reportObject ? report.value("sourceCount", recordCount) : recordCount },
        { "metadataLoadedCount", metadataLoadedCount },
        { "invalidRecordIds", invalidRecordIds },
        { "rawSafety", {
            { "requestedRecordCount", recordsWithRawSafetyRequested },
            { "loadedRecordCount", rawSafetyLoadedCount },
            { "reportRequested", reportObject ? report.value("rawSafetyRequested", false) : false },
            { "reportLoadedCount", reportObject ? report.value("rawSafetyLoadedCount", rawSafetyLoadedCount) : rawSafetyLoadedCount },
            { "notRequestedRecordIds", rawSafetyNotRequestedRecordIds },
            { "unavailableRecordIds", rawSafetyUnavailableRecordIds }
        } },
        { "metadataEvidence", {
            { "loadedRecordCount", metadataLoadedCount },
            { "missingRecordCount", recordCount - metadataLoadedCount },
            { "missingRecordIds", metadataMissingRecordIds }
        } },
        { "annotations", {
            { "file", reportObject ? report.value("annotationFile", "") : "" },
            { "entryCount", reportObject ? report.value("annotationEntryCount", 0) : 0 },
            { "matchedEntryCount", reportObject ? report.value("annotationMatchedEntryCount", 0) : 0 },
            { "unmatchedEntryCount", reportObject ? report.value("annotationUnmatchedEntryCount", 0) : 0 },
            { "matchedRecordCount", annotationMatchedRecordCount },
            { "appliedRecordCount", reportObject ? report.value("annotationAppliedCount", annotationMatchedRecordCount) : annotationMatchedRecordCount },
            { "templateFile", reportObject ? report.value("annotationTemplateFile", "") : "" },
            { "templateRecordCount", reportObject ? report.value("annotationTemplateRecordCount", 0) : 0 }
        } },
        { "categoryCoverage", {
            { "recordsWithCategoryTags", recordsWithCategoryTags },
            { "recordsRequiringHumanCategoryTags", recordsRequiringCategoryTags },
            { "categoryTagCounts", categoryTagCounts },
            { "recommendedCategoriesPresent", recommendedCategoriesPresent },
            { "recommendedCategoriesTotal", static_cast<int>(recommendedCategories.size()) },
            { "missingRecommendedCategories", missingRecommendedCategories },
            { "missingCategoryRecordIds", missingCategoryRecordIds },
            { "representativeCoverageComplete", missingRecommendedCategories.empty() },
            { "recommendedCategoryCoverage", recommendedCategoryCoverage }
        } },
        { "humanReview", {
            { "completeRecordCount", humanReviewCompleteCount },
            { "missingRecordCount", recordCount - humanReviewCompleteCount },
            { "incompleteRecordIds", incompleteHumanReviewRecordIds },
            { "requiredFields", JsonStringVector(requiredHumanReviewFields) },
            { "missingFieldCounts", missingHumanReviewFieldCounts },
            { "booleanFieldCounts", humanReviewBoolCounts },
            { "nextManualControlCounts", nextManualControlCounts }
        } },
        { "startingPointDiagnostics", {
            { "recordsWithDiagnostics", recordsWithStartingPointDiagnostics },
            { "recordsWithCandidates", recordsWithCandidateDiagnostics },
            { "recordsWithExpectedCandidateKinds", recordsWithExpectedCandidateKinds },
            { "recordsWithExpectedCandidateScores", recordsWithExpectedCandidateScores },
            { "recordsWithExpectedVisibleControlLines", recordsWithExpectedVisibleControlLines },
            { "recordsWithTrackedCandidateControlValueLines", recordsWithTrackedCandidateControlValueLines },
            { "recordsWithTrackedCandidateControlValueLineDetails", recordsWithTrackedCandidateControlValueLineDetails },
            { "recordsWithExpectedScoreComponentLines", recordsWithExpectedScoreComponentLines },
            { "recordsWithExpectedWarningLines", recordsWithExpectedWarningLines },
            { "recordsWithExpectedCandidateStageDiagnostics", recordsWithExpectedCandidateStageDiagnostics },
            { "recordsWithSourceLine", recordsWithSourceLine },
            { "recordsWithSourceScopeDetail", recordsWithSourceScopeDetail },
            { "recordsWithSelectedCandidate", recordsWithSelectedCandidate },
            { "recordsWithSelectedCandidateDetailLine", recordsWithSelectedCandidateDetailLine },
            { "recordsWithSelectedCandidateVisibleControlDetail", recordsWithSelectedCandidateVisibleControlDetail },
            { "recordsWithCandidateScoreOrderLine", recordsWithCandidateScoreOrderLine },
            { "recordsWithCandidateScoreOrderGuardrailDetail", recordsWithCandidateScoreOrderGuardrailDetail },
            { "recordsWithVisibleActionScopeLine", recordsWithVisibleActionScopeLine },
            { "recordsWithVisibleActionScopeGuardrailDetail", recordsWithVisibleActionScopeGuardrailDetail },
            { "recordsWithDryRunLine", recordsWithDryRunLine },
            { "recordsWithDryRunReadOnlyDetail", recordsWithDryRunReadOnlyDetail },
            { "recordsWithRecipeWritesLine", recordsWithRecipeWritesLine },
            { "recordsWithRecipeWritesExplicitActionDetail", recordsWithRecipeWritesExplicitActionDetail },
            { "recordsWithStageEvidenceLine", recordsWithStageEvidenceLine },
            { "recordsWithStageEvidenceSourceDetail", recordsWithStageEvidenceSourceDetail },
            { "recordsWithPartialEvidenceWarnings", recordsWithPartialEvidenceWarnings },
            { "recordsWithPartialEvidenceUiLines", recordsWithPartialEvidenceUiLines },
            { "totalPartialEvidenceWarnings", totalPartialEvidenceWarnings },
            { "totalPartialEvidenceUiLines", totalPartialEvidenceUiLines },
            { "recordsWithActionReadinessDiagnostics", recordsWithActionReadinessDiagnostics },
            { "recordsWithActionReadinessGuardrailDetails", recordsWithActionReadinessGuardrailDetails },
            { "recordsWithCompleteUiDiagnosticSet", recordsWithCompleteUiDiagnosticSet },
            { "candidateEvidenceComplete", candidateEvidenceComplete },
            { "candidateKindCoverageComplete", candidateKindCoverageComplete },
            { "candidateKindCoverageIsGating", false },
            { "candidateScoreCoverageComplete", candidateScoreCoverageComplete },
            { "candidateScoreCoverageIsGating", false },
            { "candidateVisibleControlLineCoverageComplete", candidateVisibleControlLineCoverageComplete },
            { "candidateVisibleControlLineCoverageIsGating", false },
            { "candidateControlValueLineCoverageComplete", candidateControlValueLineCoverageComplete },
            { "candidateControlValueLineCoverageIsGating", false },
            { "candidateScoreComponentLineCoverageComplete", candidateScoreComponentLineCoverageComplete },
            { "candidateScoreComponentLineCoverageIsGating", false },
            { "candidateWarningLineCoverageComplete", candidateWarningLineCoverageComplete },
            { "candidateWarningLineCoverageIsGating", false },
            { "candidateStageDiagnosticCoverageComplete", candidateStageDiagnosticCoverageComplete },
            { "candidateStageDiagnosticCoverageIsGating", false },
            { "candidateStageEvidenceAccountingComplete", candidateStageEvidenceAccountingComplete },
            { "candidateStageEvidenceAccountingIsGating", true },
            { "candidateStageEvidenceGateBlockedCombinationCount", candidateStageEvidenceGateBlockedCombinationCount },
            { "leastCompleteCandidateStageEvidenceRecordCount", leastCompleteCandidateStageEvidenceRecordCount },
            { "sourceAttributionCoverageComplete", sourceAttributionCoverageComplete },
            { "sourceAttributionCoverageIsGating", false },
            { "selectedCandidateDetailCoverageComplete", selectedCandidateDetailCoverageComplete },
            { "selectedCandidateDetailCoverageIsGating", false },
            { "candidateScoreOrderCoverageComplete", candidateScoreOrderCoverageComplete },
            { "candidateScoreOrderCoverageIsGating", false },
            { "visibleActionScopeCoverageComplete", visibleActionScopeCoverageComplete },
            { "visibleActionScopeCoverageIsGating", false },
            { "dryRunGuardrailCoverageComplete", dryRunGuardrailCoverageComplete },
            { "dryRunGuardrailCoverageIsGating", false },
            { "recipeWritesGuardrailCoverageComplete", recipeWritesGuardrailCoverageComplete },
            { "recipeWritesGuardrailCoverageIsGating", false },
            { "stageEvidenceUiLineCoverageComplete", stageEvidenceUiLineCoverageComplete },
            { "stageEvidenceUiLineCoverageIsGating", false },
            { "partialEvidenceUiCoverageComplete", partialEvidenceUiCoverageComplete },
            { "partialEvidenceUiCoverageIsGating", false },
            { "actionReadinessDiagnosticsComplete", actionReadinessDiagnosticsComplete },
            { "actionReadinessCoverageIsGating", false },
            { "actionReadinessGuardrailDetailCoverageComplete", actionReadinessGuardrailDetailCoverageComplete },
            { "actionReadinessGuardrailDetailCoverageIsGating", false },
            { "recordDiagnosticCompletenessComplete", recordDiagnosticCompletenessComplete },
            { "recordDiagnosticCompletenessIsGating", false },
            { "missingCandidateDiagnosticsRecordIds", missingCandidateDiagnosticsRecordIds },
            { "missingSelectedCandidateRecordIds", missingSelectedCandidateRecordIds },
            { "selectedCandidateKindCounts", selectedCandidateKindCounts },
            { "candidateKindCoverage", candidateKindCoverage },
            { "candidateScoreCoverage", candidateScoreCoverage },
            { "candidateVisibleControlLineCoverage", candidateVisibleControlLineCoverage },
            { "candidateControlValueLineCoverage", candidateControlValueLineCoverage },
            { "candidateScoreComponentLineCoverage", candidateScoreComponentLineCoverage },
            { "candidateWarningLineCoverage", candidateWarningLineCoverage },
            { "candidateStageDiagnosticCoverage", candidateStageDiagnosticCoverage },
            { "candidateStageEvidenceAccounting", candidateStageEvidenceAccounting },
            { "sourceAttributionCoverage", sourceAttributionCoverage },
            { "selectedCandidateDetailCoverage", selectedCandidateDetailCoverage },
            { "candidateScoreOrderCoverage", candidateScoreOrderCoverage },
            { "visibleActionScopeCoverage", visibleActionScopeCoverage },
            { "dryRunGuardrailCoverage", dryRunGuardrailCoverage },
            { "recipeWritesGuardrailCoverage", recipeWritesGuardrailCoverage },
            { "stageEvidenceUiLineCoverage", stageEvidenceUiLineCoverage },
            { "partialEvidenceUiCoverage", partialEvidenceUiCoverage },
            { "missingActionReadinessRecordIds", missingActionReadinessRecordIds },
            { "actionReadinessLineCoverage", actionReadinessLineCoverage },
            { "actionReadinessGuardrailDetailCoverage", actionReadinessGuardrailDetailCoverage },
            { "recordDiagnosticCompleteness", recordDiagnosticCompleteness }
        } },
        { "stageEvidence", {
            { "sidecar", {
                { "file", reportObject ? report.value("stageEvidenceFile", "") : "" },
                { "entryCount", reportObject ? report.value("stageEvidenceEntryCount", 0) : 0 },
                { "matchedEntryCount", reportObject ? report.value("stageEvidenceMatchedEntryCount", 0) : 0 },
                { "unmatchedEntryCount", reportObject ? report.value("stageEvidenceUnmatchedEntryCount", 0) : 0 },
                { "matchedRecordCount", stageEvidenceMatchedRecordCount },
                { "appliedRecordCount", reportObject ? report.value("stageEvidenceAppliedCount", stageEvidenceAppliedRecordCount) : stageEvidenceAppliedRecordCount }
            } },
            { "allRequiredStagesComplete", requiredStageEvidenceComplete },
            { "requiredStageCoverage", stageEvidenceCoverage }
        } },
        { "evidenceChecklist", {
            { "allRequiredEvidenceComplete", allRequiredEvidenceComplete },
            { "items", evidenceChecklistItems }
        } },
        { "constantTuningEvidence", constantTuningEvidence },
        { "recordStatusCounts", recordStatusCounts },
        { "validationGapCounts", validationGapCounts },
        { "visibleFieldChanges", {
            { "recordsWithChanges", recordsWithVisibleFieldChanges },
            { "expectedForValidationRecords", 0 },
            { "recordIds", visibleFieldChangeRecordIds }
        } },
        { "visibleRecipeWriteAudit", {
            { "recordsWithAudit", recordsWithVisibleRecipeWriteAudit },
            { "recordsWithProposedVisibleWrites", recordsWithVisibleRecipeWriteAuditControls },
            { "totalProposedVisibleControlWrites", totalAuditedVisibleRecipeControlWrites },
            { "recordsWithProposedValueDetails", recordsWithVisibleRecipeWriteAuditValues },
            { "totalProposedValueDetails", totalAuditedVisibleRecipeControlValues },
            { "controlCounts", visibleRecipeWriteAuditControlCounts },
            { "valueDetailControlCounts", visibleRecipeWriteAuditValueControlCounts },
            { "recipeFieldCounts", visibleRecipeWriteAuditRecipeFieldCounts },
            { "missingAuditRecordIds", missingVisibleRecipeWriteAuditRecordIds },
            { "recordsWithoutProposedVisibleWrites", noVisibleRecipeWriteAuditControlsRecordIds },
            { "missingProposedValueDetailRecordIds", missingVisibleRecipeWriteAuditValueRecordIds },
            { "coverageComplete", visibleRecipeWriteAuditComplete },
            { "valueDetailCoverageComplete", visibleRecipeWriteAuditValuesComplete },
            { "coverageIsGating", false },
            { "validationCommandMutatesRecipe", false },
            { "hiddenOutputPass", false }
        } },
        { "tuningReadiness", {
            { "mechanicalInputsComplete", blockers.empty() },
            { "constantEvidenceStatusVersion", constantTuningEvidence.value("version", 0) },
            { "readyConstantCount", constantTuningEvidence.value("readyConstantCount", 0) },
            { "readyForFullTuningReview", constantTuningEvidence.value("readyForFullTuningReview", false) },
            { "constantsTunedByThisCommand", false },
            { "blockingReasons", blockers },
            { "nextAction", blockers.empty()
                ? "Review representative coverage manually before tuning constants."
                : "Resolve blockingReasons before tuning constants." }
        } },
        { "validationWorkflow", BuildValidationWorkflowChecklist(
            reportObject ? std::filesystem::path(report.value("workspaceRoot", "")) : std::filesystem::path(),
            reportObject ? std::filesystem::path(report.value("annotationTemplateFile", "")) : std::filesystem::path(),
            reportObject ? std::filesystem::path(report.value("annotationFile", "")) : std::filesystem::path(),
            reportObject ? std::filesystem::path(report.value("stageEvidenceTemplateFile", "")) : std::filesystem::path(),
            reportObject ? std::filesystem::path(report.value("stageEvidenceFile", "")) : std::filesystem::path(),
            recordsPath) }
    };
}

nlohmann::json BuildCurrentEngineeringDefaults() {
    return {
        { "state", "engineering-defaults-not-validation-tuned" },
        { "constantTuningEvidenceGuide", BuildTuningConstantEvidenceGuide() },
        { "rawExposure", {
            { "targetMedianRelativeToWhiteEv", -2.70 },
            { "deltaClampMinEv", -0.50 },
            { "deltaClampMaxEv", 1.00 },
            { "autoApplyConfidenceMin", 0.85 },
            { "autoApplyMaxAbsDeltaEv", 0.50 }
        } },
        { "balancedLocal", {
            { "minConfidence", 0.70 },
            { "maxAbsDeltaEv", 1.00 },
            { "maxAdjustmentPoints", 2 },
            { "maxColorTargetedPoints", 1 }
        } },
        { "mildFinishTone", {
            { "maxStrength", 0.25 },
            { "minApplyStrength", 0.035 },
            { "midSpreadGoodEv", 1.00 },
            { "midSpreadFlatLimitEv", 2.40 },
            { "wideSpreadGoodEv", 5.50 },
            { "wideSpreadLimitEv", 8.00 },
            { "pointYDeltaPerStrength", 0.07 }
        } },
        { "tuningRequirement",
          "Do not change these constants from this inventory alone. Tune only after representative real RAW records and human review." }
    };
}

nlohmann::json BuildRecordForSource(
    const Stack::RawWorkspace::SourceRecord& source,
    const std::vector<std::string>& tags,
    const RawStartingPointRecordOptions& options,
    const RawStartingPointAnnotationEntry* annotation,
    const std::string& annotationMatchedKey,
    const RawStartingPointAnnotationEntry* stageEvidence,
    const std::string& stageEvidenceMatchedKey,
    bool& outMetadataLoaded,
    bool& outRawSafetyLoaded,
    bool& outStageEvidenceApplied) {
    outMetadataLoaded = false;
    outRawSafetyLoaded = false;
    outStageEvidenceApplied = false;
    std::vector<std::string> recordTags = tags;
    MergeAnnotationTags(recordTags, annotation);
    nlohmann::json humanReview = BuildHumanReviewTemplate();
    MergeAnnotationHumanReview(humanReview, annotation);

    Raw::RawMetadata metadata;
    Raw::RawImageData rawData;
    bool rawFileLoaded = false;
    if (options.loadRawSafety) {
        rawFileLoaded = Raw::RawLoader::LoadFile(source.absolutePath.string(), rawData);
        metadata = rawData.metadata;
        outMetadataLoaded = rawFileLoaded && metadata.error.empty();
    } else {
        outMetadataLoaded = Raw::RawLoader::LoadMetadata(source.absolutePath.string(), metadata);
    }

    nlohmann::json record = {
        { "source", SerializeSourceIdentity(source) },
        { "realRawCapturePlan", BuildRealRawSourceCapturePlan(source) },
        { "imageCategoryTags", JsonStringVector(recordTags) },
        { "requiresHumanCategoryTags", recordTags.empty() },
        { "humanReview", humanReview },
        { "annotation", SerializeAnnotationMatch(options, annotation, annotationMatchedKey) }
    };

    auto applyStageEvidence = [&]() {
        const bool sourceIdentityMatches =
            StageEvidenceSourceIdentityMatches(source, stageEvidence);
        outStageEvidenceApplied =
            sourceIdentityMatches &&
            MergeStageEvidenceDiagnostics(record, stageEvidence);
        record["stageEvidence"] = SerializeStageEvidenceMatch(
            options,
            stageEvidence,
            stageEvidenceMatchedKey,
            outStageEvidenceApplied);
        record["stageEvidence"]["sourceIdentity"] =
            SerializeStageEvidenceSourceIdentityCheck(source, stageEvidence);
        if (stageEvidence != nullptr && !sourceIdentityMatches) {
            nlohmann::json validationGaps =
                record.value("validationGaps", nlohmann::json::array());
            if (!validationGaps.is_array()) {
                validationGaps = nlohmann::json::array();
            }
            validationGaps.push_back("stage evidence source identity mismatch");
            record["validationGaps"] = std::move(validationGaps);
        }
    };
    auto refreshVisibleRecipeWriteAudit = [&]() {
        record["visibleRecipeWriteAudit"] =
            BuildVisibleRecipeWriteAudit(
                record.value("startingPointDiagnostics", nlohmann::json::object()));
    };

    if (!outMetadataLoaded) {
        record["metadata"] = {
            { "loaded", false },
            { "error", "RawLoader::LoadMetadata failed." }
        };
        if (options.loadRawSafety) {
            record["metadata"]["error"] = metadata.error.empty()
                ? "RawLoader::LoadFile failed."
                : metadata.error;
            record["rawBufferSafety"] = {
                { "requested", true },
                { "loaded", false },
                { "source", "unavailable" },
                { "statusMessage", "Raw buffer safety unavailable because RAW loading failed." },
                { "error", JsonStringOrNull(metadata.error) }
            };
        }
        record["recordStatus"] = "metadata-failed";
        applyStageEvidence();
        refreshVisibleRecipeWriteAudit();
        return record;
    }

    if (metadata.sourcePath.empty()) {
        metadata.sourcePath = source.absolutePath.string();
    }

    const Stack::RawAnalysis::RawMetadataSummary metadataSummary =
        Stack::RawAnalysis::BuildRawMetadataSummary(metadata);
    const Stack::RawAutoStartPoint::RawAutoStartPointRawSafetyStats metadataSafety =
        BuildMetadataOnlyRawSafety(metadata, metadataSummary);
    RawBufferSafetyEvidence rawSafetyEvidence;
    rawSafetyEvidence.requested = options.loadRawSafety;
    if (options.loadRawSafety) {
        rawSafetyEvidence = BuildRawBufferSafetyEvidence(
            rawData,
            metadata,
            metadataSummary,
            options.maxRawSafetySamples);
        outRawSafetyLoaded = rawSafetyEvidence.loaded;
    } else {
        rawSafetyEvidence.details = {
            { "requested", false },
            { "loaded", false },
            { "source", "metadata-proxy" },
            { "statusMessage", "Run with --load-raw-safety to sample decoded rawBuffer evidence." }
        };
    }
    const Stack::RawAutoStartPoint::RawAutoStartPointRawSafetyStats diagnosticsRawSafety =
        rawSafetyEvidence.loaded ? rawSafetyEvidence.stats : metadataSafety;

    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe(source.absolutePath.string(), source.fileName);

    Stack::RawAnalysis::RawImageAnalysis analysis;
    analysis.sourceKey = source.relativePathKey;
    analysis.metadata = metadataSummary;
    analysis.statusMessage = rawSafetyEvidence.loaded
        ? "Validation record loaded sampled raw-buffer safety. Render-stage stats are still pending."
        : "Validation record metadata loaded. Render-stage stats are not captured by this metadata-only record command.";
    analysis.highlight.sensorStatus = Stack::RawAnalysis::AnalysisStageStatus::Unavailable;
    analysis.highlight.displayStatus = Stack::RawAnalysis::AnalysisStageStatus::Unavailable;
    analysis.highlight.blocksPositiveRawExposure = true;
    analysis.highlight.statusMessage = rawSafetyEvidence.loaded
        ? "RAW buffer safety was sampled for diagnostics; display clipping still requires rendered validation records."
        : "RAW sensor and display clipping require rendered validation records.";

    const Stack::RawAutoBase::AutoBaseRecommendations recommendations =
        Stack::RawAutoBase::BuildAutoBaseRecommendations(analysis, recipe);
    Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics diagnostics =
        Stack::RawAutoStartPoint::MakeUnavailableDiagnostics(
            source.relativePathKey,
            rawSafetyEvidence.loaded
                ? "RAW Starting Point validation record captured raw-buffer safety; render-stage evidence is pending."
                : "RAW Starting Point validation record captured metadata only; render-stage evidence is pending.");
    diagnostics.rawSafety = diagnosticsRawSafety;
    diagnostics =
        Stack::RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            diagnostics,
            recipe,
            analysis,
            recommendations);
    diagnostics.rawSafety = diagnosticsRawSafety;

    record["recordStatus"] = rawSafetyEvidence.loaded
        ? "raw-safety-loaded"
        : (options.loadRawSafety ? "raw-safety-unavailable" : "metadata-only");
    record["metadata"] = SerializeRawMetadata(metadata);
    record["metadataSummary"] = SerializeMetadataSummary(metadataSummary);
    record["rawBufferSafety"] = rawSafetyEvidence.details;
    record["currentRecipeSummary"] = SerializeRecipeSummary(recipe);
    record["startingPointDiagnostics"] =
        Stack::RawAutoStartPoint::SerializeDiagnostics(diagnostics);
    record["visibleFieldsChanged"] = nlohmann::json::array();
    nlohmann::json validationGaps = nlohmann::json::array();
    if (!rawSafetyEvidence.loaded) {
        validationGaps.push_back(options.loadRawSafety
            ? "raw buffer safety load failed or unavailable"
            : "raw buffer safety (--load-raw-safety not requested)");
    }
    validationGaps.push_back("neutral scene stats");
    validationGaps.push_back("raw placement stats");
    validationGaps.push_back("local candidate stats");
    validationGaps.push_back("finish tone candidate stats");
    validationGaps.push_back("display candidate stats");
    validationGaps.push_back("human review");
    record["validationGaps"] = std::move(validationGaps);
    applyStageEvidence();
    refreshVisibleRecipeWriteAudit();
    return record;
}

bool WriteReport(const nlohmann::json& report, const std::filesystem::path& outputPath) {
    if (outputPath.empty()) {
        std::cout << report.dump(2) << "\n";
        return true;
    }

    std::error_code ec;
    if (outputPath.has_parent_path()) {
        std::filesystem::create_directories(outputPath.parent_path(), ec);
        if (ec) {
            std::cerr << "RAW Starting Point record validation failed: could not create output folder "
                      << outputPath.parent_path().string() << "\n";
            return false;
        }
    }

    const std::filesystem::path tempPath =
        outputPath.parent_path() / (outputPath.filename().string() + ".tmp");
    std::filesystem::remove(tempPath, ec);
    std::ofstream out(tempPath, std::ios::binary);
    if (!out) {
        std::cerr << "RAW Starting Point record validation failed: could not open output file "
                  << tempPath.string() << "\n";
        return false;
    }
    out << report.dump(2) << "\n";
    out.close();
    if (!out) {
        std::cerr << "RAW Starting Point record validation failed: could not write output file "
                  << tempPath.string() << "\n";
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    std::filesystem::remove(outputPath, ec);
    ec.clear();
    std::filesystem::rename(tempPath, outputPath, ec);
    if (ec) {
        std::cerr << "RAW Starting Point record validation failed: could not move output file to "
                  << outputPath.string() << "\n";
        std::filesystem::remove(tempPath, ec);
        return false;
    }
    return true;
}

void PrintValidationWorkflowUsage() {
    std::cerr
        << "RAW Starting Point validation workflow usage: "
        << "--raw-starting-point-validation-workflow "
        << "[--workspace-root <workspace-folder>] "
        << "[--annotation-template annotations-template.json] "
        << "[--annotations annotations.json] "
        << "[--stage-evidence-template stage-evidence-template.json] "
        << "[--stage-evidence stage-evidence.json] "
        << "[--records records.json] "
        << "[--evidence-manifest-out evidence-manifest.json] "
        << "[--out workflow.json]\n";
}

bool ParseValidationWorkflowOptions(
    int rawArgCount,
    char** rawArgs,
    RawStartingPointValidationWorkflowOptions& options) {
    if (rawArgCount > 0 && rawArgs == nullptr) {
        PrintValidationWorkflowUsage();
        return false;
    }

    for (int i = 0; i < rawArgCount; ++i) {
        const std::string option = rawArgs[i] ? rawArgs[i] : "";
        auto requireValue = [&](const char* name) -> const char* {
            if (i + 1 >= rawArgCount || rawArgs[i + 1] == nullptr) {
                std::cerr << "RAW Starting Point validation workflow failed: "
                          << name << " requires a value.\n";
                return nullptr;
            }
            return rawArgs[++i];
        };

        if (option == "--workspace-root") {
            const char* value = requireValue("--workspace-root");
            if (value == nullptr) {
                return false;
            }
            options.workspaceRoot = value;
        } else if (option == "--annotation-template") {
            const char* value = requireValue("--annotation-template");
            if (value == nullptr) {
                return false;
            }
            options.annotationTemplatePath = value;
        } else if (option == "--annotations") {
            const char* value = requireValue("--annotations");
            if (value == nullptr) {
                return false;
            }
            options.annotationPath = value;
        } else if (option == "--stage-evidence-template") {
            const char* value = requireValue("--stage-evidence-template");
            if (value == nullptr) {
                return false;
            }
            options.stageEvidenceTemplatePath = value;
        } else if (option == "--stage-evidence") {
            const char* value = requireValue("--stage-evidence");
            if (value == nullptr) {
                return false;
            }
            options.stageEvidencePath = value;
        } else if (option == "--records") {
            const char* value = requireValue("--records");
            if (value == nullptr) {
                return false;
            }
            options.recordsPath = value;
        } else if (option == "--evidence-manifest-out") {
            const char* value = requireValue("--evidence-manifest-out");
            if (value == nullptr) {
                return false;
            }
            options.evidenceManifestOutputPath = value;
        } else if (option == "--out") {
            const char* value = requireValue("--out");
            if (value == nullptr) {
                return false;
            }
            options.outputPath = value;
        } else {
            std::cerr << "RAW Starting Point validation workflow failed: unknown argument "
                      << option << "\n";
            PrintValidationWorkflowUsage();
            return false;
        }
    }
    return true;
}

bool NormalizeOptionalWorkflowPath(std::filesystem::path& path, const char* label) {
    if (path.empty()) {
        return true;
    }

    std::error_code ec;
    path = std::filesystem::absolute(path, ec).lexically_normal();
    if (ec) {
        std::cerr << "RAW Starting Point validation workflow failed: invalid "
                  << label << " path.\n";
        return false;
    }
    return true;
}

void PrintValidationGateStatusUsage() {
    std::cerr
        << "RAW Starting Point validation gate check usage: "
        << "--check-raw-starting-point-validation-gates "
        << "[--annotation-check annotations-check.json] "
        << "[--stage-evidence-check stage-evidence-check.json] "
        << "[--sidecar-preflight sidecar-preflight.json] "
        << "[--records-summary records-summary.json] "
        << "[--constant-review-check constant-review-check.json] "
        << "[--out gate-status.json] [--require-ready]\n";
}

bool ParseValidationGateStatusOptions(
    int rawArgCount,
    char** rawArgs,
    RawStartingPointValidationGateStatusOptions& options) {
    if (rawArgCount > 0 && rawArgs == nullptr) {
        PrintValidationGateStatusUsage();
        return false;
    }

    for (int i = 0; i < rawArgCount; ++i) {
        const std::string option = rawArgs[i] ? rawArgs[i] : "";
        auto requireValue = [&](const char* name) -> const char* {
            if (i + 1 >= rawArgCount || rawArgs[i + 1] == nullptr) {
                std::cerr << "RAW Starting Point validation gate check failed: "
                          << name << " requires a value.\n";
                return nullptr;
            }
            return rawArgs[++i];
        };

        if (option == "--annotation-check") {
            const char* value = requireValue("--annotation-check");
            if (value == nullptr) {
                return false;
            }
            options.annotationCheckPath = value;
        } else if (option == "--stage-evidence-check") {
            const char* value = requireValue("--stage-evidence-check");
            if (value == nullptr) {
                return false;
            }
            options.stageEvidenceCheckPath = value;
        } else if (option == "--sidecar-preflight") {
            const char* value = requireValue("--sidecar-preflight");
            if (value == nullptr) {
                return false;
            }
            options.recordSidecarPreflightPath = value;
        } else if (option == "--records-summary") {
            const char* value = requireValue("--records-summary");
            if (value == nullptr) {
                return false;
            }
            options.validationSummaryPath = value;
        } else if (option == "--constant-review-check") {
            const char* value = requireValue("--constant-review-check");
            if (value == nullptr) {
                return false;
            }
            options.constantReviewCheckPath = value;
        } else if (option == "--out") {
            const char* value = requireValue("--out");
            if (value == nullptr) {
                return false;
            }
            options.outputPath = value;
        } else if (option == "--require-ready") {
            options.requireReady = true;
        } else {
            std::cerr << "RAW Starting Point validation gate check failed: unknown argument "
                      << option << "\n";
            PrintValidationGateStatusUsage();
            return false;
        }
    }
    return true;
}

void PrintSummaryUsage() {
    std::cerr
        << "RAW Starting Point summary usage: --summarize-raw-starting-point-records "
        << "<records.json> [--out summary.json] [--require-ready] "
        << "[--constant-review-template-out constant-review-template.json]\n";
}

bool ParseSummaryOptions(
    int rawArgCount,
    char** rawArgs,
    RawStartingPointSummaryOptions& options) {
    if (rawArgCount <= 0 || rawArgs == nullptr || rawArgs[0] == nullptr || PathLooksLikeOption(rawArgs[0])) {
        PrintSummaryUsage();
        return false;
    }

    options.inputPath = rawArgs[0];
    for (int i = 1; i < rawArgCount; ++i) {
        const std::string option = rawArgs[i] ? rawArgs[i] : "";
        auto requireValue = [&](const char* name) -> const char* {
            if (i + 1 >= rawArgCount || rawArgs[i + 1] == nullptr) {
                std::cerr << "RAW Starting Point summary failed: "
                          << name << " requires a value.\n";
                return nullptr;
            }
            return rawArgs[++i];
        };

        if (option == "--out") {
            const char* value = requireValue("--out");
            if (value == nullptr) {
                return false;
            }
            options.outputPath = value;
        } else if (option == "--require-ready") {
            options.requireReady = true;
        } else if (option == "--constant-review-template-out") {
            const char* value = requireValue("--constant-review-template-out");
            if (value == nullptr) {
                return false;
            }
            options.constantReviewTemplateOutputPath = value;
        } else {
            std::cerr << "RAW Starting Point summary failed: unknown argument "
                      << option << "\n";
            return false;
        }
    }
    return true;
}

void PrintConstantReviewCheckUsage() {
    std::cerr
        << "RAW Starting Point constant review check usage: "
        << "--check-raw-starting-point-constant-review "
        << "<records-summary.json> <constant-review-template.json> "
        << "[--out check.json] [--repair-out repair.json] "
        << "[--suggested-review-patch-bundle-out bundle.json] "
        << "[--require-ready]\n";
}

bool ParseConstantReviewCheckOptions(
    int rawArgCount,
    char** rawArgs,
    RawStartingPointConstantReviewCheckOptions& options) {
    if (rawArgCount < 2 ||
        rawArgs == nullptr ||
        rawArgs[0] == nullptr ||
        rawArgs[1] == nullptr ||
        PathLooksLikeOption(rawArgs[0]) ||
        PathLooksLikeOption(rawArgs[1])) {
        PrintConstantReviewCheckUsage();
        return false;
    }

    options.summaryPath = rawArgs[0];
    options.reviewTemplatePath = rawArgs[1];
    for (int i = 2; i < rawArgCount; ++i) {
        const std::string option = rawArgs[i] ? rawArgs[i] : "";
        auto requireValue = [&](const char* name) -> const char* {
            if (i + 1 >= rawArgCount || rawArgs[i + 1] == nullptr) {
                std::cerr << "RAW Starting Point constant review check failed: "
                          << name << " requires a value.\n";
                return nullptr;
            }
            return rawArgs[++i];
        };

        if (option == "--out") {
            const char* value = requireValue("--out");
            if (value == nullptr) {
                return false;
            }
            options.outputPath = value;
        } else if (option == "--repair-out") {
            const char* value = requireValue("--repair-out");
            if (value == nullptr) {
                return false;
            }
            options.repairOutputPath = value;
        } else if (option == "--suggested-review-patch-bundle-out") {
            const char* value = requireValue("--suggested-review-patch-bundle-out");
            if (value == nullptr) {
                return false;
            }
            options.suggestedReviewPatchBundleOutputPath = value;
        } else if (option == "--require-ready") {
            options.requireReady = true;
        } else {
            std::cerr << "RAW Starting Point constant review check failed: unknown argument "
                      << option << "\n";
            return false;
        }
    }
    return true;
}

Stack::RawAnalysis::RawImageAnalysis BuildSyntheticStartingPointHandoffAnalysis(
    float p01Ev,
    float p05Ev,
    float p50Ev,
    float p99Ev,
    float p999Ev,
    float dynamicRangeEv) {
    Stack::RawAnalysis::RawImageAnalysis analysis;
    analysis.valid = true;
    analysis.sourceKey = "raw/staged-cap-validation.dng";
    analysis.currentFrameStats.valid = true;
    analysis.currentFrameStats.status = Stack::RawAnalysis::AnalysisStageStatus::Complete;
    analysis.currentFrameStats.p01Ev = p01Ev;
    analysis.currentFrameStats.p05Ev = p05Ev;
    analysis.currentFrameStats.p50Ev = p50Ev;
    analysis.currentFrameStats.p99Ev = p99Ev;
    analysis.currentFrameStats.p999Ev = p999Ev;
    analysis.currentFrameStats.p01Luma = std::exp2(p01Ev);
    analysis.currentFrameStats.p05Luma = std::exp2(p05Ev);
    analysis.currentFrameStats.p50Luma = std::exp2(p50Ev);
    analysis.currentFrameStats.p99Luma = std::exp2(p99Ev);
    analysis.currentFrameStats.p999Luma = std::exp2(p999Ev);
    analysis.currentFrameStats.dynamicRangeEv = dynamicRangeEv;
    analysis.currentFrameStats.validPixelPercent = 100.0f;
    analysis.technicalStats = analysis.currentFrameStats;
    analysis.technicalStats.status = Stack::RawAnalysis::AnalysisStageStatus::Complete;
    analysis.highlight.valid = true;
    analysis.highlight.sensorStatus = Stack::RawAnalysis::AnalysisStageStatus::Complete;
    analysis.highlight.displayStatus = Stack::RawAnalysis::AnalysisStageStatus::Complete;
    analysis.highlight.hdrPixelPercent = 0.0f;
    analysis.highlight.displayClipPercent = 0.0f;
    analysis.highlight.anyChannelClipPercent = 0.0f;
    analysis.highlight.allChannelClipPercent = 0.0f;
    analysis.highlight.partialClipColorRisk = false;
    analysis.highlight.severeSensorClip = false;
    analysis.highlight.blocksPositiveRawExposure = false;
    return analysis;
}

Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics
BuildSyntheticStagedCapHandoffDiagnostics(
    float neutralP50Ev,
    float neutralP99Ev,
    float neutralP999Ev,
    float wbScaledHeadroomEv) {
    namespace RawAutoStartPoint = Stack::RawAutoStartPoint;

    RawAutoStartPoint::RawAutoStartPointStageDiagnostics rawTechnicalStage;
    rawTechnicalStage.stage = RawAutoStartPoint::RawAutoStartPointStage::RawTechnical;
    rawTechnicalStage.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    rawTechnicalStage.confidence01 = 1.0f;
    rawTechnicalStage.rawSafety.valid = true;
    rawTechnicalStage.rawSafety.activeValidFraction = 1.0f;
    rawTechnicalStage.rawSafety.wbScaledHeadroomEv = wbScaledHeadroomEv;
    rawTechnicalStage.rawSafety.headroomEv = wbScaledHeadroomEv;
    rawTechnicalStage.rawSafety.perChannelNearClippedFraction = { 0.0001f, 0.0002f, 0.0001f };
    rawTechnicalStage.rawSafety.highlightRecoverabilityScore = 0.95f;
    rawTechnicalStage.statusMessage =
        "Synthetic Raw Technical safety ledger for native editor-state validation.";

    RawAutoStartPoint::RawAutoStartPointStageDiagnostics neutralSceneStage;
    neutralSceneStage.stage = RawAutoStartPoint::RawAutoStartPointStage::NeutralScene;
    neutralSceneStage.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    neutralSceneStage.confidence01 = 1.0f;
    neutralSceneStage.scene.valid = true;
    neutralSceneStage.scene.stage = RawAutoStartPoint::RawAutoStartPointStage::NeutralScene;
    neutralSceneStage.scene.status = RawAutoStartPoint::RawAutoStartPointStageStatus::Complete;
    neutralSceneStage.scene.evPercentiles.valid = true;
    neutralSceneStage.scene.evPercentiles.p50 = neutralP50Ev;
    neutralSceneStage.scene.evPercentiles.p99 = neutralP99Ev;
    neutralSceneStage.scene.evPercentiles.p999 = neutralP999Ev;
    neutralSceneStage.statusMessage =
        "Synthetic Neutral Scene analysis render for native editor-state validation.";

    RawAutoStartPoint::RawAutoStartPointCandidate candidate;
    candidate.valid = true;
    candidate.stageDiagnostics.push_back(rawTechnicalStage);
    candidate.stageDiagnostics.push_back(neutralSceneStage);

    RawAutoStartPoint::RawAutoStartPointDiagnostics diagnostics;
    diagnostics.valid = true;
    diagnostics.candidates.push_back(candidate);
    return diagnostics;
}

bool ControlListContains(
    const std::vector<Stack::RawAutoStartPoint::RawAutoStartPointControl>& controls,
    Stack::RawAutoStartPoint::RawAutoStartPointControl expected) {
    return std::find(controls.begin(), controls.end(), expected) != controls.end();
}

bool SummaryListContains(
    const std::vector<std::string>& summaries,
    const std::string& fragment) {
    return std::find_if(
        summaries.begin(),
        summaries.end(),
        [&](const std::string& summary) {
            return summary.find(fragment) != std::string::npos;
        }) != summaries.end();
}

std::string JoinValidationSummaryParts(const std::vector<std::string>& parts) {
    std::string summary;
    for (const std::string& part : parts) {
        if (part.empty()) {
            continue;
        }
        if (!summary.empty()) {
            summary += ", ";
        }
        summary += part;
    }
    return summary.empty() ? std::string("None") : summary;
}

bool RunRawStartingPointEditorStateHandoffChecks(std::vector<std::string>& errors) {
    namespace EditorTypes = Stack::EditorModuleTypes;
    namespace RawAutoBase = Stack::RawAutoBase;
    namespace RawAutoStartPoint = Stack::RawAutoStartPoint;
    namespace RawRecipe = Stack::RawRecipe;

    const auto require = [&](bool condition, const std::string& message) {
        if (!condition) {
            errors.push_back(message);
        }
        return condition;
    };

    RawRecipe::RawDevelopmentRecipe recipe =
        RawRecipe::MakeDefaultRecipe(
            "D:/validation/raw/staged-cap-validation.dng",
            "staged-cap-validation.dng");
    const Stack::RawAnalysis::RawImageAnalysis analysis =
        BuildSyntheticStartingPointHandoffAnalysis(-8.0f, -7.0f, -5.0f, -0.2f, 0.0f, 5.0f);
    const RawAutoBase::AutoBaseRecommendations recommendations =
        RawAutoBase::BuildAutoBaseRecommendations(analysis, recipe);
    require(
        recommendations.exposure.valid &&
            !recommendations.exposure.autoApplyAllowed &&
            recommendations.exposure.deltaEv > 0.50f,
        "Synthetic baseline should produce a large manual-only RAW Exposure lift before staged capping.");

    const RawAutoStartPoint::RawAutoStartPointDiagnostics diagnostics =
        BuildSyntheticStagedCapHandoffDiagnostics(-5.0f, -0.2f, 0.0f, 1.20f);
    const RawAutoStartPoint::RawAutoStartPointConservativePlan plan =
        RawAutoStartPoint::BuildConservativeStartingPointPlan(
            recipe,
            analysis,
            recommendations,
            diagnostics);

    const bool planShapeValid =
        require(plan.valid, "Staged capped handoff plan should be valid.") &&
        require(plan.hasUpstreamRecipeChanges,
            "Staged capped handoff plan should require the post-fit editor path.") &&
        require(
            std::abs(plan.upstreamRecipe.preToneExposureEv -
                (recipe.preToneExposureEv + 0.50f)) < 0.001f,
            "Staged capped handoff plan should apply the visible +0.50 EV cap.") &&
        require(
            ControlListContains(
                plan.visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::RawExposure),
            "Staged capped handoff plan should report RAW Exposure as a touched visible control.") &&
        require(
            ControlListContains(
                plan.visibleEdits.touchedControls,
                RawAutoStartPoint::RawAutoStartPointControl::DisplayFit),
            "Staged capped handoff plan should keep Display Fit in the final touched-control set.") &&
        require(
            SummaryListContains(plan.withheldSummaries, "one-click cap"),
            "Staged capped handoff plan should report the additional lift as withheld.") &&
        require(
            SummaryListContains(plan.evidenceSummaries, "RAW Exposure from conservative exposure evidence"),
            "Staged capped handoff plan should report the RAW Exposure evidence source.");

    if (!planShapeValid) {
        return false;
    }

    EditorTypes::RawWorkspaceStartingPointPendingAction pending;
    pending.active = true;
    pending.phase = EditorTypes::RawStartingPointPendingPhase::WaitingForPostApplyAnalysis;
    pending.sourceKey = analysis.sourceKey;
    pending.sourceHash = 7001;
    pending.recipeFingerprint = "synthetic-staged-cap-recipe";
    pending.originalRecipe = recipe;
    pending.plannedUpstreamRecipe = plan.upstreamRecipe;
    pending.appliedControls =
        EditorTypes::RawStartingPointControlsWithoutDisplayFit(
            plan.visibleEdits.touchedControls);
    pending.planSummary = plan.summary;
    pending.appliedControlsSummary =
        RawAutoStartPoint::ControlLabel(RawAutoStartPoint::RawAutoStartPointControl::RawExposure);
    pending.appliedValuesSummary = "RAW Exposure +0.50 EV";
    pending.withheldControlsSummary = JoinValidationSummaryParts(plan.withheldSummaries);
    pending.evidenceSummary = JoinValidationSummaryParts(plan.evidenceSummaries);

    EditorTypes::RawWorkspaceAutoBaseUiState ui;
    ui.hasAppliedViewFit = true;
    ui.hasRevertSnapshot = true;
    EditorTypes::MarkRawStartingPointUpstreamApplied(ui, std::move(pending));

    require(
        ui.pendingStartingPoint.active,
        "Editor-state handoff should keep a post-fit pending action active.");
    require(
        !ui.hasAppliedViewFit && ui.startingPointDisplayFitPending,
        "Editor-state handoff should mark Display Fit pending after upstream controls apply.");
    require(
        ControlListContains(
            ui.pendingStartingPoint.appliedControls,
            RawAutoStartPoint::RawAutoStartPointControl::RawExposure),
        "Editor-state handoff should keep RAW Exposure in applied upstream controls.");
    require(
        !ControlListContains(
            ui.pendingStartingPoint.appliedControls,
            RawAutoStartPoint::RawAutoStartPointControl::DisplayFit),
        "Editor-state handoff should not claim Display Fit as already applied.");
    require(
        ui.startingPointAppliedControlsSummary.find("RAW Exposure") != std::string::npos,
        "Editor-state handoff should show RAW Exposure in changed controls.");
    require(
        ui.startingPointAppliedValuesSummary.find("+0.50 EV") != std::string::npos,
        "Editor-state handoff should show the visible RAW Exposure value that changed.");
    require(
        ui.startingPointWithheldControlsSummary.find("one-click cap") != std::string::npos,
        "Editor-state handoff should keep the additional lift visible as withheld.");
    require(
        ui.startingPointEvidenceSummary.find("RAW Exposure from conservative exposure evidence") !=
            std::string::npos,
        "Editor-state handoff should keep the evidence summary visible.");
    require(
        ui.summary.find("Display Fit pending") != std::string::npos,
        "Editor-state handoff should explain that Display Fit is still pending.");
    require(
        std::abs(ui.pendingStartingPoint.plannedUpstreamRecipe.preToneExposureEv -
            (recipe.preToneExposureEv + 0.50f)) < 0.001f,
        "Editor-state handoff should preserve the capped RAW Exposure value in the pending recipe.");

    EditorTypes::RawWorkspaceAutoBaseUiState canceledPostApplyUi = ui;
    require(
        EditorTypes::CancelRawStartingPointPendingAction(
            canceledPostApplyUi,
            "recipe changed before the queued Starting Point action completed."),
        "Editor-state post-apply cancellation should report cancellation.");
    require(
        !canceledPostApplyUi.pendingStartingPoint.active,
        "Editor-state post-apply cancellation should clear the pending action.");
    require(
        canceledPostApplyUi.startingPointDisplayFitPending,
        "Editor-state post-apply cancellation should keep Refit Display visible as pending.");
    require(
        canceledPostApplyUi.hasRevertSnapshot,
        "Editor-state post-apply cancellation should preserve undo availability.");
    require(
        canceledPostApplyUi.summary.find("canceled after applying upstream controls") !=
            std::string::npos,
        "Editor-state post-apply cancellation should explain that upstream controls already changed.");
    require(
        canceledPostApplyUi.startingPointAppliedControlsSummary.find("RAW Exposure") !=
            std::string::npos,
        "Editor-state post-apply cancellation should keep RAW Exposure visible as applied.");
    require(
        canceledPostApplyUi.startingPointAppliedValuesSummary.find("+0.50 EV") !=
            std::string::npos,
        "Editor-state post-apply cancellation should keep the applied RAW Exposure value visible.");
    require(
        canceledPostApplyUi.startingPointWithheldControlsSummary.find("one-click cap") !=
            std::string::npos &&
        canceledPostApplyUi.startingPointWithheldControlsSummary.find("Display Fit canceled") !=
            std::string::npos,
        "Editor-state post-apply cancellation should preserve prior withheld controls and the Display Fit cancellation reason.");
    require(
        canceledPostApplyUi.startingPointEvidenceSummary.find("RAW Exposure from conservative exposure evidence") !=
            std::string::npos,
        "Editor-state post-apply cancellation should preserve the evidence summary.");

    EditorTypes::RawWorkspaceAutoBaseUiState initialRenderFailureUi;
    initialRenderFailureUi.pendingStartingPoint.active = true;
    initialRenderFailureUi.pendingStartingPoint.phase =
        EditorTypes::RawStartingPointPendingPhase::WaitingForInitialAnalysis;
    require(
        EditorTypes::MarkRawStartingPointRenderFailure(
            initialRenderFailureUi,
            "render worker produced no RAW output."),
        "Editor-state initial render failure should update the pending action.");
    require(
        !initialRenderFailureUi.pendingStartingPoint.active,
        "Editor-state initial render failure should clear the pending action.");
    require(
        !initialRenderFailureUi.startingPointDisplayFitPending,
        "Editor-state initial render failure should not leave Display Fit pending before upstream controls apply.");
    require(
        initialRenderFailureUi.startingPointAppliedControlsSummary == "None",
        "Editor-state initial render failure should not claim applied controls.");
    require(
        initialRenderFailureUi.startingPointWithheldControlsSummary.find("render worker produced no RAW output") !=
            std::string::npos,
        "Editor-state initial render failure should report the render failure reason.");

    EditorTypes::RawWorkspaceAutoBaseUiState postApplyRenderFailureUi = ui;
    require(
        EditorTypes::MarkRawStartingPointRenderFailure(
            postApplyRenderFailureUi,
            "render worker produced no post-edit output."),
        "Editor-state post-apply render failure should update the pending action.");
    require(
        !postApplyRenderFailureUi.pendingStartingPoint.active,
        "Editor-state post-apply render failure should clear the pending action.");
    require(
        postApplyRenderFailureUi.startingPointDisplayFitPending,
        "Editor-state post-apply render failure should keep Refit Display visible as pending.");
    require(
        postApplyRenderFailureUi.hasRevertSnapshot,
        "Editor-state post-apply render failure should preserve undo availability.");
    require(
        postApplyRenderFailureUi.startingPointAppliedControlsSummary.find("RAW Exposure") !=
            std::string::npos,
        "Editor-state post-apply render failure should keep upstream applied controls visible.");
    require(
        postApplyRenderFailureUi.startingPointAppliedValuesSummary.find("+0.50 EV") !=
            std::string::npos,
        "Editor-state post-apply render failure should keep upstream applied values visible.");
    require(
        postApplyRenderFailureUi.startingPointWithheldControlsSummary.find("one-click cap") !=
            std::string::npos &&
        postApplyRenderFailureUi.startingPointWithheldControlsSummary.find("render worker produced no post-edit output") !=
            std::string::npos,
        "Editor-state post-apply render failure should preserve prior withheld controls and append the render failure reason.");
    require(
        postApplyRenderFailureUi.startingPointEvidenceSummary.find("RAW Exposure from conservative exposure evidence") !=
            std::string::npos,
        "Editor-state post-apply render failure should preserve the evidence summary.");

    EditorTypes::RawStartingPointContinuationContext context;
    context.sourceExists = true;
    context.activeSourceKey = ui.pendingStartingPoint.sourceKey;
    context.selectedSourceKey = ui.pendingStartingPoint.sourceKey;
    context.sourceHash = ui.pendingStartingPoint.sourceHash;
    context.analysisSourceKey = ui.pendingStartingPoint.sourceKey;
    context.analysisValid = true;
    context.recipeFingerprint = ui.pendingStartingPoint.recipeFingerprint;
    require(
        EditorTypes::EvaluateRawStartingPointContinuation(
            ui.pendingStartingPoint,
            context) ==
            EditorTypes::RawStartingPointContinuationDecision::ContinuePostApplyAnalysis,
        "Editor-state handoff should continue to Display Fit when matching post-edit analysis arrives.");

    EditorTypes::RawStartingPointContinuationContext changedHashContext = context;
    changedHashContext.sourceHash = context.sourceHash + 1;
    const EditorTypes::RawStartingPointContinuationDecision changedHashDecision =
        EditorTypes::EvaluateRawStartingPointContinuation(
            ui.pendingStartingPoint,
            changedHashContext);
    require(
        changedHashDecision ==
            EditorTypes::RawStartingPointContinuationDecision::CancelSourceChanged,
        "Editor-state continuation should cancel when the same RAW source key has a different source hash.");
    require(
        EditorTypes::RawStartingPointContinuationCancellationReason(
            changedHashDecision,
            ui.pendingStartingPoint,
            changedHashContext)
                .find("source identity changed") != std::string::npos,
        "Editor-state continuation should explain same-key source hash changes as source identity changes.");
    EditorTypes::RawWorkspaceAutoBaseUiState canceledChangedHashUi = ui;
    require(
        EditorTypes::CancelRawStartingPointPendingAction(
            canceledChangedHashUi,
            EditorTypes::RawStartingPointContinuationCancellationReason(
                changedHashDecision,
                ui.pendingStartingPoint,
                changedHashContext)),
        "Editor-state same-key source hash cancellation should report cancellation.");
    require(
        !canceledChangedHashUi.pendingStartingPoint.active,
        "Editor-state same-key source hash cancellation should clear the pending action.");
    require(
        canceledChangedHashUi.startingPointDisplayFitPending,
        "Editor-state same-key source hash cancellation after upstream apply should keep Display Fit pending.");
    require(
        canceledChangedHashUi.hasRevertSnapshot,
        "Editor-state same-key source hash cancellation should preserve undo availability.");
    require(
        canceledChangedHashUi.startingPointWithheldControlsSummary.find("source identity changed") !=
            std::string::npos,
        "Editor-state same-key source hash cancellation should surface the source identity reason.");
    require(
        canceledChangedHashUi.startingPointEvidenceSummary.find("RAW Exposure from conservative exposure evidence") !=
            std::string::npos,
        "Editor-state same-key source hash cancellation should preserve the evidence summary.");

    EditorTypes::RawStartingPointContinuationContext selectedSourceChangedContext = context;
    selectedSourceChangedContext.selectedSourceKey = "raw/other-selected-source.dng";
    const EditorTypes::RawStartingPointContinuationDecision selectedSourceChangedDecision =
        EditorTypes::EvaluateRawStartingPointContinuation(
            ui.pendingStartingPoint,
            selectedSourceChangedContext);
    require(
        selectedSourceChangedDecision ==
            EditorTypes::RawStartingPointContinuationDecision::CancelSourceChanged,
        "Editor-state continuation should cancel when the selected RAW source changes.");
    require(
        EditorTypes::RawStartingPointContinuationCancellationReason(
            selectedSourceChangedDecision,
            ui.pendingStartingPoint,
            selectedSourceChangedContext)
                .find("selected source changed") != std::string::npos,
        "Editor-state continuation should distinguish selected-source changes from same-key source identity changes.");

    const std::vector<RawAutoStartPoint::RawAutoStartPointControl> completedControls =
        EditorTypes::MarkRawStartingPointPostFitApplied(
            ui,
            ui.pendingStartingPoint,
            9002,
            "Display Fit refreshed: middle grey 0.240, black -6.00 EV, white +5.00 EV, shoulder 0.50, toe 0.20");
    require(
        !ui.pendingStartingPoint.active,
        "Editor-state completion should clear the post-fit pending action.");
    require(
        ui.hasAppliedViewFit && !ui.startingPointDisplayFitPending,
        "Editor-state completion should mark Display Fit applied and no longer pending.");
    require(
        ui.viewTransformOwner == EditorTypes::RawAutoValueOwner::AutoBase,
        "Editor-state completion should keep the Display Fit value auto-owned.");
    require(
        ui.appliedAnalysisHash == 9002,
        "Editor-state completion should store the post-edit analysis hash.");
    require(
        ui.hasRevertSnapshot,
        "Editor-state completion should preserve undo availability.");
    require(
        ControlListContains(
            completedControls,
            RawAutoStartPoint::RawAutoStartPointControl::RawExposure),
        "Editor-state completion should still report RAW Exposure as applied.");
    require(
        ControlListContains(
            completedControls,
            RawAutoStartPoint::RawAutoStartPointControl::DisplayFit),
        "Editor-state completion should report Display Fit as applied.");
    require(
        ui.startingPointAppliedControlsSummary.find("RAW Exposure") != std::string::npos &&
            ui.startingPointAppliedControlsSummary.find("Display Fit") != std::string::npos,
        "Editor-state completion should show RAW Exposure and Display Fit in changed controls.");
    require(
        ui.startingPointAppliedValuesSummary.find("+0.50 EV") != std::string::npos &&
            ui.startingPointAppliedValuesSummary.find("middle grey 0.240") != std::string::npos &&
            ui.startingPointAppliedValuesSummary.find("black -6.00 EV") != std::string::npos &&
            ui.startingPointAppliedValuesSummary.find("white +5.00 EV") != std::string::npos,
        "Editor-state completion should preserve upstream values and add the final Display Fit value summary.");
    require(
        ui.startingPointWithheldControlsSummary.find("one-click cap") != std::string::npos,
        "Editor-state completion should keep the additional lift visible as withheld.");
    require(
        ui.startingPointEvidenceSummary.find("RAW Exposure from conservative exposure evidence") !=
            std::string::npos,
        "Editor-state completion should preserve the evidence summary.");
    require(
        ui.summary.find("Display Fit refreshed") != std::string::npos,
        "Editor-state completion should explain that post-edit Display Fit finished.");

    RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest renderRequest;
    renderRequest.valid = true;
    renderRequest.id = "validation-base-display";
    renderRequest.stage = RawAutoStartPoint::RawAutoStartPointStage::DisplayCandidate;
    renderRequest.replacesStatus = RawAutoStartPoint::RawAutoStartPointStageStatus::Pending;
    renderRequest.hasRecipe = true;
    renderRequest.recipe = plan.upstreamRecipe;
    renderRequest.expectedControls = {
        RawAutoStartPoint::RawAutoStartPointControl::RawExposure,
        RawAutoStartPoint::RawAutoStartPointControl::DisplayFit
    };
    renderRequest.reason = "Validation request for a queued post-exposure Display Candidate render.";

    const std::uint64_t queueSourceHash = 7001;
    const std::uint64_t changedSourceHash = 7002;
    EditorTypes::RawWorkspaceStartingPointCandidateRenderQueueState queue;
    EditorTypes::StoreRawStartingPointCandidateRenderQueue(
        queue,
        analysis.sourceKey,
        { renderRequest },
        9100,
        queueSourceHash);
    require(
        queue.sourceKey == analysis.sourceKey &&
            queue.sourceHash == queueSourceHash &&
            queue.generation == 9100 &&
            queue.requests.size() == 1 &&
            queue.requests.front().id == renderRequest.id,
        "Candidate render queue should store source-scoped requests, source hash, and generation.");
    require(
        EditorTypes::RawStartingPointCandidateRenderQueueMatchesSource(
            queue,
            analysis.sourceKey,
            queueSourceHash),
        "Candidate render queue should match its active RAW source and hash.");
    RawAutoStartPoint::RawAutoStartPointCandidateRenderResult renderResult;
    renderResult.request = renderRequest;
    renderResult.attempted = true;
    renderResult.success = true;
    const std::vector<RawAutoStartPoint::RawAutoStartPointCandidateRenderResult> renderResults = {
        renderResult
    };
    require(
        EditorTypes::RawStartingPointCandidateRenderResultsMatchSource(
            queue,
            renderResults,
            analysis.sourceKey,
            queueSourceHash),
        "Candidate render results should be mergeable only when their queued source hash matches.");
    require(
        !EditorTypes::RawStartingPointCandidateRenderResultsMatchSource(
            queue,
            renderResults,
            analysis.sourceKey,
            changedSourceHash),
        "Candidate render results should not be mergeable after a same-key source hash change.");
    require(
        !EditorTypes::RawStartingPointCandidateRenderResultsMatchSource(
            queue,
            {},
            analysis.sourceKey,
            queueSourceHash),
        "Empty candidate render results should not be mergeable.");
    require(
        !EditorTypes::RawStartingPointCandidateRenderQueueMatchesSource(
            queue,
            analysis.sourceKey,
            changedSourceHash),
        "Candidate render queue should not match the same RAW source key with a different source hash.");
    require(
        !EditorTypes::RawStartingPointCandidateRenderQueueMatchesSource(
            queue,
            "raw/other-source.dng"),
        "Candidate render queue should not match a different RAW source.");
    require(
        !EditorTypes::ClearRawStartingPointCandidateRenderQueueIfSourceMismatch(
            queue,
            analysis.sourceKey,
            queueSourceHash),
        "Candidate render queue should stay cached for the matching active source hash.");
    require(
        !EditorTypes::ShouldClearRawStartingPointCandidateRenderQueueForRejectedResult(
            queue,
            "raw/rejected-other-source.dng",
            analysis.sourceKey,
            0,
            queueSourceHash),
        "Candidate render queue should survive an unrelated rejected render while its source remains active.");
    require(
        !EditorTypes::ShouldClearRawStartingPointCandidateRenderQueueForRejectedResult(
            queue,
            analysis.sourceKey,
            analysis.sourceKey,
            changedSourceHash,
            queueSourceHash),
        "Candidate render queue should survive a stale same-key rejected result when the active source hash still matches the queue.");
    require(
        !EditorTypes::ShouldClearRawStartingPointCandidateRenderQueueForFailedResult(
            queue,
            "raw/failed-other-source.dng",
            analysis.sourceKey,
            0,
            queueSourceHash),
        "Candidate render queue should survive an unrelated failed render while its source remains active.");
    require(
        EditorTypes::ClearRawStartingPointCandidateRenderQueueIfSourceMismatch(
            queue,
            analysis.sourceKey,
            changedSourceHash),
        "Candidate render queue should clear when the active RAW source hash changes.");
    require(
        queue.sourceKey.empty() && queue.requests.empty() && queue.sourceHash == 0 && queue.generation == 0,
        "Candidate render queue hash clear should reset source, hash, requests, and generation.");
    require(
        !EditorTypes::RawStartingPointCandidateRenderResultsMatchSource(
            queue,
            renderResults,
            analysis.sourceKey,
            changedSourceHash),
        "Candidate render results should not be mergeable after the source queue is cleared.");
    EditorTypes::StoreRawStartingPointCandidateRenderQueue(
        queue,
        analysis.sourceKey,
        { renderRequest },
        9100,
        queueSourceHash);
    require(
        EditorTypes::ClearRawStartingPointCandidateRenderQueueIfSourceMismatch(
            queue,
            "raw/other-active-source.dng"),
        "Candidate render queue should clear when the active RAW source changes.");
    require(
        queue.sourceKey.empty() && queue.requests.empty() && queue.sourceHash == 0 && queue.generation == 0,
        "Candidate render queue clear should reset source, hash, requests, and generation.");
    EditorTypes::StoreRawStartingPointCandidateRenderQueue(
        queue,
        analysis.sourceKey,
        {},
        9100,
        queueSourceHash);
    require(
        queue.sourceKey.empty() && queue.requests.empty() && queue.sourceHash == 0 && queue.generation == 0,
        "Candidate render queue should clear instead of storing empty request sets.");

    RawAutoStartPoint::RawAutoStartPointDiagnostics resetDiagnostics;
    resetDiagnostics.valid = true;
    resetDiagnostics.sourceKey = analysis.sourceKey;
    resetDiagnostics.candidates.push_back(RawAutoStartPoint::RawAutoStartPointCandidate());
    EditorTypes::StoreRawStartingPointCandidateRenderQueue(
        queue,
        analysis.sourceKey,
        { renderRequest },
        9101,
        queueSourceHash);
    std::vector<RawAutoStartPoint::RawAutoStartPointCandidateRenderResult> resetRenderResults =
        renderResults;
    EditorTypes::ClearRawStartingPointCandidateEvidenceCache(
        resetDiagnostics,
        queue,
        resetRenderResults);
    require(
        !resetDiagnostics.valid && resetDiagnostics.sourceKey.empty() &&
            resetDiagnostics.candidates.empty(),
        "Candidate evidence cache clear should reset diagnostics to the default empty state.");
    require(
        queue.sourceKey.empty() && queue.requests.empty() &&
            queue.sourceHash == 0 && queue.generation == 0 &&
            resetRenderResults.empty(),
        "Candidate evidence cache clear should reset queue identity and cached render results.");

    EditorTypes::StoreRawStartingPointCandidateRenderQueue(
        queue,
        analysis.sourceKey,
        { renderRequest },
        9101,
        queueSourceHash);
    require(
        EditorTypes::ShouldClearRawStartingPointCandidateRenderQueueForRejectedResult(
            queue,
            analysis.sourceKey,
            "raw/other-active-source.dng",
            queueSourceHash,
            0),
        "Candidate render queue should clear when a rejected result belongs to the queued source.");
    require(
        EditorTypes::ShouldClearRawStartingPointCandidateRenderQueueForFailedResult(
            queue,
            analysis.sourceKey,
            "raw/other-active-source.dng",
            queueSourceHash,
            0),
        "Candidate render queue should clear when a failed result belongs to the queued source.");
    require(
        EditorTypes::ShouldClearRawStartingPointCandidateRenderQueueForRejectedResult(
            queue,
            "raw/rejected-other-source.dng",
            "raw/other-active-source.dng",
            0,
            0),
        "Candidate render queue should clear when the queued source is no longer active.");
    require(
        EditorTypes::ShouldClearRawStartingPointCandidateRenderQueueForFailedResult(
            queue,
            "raw/failed-other-source.dng",
            "raw/other-active-source.dng",
            0,
            0),
        "Candidate render queue should clear after failed renders when the queued source is no longer active.");
    EditorTypes::StoreRawStartingPointCandidateRenderQueue(
        queue,
        {},
        { renderRequest },
        9102);
    require(
        queue.sourceKey.empty() && queue.requests.empty() && queue.sourceHash == 0 && queue.generation == 0,
        "Candidate render queue should clear instead of storing source-less requests.");

    Stack::RawWorkspace::SourceRecord identitySource;
    identitySource.relativePathKey = "raw/source-identity.dng";
    identitySource.relativePath = "raw/source-identity.dng";
    identitySource.absolutePath = "D:/validation/raw/source-identity.dng";
    identitySource.fileName = "source-identity.dng";
    identitySource.stem = "source-identity";
    identitySource.extension = ".dng";
    identitySource.fileSizeBytes = 1234567;
    identitySource.modifiedTimeTicks = 7654321;
    identitySource.fingerprint = "source-fingerprint-a";
    const std::string expectedIdentityToken =
        SourceIdentityToken(identitySource);
    require(
        SerializeSourceIdentity(identitySource).value("identityToken", std::string()) ==
            expectedIdentityToken,
        "Validation source serialization should include the stable source identity token.");
    const nlohmann::json capturePlan =
        BuildRealRawSourceCapturePlan(identitySource);
    require(
        capturePlan.value("schema", std::string()) ==
            "stack.raw-starting-point.real-raw-source-capture-plan" &&
            capturePlan.value("sidecarRecordKey", std::string()) ==
                identitySource.relativePathKey &&
            capturePlan.value("requiresSameSourceIdentityToken", false) &&
            !capturePlan.value("mutatesSidecarsAutomatically", true) &&
            !capturePlan.value("tunesConstants", true),
        "Real RAW source capture plan should be source-scoped, advisory-only, and non-tuning.");
    require(
        capturePlan
            .value("source", nlohmann::json::object())
            .value("identityToken", std::string()) == expectedIdentityToken &&
            capturePlan.value("requiredStageCount", 0) ==
                static_cast<int>(RequiredStageEvidenceSpecs().size()),
        "Real RAW source capture plan should carry the source identity token and every required stage.");
    const nlohmann::json stageTemplate =
        BuildStageEvidenceTemplateRecord(identitySource);
    require(
        stageTemplate
            .value("realRawCapturePlan", nlohmann::json::object())
            .value("sidecarRecordKey", std::string()) ==
                identitySource.relativePathKey,
        "Stage-evidence templates should include the per-source real RAW capture plan.");

    RawStartingPointAnnotationEntry mismatchedStageEvidence;
    mismatchedStageEvidence.primaryKey = identitySource.relativePathKey;
    mismatchedStageEvidence.keys = { identitySource.relativePathKey };
    mismatchedStageEvidence.value = {
        { "source", {
            { "id", identitySource.relativePathKey },
            { "identityToken", "raw-source-v1|raw/source-identity.dng|999|888|different-fingerprint" }
        } },
        { "startingPointDiagnostics", {
            { "version", 1 },
            { "hasSelectedCandidate", true },
            { "selectedCandidateId", "base" },
            { "candidates", nlohmann::json::array() },
            { "stageDiagnostics", nlohmann::json::array() }
        } }
    };

    RawStartingPointStageEvidenceCheckOptions evidenceOptions;
    evidenceOptions.workspaceRoot = "D:/validation";
    evidenceOptions.stageEvidencePath = "D:/validation/stage-evidence.json";
    evidenceOptions.stageEvidenceEntries = { mismatchedStageEvidence };
    const nlohmann::json identityMismatchReport =
        BuildStageEvidenceCheckReport(evidenceOptions, { identitySource });
    const nlohmann::json identityMismatchRecords =
        identityMismatchReport.value("records", nlohmann::json::array());
    require(
        identityMismatchRecords.is_array() && !identityMismatchRecords.empty(),
        "Stage evidence source-identity check should emit a per-source record.");
    if (identityMismatchRecords.is_array() && !identityMismatchRecords.empty()) {
        require(
            identityMismatchRecords[0].value("status", std::string()) ==
                "source-identity-mismatch",
            "Stage evidence matched by key should be blocked when its source identity token differs.");
        const nlohmann::json sourceIdentity =
            identityMismatchRecords[0].value("sourceIdentity", nlohmann::json::object());
        require(
            sourceIdentity.value("expectedToken", std::string()) ==
                expectedIdentityToken &&
                sourceIdentity.value("sidecarTokenPresent", false) &&
                !sourceIdentity.value("matched", true),
            "Stage evidence source-identity diagnostics should report expected, present, and mismatched tokens.");
    }
    const nlohmann::json identityMismatchCoverage =
        identityMismatchReport.value("sourceCoverage", nlohmann::json::object());
    require(
        identityMismatchCoverage.value("recordsWithSourceIdentityToken", 0) == 1 &&
            identityMismatchCoverage
                .value("sourceIdentityMismatchSourceIds", nlohmann::json::array())
                .size() == 1,
        "Stage evidence coverage should count mismatched source identity tokens.");
    require(
        !identityMismatchReport
            .value("readiness", nlohmann::json::object())
            .value("readyForRecordMerge", true),
        "Stage evidence with mismatched source identity should not be ready for record merge.");

    auto makeStageStatus = [](const char* stage, const char* status) {
        return nlohmann::json {
            { "stage", stage },
            { "status", status }
        };
    };
    auto makeCandidate = [](
        const char* kind,
        nlohmann::json stageDiagnostics) {
        return nlohmann::json {
            { "kind", kind },
            { "id", kind },
            { "visibleEdits", {
                { "rawExposure", {
                    { "valid", true },
                    { "recipeField", "preToneExposureEv" },
                    { "preToneExposureEv", 0.5f }
                } },
                { "displayFit", {
                    { "valid", true },
                    { "recipeField", "viewTransform.layerJson" },
                    { "summary", "Display Fit will be refreshed after upstream Starting Point edits settle." }
                } },
                { "touchedControls", nlohmann::json::array({
                    {
                        { "id", "raw-exposure" },
                        { "label", "RAW Exposure" }
                    },
                    {
                        { "id", "display-fit" },
                        { "label", "Display Fit / View Transform" }
                    }
                }) }
            } },
            { "score", {
                { "valid", true },
                { "totalScore", 1.0 }
            } },
            { "stageDiagnostics", std::move(stageDiagnostics) }
        };
    };
    const nlohmann::json accountingDiagnosticsRecord = {
        { "version", 1 },
        { "hasSelectedCandidate", true },
        { "selectedCandidateKind", "base" },
        { "selectedCandidateIndex", 0 },
        { "candidates", nlohmann::json::array({
            makeCandidate(
                "base",
                nlohmann::json::array({
                    makeStageStatus("neutral-scene", "complete"),
                    makeStageStatus("raw-placement", "projected"),
                    makeStageStatus("local-candidate", "fallback"),
                    makeStageStatus("finish-tone-candidate", "complete"),
                    makeStageStatus("display-candidate", "pending")
                }))
        }) }
    };
    const nlohmann::json visibleRecipeWriteAudit =
        BuildVisibleRecipeWriteAudit(accountingDiagnosticsRecord);
    require(
        visibleRecipeWriteAudit.value("available", false) &&
            visibleRecipeWriteAudit.value("writesVisibleManualControlsOnly", false) &&
            visibleRecipeWriteAudit.value("controlCount", 0) == 2 &&
            visibleRecipeWriteAudit.value("controlsWithProposedValues", 0) == 2 &&
            visibleRecipeWriteAudit
                .value("missingProposedValueControls", nlohmann::json::array())
                .empty() &&
            visibleRecipeWriteAudit
                .value("recipeFields", nlohmann::json::array())
                .size() == 2,
        "Visible recipe write audit should list selected candidate editable recipe fields and proposed values.");
    const nlohmann::json auditedVisibleControls =
        visibleRecipeWriteAudit.value("controls", nlohmann::json::array());
    require(
        auditedVisibleControls.is_array() &&
            auditedVisibleControls.size() == 2 &&
            auditedVisibleControls[0].value("hasProposedValue", false) &&
            auditedVisibleControls[0]
                .value("proposedValue", nlohmann::json::object())
                .value("preToneExposureEv", 0.0f) == 0.5f &&
            auditedVisibleControls[1].value("hasProposedValue", false) &&
            auditedVisibleControls[1]
                .value("proposedValue", nlohmann::json::object())
                .value("summary", std::string())
                .find("Display Fit") != std::string::npos,
        "Visible recipe write audit should include proposed value details for each touched visible control.");
    const nlohmann::json accountingSummary =
        BuildValidationSetSummary({
            { "records", nlohmann::json::array({
                {
                    { "source", {
                        { "id", "raw/accounting.dng" },
                        { "identityToken", "raw-source-v1|raw/accounting.dng|1|2|accounting" }
                    } },
                    { "recordStatus", "raw-safety-loaded" },
                    { "metadata", {
                        { "loaded", true }
                    } },
                    { "rawBufferSafety", {
                        { "requested", true },
                        { "loaded", true }
                    } },
                    { "imageCategoryTags", nlohmann::json::array({ "normal daylight" }) },
                    { "humanReview", {
                        { "tooDark", false },
                        { "tooBright", false },
                        { "tooLocal", false },
                        { "tooFlat", false },
                        { "tooFinished", false },
                        { "nextManualControl", "RAW Exposure" },
                        { "notes", "Synthetic accounting fixture." }
                    } },
                    { "visibleFieldsChanged", nlohmann::json::array() },
                    { "visibleRecipeWriteAudit", visibleRecipeWriteAudit },
                    { "startingPointDiagnostics", accountingDiagnosticsRecord }
                }
            }) }
        });
    const nlohmann::json accountingRows =
        accountingSummary
            .value("startingPointDiagnostics", nlohmann::json::object())
            .value("candidateStageEvidenceAccounting", nlohmann::json::array());
    const nlohmann::json accountingDiagnostics =
        accountingSummary.value("startingPointDiagnostics", nlohmann::json::object());
    require(
        !accountingDiagnostics.value("candidateStageEvidenceAccountingComplete", true) &&
            accountingDiagnostics.value("candidateStageEvidenceAccountingIsGating", false) &&
            accountingDiagnostics.value("candidateStageEvidenceGateBlockedCombinationCount", 0) > 0,
        "Validation summary should gate tuning readiness on complete candidate-stage evidence accounting.");
    auto findAccountingRow = [](
        const nlohmann::json& rows,
        const std::string& key) -> const nlohmann::json* {
        if (!rows.is_array()) {
            return nullptr;
        }
        for (const nlohmann::json& row : rows) {
            if (row.is_object() && row.value("key", std::string()) == key) {
                return &row;
            }
        }
        return nullptr;
    };
    const nlohmann::json* baseRawPlacementAccounting =
        findAccountingRow(accountingRows, "base/raw-placement");
    require(
        baseRawPlacementAccounting != nullptr &&
            baseRawPlacementAccounting->value("projectedRecordCount", 0) == 1 &&
            baseRawPlacementAccounting->value("nonCompleteRecordCount", 0) == 1 &&
            baseRawPlacementAccounting->value("gating", false),
        "Validation summary should account for projected Base Raw Placement evidence.");
    const nlohmann::json* baseDisplayAccounting =
        findAccountingRow(accountingRows, "base/display-candidate");
    require(
        baseDisplayAccounting != nullptr &&
            baseDisplayAccounting->value("pendingRecordCount", 0) == 1 &&
            baseDisplayAccounting->value("complete", true) == false,
        "Validation summary should account for pending Base Display Candidate evidence.");
    const nlohmann::json* currentFitRawPlacementAccounting =
        findAccountingRow(accountingRows, "current-fit/raw-placement");
    require(
        currentFitRawPlacementAccounting != nullptr &&
            currentFitRawPlacementAccounting->value("missingRecordCount", 0) == 1,
        "Validation summary should account for missing expected candidate stage evidence.");
    auto jsonArrayContainsFragment = [](
        const nlohmann::json& values,
        const std::string& fragment) {
        if (!values.is_array()) {
            return false;
        }
        for (const nlohmann::json& value : values) {
            if (value.is_string() &&
                value.get<std::string>().find(fragment) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    const nlohmann::json accountingReadiness =
        accountingSummary.value("tuningReadiness", nlohmann::json::object());
    require(
        !accountingReadiness.value("mechanicalInputsComplete", true) &&
            jsonArrayContainsFragment(
                accountingReadiness.value("blockingReasons", nlohmann::json::array()),
                "candidate-stage evidence"),
        "Validation summary readiness should block when candidate-stage evidence is projected, pending, fallback, or missing.");
    const nlohmann::json visibleAuditSummary =
        accountingSummary.value("visibleRecipeWriteAudit", nlohmann::json::object());
    require(
        visibleAuditSummary.value("recordsWithAudit", 0) == 1 &&
            visibleAuditSummary.value("recordsWithProposedVisibleWrites", 0) == 1 &&
            visibleAuditSummary.value("totalProposedVisibleControlWrites", 0) == 2 &&
            visibleAuditSummary.value("recordsWithProposedValueDetails", 0) == 1 &&
            visibleAuditSummary.value("totalProposedValueDetails", 0) == 2 &&
            visibleAuditSummary
                .value("recipeFieldCounts", nlohmann::json::object())
                .value("preToneExposureEv", 0) == 1 &&
            visibleAuditSummary
                .value("recipeFieldCounts", nlohmann::json::object())
                .value("viewTransform.layerJson", 0) == 1 &&
            visibleAuditSummary
                .value("valueDetailControlCounts", nlohmann::json::object())
                .value("raw-exposure", 0) == 1 &&
            visibleAuditSummary
                .value("valueDetailControlCounts", nlohmann::json::object())
                .value("display-fit", 0) == 1 &&
            visibleAuditSummary
                .value("missingProposedValueDetailRecordIds", nlohmann::json::array())
                .empty() &&
            !visibleAuditSummary.value("coverageIsGating", true),
        "Validation summary should account for selected-candidate visible recipe write audit values without making them a tuning gate.");

    return errors.empty();
}

} // namespace

bool ValidateRawStartingPointEditorStateHandoff() {
    std::vector<std::string> errors;
    if (!RunRawStartingPointEditorStateHandoffChecks(errors)) {
        std::cerr << "RAW Starting Point editor-state handoff validation failed.\n";
        for (const std::string& error : errors) {
            std::cerr << "  - " << error << "\n";
        }
        return false;
    }

    std::cout << "RAW Starting Point editor-state handoff validation passed." << std::endl;
    return true;
}

bool ValidateRawStartingPointValidationWorkflow(int rawArgCount, char** rawArgs) {
    RawStartingPointValidationWorkflowOptions options;
    if (!ParseValidationWorkflowOptions(rawArgCount, rawArgs, options)) {
        return false;
    }

    if (!NormalizeOptionalWorkflowPath(options.workspaceRoot, "workspace root") ||
        !NormalizeOptionalWorkflowPath(options.annotationTemplatePath, "annotation template") ||
        !NormalizeOptionalWorkflowPath(options.annotationPath, "annotations") ||
        !NormalizeOptionalWorkflowPath(options.stageEvidenceTemplatePath, "stage evidence template") ||
        !NormalizeOptionalWorkflowPath(options.stageEvidencePath, "stage evidence") ||
        !NormalizeOptionalWorkflowPath(options.recordsPath, "records") ||
        !NormalizeOptionalWorkflowPath(options.evidenceManifestOutputPath, "evidence manifest output") ||
        !NormalizeOptionalWorkflowPath(options.outputPath, "output")) {
        return false;
    }

    const nlohmann::json report = BuildValidationWorkflowReport(options);
    if (!WriteReport(report, options.outputPath)) {
        return false;
    }
    if (!options.evidenceManifestOutputPath.empty()) {
        const nlohmann::json manifest =
            report.value("evidencePackageManifest", nlohmann::json::object());
        if (!manifest.is_object() ||
            manifest.value("schema", std::string()) !=
                "stack.raw-starting-point.validation-evidence-package-manifest") {
            std::cerr << "RAW Starting Point validation workflow failed: evidence manifest is missing from workflow report.\n";
            return false;
        }
        if (!WriteReport(manifest, options.evidenceManifestOutputPath)) {
            return false;
        }
    }
    if (!options.outputPath.empty()) {
        std::cout << "RAW Starting Point validation workflow written: path=\""
                  << options.outputPath.string()
                  << "\" workflowVersion="
                  << kValidationWorkflowSchemaVersion
                  << " reportVersion="
                  << kValidationWorkflowReportSchemaVersion
                  << " evidenceManifestPath=\""
                  << options.evidenceManifestOutputPath.string()
                  << "\""
                  << " behaviorChanged=false constantsTuned=false\n";
    }
    return true;
}

bool ValidateRawStartingPointValidationGates(int rawArgCount, char** rawArgs) {
    RawStartingPointValidationGateStatusOptions options;
    if (!ParseValidationGateStatusOptions(rawArgCount, rawArgs, options)) {
        return false;
    }

    if (!NormalizeOptionalWorkflowPath(options.annotationCheckPath, "annotation check") ||
        !NormalizeOptionalWorkflowPath(options.stageEvidenceCheckPath, "stage evidence check") ||
        !NormalizeOptionalWorkflowPath(options.recordSidecarPreflightPath, "sidecar preflight") ||
        !NormalizeOptionalWorkflowPath(options.validationSummaryPath, "records summary") ||
        !NormalizeOptionalWorkflowPath(options.constantReviewCheckPath, "constant review check") ||
        !NormalizeOptionalWorkflowPath(options.outputPath, "output")) {
        return false;
    }

    const nlohmann::json report = BuildValidationGateStatusReport(options);
    if (!WriteReport(report, options.outputPath)) {
        return false;
    }

    const nlohmann::json readiness =
        report.value("readiness", nlohmann::json::object());
    const nlohmann::json nonGatingAdvisories =
        report.value("nonGatingAdvisories", nlohmann::json::object());
    const bool allRequiredGatesReady =
        readiness.value("allRequiredGatesReady", false);
    if (!options.outputPath.empty()) {
        std::cout << "RAW Starting Point validation gate check written: path=\""
                  << options.outputPath.string()
                  << "\" readyGateCount="
                  << readiness.value("readyGateCount", 0)
                  << " requiredGateCount="
                  << readiness.value("requiredGateCount", 0)
                  << " allRequiredGatesReady="
                  << (allRequiredGatesReady ? "true" : "false")
                  << " requireReady="
                  << (options.requireReady ? "true" : "false")
                  << " nonGatingAdvisoryChecks="
                  << nonGatingAdvisories.value("checkCount", 0)
                  << " nonGatingAdvisoryIncomplete="
                  << nonGatingAdvisories.value("incompleteCheckCount", 0)
                  << " behaviorChanged=false constantsTuned=false\n";
    }

    if (options.requireReady && !allRequiredGatesReady) {
        std::cerr << "--require-ready blocked RAW Starting Point constant tuning.\n";
        const nlohmann::json blockers =
            readiness.value("blockingReasons", nlohmann::json::array());
        for (const nlohmann::json& blocker : blockers) {
            if (blocker.is_string()) {
                std::cerr << "  - " << blocker.get<std::string>() << "\n";
            }
        }
        return false;
    }
    return true;
}

bool ValidateRawStartingPointAnnotationCheck(int rawArgCount, char** rawArgs) {
    RawStartingPointAnnotationCheckOptions options;
    if (!ParseAnnotationCheckOptions(rawArgCount, rawArgs, options)) {
        return false;
    }

    std::error_code ec;
    options.workspaceRoot =
        std::filesystem::absolute(options.workspaceRoot, ec).lexically_normal();
    if (ec ||
        !std::filesystem::exists(options.workspaceRoot, ec) || ec ||
        !std::filesystem::is_directory(options.workspaceRoot, ec) || ec) {
        std::cerr << "RAW Starting Point annotation check failed: workspace folder does not exist: "
                  << options.workspaceRoot.string() << "\n";
        return false;
    }

    options.annotationPath =
        std::filesystem::absolute(options.annotationPath, ec).lexically_normal();
    if (ec) {
        std::cerr << "RAW Starting Point annotation check failed: invalid annotations path.\n";
        return false;
    }
    if (!LoadAnnotationEntries(options.annotationPath, options.annotationEntries)) {
        return false;
    }

    if (!options.outputPath.empty()) {
        options.outputPath = std::filesystem::absolute(options.outputPath, ec).lexically_normal();
        if (ec) {
            std::cerr << "RAW Starting Point annotation check failed: invalid output path.\n";
            return false;
        }
    }
    if (!options.repairOutputPath.empty()) {
        options.repairOutputPath =
            std::filesystem::absolute(options.repairOutputPath, ec).lexically_normal();
        if (ec) {
            std::cerr << "RAW Starting Point annotation check failed: invalid repair output path.\n";
            return false;
        }
    }

    auto isRawPath = [](const std::filesystem::path& path) {
        return Raw::RawLoader::IsRawPath(path.string()) ||
            Stack::RawWorkspace::DefaultRawPathPredicate(path);
    };

    Stack::RawWorkspace::ScanResult scan =
        Stack::RawWorkspace::ScanWorkspace(options.workspaceRoot, isRawPath);
    if (!scan.success) {
        std::cerr << "RAW Starting Point annotation check failed: scan failed: "
                  << scan.errorMessage << "\n";
        return false;
    }
    if (static_cast<int>(scan.sources.size()) < options.expectMinSources) {
        std::cerr << "RAW Starting Point annotation check failed: expected at least "
                  << options.expectMinSources << " RAW sources, found "
                  << scan.sources.size() << "\n";
        return false;
    }

    std::vector<Stack::RawWorkspace::SourceRecord> sources = scan.sources;
    std::sort(
        sources.begin(),
        sources.end(),
        [](const Stack::RawWorkspace::SourceRecord& a, const Stack::RawWorkspace::SourceRecord& b) {
            return a.relativePathKey < b.relativePathKey;
        });
    if (options.maxSources > 0 &&
        sources.size() > static_cast<std::size_t>(options.maxSources)) {
        sources.resize(static_cast<std::size_t>(options.maxSources));
    }

    const nlohmann::json report = BuildAnnotationCheckReport(options, sources);
    if (!WriteReport(report, options.outputPath)) {
        return false;
    }
    if (!options.repairOutputPath.empty()) {
        const nlohmann::json repairReport =
            BuildAnnotationRepairOutputReport(options, report);
        if (!WriteReport(repairReport, options.repairOutputPath)) {
            return false;
        }
    }

    const nlohmann::json readiness = report.value("readiness", nlohmann::json::object());
    if (!options.outputPath.empty()) {
        std::cout << "RAW Starting Point annotation check written: path=\""
                  << options.outputPath.string()
                  << "\" sources=" << sources.size()
                  << " readyForRecordMerge="
                  << (readiness.value("readyForRecordMerge", false) ? "true" : "false")
                  << " requireReady="
                  << (options.requireReady ? "true" : "false");
        if (!options.repairOutputPath.empty()) {
            std::cout << " repairOut=\""
                      << options.repairOutputPath.string()
                      << "\"";
        }
        std::cout << "\n";
    }
    return !options.requireReady ||
        readiness.value("readyForRecordMerge", false);
}

bool ValidateRawStartingPointStageEvidenceCheck(int rawArgCount, char** rawArgs) {
    RawStartingPointStageEvidenceCheckOptions options;
    if (!ParseStageEvidenceCheckOptions(rawArgCount, rawArgs, options)) {
        return false;
    }

    std::error_code ec;
    options.workspaceRoot =
        std::filesystem::absolute(options.workspaceRoot, ec).lexically_normal();
    if (ec ||
        !std::filesystem::exists(options.workspaceRoot, ec) || ec ||
        !std::filesystem::is_directory(options.workspaceRoot, ec) || ec) {
        std::cerr << "RAW Starting Point stage evidence check failed: workspace folder does not exist: "
                  << options.workspaceRoot.string() << "\n";
        return false;
    }

    options.stageEvidencePath =
        std::filesystem::absolute(options.stageEvidencePath, ec).lexically_normal();
    if (ec) {
        std::cerr << "RAW Starting Point stage evidence check failed: invalid stage evidence path.\n";
        return false;
    }
    if (!LoadStageEvidenceEntries(options.stageEvidencePath, options.stageEvidenceEntries)) {
        return false;
    }

    if (!options.outputPath.empty()) {
        options.outputPath = std::filesystem::absolute(options.outputPath, ec).lexically_normal();
        if (ec) {
            std::cerr << "RAW Starting Point stage evidence check failed: invalid output path.\n";
            return false;
        }
    }
    if (!options.repairOutputPath.empty()) {
        options.repairOutputPath =
            std::filesystem::absolute(options.repairOutputPath, ec).lexically_normal();
        if (ec) {
            std::cerr << "RAW Starting Point stage evidence check failed: invalid repair output path.\n";
            return false;
        }
    }

    auto isRawPath = [](const std::filesystem::path& path) {
        return Raw::RawLoader::IsRawPath(path.string()) ||
            Stack::RawWorkspace::DefaultRawPathPredicate(path);
    };

    Stack::RawWorkspace::ScanResult scan =
        Stack::RawWorkspace::ScanWorkspace(options.workspaceRoot, isRawPath);
    if (!scan.success) {
        std::cerr << "RAW Starting Point stage evidence check failed: scan failed: "
                  << scan.errorMessage << "\n";
        return false;
    }
    if (static_cast<int>(scan.sources.size()) < options.expectMinSources) {
        std::cerr << "RAW Starting Point stage evidence check failed: expected at least "
                  << options.expectMinSources << " RAW sources, found "
                  << scan.sources.size() << "\n";
        return false;
    }

    std::vector<Stack::RawWorkspace::SourceRecord> sources = scan.sources;
    std::sort(
        sources.begin(),
        sources.end(),
        [](const Stack::RawWorkspace::SourceRecord& a, const Stack::RawWorkspace::SourceRecord& b) {
            return a.relativePathKey < b.relativePathKey;
        });
    if (options.maxSources > 0 &&
        sources.size() > static_cast<std::size_t>(options.maxSources)) {
        sources.resize(static_cast<std::size_t>(options.maxSources));
    }

    const nlohmann::json report =
        BuildStageEvidenceCheckReport(options, sources);
    if (!WriteReport(report, options.outputPath)) {
        return false;
    }
    if (!options.repairOutputPath.empty()) {
        const nlohmann::json repairReport =
            BuildStageEvidenceRepairOutputReport(options, report);
        if (!WriteReport(repairReport, options.repairOutputPath)) {
            return false;
        }
    }

    const nlohmann::json readiness =
        report.value("readiness", nlohmann::json::object());
    if (!options.outputPath.empty()) {
        std::cout << "RAW Starting Point stage evidence check written: path=\""
                  << options.outputPath.string()
                  << "\" sources=" << sources.size()
                  << " readyForRecordMerge="
                  << (readiness.value("readyForRecordMerge", false) ? "true" : "false");
        if (!options.repairOutputPath.empty()) {
            std::cout << " repairOut=\""
                      << options.repairOutputPath.string()
                      << "\"";
        }
        std::cout << "\n";
    }
    return readiness.value("readyForRecordMerge", false);
}

bool ValidateRawStartingPointRecords(int rawArgCount, char** rawArgs) {
    RawStartingPointRecordOptions options;
    if (!ParseOptions(rawArgCount, rawArgs, options)) {
        return false;
    }

    std::error_code ec;
    options.workspaceRoot =
        std::filesystem::absolute(options.workspaceRoot, ec).lexically_normal();
    if (ec ||
        !std::filesystem::exists(options.workspaceRoot, ec) || ec ||
        !std::filesystem::is_directory(options.workspaceRoot, ec) || ec) {
        std::cerr << "RAW Starting Point record validation failed: workspace folder does not exist: "
                  << options.workspaceRoot.string() << "\n";
        return false;
    }
    if (!options.outputPath.empty()) {
        options.outputPath = std::filesystem::absolute(options.outputPath, ec).lexically_normal();
        if (ec) {
            std::cerr << "RAW Starting Point record validation failed: invalid output path.\n";
            return false;
        }
    }
    if (!options.annotationTemplateOutputPath.empty()) {
        options.annotationTemplateOutputPath =
            std::filesystem::absolute(options.annotationTemplateOutputPath, ec).lexically_normal();
        if (ec) {
            std::cerr << "RAW Starting Point record validation failed: invalid annotation template output path.\n";
            return false;
        }
    }
    if (!options.stageEvidenceTemplateOutputPath.empty()) {
        options.stageEvidenceTemplateOutputPath =
            std::filesystem::absolute(options.stageEvidenceTemplateOutputPath, ec).lexically_normal();
        if (ec) {
            std::cerr << "RAW Starting Point record validation failed: invalid stage evidence template output path.\n";
            return false;
        }
    }
    if (!options.sidecarPreflightOutputPath.empty()) {
        options.sidecarPreflightOutputPath =
            std::filesystem::absolute(options.sidecarPreflightOutputPath, ec).lexically_normal();
        if (ec) {
            std::cerr << "RAW Starting Point record validation failed: invalid sidecar preflight output path.\n";
            return false;
        }
    }
    if (options.templatesOnly) {
        if (options.annotationTemplateOutputPath.empty() &&
            options.stageEvidenceTemplateOutputPath.empty()) {
            std::cerr << "RAW Starting Point record validation failed: "
                      << "--templates-only requires --annotation-template-out or "
                      << "--stage-evidence-template-out.\n";
            return false;
        }
        if (!options.annotationPath.empty() || !options.stageEvidencePath.empty()) {
            std::cerr << "RAW Starting Point record validation failed: "
                      << "--templates-only does not consume --annotations or --stage-evidence.\n";
            return false;
        }
        if (options.loadRawSafety) {
            std::cerr << "RAW Starting Point record validation failed: "
                      << "--templates-only does not load RAW safety buffers.\n";
            return false;
        }
        if (options.requireReadySidecars) {
            std::cerr << "RAW Starting Point record validation failed: "
                      << "--templates-only does not run sidecar readiness gates.\n";
            return false;
        }
        if (!options.sidecarPreflightOutputPath.empty()) {
            std::cerr << "RAW Starting Point record validation failed: "
                      << "--templates-only does not write sidecar preflight reports.\n";
            return false;
        }
    }
    if (!options.sidecarPreflightOutputPath.empty() &&
        !options.requireReadySidecars) {
        std::cerr << "RAW Starting Point record validation failed: "
                  << "--sidecar-preflight-out requires --require-ready-sidecars.\n";
        return false;
    }
    if (!options.annotationPath.empty()) {
        options.annotationPath = std::filesystem::absolute(options.annotationPath, ec).lexically_normal();
        if (ec) {
            std::cerr << "RAW Starting Point record validation failed: invalid annotations path.\n";
            return false;
        }
        if (!LoadAnnotationEntries(options.annotationPath, options.annotationEntries)) {
            return false;
        }
    }
    if (!options.stageEvidencePath.empty()) {
        options.stageEvidencePath =
            std::filesystem::absolute(options.stageEvidencePath, ec).lexically_normal();
        if (ec) {
            std::cerr << "RAW Starting Point record validation failed: invalid stage evidence path.\n";
            return false;
        }
        if (!LoadStageEvidenceEntries(options.stageEvidencePath, options.stageEvidenceEntries)) {
            return false;
        }
    }

    auto isRawPath = [](const std::filesystem::path& path) {
        return Raw::RawLoader::IsRawPath(path.string()) ||
            Stack::RawWorkspace::DefaultRawPathPredicate(path);
    };

    Stack::RawWorkspace::ScanResult scan =
        Stack::RawWorkspace::ScanWorkspace(options.workspaceRoot, isRawPath);
    if (!scan.success) {
        std::cerr << "RAW Starting Point record validation failed: scan failed: "
                  << scan.errorMessage << "\n";
        return false;
    }
    if (static_cast<int>(scan.sources.size()) < options.expectMinSources) {
        std::cerr << "RAW Starting Point record validation failed: expected at least "
                  << options.expectMinSources << " RAW sources, found "
                  << scan.sources.size() << "\n";
        return false;
    }

    std::vector<Stack::RawWorkspace::SourceRecord> sources = scan.sources;
    std::sort(
        sources.begin(),
        sources.end(),
        [](const Stack::RawWorkspace::SourceRecord& a, const Stack::RawWorkspace::SourceRecord& b) {
            return a.relativePathKey < b.relativePathKey;
        });
    if (options.maxSources > 0 &&
        sources.size() > static_cast<std::size_t>(options.maxSources)) {
        sources.resize(static_cast<std::size_t>(options.maxSources));
    }

    nlohmann::json sidecarPreflight;
    if (options.requireReadySidecars) {
        sidecarPreflight =
            BuildRecordSidecarPreflightReport(options, sources);
        if (!options.sidecarPreflightOutputPath.empty()) {
            if (!WriteReport(sidecarPreflight, options.sidecarPreflightOutputPath)) {
                return false;
            }
        }
        if (!sidecarPreflight.value("readyForRecordGeneration", false)) {
            PrintRecordSidecarPreflightFailure(sidecarPreflight);
            return false;
        }
    }

    if (!options.annotationTemplateOutputPath.empty()) {
        const nlohmann::json annotationTemplate =
            BuildAnnotationTemplateReport(options, sources);
        if (!WriteReport(annotationTemplate, options.annotationTemplateOutputPath)) {
            return false;
        }
    }
    if (!options.stageEvidenceTemplateOutputPath.empty()) {
        const nlohmann::json stageEvidenceTemplate =
            BuildStageEvidenceTemplateReport(options, sources);
        if (!WriteReport(stageEvidenceTemplate, options.stageEvidenceTemplateOutputPath)) {
            return false;
        }
    }

    if (options.templatesOnly) {
        const nlohmann::json report =
            BuildTemplateGenerationReport(options, sources);
        if (!WriteReport(report, options.outputPath)) {
            return false;
        }

        if (!options.outputPath.empty()) {
            std::cout << "RAW Starting Point validation templates written: path=\""
                      << options.outputPath.string()
                      << "\" sources=" << sources.size();
            if (!options.annotationTemplateOutputPath.empty()) {
                std::cout << " annotationTemplate=\""
                          << options.annotationTemplateOutputPath.string()
                          << "\"";
            }
            if (!options.stageEvidenceTemplateOutputPath.empty()) {
                std::cout << " stageEvidenceTemplate=\""
                          << options.stageEvidenceTemplateOutputPath.string()
                          << "\"";
            }
            std::cout << "\n";
        }
        return true;
    }

    nlohmann::json records = nlohmann::json::array();
    int metadataLoadedCount = 0;
    int rawSafetyLoadedCount = 0;
    int annotationAppliedCount = 0;
    int stageEvidenceAppliedCount = 0;
    std::set<int> matchedAnnotationIndices;
    std::set<int> matchedStageEvidenceIndices;
    for (const Stack::RawWorkspace::SourceRecord& source : sources) {
        bool metadataLoaded = false;
        bool rawSafetyLoaded = false;
        bool stageEvidenceApplied = false;
        int annotationIndex = -1;
        std::string annotationMatchedKey;
        const RawStartingPointAnnotationEntry* annotation =
            FindAnnotationForSource(
                options.annotationEntries,
                source,
                annotationIndex,
                annotationMatchedKey);
        if (annotation != nullptr) {
            ++annotationAppliedCount;
            matchedAnnotationIndices.insert(annotationIndex);
        }
        int stageEvidenceIndex = -1;
        std::string stageEvidenceMatchedKey;
        const RawStartingPointAnnotationEntry* stageEvidence =
            FindAnnotationForSource(
                options.stageEvidenceEntries,
                source,
                stageEvidenceIndex,
                stageEvidenceMatchedKey);
        if (stageEvidence != nullptr) {
            matchedStageEvidenceIndices.insert(stageEvidenceIndex);
        }
        records.push_back(BuildRecordForSource(
            source,
            options.tags,
            options,
            annotation,
            annotationMatchedKey,
            stageEvidence,
            stageEvidenceMatchedKey,
            metadataLoaded,
            rawSafetyLoaded,
            stageEvidenceApplied));
        if (metadataLoaded) {
            ++metadataLoadedCount;
        }
        if (rawSafetyLoaded) {
            ++rawSafetyLoadedCount;
        }
        if (stageEvidenceApplied) {
            ++stageEvidenceAppliedCount;
        }
    }

    nlohmann::json report = {
        { "schema", "stack.raw-starting-point.validation-records" },
        { "version", kValidationRecordsSchemaVersion },
        { "workspaceRoot", options.workspaceRoot.string() },
        { "recordsFile", options.outputPath.string() },
        { "sourceCount", sources.size() },
        { "metadataLoadedCount", metadataLoadedCount },
        { "rawSafetyRequested", options.loadRawSafety },
        { "rawSafetyLoadedCount", rawSafetyLoadedCount },
        { "rawSafetyMaxSamples", options.maxRawSafetySamples },
        { "tagsAppliedToAllRecords", JsonStringVector(options.tags) },
        { "annotationFile", options.annotationPath.string() },
        { "annotationEntryCount", options.annotationEntries.size() },
        { "annotationAppliedCount", annotationAppliedCount },
        { "annotationMatchedEntryCount", matchedAnnotationIndices.size() },
        { "annotationUnmatchedEntryCount",
          options.annotationEntries.size() - matchedAnnotationIndices.size() },
        { "stageEvidenceFile", options.stageEvidencePath.string() },
        { "stageEvidenceEntryCount", options.stageEvidenceEntries.size() },
        { "stageEvidenceAppliedCount", stageEvidenceAppliedCount },
        { "stageEvidenceMatchedEntryCount", matchedStageEvidenceIndices.size() },
        { "stageEvidenceUnmatchedEntryCount",
          options.stageEvidenceEntries.size() - matchedStageEvidenceIndices.size() },
        { "requireReadySidecars", options.requireReadySidecars },
        { "sidecarPreflightOutputFile", options.sidecarPreflightOutputPath.string() },
        { "sidecarPreflight", sidecarPreflight.is_null()
            ? nlohmann::json::object()
            : sidecarPreflight },
        { "annotationTemplateFile", options.annotationTemplateOutputPath.string() },
        { "annotationTemplateRecordCount",
          options.annotationTemplateOutputPath.empty() ? 0 : sources.size() },
        { "stageEvidenceTemplateFile", options.stageEvidenceTemplateOutputPath.string() },
        { "stageEvidenceTemplateRecordCount",
          options.stageEvidenceTemplateOutputPath.empty() ? 0 : sources.size() },
        { "currentEngineeringDefaults", BuildCurrentEngineeringDefaults() },
        { "tuningStatus", {
            { "constantsTuned", false },
            { "reason", options.loadRawSafety
                ? "This command records sampled real RAW buffer evidence. Constants must be tuned only after representative records and human review are available."
                : "This command records metadata and diagnostics scaffolding. Re-run with --load-raw-safety and representative real RAW files before tuning constants." }
        } },
        { "requiredHumanReviewFields", JsonStringVector(RequiredHumanReviewFields()) },
        { "records", std::move(records) }
    };
    report["validationSetSummary"] = BuildValidationSetSummary(report, options.outputPath);

    if (!WriteReport(report, options.outputPath)) {
        return false;
    }

    if (!options.outputPath.empty()) {
        std::cout << "RAW Starting Point validation records written: path=\""
                  << options.outputPath.string()
                  << "\" sources=" << sources.size()
                  << " metadataLoaded=" << metadataLoadedCount;
        if (options.loadRawSafety) {
            std::cout << " rawSafetyLoaded=" << rawSafetyLoadedCount;
        }
        if (!options.annotationPath.empty()) {
            std::cout << " annotationsApplied=" << annotationAppliedCount
                      << " annotationUnmatched="
                      << (options.annotationEntries.size() - matchedAnnotationIndices.size());
        }
        if (!options.stageEvidencePath.empty()) {
            std::cout << " stageEvidenceApplied=" << stageEvidenceAppliedCount
                      << " stageEvidenceUnmatched="
                      << (options.stageEvidenceEntries.size() - matchedStageEvidenceIndices.size());
        }
        if (options.requireReadySidecars) {
            std::cout << " readySidecars=true";
            if (!options.sidecarPreflightOutputPath.empty()) {
                std::cout << " sidecarPreflight=\""
                          << options.sidecarPreflightOutputPath.string()
                          << "\"";
            }
        }
        if (!options.annotationTemplateOutputPath.empty()) {
            std::cout << " annotationTemplate=\""
                      << options.annotationTemplateOutputPath.string()
                      << "\"";
        }
        if (!options.stageEvidenceTemplateOutputPath.empty()) {
            std::cout << " stageEvidenceTemplate=\""
                      << options.stageEvidenceTemplateOutputPath.string()
                      << "\"";
        }
        std::cout << "\n";
    }
    return metadataLoadedCount == static_cast<int>(sources.size()) &&
        (!options.loadRawSafety || rawSafetyLoadedCount == static_cast<int>(sources.size()));
}

bool ValidateRawStartingPointConstantReviewCheck(int rawArgCount, char** rawArgs) {
    RawStartingPointConstantReviewCheckOptions options;
    if (!ParseConstantReviewCheckOptions(rawArgCount, rawArgs, options)) {
        return false;
    }

    std::error_code ec;
    options.summaryPath =
        std::filesystem::absolute(options.summaryPath, ec).lexically_normal();
    if (ec ||
        !std::filesystem::exists(options.summaryPath, ec) || ec ||
        !std::filesystem::is_regular_file(options.summaryPath, ec) || ec) {
        std::cerr << "RAW Starting Point constant review check failed: summary file does not exist: "
                  << options.summaryPath.string() << "\n";
        return false;
    }
    options.reviewTemplatePath =
        std::filesystem::absolute(options.reviewTemplatePath, ec).lexically_normal();
    if (ec ||
        !std::filesystem::exists(options.reviewTemplatePath, ec) || ec ||
        !std::filesystem::is_regular_file(options.reviewTemplatePath, ec) || ec) {
        std::cerr << "RAW Starting Point constant review check failed: review template file does not exist: "
                  << options.reviewTemplatePath.string() << "\n";
        return false;
    }
    if (!options.outputPath.empty()) {
        options.outputPath =
            std::filesystem::absolute(options.outputPath, ec).lexically_normal();
        if (ec) {
            std::cerr << "RAW Starting Point constant review check failed: invalid output path.\n";
            return false;
        }
    }
    if (!options.repairOutputPath.empty()) {
        options.repairOutputPath =
            std::filesystem::absolute(options.repairOutputPath, ec).lexically_normal();
        if (ec) {
            std::cerr << "RAW Starting Point constant review check failed: invalid repair output path.\n";
            return false;
        }
    }
    if (!options.suggestedReviewPatchBundleOutputPath.empty()) {
        options.suggestedReviewPatchBundleOutputPath =
            std::filesystem::absolute(
                options.suggestedReviewPatchBundleOutputPath,
                ec).lexically_normal();
        if (ec) {
            std::cerr << "RAW Starting Point constant review check failed: invalid patch bundle output path.\n";
            return false;
        }
    }

    auto readJsonFile = [](
        const std::filesystem::path& path,
        const char* label,
        nlohmann::json& outJson) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            std::cerr << "RAW Starting Point constant review check failed: could not open "
                      << label << " file " << path.string() << "\n";
            return false;
        }
        try {
            in >> outJson;
        } catch (const std::exception& ex) {
            std::cerr << "RAW Starting Point constant review check failed: invalid JSON in "
                      << label << " file " << path.string() << ": "
                      << ex.what() << "\n";
            return false;
        }
        return true;
    };

    nlohmann::json summaryReport;
    if (!readJsonFile(options.summaryPath, "summary", summaryReport)) {
        return false;
    }
    nlohmann::json reviewTemplate;
    if (!readJsonFile(options.reviewTemplatePath, "constant review template", reviewTemplate)) {
        return false;
    }

    ConstantReviewEvidenceIndex evidenceIndex;
    TryLoadRecordsForConstantReview(summaryReport, evidenceIndex);

    const nlohmann::json report =
        BuildConstantReviewCheckReport(
            summaryReport,
            reviewTemplate,
            options,
            evidenceIndex);
    if (!WriteReport(report, options.outputPath)) {
        return false;
    }
    if (!options.repairOutputPath.empty()) {
        const nlohmann::json repairReport =
            BuildConstantReviewRepairOutputReport(
                options,
                summaryReport,
                reviewTemplate,
                report);
        if (!WriteReport(repairReport, options.repairOutputPath)) {
            return false;
        }
    }
    if (!options.suggestedReviewPatchBundleOutputPath.empty()) {
        const nlohmann::json patchBundleReport =
            BuildConstantReviewPatchBundleOutputReport(options, report);
        if (!WriteReport(
                patchBundleReport,
                options.suggestedReviewPatchBundleOutputPath)) {
            return false;
        }
    }

    const nlohmann::json readiness =
        report.value("readiness", nlohmann::json::object());
    const bool readyForTuningPass =
        readiness.value("readyForTuningPass", false);
    if (!options.outputPath.empty()) {
        const nlohmann::json reviewSummary =
            report.value("reviewSummary", nlohmann::json::object());
        std::cout << "RAW Starting Point constant review check written: path=\""
                  << options.outputPath.string()
                  << "\" changedConstants="
                  << reviewSummary.value("changedConstantCount", 0)
                  << " readyForTuningPass="
                  << (readyForTuningPass ? "true" : "false")
                  << " requireReady="
                  << (options.requireReady ? "true" : "false");
        if (!options.repairOutputPath.empty()) {
            std::cout << " repairOut=\""
                      << options.repairOutputPath.string()
                      << "\"";
        }
        if (!options.suggestedReviewPatchBundleOutputPath.empty()) {
            std::cout << " patchBundleOut=\""
                      << options.suggestedReviewPatchBundleOutputPath.string()
                      << "\"";
        }
        std::cout << "\n";
    }
    if (options.requireReady && !readyForTuningPass) {
        std::cerr << "--require-ready blocked RAW Starting Point constant tuning.\n";
        const nlohmann::json blockers =
            readiness.value("blockingReasons", nlohmann::json::array());
        for (const nlohmann::json& blocker : blockers) {
            if (blocker.is_string()) {
                std::cerr << "  - " << blocker.get<std::string>() << "\n";
            }
        }
        return false;
    }
    return true;
}

bool ValidateRawStartingPointRecordSummary(int rawArgCount, char** rawArgs) {
    RawStartingPointSummaryOptions options;
    if (!ParseSummaryOptions(rawArgCount, rawArgs, options)) {
        return false;
    }

    std::error_code ec;
    options.inputPath = std::filesystem::absolute(options.inputPath, ec).lexically_normal();
    if (ec ||
        !std::filesystem::exists(options.inputPath, ec) || ec ||
        !std::filesystem::is_regular_file(options.inputPath, ec) || ec) {
        std::cerr << "RAW Starting Point summary failed: records file does not exist: "
                  << options.inputPath.string() << "\n";
        return false;
    }
    if (!options.outputPath.empty()) {
        options.outputPath = std::filesystem::absolute(options.outputPath, ec).lexically_normal();
        if (ec) {
            std::cerr << "RAW Starting Point summary failed: invalid output path.\n";
            return false;
        }
    }
    if (!options.constantReviewTemplateOutputPath.empty()) {
        options.constantReviewTemplateOutputPath =
            std::filesystem::absolute(options.constantReviewTemplateOutputPath, ec).lexically_normal();
        if (ec) {
            std::cerr << "RAW Starting Point summary failed: invalid constant review template output path.\n";
            return false;
        }
    }

    std::ifstream in(options.inputPath, std::ios::binary);
    if (!in) {
        std::cerr << "RAW Starting Point summary failed: could not open records file "
                  << options.inputPath.string() << "\n";
        return false;
    }

    nlohmann::json report;
    try {
        in >> report;
    } catch (const std::exception& ex) {
        std::cerr << "RAW Starting Point summary failed: invalid JSON in "
                  << options.inputPath.string() << ": " << ex.what() << "\n";
        return false;
    }

    nlohmann::json summaryReport = {
        { "schema", "stack.raw-starting-point.validation-summary-report" },
        { "version", kValidationSummaryReportSchemaVersion },
        { "sourceReportPath", options.inputPath.string() },
        { "constantReviewTemplateFile", options.constantReviewTemplateOutputPath.string() },
        { "constantReviewTemplateSchemaVersion",
          options.constantReviewTemplateOutputPath.empty()
              ? 0
              : kConstantReviewTemplateSchemaVersion },
        { "requireReady", options.requireReady },
        { "validationSetSummary", BuildValidationSetSummary(report, options.inputPath) }
    };

    if (!WriteReport(summaryReport, options.outputPath)) {
        return false;
    }
    if (!options.constantReviewTemplateOutputPath.empty()) {
        const nlohmann::json reviewTemplate =
            BuildConstantReviewTemplateReport(
                summaryReport,
                options.constantReviewTemplateOutputPath);
        if (!WriteReport(reviewTemplate, options.constantReviewTemplateOutputPath)) {
            return false;
        }
    }

    const nlohmann::json& summary = summaryReport["validationSetSummary"];
    const nlohmann::json& readiness =
        summary.value("tuningReadiness", nlohmann::json::object());
    const bool mechanicalInputsComplete =
        readiness.value("mechanicalInputsComplete", false);
    if (!options.outputPath.empty()) {
        std::cout << "RAW Starting Point validation summary written: path=\""
                  << options.outputPath.string()
                  << "\" records=" << summary.value("recordCount", 0)
                  << " mechanicalInputsComplete="
                  << (mechanicalInputsComplete ? "true" : "false")
                  << " requireReady="
                  << (options.requireReady ? "true" : "false");
        if (!options.constantReviewTemplateOutputPath.empty()) {
            std::cout << " constantReviewTemplate=\""
                      << options.constantReviewTemplateOutputPath.string()
                      << "\"";
        }
        std::cout << "\n";
    }
    if (options.requireReady && !mechanicalInputsComplete) {
        std::cerr << "--require-ready blocked RAW Starting Point constant tuning.\n";
        const nlohmann::json blockers =
            readiness.value("blockingReasons", nlohmann::json::array());
        for (const nlohmann::json& blocker : blockers) {
            if (blocker.is_string()) {
                std::cerr << "  - " << blocker.get<std::string>() << "\n";
            }
        }
        return false;
    }
    return true;
}

} // namespace Stack::Validation
