#include "Editor/EditorModule.h"

void EditorModule::ClearRawCurveDragState() {
    m_RawCurveDragCleanupPending = false;
    // A curve may be hidden or replaced before its widget sees mouse release.
    // Only transient drag flags are cleared; selection and authored edits stay
    // with the graph's normal commit/history path.
    const auto clear = [](Stack::EditorModuleTypes::RawCurveGraphUiState& curve) {
        curve.draggingExposure = false;
        curve.draggingPoint = -1;
        curve.draggingSegment = -1;
        curve.draggingSide = -1;
    };
    clear(m_RawWorkspaceLabUi.zonesCurveGraph);
    for (auto& curve : m_RawWorkspaceLabUi.toneCurveGraphs) clear(curve);
    for (auto& entry : m_RawWorkspaceLabUi.zoneAreas.graphs) clear(entry.second.interaction);
}
