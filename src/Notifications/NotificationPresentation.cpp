#include "Notifications/NotificationPresenter.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace Stack::Notifications {
namespace {
bool IsRunning(const Record& record) { return record.state == RecordState::Running; }
const char* OutcomeLabel(const Record& record) {
    switch (record.outcome) {
    case Outcome::Success: return "Completed";
    case Outcome::Failure: return "Failed";
    case Outcome::Partial: return "Partly completed";
    case Outcome::Cancelled: return "Cancelled";
    default: return record.NeedsAttention() ? "Needs attention" : "Info";
    }
}
ImVec4 StatusInk(const Record& record) {
    const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const ImVec4 background = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const bool light = background.x + background.y + background.z > 1.5f;
    if (record.content.severity == Severity::Error || record.outcome == Outcome::Failure)
        return light ? ImVec4(.66f,.22f,.20f,1) : ImVec4(.93f,.63f,.59f,1);
    if (record.content.severity == Severity::Warning)
        return light ? ImVec4(.53f,.33f,.10f,1) : ImVec4(.90f,.73f,.46f,1);
    if (record.outcome == Outcome::Success)
        return light ? ImVec4(.22f,.42f,.29f,1) : ImVec4(.63f,.78f,.69f,1);
    return text;
}
void Wrapped(const std::string& text) {
    if (text.empty()) return;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
}
void Muted(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    Wrapped(text);
    ImGui::PopStyleColor();
}
void Label(const char* text, int count) {
    ImGui::Spacing();
    ImGui::TextDisabled("%s  %d", text, count);
    ImGui::Separator();
}
std::string Brief(std::string text, std::size_t limit) {
    if (text.size() <= limit) return text;
    std::size_t end = limit;
    while (end && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
    text.resize(end);
    return text + "...";
}
std::string ActionLabel(const std::string& text, float width) {
    std::string label;
    const char* at = text.c_str();
    const char* end = at + text.size();
    auto* font = ImGui::GetFont();
    const float wrapWidth = std::max(1.f,width - ImGui::GetStyle().FramePadding.x * 2);
    while (at < end) {
        const char* next = font->CalcWordWrapPosition(ImGui::GetFontSize(),at,end,wrapWidth);
        if (next <= at) {
            next = at + 1;
            while (next < end && (static_cast<unsigned char>(*next) & 0xc0) == 0x80) ++next;
        }
        if (!label.empty()) label += '\n';
        label.append(at,next);
        at = next;
        while (at < end && *at == ' ') ++at;
    }
    return label;
}
float FooterHeight(const Record& record, float width) {
    const auto& style = ImGui::GetStyle();
    float total = 0, rowHeight = 0, used = 0;
    for (const auto& action : record.content.actions) {
        const auto text = ActionLabel(action.label,width);
        const auto textSize = ImGui::CalcTextSize(text.c_str());
        const float size = std::min(width,textSize.x + style.FramePadding.x * 2);
        if (used && used + style.ItemSpacing.x + size > width) { total += rowHeight + style.ItemSpacing.y; used = 0; rowHeight = 0; }
        rowHeight = std::max(rowHeight,textSize.y + style.FramePadding.y * 2);
        used += (used ? style.ItemSpacing.x : 0) + size;
    }
    return total + rowHeight + style.ItemSpacing.y * 3 +
        (record.actionPending ? ImGui::GetTextLineHeightWithSpacing() : 0);
}
}

bool SystemReducedMotion() {
#if defined(_WIN32)
    BOOL animate = TRUE;
    return SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0) && !animate;
#else
    return false;
#endif
}

