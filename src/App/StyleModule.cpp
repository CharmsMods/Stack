#include "StyleModule.h"
#include "imgui.h"
#include <algorithm>
#include <vector>
#include <string>

StyleModule::StyleModule() = default;

void StyleModule::Initialize(StackAppearance::AppearanceManager* appearanceManager) {
    m_Appearance = appearanceManager;
}

void StyleModule::Shutdown() {
    m_Appearance = nullptr;
}

void StyleModule::ApplyCurrentTheme() {
    if (m_Appearance == nullptr) {
        return;
    }
    m_Appearance->ApplyCurrentTheme(ImGui::GetIO(), ImGui::GetStyle());
}

void StyleModule::RenderUI() {
    if (m_Appearance == nullptr) {
        ImGui::TextDisabled("Appearance manager is not available.");
        return;
    }

    // Outer Container for elegant breathing room and side-of-screen spacing
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(48.0f, 36.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0)); // Transparent container
    ImGui::BeginChild("StyleTabContainer", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // Top spacer
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    // Dashboard Header
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::TextUnformatted("STYLE & VISUAL THEMES");
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted("Select an official system theme below to customize your creative studio environment.");
    ImGui::PopStyleColor();

    ImGui::Dummy(ImVec2(0.0f, 32.0f)); // Generous spacer before cards

    // Layout variables
    float availWidth = ImGui::GetContentRegionAvail().x;
    float maxDashboardWidth = 880.0f;
    float startOffsetX = 0.0f;

    if (availWidth > maxDashboardWidth) {
        startOffsetX = (availWidth - maxDashboardWidth) * 0.5f;
        availWidth = maxDashboardWidth;
    }

    const float cardWidth = (availWidth - 24.0f) * 0.5f;
    const float cardHeight = 145.0f; // Airy, spacious card layout

    const std::vector<StackAppearance::ThemeDefinition>& themes = m_Appearance->GetFactoryThemes();
    const std::string activePresetId = m_Appearance->GetActivePresetId();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Iterate through factory themes in a 2x2 grid
    for (size_t i = 0; i < themes.size(); ++i) {
        const auto& theme = themes[i];

        // Hardcode reliable theme descriptions
        const char* desc = "";
        if (theme.id == "premium-dark-studio") {
            desc = "Sleek low-fatigue slate dark mode designed for professional production.";
        } else if (theme.id == "light") {
            desc = "Crisp, clean layout calibrated for high-visibility bright workspaces.";
        } else if (theme.id == "solarized") {
            desc = "Warm retro-inspired solarized dark mode offering superb visual comfort.";
        } else if (theme.id == "solarized-light") {
            desc = "Gentle cream-toned paper theme with soft contrast and excellent legibility.";
        } else if (theme.id == "yellow-dark") {
            desc = "A bold dark theme with a vibrant yellow accent for focused creativity.";
        } else if (theme.id == "yellow-light") {
            desc = "A bright, energetic light theme accented with vivid yellow.";
        }

        // Horizontal positioning
        if (i > 0 && i % 2 != 0) {
            ImGui::SameLine(0.0f, 24.0f);
        } else {
            if (i > 0 && i % 2 == 0) {
                ImGui::Dummy(ImVec2(0.0f, 24.0f)); // Row gutter spacing
            }
            if (startOffsetX > 0.0f) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startOffsetX);
            }
        }

        ImGui::PushID(theme.id.c_str());
        const bool isActive = activePresetId == theme.id;

        // Custom premium borders and backgrounds for cards
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, isActive ? 2.0f : 1.0f);

        ImVec4 cardBgColor = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
        
        // Dynamic border glow highlight
        ImVec4 borderColor = isActive
            ? ImGui::GetStyleColorVec4(ImGuiCol_CheckMark)
            : ImVec4(1.0f, 1.0f, 1.0f, 0.12f);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, cardBgColor);
        ImGui::PushStyleColor(ImGuiCol_Border, borderColor);

        ImGui::BeginChild(theme.id.c_str(), ImVec2(cardWidth, cardHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);

        // Header Title
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
        }
        ImGui::TextUnformatted(theme.displayName.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        // Description
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("%s", desc);
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        // Row of circular color swatches
        ImVec2 swatchesStart = ImGui::GetCursorScreenPos();
        constexpr float circleRadius = 7.0f;
        constexpr float spacingX = 20.0f;
        const ImGuiCol swatchCols[] = { ImGuiCol_WindowBg, ImGuiCol_ChildBg, ImGuiCol_FrameBg, ImGuiCol_Header, ImGuiCol_ButtonActive };
        for (int c = 0; c < 5; ++c) {
            ImVec4 col = theme.colors[swatchCols[c]];
            ImVec2 center(swatchesStart.x + circleRadius + c * spacingX, swatchesStart.y + circleRadius + 2.0f);
            drawList->AddCircleFilled(center, circleRadius, ImGui::ColorConvertFloat4ToU32(col));
            drawList->AddCircle(center, circleRadius, IM_COL32(0, 0, 0, 35), 0, 1.0f);
        }

        // Active State Badge at bottom right of card
        const float activeTextWidth = isActive ? ImGui::CalcTextSize("[ Selected ]").x : ImGui::CalcTextSize("Click to apply").x;
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - activeTextWidth - 4.0f);
        
        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
            ImGui::TextUnformatted("[ Selected ]");
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted("Click to apply");
            ImGui::PopStyleColor();
        }

        // Detect mouse hover and click to activate card
        const bool hovered = ImGui::IsWindowHovered();
        ImGui::EndChild();

        if (hovered && ImGui::IsMouseClicked(0)) {
            m_Appearance->SelectPresetById(theme.id);
            ApplyCurrentTheme();
        }

        // Draw dynamic hover outline glow if hovered and not active
        if (hovered && !isActive) {
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();
            ImU32 hoverGlow = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
            drawList->AddRect(min, max, hoverGlow, 12.0f, 0, 1.5f);
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        ImGui::PopID();
    }

    ImGui::EndChild(); // StyleTabContainer
}
