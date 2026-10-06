#pragma once

#include "Notifications/NotificationStore.h"
#include "Utils/UiActivity.h"
#include "Notifications/AnimatedIndicatorLabel.h"
#include <imgui.h>
#include <functional>
#include <unordered_map>

namespace Stack::Notifications {

struct PresentationContext {
    OwnerId currentOwner = 0;
    ImVec2 workspacePosition{};
    ImVec2 workspaceSize{};
    bool reducedMotion = false;
    std::function<void(ImDrawList*)> keepFixed;
    std::function<void(OwnerId)> viewProject;
};

// Owns display state only. Operation state and actions stay with their feature.
class Presenter {
public:
    void BeginFrame(NotificationStore& store);
    bool BlocksInput() const { return m_Dialog != 0; }
    bool PanelOpen() const { return m_PanelOpen; }
    void TogglePanel() { m_PanelOpen = !m_PanelOpen; }
    float RenderIndicator(NotificationStore& store,
        const UiActivity::Snapshot& snapshot, UiActivity::Presentation& activity,
        float width, float height, bool openPanel, const PresentationContext& context);
    void RenderActivity(NotificationStore& store, const PresentationContext& context);
    void RenderDialog(NotificationStore& store, const PresentationContext& context);

private:
    AnimatedIndicatorLabel m_IndicatorLabel;
    void RenderRecord(NotificationStore& store, const Record& record,
        const PresentationContext& context, bool dialog);
    void RenderActions(NotificationStore& store, const Record& record, bool dialog,
        bool appearing = false);
    void UpdatePreview(const std::vector<Record>& records);
    bool m_PanelOpen = false;
    EventId m_Selected = 0;
    EventId m_Dialog = 0;
    EventId m_PopupDialog = 0;
    EventId m_Preview = 0;
    double m_PreviewRemaining = 0.0;
    std::uint64_t m_SeenPreviewRevision = 0;
    ImVec2 m_Anchor{};
    bool m_HasAnchor = false;
};

bool SystemReducedMotion();

} // namespace Stack::Notifications