void Presenter::BeginFrame(NotificationStore& store) {
    // Retained editing and naming forms finish before a notification dialog.
    // Disabling that form while waiting for it to close would deadlock input.
    if (ImGui::GetCurrentContext()) {
        const auto* modal = ImGui::GetTopMostPopupModal();
        if (modal && std::strcmp(modal->Name,"##StackNotificationDialog") != 0) { m_Dialog = 0; return; }
    }
    if (m_Dialog) {
        const auto current = store.Find(m_Dialog);
        if (!current || !current->ownerValid || !current->centerRequested || current->Terminal())
            m_Dialog = 0;
    }
    if (!m_Dialog) {
        for (const auto& record : store.Snapshot()) {
            if (record.ownerValid && record.centerRequested && !record.Terminal()) {
                m_Dialog = record.id;
                break;
            }
        }
    }
}

void Presenter::UpdatePreview(const std::vector<Record>& records) {
    const Record* next = nullptr;
    for (const auto& record : records) {
        if (record.previewRevision > m_SeenPreviewRevision && record.content.preview &&
            !record.dismissed && !IsRunning(record))
            if (!next || record.previewRevision > next->previewRevision) next = &record;
    }
    if (next && !BlocksInput()) {
        m_SeenPreviewRevision = next->previewRevision;
        m_Preview = next->id;
        m_PreviewRemaining = 4.0;
    }
}

void Presenter::RenderRecord(NotificationStore& store, const Record& record,
    const PresentationContext& context, bool dialog) {
    if (dialog) {
        if (!record.content.title.empty()) Wrapped(record.content.title);
        Muted(record.owner.label + (record.content.context.empty() ? "" : " · " + record.content.context));
    } else if (!record.content.context.empty()) Muted(record.content.context);
    if (record.owner.kind == OwnerKind::Project && record.owner.id != context.currentOwner &&
        context.viewProject && record.ownerValid) {
        if (ImGui::SmallButton("View project")) context.viewProject(record.owner.id);
    }
    Wrapped(record.content.message);
    if (record.content.progress) {
        const auto& progress = *record.content.progress;
        if (progress.Measured()) {
            const float fraction = static_cast<float>(std::clamp(progress.completed / progress.total, 0.0, 1.0));
            ImGui::ProgressBar(fraction, ImVec2(-1, 4 * ImGui::GetFontSize() / 13.0f), "");
        }
        if (!progress.label.empty()) Muted(progress.label);
    }
    if (record.content.imagePreview && record.ownerValid && store.IsOperationCurrent(record.owner,record.operationId)) {
        const auto& preview = *record.content.imagePreview;
        bool show = preview.textureId && preview.width > 0 && preview.height > 0;
        try { show = show && (!preview.canShow || preview.canShow()); } catch (...) { show = false; }
        if (show) {
            const float scale = std::min({1.f,ImGui::GetContentRegionAvail().x / preview.width,
                160.f * ImGui::GetFontSize() / 13.f / preview.height});
            ImGui::Image(static_cast<ImTextureID>(preview.textureId),ImVec2(preview.width * scale,preview.height * scale));
            if (!preview.caption.empty()) Muted(preview.caption);
        }
    }
    if (!record.content.items.empty()) {
        for (const auto& item : record.content.items) {
            Wrapped(item.label + (item.value.empty() ? "" : " · " + item.value));
        }
    }
    if (!record.content.details.empty() && ImGui::TreeNodeEx("Details", ImGuiTreeNodeFlags_SpanAvailWidth)) {
        Muted(record.content.details);
        ImGui::TreePop();
    }
    if (record.content.customBody && record.ownerValid && (dialog || record.kind != RecordKind::Decision)) record.content.customBody();
    if (!record.actionError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, StatusInk(record));
        Wrapped(record.actionError);
        ImGui::PopStyleColor();
    }
    if (!record.ownerValid) Muted("This owner is closed. Its actions are unavailable.");
    if (!dialog) {
        ImGui::Spacing();
        RenderActions(store, record, false);
        if (record.kind == RecordKind::Decision && record.ownerValid) {
            if (ImGui::Button("Review")) {
                store.RequestCenter(record.id);
                m_Dialog = record.id;
            }
        } else if (record.NeedsAttention() && record.content.actions.empty()) {
            if (ImGui::Button("Acknowledge")) store.Resolve(record.id);
        }
    }
}

