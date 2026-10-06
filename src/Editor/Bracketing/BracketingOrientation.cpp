#include "Editor/EditorModule.h"
#include "BracketingSession.h"
#include <algorithm>
#include <map>

namespace {
using Source = Stack::Editor::BracketingDraftSource;
std::map<int, std::vector<const Source*>> OrientationGroups(const Stack::Editor::BracketingSession& ui) {
    std::map<int, std::vector<const Source*>> groups;
    for (const auto& group : ui.recipe.groups) for (const auto& frame : group.frames) {
        if ((!group.enabled || !frame.enabled) && frame.id != ui.recipe.originFrameId) continue;
        const auto found = std::find_if(ui.sources.begin(), ui.sources.end(),
            [&](const auto& source) { return source.frameId == frame.id; });
        const Source* source = found == ui.sources.end() ? nullptr : &*found;
        if (!source) {
            const auto previous = ui.sourceHistory.find(frame.id);
            if (previous != ui.sourceHistory.end()) source = &previous->second;
        }
        if (source && source->inspected) groups[source->metadata.orientation].push_back(source);
    }
    return groups;
}
const char* OrientationName(int value) {
    static const char* names[] = {"Upright", "Upright", "Mirror horizontally", "180 degrees",
        "Mirror vertically", "Transpose", "90 degrees clockwise", "Transverse", "90 degrees counterclockwise"};
    return value >= 0 && value <= 8 ? names[value] : "Unknown";
}
ImVec2 ToSensor(ImVec2 p, int orientation) {
    switch (orientation) {
    case 2: return {1-p.x, p.y};
    case 3: return {1-p.x, 1-p.y};
    case 4: return {p.x, 1-p.y};
    case 5: return {p.y, p.x};
    case 6: return {p.y, 1-p.x};
    case 7: return {1-p.y, 1-p.x};
    case 8: return {1-p.y, p.x};
    default: return p;
    }
}
bool MatchingChoices(const Stack::Editor::BracketingSession& ui) {
    const auto groups = OrientationGroups(ui);
    auto recipe = ui.recipe;
    recipe.orientationOverrides = ui.orientationChoices;
    int common = -1;
    if (groups.empty()) return false;
    for (const auto& group : groups) {
        const int value = Raw::Bracketing::ResolveOrientation(recipe, group.first);
        if (common >= 0 && common != value) return false;
        common = value;
    }
    return true;
}
}
namespace Stack::Editor {
bool NeedsBracketingOrientationReview(const BracketingSession& ui) {
    if(ui.recipe.reconstruction==Raw::Bracketing::ReconstructionMode::Panorama)return false;
    int reference = -1;
    for (const auto& group : OrientationGroups(ui)) {
        const int effective = Raw::Bracketing::ResolveOrientation(ui.recipe, group.first);
        if (reference >= 0 && reference != effective) return true;
        reference = effective;
    }
    return false;
}
}
void EditorModule::RenderBracketingOrientationReview() {
    if (!m_Bracketing || !m_Bracketing->orientationReviewRequested) return;
    auto state = m_Bracketing;
    auto& ui = *state;
    ui.orientationReviewRequested = false;
    namespace N = Stack::Notifications;
    const auto document = GetProjectDocumentId();
    const auto recipe = std::make_shared<std::string>(ui.editedRecipe);
    const auto valid = [this, state, document, recipe] {
        return m_Bracketing == state && GetProjectDocumentId() == document && state->editedRecipe == *recipe;
    };
    N::NoticeSpec notice;
    notice.title = "Review capture orientations";
    notice.message = "Choose matching orientations for captures of the same scene.";
    notice.details = "This corrects orientation tags for the project. Original files stay unchanged.";
    notice.dialogSize = N::DialogSize::Large;
    notice.route = N::Route::Center;
    notice.foreground = m_NotificationForeground;
    notice.operationId = GetNotifier().NewOperation();
    notice.customBody = [this, state, valid] {
    if (!valid()) {
        ImGui::TextWrapped("The bracket changed. Cancel and review its current captures again.");
        return;
    }
    auto& ui = *state;
    const auto groups = OrientationGroups(ui);
    auto candidate = ui.recipe;
    candidate.orientationOverrides = ui.orientationChoices;
    int reference = -1;
    for (const auto& group : groups) for (const auto* source : group.second)
        if (source->frameId == ui.recipe.originFrameId)
            reference = Raw::Bracketing::ResolveOrientation(candidate, group.first);
    if (reference < 0 && !groups.empty()) reference = Raw::Bracketing::ResolveOrientation(candidate, groups.begin()->first);
    if (ImGui::Button("Match all to reference"))
        for (const auto& group : groups) ui.orientationChoices[group.first] = reference;
    for (const auto& group : groups) {
        ImGui::PushID(group.first);
        ImGui::Text("%s: %zu captures", OrientationName(group.first), group.second.size());
        int corrected = ui.orientationChoices.count(group.first) ? ui.orientationChoices[group.first] : group.first;
        if (corrected == 0) corrected = 1;
        ImGui::SetNextItemWidth(250);
        if (ImGui::BeginCombo("Use orientation", OrientationName(corrected))) {
            for (int value = 1; value <= 8; ++value)
                if (ImGui::Selectable(OrientationName(value), value == corrected)) {
                    corrected = value;
                    ui.orientationChoices[group.first] = value;
                }
            ImGui::EndCombo();
        }
        const auto* first = group.second.front();
        if (const auto* source = FindRawWorkspaceSourceByKey(first->sourceKey)) {
            int width = 0, height = 0;
            const auto texture = GetRawWorkspaceThumbnailTexture(*source, &width, &height, true);
            if (texture && width > 0 && height > 0) {
                // RAW gallery thumbnails are stored in sensor coordinates.
                // Apply the selected tag once, just as the result renderer does.
                if (corrected >= 5) std::swap(width, height);
                const float scale = std::min(200.0f / width, 125.0f / height);
                const ImVec2 size(width*scale, height*scale), min = ImGui::GetCursorScreenPos();
                ImGui::Dummy(size);
                const auto uv = [&](ImVec2 p) { return ToSensor(p, corrected); };
                ImGui::GetWindowDrawList()->AddImageQuad(static_cast<ImTextureID>(texture),
                    min, ImVec2(min.x+size.x,min.y), ImVec2(min.x+size.x,min.y+size.y), ImVec2(min.x,min.y+size.y),
                    uv({0,0}), uv({1,0}), uv({1,1}), uv({0,1}));
            }
        }
        if (ImGui::TreeNode("Captures")) {
            for (const auto* source : group.second)
                ImGui::TextUnformatted(source->path.filename().string().c_str());
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    if (!MatchingChoices(ui)) ImGui::TextWrapped("Choose matching orientations for all sets to continue.");
    };
    N::ActionSpec apply;
    apply.label = ui.processAfterOrientationReview ? "Apply and process" : "Apply and save";
    apply.canInvoke = [valid, state] { return valid() && MatchingChoices(*state); };
    apply.invoke = [this, state, recipe] {
        auto& ui = *state;
        ui.recipe.orientationOverrides = ui.orientationChoices;
        ui.redo.clear();
        CommitBracketingEdit();
        *recipe = ui.editedRecipe;
        std::string error;
        if (!CommitBracketingDraft(ui.processAfterOrientationReview, &error))
            return N::ActionResult::Failure(error.empty() ? "The bracket could not be saved or processed." : error);
        return N::ActionResult::Success();
    };
    N::ActionSpec cancel;
    cancel.label = "Cancel";
    cancel.safeCancel = true;
    cancel.invoke = [] { return N::ActionResult::Success(); };
    notice.actions = {std::move(apply), std::move(cancel)};
    RequestNotificationDecision(std::move(notice));
}
