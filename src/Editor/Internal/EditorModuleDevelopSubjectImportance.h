#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "ThirdParty/json.hpp"

#include <array>
#include <string>

namespace Stack::Editor::DevelopDynamicRange {
struct DevelopDynamicRangeRegionEvidence;
struct DevelopToneAutoStats;
} // namespace Stack::Editor::DevelopDynamicRange

namespace Stack::Editor::DevelopSubjectImportance {

inline constexpr const char* kDevelopSubjectSceneIntentVersion = "SubjectSceneIntentV1";
inline constexpr const char* kDevelopSubjectImportanceMapVersion = "SubjectImportanceMapV1";
inline constexpr const char* kDevelopSubjectRefinedMapVersion = "SubjectRefinedMapV1";
inline constexpr const char* kDevelopSubjectImportanceSolveNotesVersion = "SubjectImportanceSolveNotesV1";
inline constexpr int kDevelopSubjectImportanceMapGridSize = 5;
inline constexpr int kDevelopSubjectImportanceMapCellCount =
    kDevelopSubjectImportanceMapGridSize * kDevelopSubjectImportanceMapGridSize;

struct DevelopSubjectSceneIntent {
    std::string id = "automaticSceneBalance";
    std::string label = "Automatic Scene Balance";
    std::string reason = "No user importance brush is active; Auto is using weak composition and rendered evidence only.";
    std::string userGuidanceStatus = "notAvailable";
    bool userGuidanceActive = false;
    bool automaticOnly = true;
    float userSubjectSceneBias = 0.0f;
    float userMoodReadabilityBias = 0.0f;
    float userGuidanceStrength = 0.0f;
    float automaticConfidence = 0.0f;
    float centerPrior = 0.0f;
    float readabilityPressure = 0.0f;
    float protectionPressure = 0.0f;
    float moodPreservationPressure = 0.0f;
    float subjectPriority = 0.5f;
    float sceneIntegrity = 0.5f;
    float improveReadability = 0.5f;
    float preserveMood = 0.5f;
    float subjectSceneAxis = 0.0f;
    float moodReadabilityAxis = 0.0f;
    int importanceRegionCount = 0;
    int importanceStrokeCount = 0;
    float importanceStrength = 0.0f;
    float importanceImportant = 0.0f;
    float importanceReveal = 0.0f;
    float importanceProtect = 0.0f;
    float importancePreserveMood = 0.0f;
    float importanceIgnore = 0.0f;
    float importanceSubjectPriority = 0.0f;
    float importanceReadability = 0.0f;
    float importanceProtection = 0.0f;
    float importanceMood = 0.0f;
    float importanceLowPriority = 0.0f;
    nlohmann::json importanceMap = nlohmann::json::object();
    float importanceMapCoverage = 0.0f;
    float importanceMapPositiveCoverage = 0.0f;
    float importanceMapLowPriorityCoverage = 0.0f;
    float importanceMapRevealCoverage = 0.0f;
    float importanceMapProtectCoverage = 0.0f;
    float importanceMapMoodCoverage = 0.0f;
    float importanceMapPeak = 0.0f;
    float importanceMapConfidence = 0.0f;
    float importanceMapCenterBias = 0.0f;
    float importanceMapEdgeBias = 0.0f;
    nlohmann::json refinedImportanceMap = nlohmann::json::object();
    float refinedMapCoverage = 0.0f;
    float refinedMapLowPriorityCoverage = 0.0f;
    float refinedMapReadabilityCoverage = 0.0f;
    float refinedMapProtectionCoverage = 0.0f;
    float refinedMapMoodCoverage = 0.0f;
    float refinedMapPeak = 0.0f;
    float refinedMapConfidence = 0.0f;
    float refinedMapBoundaryHint = 0.0f;
    nlohmann::json solveNotes = nlohmann::json::array();
    nlohmann::json evidence = nlohmann::json::object();
};

struct DevelopSubjectImportanceSummary {
    bool enabled = false;
    int activeRegionCount = 0;
    int activeStrokeCount = 0;
    float strength = 0.0f;
    float important = 0.0f;
    float reveal = 0.0f;
    float protect = 0.0f;
    float preserveMood = 0.0f;
    float ignore = 0.0f;
    float subjectPriority = 0.0f;
    float readability = 0.0f;
    float protection = 0.0f;
    float mood = 0.0f;
    float lowPriority = 0.0f;
};

struct DevelopSubjectImportanceMapCell {
    float importance = 0.0f;
    float reveal = 0.0f;
    float protect = 0.0f;
    float preserveMood = 0.0f;
    float lowPriority = 0.0f;
};

struct DevelopSubjectRefinedMapCell {
    float importance = 0.0f;
    float confidence = 0.0f;
    float readability = 0.0f;
    float protection = 0.0f;
    float preserveMood = 0.0f;
    float lowPriority = 0.0f;
    float boundaryHint = 0.0f;
};

struct DevelopSubjectImportanceInterpretation {
    bool enabled = false;
    bool active = false;
    std::string status = "disabled";
    std::string reason = "Subject importance guidance is disabled.";
    int gridWidth = kDevelopSubjectImportanceMapGridSize;
    int gridHeight = kDevelopSubjectImportanceMapGridSize;
    int activeRegionCount = 0;
    int activeStrokeCount = 0;
    float coverage = 0.0f;
    float positiveCoverage = 0.0f;
    float lowPriorityCoverage = 0.0f;
    float revealCoverage = 0.0f;
    float protectCoverage = 0.0f;
    float moodCoverage = 0.0f;
    float peakImportance = 0.0f;
    float meanImportance = 0.0f;
    float centerBias = 0.0f;
    float edgeBias = 0.0f;
    float mapConfidence = 0.0f;
    std::array<DevelopSubjectImportanceMapCell, kDevelopSubjectImportanceMapCellCount> cells {};
};

struct DevelopSubjectRefinedMap {
    bool enabled = false;
    bool active = false;
    std::string status = "disabled";
    std::string reason = "Subject importance guidance is disabled.";
    std::string sourceMapVersion = kDevelopSubjectImportanceMapVersion;
    int gridWidth = kDevelopSubjectImportanceMapGridSize;
    int gridHeight = kDevelopSubjectImportanceMapGridSize;
    float coverage = 0.0f;
    float lowPriorityCoverage = 0.0f;
    float readabilityCoverage = 0.0f;
    float protectionCoverage = 0.0f;
    float moodCoverage = 0.0f;
    float peakImportance = 0.0f;
    float meanConfidence = 0.0f;
    float boundaryHint = 0.0f;
    std::array<DevelopSubjectRefinedMapCell, kDevelopSubjectImportanceMapCellCount> cells {};
};

DevelopSubjectImportanceInterpretation InterpretDevelopSubjectImportanceMap(
    const EditorNodeGraph::DevelopSubjectImportanceMap& importance);
nlohmann::json DevelopSubjectImportanceInterpretationToJson(
    const DevelopSubjectImportanceInterpretation& map);
DevelopSubjectRefinedMap BuildDevelopSubjectRefinedMap(
    const DevelopSubjectImportanceInterpretation& sourceMap,
    const DevelopSubjectSceneIntent& subjectIntent);
nlohmann::json DevelopSubjectRefinedMapToJson(const DevelopSubjectRefinedMap& map);
void ApplyDevelopSubjectRefinedMap(
    DevelopSubjectSceneIntent& subjectIntent,
    const DevelopSubjectImportanceInterpretation& importanceMap);
DevelopSubjectImportanceSummary SummarizeDevelopSubjectImportance(
    const EditorNodeGraph::DevelopSubjectImportanceMap& importance);
DevelopSubjectSceneIntent ResolveDevelopSubjectSceneIntent(
    EditorNodeGraph::DevelopAutoIntent intent,
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    const EditorNodeGraph::DevelopSubjectImportanceMap& importance,
    const DevelopDynamicRange::DevelopToneAutoStats& stats,
    const DevelopDynamicRange::DevelopDynamicRangeRegionEvidence& regionEvidence);
nlohmann::json BuildDevelopSubjectSolveNotes(const DevelopSubjectSceneIntent& subjectIntent);
nlohmann::json DevelopSubjectSceneIntentToJson(const DevelopSubjectSceneIntent& subjectIntent);

} // namespace Stack::Editor::DevelopSubjectImportance