void Presenter::RenderActions(NotificationStore& store, const Record& record, bool dialog, bool appearing) {
    if (record.actionPending) ImGui::TextDisabled("Working...");
    std::size_t defaultIndex = record.content.actions.size();
    if (appearing) {
        for (std::size_t i = 0; i < record.content.actions.size(); ++i)
            if (record.content.actions[i].safeCancel && store.CanInvokeAction(record.id,i)) { defaultIndex = i; break; }
        if (defaultIndex == record.content.actions.size())
            for (std::size_t i = 0; i < record.content.actions.size(); ++i)
                if (!record.content.actions[i].destructive && record.content.actions[i].defaultAction && store.CanInvokeAction(record.id,i)) { defaultIndex = i; break; }
    }
    for (std::size_t index = 0; index < record.content.actions.size(); ++index) {
        const auto& action = record.content.actions[index];
        // Decision commands are reviewed in the fixed dialog, not fired from a preview.
        if (!dialog && record.kind == RecordKind::Decision) continue;
        const float availableWidth = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
        const auto label = ActionLabel(action.label,availableWidth);
        const float buttonWidth = std::min(availableWidth,ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2);
        const float rightEdge = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        if (index > 0 && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + buttonWidth < rightEdge)
            ImGui::SameLine();
        ImGui::PushID(static_cast<int>(index));
        const bool available = !record.actionPending && store.CanInvokeAction(record.id, index);
        ImGui::BeginDisabled(!available);
        if (appearing && index == defaultIndex) ImGui::SetKeyboardFocusHere();
        Record destructiveInk;
        destructiveInk.content.severity = Severity::Error;
        if (action.destructive) ImGui::PushStyleColor(ImGuiCol_Text, StatusInk(destructiveInk));
        ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus,action.destructive);
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav,appearing && action.destructive);
        const bool clicked = ImGui::Button((label + "###NotificationAction").c_str(),ImVec2(buttonWidth,0));
        ImGui::PopItemFlag();
        ImGui::PopItemFlag();
        if (appearing && index == defaultIndex)
            ImGui::SetItemDefaultFocus();
        if (action.destructive) ImGui::PopStyleColor();
        ImGui::EndDisabled();
        ImGui::PopID();
        if (clicked) {
            store.InvokeAction(record.id, index);
            if (dialog) {
                const auto updated = store.Find(record.id);
                if (!updated || !updated->centerRequested || updated->Terminal()) {
                    ImGui::CloseCurrentPopup();
                    m_Dialog = 0;
                }
            }
            break;
        }
    }
    if (dialog && ImGui::IsKeyPressed(ImGuiKey_Escape) && !record.actionPending) {
        for (std::size_t index = 0; index < record.content.actions.size(); ++index) {
            if (record.content.actions[index].safeCancel && store.CanInvokeAction(record.id, index)) {
                store.InvokeAction(record.id, index);
                const auto updated = store.Find(record.id);
                if (!updated || !updated->centerRequested || updated->Terminal()) {
                    ImGui::CloseCurrentPopup();
                    m_Dialog = 0;
                }
                break;
            }
        }
    }
}

