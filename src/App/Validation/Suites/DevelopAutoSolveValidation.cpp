#include "App/Validation/ValidationSuites.h"
#include "App/Validation/Suites/DevelopAutoSolveValidationCandidateProbes.h"
#include "App/Validation/Suites/DevelopAutoSolveValidationHelpers.h"
#include "App/Validation/Suites/DevelopAutoSolveValidationRenderedMetrics.h"

#include "Editor/EditorModule.h"
#include "Editor/EditorRenderWorker.h"
#include "Editor/Layers/ToneLayers.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/MaskRenderTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string>

namespace {

bool ValidateDevelopAutoSolveBehavior() {
    // Scenario fragments intentionally share this validation function state so the
    // historic assertion order and failure report stay unchanged while each phase
    // lives in a smaller file that future Develop passes can edit directly.
#include "DevelopAutoSolveValidationScenarios/01_initial_solve_and_memory.inl"
#include "DevelopAutoSolveValidationScenarios/02_regional_subject_scenarios.inl"
#include "DevelopAutoSolveValidationScenarios/03_candidate_payload_and_scheduler.inl"
#include "DevelopAutoSolveValidationScenarios/04_rendered_feedback_scenarios.inl"
#include "DevelopAutoSolveValidationScenarios/05_core_dynamic_range_profiles.inl"
#include "DevelopAutoSolveValidationScenarios/06_result_aggregation_and_report.inl"
} // namespace

namespace Stack::Validation {

bool ValidateDevelopAutoSolveBehavior() {
    return ::ValidateDevelopAutoSolveBehavior();
}

} // namespace Stack::Validation
