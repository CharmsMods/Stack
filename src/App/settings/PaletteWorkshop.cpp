#include "PrimaryAction.h"
#include "PaletteWorkshop.h"
#include "AppearanceTheme.h"
#include <array>
#include <string>

namespace StackAppearance {
void RenderPaletteWorkshop(AppearanceManager& appearance, PaletteWorkshopState& state,
    const Stack::Notifications::Notifier& notifier) {
    ImGui::TextUnformatted("Appearance palette");
    ImGui::TextWrapped("Choose a built-in appearance or make a custom color variant.");
    if (!ImGui::CollapsingHeader("Palette workshop", ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto& error = state.error;
    auto& name = state.name;
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
    CreamPalette working=appearance.GetCreamPalette();
    if (ImGui::BeginCombo("Starting mix",working.name.c_str())) {
        auto choice=[&](const CreamPalette& p) {
            if (ImGui::Selectable(p.name.c_str(),p.id==working.id)) appearance.PreviewCreamPalette(p);
        };
        for (const auto& p : FactoryCreamPalettes()) choice(p);
        for (const auto& p : appearance.GetLibrary().creamVariants) { ImGui::PushID(p.id.c_str()); choice(p); ImGui::PopID(); }
        ImGui::EndCombo();
    }
    working=appearance.GetCreamPalette();
    bool changed=false;
    const bool monochrome=working.colorPolicy==PaletteColorPolicy::Monochrome;
    if (monochrome) {
        ImGui::TextWrapped("This built-in theme uses a fixed grayscale system across application chrome and controls.");
    } else {
        ImGui::TextUnformatted("Node accents");
        const char* accents[]{"Dusty blue","Retro teal","Olive green"};
        int numberAccent=static_cast<int>(working.numberAccent), resetAccent=static_cast<int>(working.resetAccent);
        if (ImGui::Combo("Number color",&numberAccent,accents,3)) { working.numberAccent=static_cast<NodeAccent>(numberAccent); changed=true; }
        if (ImGui::Combo("Reset accent",&resetAccent,accents,3)) { working.resetAccent=static_cast<NodeAccent>(resetAccent); changed=true; }
        ImGui::TextWrapped("Preview these colors on all nodes. Reset arrows use their accent on hover and focus.");
        ImGui::TextUnformatted("Foundation");
        for (size_t i : {size_t(0),size_t(1),size_t(4),size_t(6)})
            changed |= ImGui::ColorEdit3(CreamSeedNames()[i],&working.seeds[i].x,ImGuiColorEditFlags_NoAlpha);
        ImGui::Spacing(); ImGui::TextUnformatted("Accent collection");
        for (size_t i : {size_t(7),size_t(8),size_t(5),size_t(2),size_t(3)})
            changed |= ImGui::ColorEdit3(CreamSeedNames()[i],&working.seeds[i].x,ImGuiColorEditFlags_NoAlpha);
    }
    if (changed) appearance.PreviewCreamPalette(working);
    if (!CreamPaletteHasReadableText(appearance.GetResolvedCreamPalette()))
        ImGui::TextWrapped("Low text contrast. Some labels may be difficult to read with these colors.");
    if (appearance.HasCreamPaletteChanges()) ImGui::TextUnformatted("Preview has unsaved changes.");
    if (PrimaryActionButton("Save",ImVec2(0,0),&appearance.GetResolvedCreamPalette().primaryAction)) save();
    ImGui::SameLine();
    if (ImGui::Button("Revert")) { appearance.RevertCreamPalette(); error.clear(); }
    if (!monochrome && ImGui::Button("Save as variant")) { name.fill(0); ImGui::OpenPopup("Name cream variant"); }
    if (ImGui::BeginPopup("Name cream variant")) {
        ImGui::InputText("Name",name.data(),name.size());
        const std::string requested(name.data());
        const bool empty=requested.find_first_not_of(" \t\r\n")==std::string::npos;
        ImGui::BeginDisabled(empty);
        if (PrimaryActionButton("Save variant",ImVec2(0,0),&appearance.GetResolvedCreamPalette().primaryAction)) {
            if (save(requested)) ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    if (!error.empty()) ImGui::TextWrapped("%s",error.c_str());
}
}
