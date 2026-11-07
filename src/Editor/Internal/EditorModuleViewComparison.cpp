#include "Editor/EditorModule.h"
#include "Editor/Bracketing/BracketingSession.h"
#include <cstdio>
#include "Hdr/Presentation.h"

void EditorModule::UpdateViewComparisonPreview(Stack::Editor::ViewComparisonPreview& preview, int edge) {
    preview.SetHdrOutput(IsHdrOutputPreview());
    const auto revision=m_Project->editRevision ^ (IsHdrOutputPreview() ? (std::uint64_t{1}<<63) : 0);
    if (m_Bracketing && IsMultiFrameRawProjectActive()) {
        const auto& bracket = *m_Bracketing;
        if (!bracket.result || !bracket.result->analysis || !bracket.result->raw) {
            preview.SetUnavailable("Prepared bracket originals are unavailable.");
            return;
        }
        std::vector<Stack::Editor::ViewComparisonPreview::Capture> captures;
        Raw::Bracketing::BracketingRecipe recipe;
        if (preview.NeedsBracketCatalog(bracket.result.get())) {
            // Inspection belongs to the accepted result, never a pending draft.
            std::string error;
            const auto serialized = nlohmann::json::parse(bracket.completedRecipe, nullptr, false);
            if (serialized.is_discarded() || !Raw::Bracketing::Deserialize(serialized, recipe, error, true)) {
                preview.SetUnavailable("The completed bracket's capture recipe is unavailable.");
                return;
            }
            for (const auto& group : recipe.groups) for (const auto& frame : group.frames) {
                Stack::Editor::ViewComparisonPreview::Capture capture;
                capture.id = frame.id;
                const auto label = bracket.frameLabels.find(frame.id);
                capture.label = label == bracket.frameLabels.end() ? frame.id : label->second;
                const auto original = bracket.result->analysis->originals.find(frame.id);
                if (original == bracket.result->analysis->originals.end() || !original->second)
                    capture.unavailable = "This capture was not prepared for the completed result.";
                const auto ev = bracket.result->analysis->calibratedEv.find(frame.id);
                if (ev != bracket.result->analysis->calibratedEv.end()) {
                    char exposure[48]; std::snprintf(exposure, sizeof(exposure), " / %+.2f EV", ev->second);
                    capture.label += exposure;
                }
                captures.push_back(std::move(capture));
            }
        }
        preview.UpdateBracket(bracket.result, recipe, captures, m_Project->rawRecipe,
            revision, edge);
    } else if (IsRawWorkspaceProjectActive()) {
        preview.UpdateRaw(m_Project->singleRawSource, m_Project->rawRecipe,
            revision, edge);
    } else {
        preview.SetUnavailable("The original source is unavailable.");
    }
}
