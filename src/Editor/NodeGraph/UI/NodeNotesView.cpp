#include "NodeNotesView.h"
#include "Editor/NodeGraph/GraphEditorContext.h"
#include "Utils/ImGuiExtras.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace Stack::Editor::NodeGraphUI {
namespace {
constexpr float kTextSize = 11.0f;
constexpr float kLineHeight = 14.0f;
constexpr float kInset = 8.0f;
constexpr const char* kEmptyNote = "Click to add a note.";
}

void NodeNotesView::BeginFrame(bool revealHeld) {
    // A hidden or replaced canvas must not resume a stale keyboard capture.
    const int frame = ImGui::GetFrameCount();
    if (lastFrame_ >= 0 && frame > lastFrame_ + 1) edit_.reset();
    lastFrame_ = frame;
    held_ = revealHeld;
}

void NodeNotesView::Reset() {
    layouts_.clear(); edit_.reset(); commit_.reset(); held_ = false; lastFrame_ = -1;
}

void NodeNotesView::Finish(bool cancel) {
    if (!edit_) return;
    if (!cancel && edit_->target.text != edit_->original)
        commit_ = std::move(edit_->target);
    edit_.reset();
    ImGui::ClearActiveID();
}

std::optional<NodeNotesView::Commit> NodeNotesView::TakeCommit() {
    auto result = std::move(commit_); commit_.reset(); return result;
}

