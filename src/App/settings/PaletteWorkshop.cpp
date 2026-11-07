#include "PrimaryAction.h"
#include "PaletteWorkshop.h"
#include "AppearanceTheme.h"
#include <array>
#include <string>

namespace StackAppearance {
void RenderPaletteWorkshop(AppearanceManager& appearance, PaletteWorkshopState& state,
    const Stack::Notifications::Notifier& notifier) {
    ImGui::TextUnformatted("Theme brightness");
    ImGui::TextWrapped("One neutral theme, from pitch black to gray. Floating controls stay slightly raised.");
    auto& error = state.error;
    const auto save = [&](const std::string& variant = std::string{}) {
        const bool saved = appearance.SaveCreamPalette(variant);
        if (saved) {
            error.clear();
            notifier.Resolve(state.saveError);
            state.saveError = 0;
            Stack::Notifications::NoticeSpec notice;
            notice.title = variant.empty() ? "Palette saved" : "Palette variant saved";
            notice.message = "Appearance updated.";
            notice.severity = Stack::Notifications::Severity::Success;
            notice.outcome = Stack::Notifications::Outcome::Success;
            notice.operationId = notifier.NewOperation();
            notifier.Post(std::move(notice));
        } else {
            error = "Could not save the palette.";
            Stack::Notifications::NoticeSpec notice;
            notice.title = "Palette was not saved";
            notice.message = error;
            notice.severity = Stack::Notifications::Severity::Error;
            notice.outcome = Stack::Notifications::Outcome::Failure;
            notice.dedupeKey = "settings-palette-save";
            state.saveError = notifier.Post(std::move(notice));
        }
        return saved;
    };
    float brightness=appearance.GetCreamPalette().brightness * 100.f;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::SliderFloat("##ThemeBrightness", &brightness, 0.f, 100.f, "Brightness %.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
        auto working=appearance.GetCreamPalette();
        working.brightness=brightness / 100.f;
        appearance.PreviewCreamPalette(working);
    }
    if (ImGui::Button("Reset brightness")) {
        auto working=appearance.GetCreamPalette();
        working.brightness=CreamPalette{}.brightness;
        appearance.PreviewCreamPalette(working);
    }
    if (appearance.HasCreamPaletteChanges()) ImGui::TextUnformatted("Preview has unsaved changes.");
    if (PrimaryActionButton("Save",ImVec2(0,0),&appearance.GetResolvedCreamPalette().primaryAction)) save();
    ImGui::SameLine();
    if (ImGui::Button("Revert")) { appearance.RevertCreamPalette(); error.clear(); }
    if (!error.empty()) ImGui::TextWrapped("%s",error.c_str());
}
}
