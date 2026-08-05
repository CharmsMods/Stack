#pragma once

#include <cstdint>
#include <string>

namespace Stack::Validation {

bool ValidateToneCurveAutoIntegration();
bool ValidateNodeMathPhase4Integration();
bool ValidateNodeMathPhase5Integration();
bool ValidateNodeMathPhase6Integration();
bool ValidateEditorGraphTransactions();
bool ValidateMultiSourceProjectFoundation();
bool ValidateMfdProjectFoundation();
bool ValidateMfdPhase0Contracts();
bool ValidateMfdPhase1Preparation();
bool ValidateMfdPhase2NoiseModel();
bool ValidateMfdPhase3SameCfaSampler();
bool ValidateMfdPhase4GlobalRegistration();
bool ValidateMfdPhase5LocalMotion();
bool ValidateMfdPhase6Reliability();
bool ValidateMfdPhase7Fusion();
bool ValidateMfdPhase8Streaming();
bool ValidateMfdPhase9Evaluation(int argc, char** argv);
bool ValidateMfdCorpusIntake();
bool WriteMfdCorpusIntakeTemplate(
    const std::string& outputPath,
    std::string* errorMessage = nullptr);
bool LockMfdCorpusDefinition(
    const std::string& definitionPath,
    const std::string& outputPath,
    std::string* errorMessage = nullptr);
bool RunMfdCorpusEntry(
    const std::string& definitionPath,
    const std::string& sampleId,
    const std::string& outputDirectory,
    std::uint64_t memoryBudgetBytes,
    std::uint32_t workerCount,
    std::string* errorMessage = nullptr);
bool ValidateMfdEndToEndProcessor();
bool WriteNodeSocketCatalog(const std::string& outputPath, std::string* errorMessage = nullptr);
bool ValidateDevelopAutoSolveBehavior();
bool ValidateDevelopNodeSmoke();
bool ValidateDevelopRealRawSmoke(int rawArgCount, char** rawArgs);
bool ValidateFFmpegProvider();
bool ValidateRawWorkspaceLoadingSmoke(int rawArgCount, char** rawArgs);
bool ValidateRawStartingPointAnnotationCheck(int rawArgCount, char** rawArgs);
bool ValidateRawStartingPointConstantReviewCheck(int rawArgCount, char** rawArgs);
bool ValidateRawStartingPointEditorStateHandoff();
bool ValidateRawStartingPointRecords(int rawArgCount, char** rawArgs);
bool ValidateRawStartingPointRecordSummary(int rawArgCount, char** rawArgs);
bool ValidateRawStartingPointStageEvidenceCheck(int rawArgCount, char** rawArgs);
bool ValidateRawStartingPointValidationGates(int rawArgCount, char** rawArgs);
bool ValidateRawStartingPointValidationWorkflow(int rawArgCount, char** rawArgs);
bool ValidateRawEvidenceFoundation(int rawArgCount, char** rawArgs);
bool ValidateRenderedFeatureFoundation(int rawArgCount, char** rawArgs);
bool ValidatePreciseCandidateSurfaces(int rawArgCount, char** rawArgs);
bool ValidateRawOptimizerBenchmarks(int rawArgCount, char** rawArgs);
bool ValidatePreciseDryRun(int rawArgCount, char** rawArgs);
bool ValidatePreciseIntegration(int rawArgCount, char** rawArgs);

} // namespace Stack::Validation
