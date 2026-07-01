#include "Editor/Internal/EditorModuleDevelopPayloadComparison.h"

#include "Editor/Internal/EditorModuleDevelopAutoGuidanceControls.h"
#include "Editor/Internal/EditorModulePreLocalExposureControls.h"
#include "Editor/Internal/EditorModuleRawControlShared.h"

#include <cstddef>

namespace Stack::Editor::DevelopPayloadComparison {

using Stack::Editor::DevelopAutoGuidanceControls::SameDevelopAutoGuidance;
using Stack::Editor::PreLocalExposureControls::SameRawDetailFusionSettings;
using Stack::Editor::RawControls::SameRawDevelopSettings;

namespace {

bool SameDevelopSubjectImportanceRegion(
    const EditorNodeGraph::DevelopSubjectImportanceRegion& a,
    const EditorNodeGraph::DevelopSubjectImportanceRegion& b) {
    return a.id == b.id &&
        a.mode == b.mode &&
        a.enabled == b.enabled &&
        a.centerX == b.centerX &&
        a.centerY == b.centerY &&
        a.radiusX == b.radiusX &&
        a.radiusY == b.radiusY &&
        a.feather == b.feather &&
        a.strength == b.strength;
}

bool SameDevelopSubjectImportanceStroke(
    const EditorNodeGraph::DevelopSubjectImportanceStroke& a,
    const EditorNodeGraph::DevelopSubjectImportanceStroke& b) {
    if (a.id != b.id ||
        a.mode != b.mode ||
        a.enabled != b.enabled ||
        a.subtract != b.subtract ||
        a.radius != b.radius ||
        a.feather != b.feather ||
        a.strength != b.strength ||
        a.points.size() != b.points.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.points.size(); ++i) {
        if (a.points[i].x != b.points[i].x ||
            a.points[i].y != b.points[i].y) {
            return false;
        }
    }
    return true;
}

bool SameDevelopSubjectImportance(
    const EditorNodeGraph::DevelopSubjectImportanceMap& a,
    const EditorNodeGraph::DevelopSubjectImportanceMap& b) {
    if (a.schemaVersion != b.schemaVersion ||
        a.enabled != b.enabled ||
        a.showOverlay != b.showOverlay ||
        a.overlayOpacity != b.overlayOpacity ||
        a.showInterpretedMapOverlay != b.showInterpretedMapOverlay ||
        a.interpretedMapOpacity != b.interpretedMapOpacity ||
        a.showRefinedMapOverlay != b.showRefinedMapOverlay ||
        a.refinedMapOpacity != b.refinedMapOpacity ||
        a.brushEnabled != b.brushEnabled ||
        a.brushSubtract != b.brushSubtract ||
        a.brushMode != b.brushMode ||
        a.brushRadius != b.brushRadius ||
        a.brushFeather != b.brushFeather ||
        a.brushStrength != b.brushStrength ||
        a.activeRegionId != b.activeRegionId ||
        a.activeStrokeId != b.activeStrokeId ||
        a.nextRegionId != b.nextRegionId ||
        a.nextStrokeId != b.nextStrokeId ||
        a.regions.size() != b.regions.size() ||
        a.strokes.size() != b.strokes.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.regions.size(); ++i) {
        if (!SameDevelopSubjectImportanceRegion(a.regions[i], b.regions[i])) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.strokes.size(); ++i) {
        if (!SameDevelopSubjectImportanceStroke(a.strokes[i], b.strokes[i])) {
            return false;
        }
    }
    return true;
}

} // namespace

bool SameRawDevelopPayload(
    const EditorNodeGraph::RawDevelopPayload& a,
    const EditorNodeGraph::RawDevelopPayload& b) {
    return SameRawDevelopSettings(a.settings, b.settings) &&
        a.scenePrepEnabled == b.scenePrepEnabled &&
        SameRawDetailFusionSettings(a.scenePrepSettings, b.scenePrepSettings) &&
        a.integratedToneEnabled == b.integratedToneEnabled &&
        a.integratedToneLayerJson == b.integratedToneLayerJson &&
        SameDevelopAutoGuidance(a.autoGuidance, b.autoGuidance) &&
        SameDevelopSubjectImportance(a.subjectImportance, b.subjectImportance) &&
        a.uiMode == b.uiMode;
}

} // namespace Stack::Editor::DevelopPayloadComparison
