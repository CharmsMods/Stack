#pragma once

#include <string>

namespace Stack::Validation {

bool ValidateToneCurveAutoIntegration();
bool ValidateNodeMathPhase4Integration();
bool ValidateNodeMathPhase5Integration();
bool ValidateNodeMathPhase6Integration();
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
