#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "imgui.h"

namespace StackAppearance {

inline constexpr const char* kFactoryPresetId = "premium-dark-studio";
inline constexpr const char* kDarkPresetId = "dark";
inline constexpr const char* kLightPresetId = "light";
inline constexpr const char* kSolarizedPresetId = "solarized";
inline constexpr const char* kSolarizedLightPresetId = "solarized-light";
inline constexpr std::uint32_t kAppearanceSettingsVersion = 2;
inline constexpr std::uint32_t kThemePresetFileVersion = 2;

struct ThemeStyleValues {
    float alpha = 1.0f;
    ImVec2 windowPadding = ImVec2(16.0f, 16.0f);
    ImVec2 framePadding = ImVec2(10.0f, 6.0f);
    ImVec2 cellPadding = ImVec2(8.0f, 6.0f);
    ImVec2 itemSpacing = ImVec2(10.0f, 8.0f);
    ImVec2 itemInnerSpacing = ImVec2(8.0f, 6.0f);
    float indentSpacing = 18.0f;
    float scrollbarSize = 12.0f;
    float grabMinSize = 10.0f;
    float windowBorderSize = 1.0f;
    float childBorderSize = 1.0f;
    float popupBorderSize = 1.0f;
    float frameBorderSize = 0.0f;
    float tabBorderSize = 0.0f;
    float windowRounding = 12.0f;
    float childRounding = 10.0f;
    float frameRounding = 8.0f;
    float popupRounding = 12.0f;
    float scrollbarRounding = 12.0f;
    float grabRounding = 8.0f;
    float tabRounding = 8.0f;
    ImVec2 windowTitleAlign = ImVec2(0.0f, 0.5f);
    ImVec2 buttonTextAlign = ImVec2(0.5f, 0.5f);
    ImVec2 selectableTextAlign = ImVec2(0.0f, 0.0f);
    bool antiAliasedLines = true;
    bool antiAliasedFill = true;
    float curveTessellationTol = 1.25f;
};

struct ThemeDefinition {
    std::string id = kFactoryPresetId;
    std::string displayName = "Dark";
    bool readOnly = false;
    std::array<ImVec4, ImGuiCol_COUNT> colors {};
    ThemeStyleValues style {};
    float textScale = 1.0f;
};

struct AppearanceLibrary {
    std::string activePresetId = kFactoryPresetId;
    std::vector<ThemeDefinition> customPresets;
};

std::vector<ThemeDefinition> MakeFactoryThemes();
ThemeDefinition MakeFactoryPremiumDarkStudioTheme();
ThemeDefinition CloneTheme(const ThemeDefinition& theme);
bool AreThemesEquivalent(const ThemeDefinition& lhs, const ThemeDefinition& rhs);
std::string MakePresetIdFromName(const std::string& displayName, const AppearanceLibrary& library);
std::string MakeUniquePresetName(const std::string& baseName, const AppearanceLibrary& library);
const ThemeDefinition* FindPresetById(const AppearanceLibrary& library, const std::string& presetId);
ThemeDefinition* FindPresetById(AppearanceLibrary& library, const std::string& presetId);

bool LoadAppearanceLibrary(AppearanceLibrary& outLibrary);
bool SaveAppearanceLibrary(const AppearanceLibrary& library);
bool LoadThemePresetFile(const std::filesystem::path& path, ThemeDefinition& outTheme, std::string* errorMessage = nullptr);
bool SaveThemePresetFile(const std::filesystem::path& path, const ThemeDefinition& theme, std::string* errorMessage = nullptr);

class AppearanceManager {
public:
    AppearanceManager();

    bool Load();
    bool Save() const;

    const ThemeDefinition& GetFactoryTheme() const;
    const std::vector<ThemeDefinition>& GetFactoryThemes() const;
    const ThemeDefinition& GetWorkingTheme() const;
    ThemeDefinition& EditWorkingTheme();
    const AppearanceLibrary& GetLibrary() const;

    const ThemeDefinition* GetPresetById(const std::string& presetId) const;
    const ThemeDefinition* GetActivePreset() const;
    const std::string& GetActivePresetId() const;
    std::string GetActivePresetDisplayName() const;
    bool ActivePresetIsFactory() const;
    bool HasUnsavedChanges() const;

    bool SelectPresetById(const std::string& presetId);
    bool ResetWorkingTheme();
    bool SaveWorkingTheme();
    bool SaveWorkingThemeAsNew(std::string displayName);
    bool DuplicateWorkingTheme();
    bool ImportPreset(const std::filesystem::path& path, std::string* errorMessage = nullptr);
    bool ExportWorkingTheme(const std::filesystem::path& path, std::string* errorMessage = nullptr) const;

    void ApplyCurrentTheme(ImGuiIO& io, ImGuiStyle& style) const;
    void SetupFonts(ImGuiIO& io) const;
    ImVec4 GetClearColor() const;

private:
    ThemeDefinition m_FactoryTheme;
    std::vector<ThemeDefinition> m_FactoryThemes;
    ThemeDefinition m_WorkingTheme;
    AppearanceLibrary m_Library;
};

} // namespace StackAppearance
