#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "imgui.h"
#include "Renderer/RenderTiling.h"

namespace StackAppearance {

inline constexpr const char* kFactoryPresetId = "premium-dark-studio";
inline constexpr const char* kDarkPresetId = "dark";
inline constexpr const char* kLightPresetId = "light";
inline constexpr const char* kSolarizedPresetId = "solarized";
inline constexpr const char* kSolarizedLightPresetId = "solarized-light";
inline constexpr const char* kYellowDarkPresetId = "yellow-dark";
inline constexpr const char* kYellowLightPresetId = "yellow-light";
inline constexpr std::uint32_t kAppearanceSettingsVersion = 7;
inline constexpr std::uint32_t kThemePresetFileVersion = 2;

enum class GraphVisualMode {
    Classic,
    BlackNodes,
    SpotlightPrototype
};

const char* GraphVisualModeLabel(GraphVisualMode mode);
const char* GraphVisualModeDescription(GraphVisualMode mode);

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

struct BackgroundImageEntry {
    std::string id;
    std::string displayName;
    std::string path;
};

struct AppearanceLibrary {
    std::string activePresetId = kSolarizedPresetId;
    GraphVisualMode graphVisualMode = GraphVisualMode::Classic;
    bool graphSpotlightHaloOutlines = false;
    bool graphDottedMaskLinks = false;
    float graphLineOpacity = 1.0f;
    ViewportTilingSettings viewportTiling;
    bool backgroundImageEnabled = false;
    std::string backgroundImagePath;
    float backgroundImageStrength = 0.58f;
    float uiSurfaceTransparency = 0.18f;
    std::vector<BackgroundImageEntry> backgroundImages;
    std::vector<ThemeDefinition> customPresets;
};

struct RuntimeSurfacePalette {
    ImVec4 appSurface = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 panelSurface = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 popupSurface = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 chromeSurface = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 drawerSurface = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 drawerSurfaceTransparent = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    ImVec4 border = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 separator = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 controlSurface = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 controlSurfaceHovered = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    ImVec4 controlSurfaceActive = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
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
bool UseDarkIconsForCurrentTheme(const class AppearanceManager* appearance);
ImU32 ResolveThemedMonochromeIconTint(const class AppearanceManager* appearance, bool emphasized, bool hovered);

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
    GraphVisualMode GetGraphVisualMode() const;
    bool GetGraphSpotlightHaloOutlines() const;
    bool GetGraphDottedMaskLinks() const;
    float GetGraphLineOpacity() const;
    const ViewportTilingSettings& GetViewportTilingSettings() const;
    bool GetBackgroundImageEnabled() const;
    bool GetSeamlessSurfaceStylingEnabled() const;
    const std::string& GetBackgroundImagePath() const;
    const std::vector<BackgroundImageEntry>& GetBackgroundImages() const;
    float GetBackgroundImageStrength() const;
    float GetUiSurfaceTransparency() const;
    float GetUiSurfaceAlphaMultiplier() const;
    RuntimeSurfacePalette GetRuntimeSurfacePalette() const;
    std::uint64_t GetRevision() const;
    std::uint64_t GetBackgroundImageRevision() const;
    const std::string& GetBackgroundImageRuntimeStatus() const;
    std::filesystem::path GetResolvedBackgroundImagePath() const;
    ImVec4 GetEffectiveWindowBackgroundColor() const;
    ImVec4 GetEffectiveChildBackgroundColor() const;
    ImVec4 GetEffectivePopupBackgroundColor() const;
    bool ActivePresetIsFactory() const;
    bool HasUnsavedChanges() const;

    bool SelectPresetById(const std::string& presetId);
    bool SetGraphVisualMode(GraphVisualMode mode);
    bool SetGraphSpotlightHaloOutlines(bool enabled);
    bool SetGraphDottedMaskLinks(bool enabled);
    bool SetGraphLineOpacity(float opacity);
    bool SetViewportTilingSettings(const ViewportTilingSettings& settings);
    bool SetBackgroundImageEnabled(bool enabled);
    bool SetBackgroundImageStrength(float strength);
    bool SetUiSurfaceTransparency(float transparency);
    bool ImportBackgroundImageFromPath(const std::filesystem::path& sourcePath, std::string* errorMessage = nullptr);
    bool SelectBackgroundImageById(const std::string& imageId);
    bool RemoveBackgroundImageById(const std::string& imageId, std::string* errorMessage = nullptr);
    bool ClearBackgroundImage(std::string* errorMessage = nullptr);
    void SetBackgroundImageRuntimeStatus(std::string statusMessage);
    bool UpdateThemeTransition(double nowSeconds);
    bool IsThemeTransitionActive() const;
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
    void TouchRevision();
    void StartThemeTransition(const ThemeDefinition& targetTheme, double nowSeconds);
    void FinishThemeTransition();

    ThemeDefinition m_FactoryTheme;
    std::vector<ThemeDefinition> m_FactoryThemes;
    ThemeDefinition m_WorkingTheme;
    AppearanceLibrary m_Library;
    std::uint64_t m_Revision = 1;
    std::uint64_t m_BackgroundImageRevision = 0;
    std::string m_BackgroundImageRuntimeStatus;
    bool m_ThemeTransitionActive = false;
    double m_ThemeTransitionStartTime = 0.0;
    double m_ThemeTransitionDuration = 0.32;
    ThemeDefinition m_ThemeTransitionFrom;
    ThemeDefinition m_ThemeTransitionTo;
};

} // namespace StackAppearance
