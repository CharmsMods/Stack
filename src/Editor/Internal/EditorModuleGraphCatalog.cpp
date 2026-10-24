#include "Editor/EditorModule.h"
#include "App/UiPerformanceTrace.h"
#include "App/UiPerformanceProbe.h"

#include <algorithm>
#include <cmath>

void EditorModule::SetGraphCatalogHost(const ImVec2& position, const ImVec2& size,
    bool expanded, float visibleWidth) {
    m_Sidebar.GetNodeGraphUI().SetCatalogHost(position, size, expanded, visibleWidth);
}

void EditorModule::RenderGraphWorkspace() {
    Stack::UiPerformance::Probe::Drawn(Stack::UiPerformance::Probe::Visual::Workspace,1);
    if (Stack::UiPerformance::Probe::Active())
        Stack::UiPerformance::Probe::Drawn(Stack::UiPerformance::Probe::Visual::Project,
            Stack::UiPerformance::Probe::Key(GetProjectDocumentId()) ^ reinterpret_cast<std::uintptr_t>(this));
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    auto& canvas = m_Sidebar.GetNodeGraphUI();
    {
        Stack::UiPerformance::Span timing(Stack::UiPerformance::Stage::NavigationBars);
        RenderRawLayerGraphToolbar();
    }
    canvas.Render(this);

    // The project canvas shares the same rail-hosted catalog as the other
    // graph workspace. Tab toggles its project-owned panel state.
    const float target = canvas.HasDrawerOpen() && !canvas.HasCatalogHost()
        ? std::min(420.f, available.x) : 0.f;
    const float response = 1.f - std::exp(-18.f * ImGui::GetIO().DeltaTime);
    m_NodesPanelWidthAnim += (target - m_NodesPanelWidthAnim) * response;
    canvas.RenderNodesPanelDrawer(this, m_NodesPanelWidthAnim, target, available.y, position);
    FinishRawLayerGraphFrame();
    SubmitRenderIfReady();
}
