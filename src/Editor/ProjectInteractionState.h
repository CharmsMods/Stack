#pragma once

#include "Persistence/RawProjectModel.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Stack::Editor {

struct PointCurveInteractionState {
    int selectedPoint = -1;
    int draggingPoint = -1;
    int contextPoint = -1;
};

struct MultiFrameProjectDialogState {
    char projectName[160] = "MultiFrame Capture Set";
    std::filesystem::path destinationFolder;
    std::vector<std::filesystem::path> selectedPaths;
    int referenceFrameIndex = 0;
    std::vector<Project::RawCaptureCompatibilitySummary> selectedSummaries;
    std::vector<std::string> selectedSummaryWarnings;
    char newSetName[160] = "New Source Set";
    int newSetIntent = 0;
    char renameSetName[160] = {};
    double contextSpawnX = 320.0;
    double contextSpawnY = 220.0;
};

// Unsaved widget interactions belong to the editor displaying this project.
// Reset them when replacing the document, never when merely hiding its tab.
struct ProjectInteractionState {
    PointCurveInteractionState localRange;
    PointCurveInteractionState finishTone;
    int viewTransformDraggingHandle = 0;
    bool suppressConflictPrompt = false;
    MultiFrameProjectDialogState multiFrame;
};

} // namespace Stack::Editor