void Presenter::RenderActivity(NotificationStore& store, const PresentationContext& context) {
    auto records = store.Snapshot();
    std::stable_sort(records.begin(),records.end(),[&](const Record& a,const Record& b) {
        const auto section = [](const Record& r) { return IsRunning(r) ? 0 : r.NeedsAttention() ? 1 : 2; };
        if (section(a) != section(b)) return section(a) < section(b);
        const auto priority = [&](const Record& r) { return r.content.maintenance ? 2 : r.owner.id == context.currentOwner ? 0 : 1; };
        if (IsRunning(a) && IsRunning(b) && priority(a) != priority(b)) return priority(a) < priority(b);
        return a.updatedAt > b.updatedAt;
    });
    UpdatePreview(records);
    if (!m_HasAnchor || BlocksInput()) return;
    const float unit = ImGui::GetFontSize() / 13.0f;
    const auto* viewport = ImGui::GetMainViewport();
    const float width = std::min(420.0f * unit, viewport->WorkSize.x - 24.0f * unit);
    const ImVec2 anchor(std::min(m_Anchor.x, viewport->WorkPos.x + viewport->WorkSize.x - 12 * unit), m_Anchor.y + 6 * unit);
    if (m_PanelOpen) {
        ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(1,0));
        ImGui::SetNextWindowSize(ImVec2(width, std::min(560.0f * unit, viewport->WorkSize.y - 76 * unit)), ImGuiCond_Always);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 9 * unit);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * unit,14 * unit));
        if (ImGui::Begin("##StackNotificationsActivity", nullptr,
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse)) {
            if (context.keepFixed) context.keepFixed(ImGui::GetWindowDrawList());
            ImGui::TextUnformatted("Activity");
            ImGui::SameLine(std::max(0.f,ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize("Close").x - ImGui::GetStyle().FramePadding.x * 2));
            if (ImGui::SmallButton("Close")) m_PanelOpen = false;
            int running=0, attention=0, recent=0;
            for (const auto& record : records) {
                if (IsRunning(record)) ++running;
                else if (record.NeedsAttention()) ++attention;
                else ++recent;
            }
            const auto section = [&](const char* name, int count, int kind) {
                if (!count) return;
                Label(name,count);
                for (const auto& record : records) {
                    const int recordKind = IsRunning(record) ? 0 : record.NeedsAttention() ? 1 : 2;
                    if (recordKind != kind) continue;
                    ImGui::PushID(static_cast<int>(record.id));
                    const std::string title = record.content.title.empty() ? Brief(record.content.message,96) : record.content.title;
                    const bool selected = m_Selected == record.id;
                    const float textWidth = std::max(1.0f,ImGui::GetContentRegionAvail().x - 12 * unit);
                    const auto titleSize = ImGui::CalcTextSize(title.c_str(),nullptr,false,textWidth);
                    const std::string owner = record.owner.label + " · " + (IsRunning(record) ? "Running" : OutcomeLabel(record));
                    const auto ownerSize = ImGui::CalcTextSize(owner.c_str(),nullptr,false,textWidth);
                    const float rowHeight = titleSize.y + ownerSize.y + 12 * unit;
                    const auto pos = ImGui::GetCursorScreenPos();
                    if (ImGui::Selectable("##Item", selected, ImGuiSelectableFlags_None, ImVec2(0,rowHeight)))
                        m_Selected = selected ? 0 : record.id;
                    auto* draw = ImGui::GetWindowDrawList();
                    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x + 5 * unit,pos.y + 3 * unit),
                        ImGui::GetColorU32(ImGuiCol_Text),title.c_str(),nullptr,textWidth);
                    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(pos.x + 5 * unit,pos.y + titleSize.y + 5 * unit),ImGui::GetColorU32(ImGuiCol_TextDisabled),owner.c_str(),nullptr,textWidth);
                    if (m_Selected == record.id) RenderRecord(store,record,context,false);
                    ImGui::PopID();
                }
            };
            section("Running",running,0); section("Needs attention",attention,1); section("Recent",recent,2);
            if (records.empty()) ImGui::TextDisabled("No activity this session.");
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }
    if (!m_Preview || m_PreviewRemaining <= 0) return;
    const auto record = store.Find(m_Preview);
    if (!record || record->dismissed) { m_Preview = 0; return; }
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(1,0));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width * .8f,0),ImVec2(width,240 * unit));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,9 * unit);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(16 * unit,14 * unit));
    ImGui::Begin("##StackNotificationPreview",nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing);
    if (context.keepFixed) context.keepFixed(ImGui::GetWindowDrawList());
    Muted(record->owner.label);
    if (!record->content.title.empty()) Wrapped(record->content.title);
    const std::string message = Brief(record->content.message,220);
    Wrapped(message);
    if (ImGui::Button("View details")) { m_PanelOpen = true; m_Selected = record->id; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Dismiss")) { store.Dismiss(record->id); m_Preview = 0; }
    const bool paused = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    if (!paused) m_PreviewRemaining -= ImGui::GetIO().DeltaTime;
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void Presenter::RenderDialog(NotificationStore& store, const PresentationContext& context) {
    BeginFrame(store);
    if (m_PopupDialog && m_PopupDialog != m_Dialog) {
        if (m_PopupDialog && ImGui::IsPopupOpen("##StackNotificationDialog")) {
            if (ImGui::BeginPopupModal("##StackNotificationDialog",nullptr,ImGuiWindowFlags_NoDecoration)) {
                ImGui::CloseCurrentPopup(); ImGui::EndPopup();
            }
        }
        m_PopupDialog = 0;
    }
    if (!m_Dialog) return;
    const auto record=store.Find(m_Dialog);
    if (!record) return;
    if (ImGui::GetTopMostPopupModal() && m_PopupDialog != m_Dialog) return;
    if (m_PopupDialog != m_Dialog) {
        ImGui::OpenPopup("##StackNotificationDialog");
        m_PopupDialog = m_Dialog;
    }
    const float unit=ImGui::GetFontSize()/13.0f;
    const float width=std::min((record->content.dialogSize==DialogSize::Large?720.f:520.f)*unit,
        std::max(180.f,context.workspaceSize.x-32.f*unit));
    const float contentWidth = std::max(1.f,width - 40.f * unit);
    const float footerHeight = FooterHeight(*record,contentWidth);
    const float shortContent = ImGui::CalcTextSize(record->content.message.c_str(),nullptr,false,contentWidth).y +
        ImGui::CalcTextSize(record->owner.label.c_str(),nullptr,false,contentWidth).y +
        (!record->content.title.empty() ? ImGui::GetTextLineHeightWithSpacing() : 0) +
        (!record->content.details.empty() ? ImGui::GetFrameHeightWithSpacing() : 0) +
        ImGui::CalcTextSize(record->actionError.c_str(),nullptr,false,contentWidth).y +
        (record->owner.id != context.currentOwner && record->owner.kind == OwnerKind::Project ? ImGui::GetFrameHeightWithSpacing() : 0) +
        record->content.items.size() * ImGui::GetTextLineHeightWithSpacing() + 60.f * unit;
    const float desiredHeight = record->content.dialogSize==DialogSize::Large ? 620.f * unit :
        std::clamp(shortContent + footerHeight + (record->content.customBody ? 100.f * unit : 0) +
            (record->content.imagePreview ? 160.f * unit : 0),190.f * unit,420.f * unit);
    const float height=std::min(desiredHeight,
        std::max(140.f,context.workspaceSize.y-32.f*unit));
    const ImVec2 center(context.workspacePosition.x+context.workspaceSize.x*.5f,
        context.workspacePosition.y+context.workspaceSize.y*.5f);
    ImGui::SetNextWindowPos(center,ImGuiCond_Always,ImVec2(.5f,.5f));
    ImGui::SetNextWindowSize(ImVec2(width,height),ImGuiCond_Always);
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,9.f*unit);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(20.f*unit,18.f*unit));
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg,ImVec4(0,0,0,.48f));
    if (ImGui::BeginPopupModal("##StackNotificationDialog",nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) {
        if (context.keepFixed) context.keepFixed(ImGui::GetWindowDrawList());
        const bool appearing=ImGui::IsWindowAppearing();
        ImGui::BeginChild("Content",ImVec2(0,-footerHeight),false,ImGuiWindowFlags_None);
        RenderRecord(store,*record,context,true);
        ImGui::EndChild();
        ImGui::Separator();
        ImGui::Spacing();
        RenderActions(store,*record,true,appearing);
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

} // namespace Stack::Notifications