bool NodeNotesView::Draw(const EditorNodeGraph::Node& node, const GraphEditorContext& context,
    std::uint64_t editRevision, ImVec2 minimum, ImVec2 maximum, float scale,
    ImVec4 textColor, ImVec4 mutedColor, bool hovered, float opacity) {
    scale = std::max(.001f, scale);
    const ImVec2 start(minimum.x + kInset * scale, minimum.y + kInset * scale);
    const ImVec2 end(maximum.x - kInset * scale, maximum.y - kInset * scale);
    const ImVec2 size(std::max(1.f, end.x - start.x), std::max(1.f, end.y - start.y));
    const bool inside = hovered && ImGui::IsMouseHoveringRect(start, end, false);
    const bool interactive = Revealed();
    if (edit_ && edit_->target.nodeId == node.id &&
        (edit_->target.instanceUuid != node.instanceUuid || edit_->original != node.notes))
        Finish(true);

    ImGui::PushID(node.id);
    ImGui::PushID("node-notes");
    if (!edit_ && interactive && inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        Edit edit;
        edit.target = {context.documentId, context.graphId, node.instanceUuid, node.notes, node.id};
        edit.original = node.notes;
        edit.waitForFRelease = ImGui::IsKeyDown(ImGuiKey_F);
        edit_ = std::move(edit);
    }
    if (edit_ && edit_->target.nodeId == node.id) {
        auto& edit = *edit_;
        edit.waitForFRelease &= ImGui::IsKeyDown(ImGuiKey_F);
        const bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        const bool accept = ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
        const bool outside = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !inside;
        ImGuiWindow* parent = ImGui::GetCurrentWindow();
        const ImVec2 previousCursor = parent->DC.CursorPos;
        const ImVec2 previousMaximum = parent->DC.CursorMaxPos;
        const bool previousSetPosition = parent->DC.IsSetPos;
        ImGui::SetCursorScreenPos(start);
        const float globalScale = ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi;
        ImGui::PushFont(nullptr, kTextSize * scale / std::max(.001f, globalScale));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.f * scale);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_Text, textColor);
        if (edit.focus) { ImGui::SetKeyboardFocusHere(); edit.focus = false; }
        const ImGuiInputTextFlags flags = ImGuiInputTextFlags_WordWrap |
            ImGuiInputTextFlags_EnterReturnsTrue |
            (edit.waitForFRelease ? ImGuiInputTextFlags_ReadOnly : 0);
        ImGuiExtras::InputTextMultiline("##edit", edit.target.text, size, flags);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
        ImGui::PopFont();
        // The canvas already reserves its bounds. A temporary editor must not
        // extend the parent window or leave a cursor-reposition request behind.
        parent->DC.CursorPos = previousCursor;
        parent->DC.CursorMaxPos = previousMaximum;
        parent->DC.IsSetPos = previousSetPosition;
        if (cancel || accept || outside) Finish(cancel);
    } else {
        Layout& cached = layouts_[node.id];
        const float width = std::max(1.f, size.x - 7.f * scale);
        const float fontSize = kTextSize * scale;
        bool changed = cached.uuid != node.instanceUuid;
        if (changed || cached.revision != editRevision) {
            const std::string& source = node.notes;
            if (changed || cached.text != source) { cached.text = source; changed = true; }
            cached.uuid = node.instanceUuid; cached.revision = editRevision;
        }
        ImFont* font = ImGui::GetFont();
        if (changed || cached.font != font || std::abs(cached.width - width) > .01f ||
            std::abs(cached.fontSize - fontSize) > .001f || cached.lines.empty()) {
            cached.font = font; cached.width = width; cached.fontSize = fontSize; cached.lines.clear();
            const char* text = cached.text.empty() ? kEmptyNote : cached.text.c_str();
            const char* finish = text + (cached.text.empty() ? std::char_traits<char>::length(kEmptyNote) : cached.text.size());
            const char* p = text;
            while (p < finish) {
                // Measure the actual baked size: small fonts have pixel-rounded
                // advances that are not exact multiples of their unzoomed size.
                const char* next = font->CalcWordWrapPosition(fontSize, p, finish, width);
                if (next <= p) { // Always advance a complete UTF-8 codepoint.
                    next = p + 1;
                    while (next < finish && (static_cast<unsigned char>(*next) & 0xc0) == 0x80) ++next;
                }
                const char* newline = std::find(p, next, '\n');
                if (newline < next) next = newline;
                cached.lines.push_back({static_cast<std::size_t>(p-text), static_cast<std::size_t>(next-p)});
                p = next;
                while (p < finish && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
                if (p < finish && *p == '\n') ++p;
            }
        }
        const float logicalHeight = size.y / scale;
        const float fullHeight = static_cast<float>(cached.lines.size()) * kLineHeight;
        const float maxScroll = std::max(0.f, fullHeight - logicalHeight);
        if (inside && interactive && ImGui::GetIO().MouseWheel != 0) {
            cached.scroll -= ImGui::GetIO().MouseWheel * kLineHeight * 3.f;
            ImGuiExtras::ConsumeSliderWheel();
        }
        cached.scroll = std::clamp(cached.scroll, 0.f, maxScroll);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(start,end,true);
        ImVec4 color = cached.text.empty() ? mutedColor : textColor;
        color.w *= opacity;
        const char* text = cached.text.empty() ? kEmptyNote : cached.text.c_str();
        const int first = std::max(0, static_cast<int>(cached.scroll / kLineHeight));
        const int last = std::min(static_cast<int>(cached.lines.size()),
            static_cast<int>((cached.scroll+logicalHeight)/kLineHeight)+1);
        for (int i = first; i < last; ++i) {
            const auto& line = cached.lines[i];
            draw->AddText(font, kTextSize * scale,
                ImVec2(start.x,start.y+(i*kLineHeight-cached.scroll)*scale),
                ImGui::GetColorU32(color), text+line.start, text+line.start+line.length);
        }
        if (maxScroll > 0) {
            const float thumb = std::max(8.f, logicalHeight * logicalHeight / fullHeight);
            const float top = cached.scroll / maxScroll * (logicalHeight-thumb);
            draw->AddRectFilled(ImVec2(end.x-3.f*scale,start.y+top*scale),
                ImVec2(end.x,start.y+(top+thumb)*scale),ImGui::GetColorU32(mutedColor),1.f*scale);
        }
        draw->PopClipRect();
    }
    ImGui::PopID(); ImGui::PopID();
    return interactive && inside;
}
} // namespace Stack::Editor::NodeGraphUI
