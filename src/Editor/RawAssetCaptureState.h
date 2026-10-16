#pragma once
#include "Editor/RawOperationMaskTarget.h"
#include "Raw/RawAssetCapture.h"
#include "Renderer/RawAssetCaptureOverlay.h"
#include <atomic>
#include "Project/RawPendingMask.h"

namespace Stack::Editor {
struct RawAssetCaptureState {
    bool assetsTab = false;
    bool actionMenu = false, openMenu = false, excludeStroke = false, repeat = false, useCurve = false;
    int maskApplication = 0, lastMaskApplication = 0; // 0 save only, 1 apply using the owning layer's rule
    bool lastUseCurve = false, pointerExited = false;
    RawAssetCapture::Output lastOutput = RawAssetCapture::Output::Samples;
    bool armed = false, drawing = false, pending = false;
    bool feedbackNeeded = false, feedbackDirty = false, feedbackValid = false;
    std::shared_ptr<const RawAssetCapture::PreviewField> feedback;
    Renderer::RawAssetCaptureOverlay feedbackRenderer;
    double lastActivity = -1, lastMeasurement = -1;
    float feedbackOpacity = 0;
    std::optional<RawOperationMaskTarget> target;
    RawAssetCapture::Selection selection;
    int operationId = 0, width = 0, height = 0, frame = 0;
    Raw::RawWorkingSpace workingSpace = Raw::RawWorkingSpace::LinearRec2020D65;
    std::uint64_t revision = 0, sourceHash = 0, serial = 0;
    std::string sourceKey;
    nlohmann::json sourceRecipe;
    std::shared_ptr<std::atomic<bool>> cancellation;
    std::string status;
    std::optional<Project::RawPendingMask> application;
    mutable std::uint64_t checkedRevision = 0;
    mutable bool checkedInputMatches = false;
};
} // namespace Stack::Editor
