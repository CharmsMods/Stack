#include "AppearanceTheme.h"

#include "App/AppPaths.h"
#include "Composite/EmbeddedCompositeFont.h"
#include "Persistence/StackBinaryFormat.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_set>

namespace StackAppearance {
namespace {

using json = StackBinaryFormat::json;

constexpr const char* kVersionKey = "version";
constexpr const char* kAppearanceKey = "appearance";
constexpr const char* kActivePresetIdKey = "activePresetId";
constexpr const char* kPresetsKey = "presets";
constexpr const char* kPresetKey = "preset";
constexpr const char* kIdKey = "id";
constexpr const char* kNameKey = "name";
constexpr const char* kThemeKey = "theme";
constexpr const char* kLegacyThemeKey = "themePreset";
constexpr const char* kGraphVisualModeKey = "graphVisualMode";
constexpr const char* kGraphSpotlightHaloOutlinesKey = "graphSpotlightHaloOutlines";
constexpr const char* kGraphDottedMaskLinksKey = "graphDottedMaskLinks";
constexpr const char* kGraphLineOpacityKey = "graphLineOpacity";
constexpr const char* kViewportTilingKey = "viewportTiling";
constexpr const char* kViewportTilingModeKey = "mode";
constexpr const char* kViewportTilingTileSizeKey = "tileSize";
constexpr const char* kViewportTilingHaloPixelsKey = "haloPixels";
constexpr const char* kViewportTilingAutoThresholdKey = "autoPixelThresholdMegapixels";
constexpr const char* kViewportTilingProgressiveKey = "progressive";
constexpr const char* kViewportTilingDebugOverlayKey = "debugOverlay";
constexpr const char* kBackgroundImageEnabledKey = "backgroundImageEnabled";
constexpr const char* kBackgroundImagePathKey = "backgroundImagePath";
constexpr const char* kBackgroundImagesKey = "backgroundImages";
constexpr const char* kBackgroundImageStrengthKey = "backgroundImageStrength";
constexpr const char* kUiSurfaceTransparencyKey = "uiSurfaceTransparency";
constexpr const char* kFactoryThemeToken = "premium-dark-studio";
constexpr const char* kDefaultPresetToken = kSolarizedPresetId;
constexpr const char* kManagedBackgroundImageBaseName = "StackBackgroundImage";
constexpr float kMinTextScale = 0.75f;
constexpr float kMaxTextScale = 1.60f;

std::string MakeBackgroundImageIdFromPath(const std::string& storedPath);

ImVec4 MakeColor(float r, float g, float b, float a = 1.0f) {
    return ImVec4(r, g, b, a);
}

ImVec4 Blend(const ImVec4& from, const ImVec4& to, float t) {
    const float clampedT = std::clamp(t, 0.0f, 1.0f);
    return ImVec4(
        from.x + (to.x - from.x) * clampedT,
        from.y + (to.y - from.y) * clampedT,
        from.z + (to.z - from.z) * clampedT,
        from.w + (to.w - from.w) * clampedT);
}

float ClampUnit(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

ImVec4 ApplyAlphaMultiplier(const ImVec4& color, float multiplier) {
    ImVec4 tinted = color;
    tinted.w *= std::clamp(multiplier, 0.0f, 1.0f);
    return tinted;
}

float Luminance(const ImVec4& color) {
    return (0.2126f * color.x) + (0.7152f * color.y) + (0.0722f * color.z);
}

float ComputeSurfaceAlphaMultiplier(float transparency) {
    return std::clamp(0.18f + ((1.0f - ClampUnit(transparency)) * 0.28f), 0.18f, 0.46f);
}

float ComputePopupAlphaMultiplier(float transparency) {
    return std::clamp(ComputeSurfaceAlphaMultiplier(transparency) + 0.14f, 0.28f, 0.62f);
}

RuntimeSurfacePalette BuildRuntimeSurfacePalette(const ThemeDefinition& theme, float transparency, bool seamlessSurfaceStylingEnabled) {
    RuntimeSurfacePalette palette;

    if (!seamlessSurfaceStylingEnabled) {
        palette.appSurface = theme.colors[ImGuiCol_WindowBg];
        palette.panelSurface = theme.colors[ImGuiCol_ChildBg];
        palette.popupSurface = theme.colors[ImGuiCol_PopupBg];
        palette.chromeSurface = theme.colors[ImGuiCol_Header];
        palette.drawerSurface = theme.colors[ImGuiCol_ChildBg];
        palette.drawerSurfaceTransparent = palette.drawerSurface;
        palette.drawerSurfaceTransparent.w = 0.0f;
        palette.border = theme.colors[ImGuiCol_Border];
        palette.separator = theme.colors[ImGuiCol_Separator];
        palette.controlSurface = theme.colors[ImGuiCol_FrameBg];
        palette.controlSurfaceHovered = theme.colors[ImGuiCol_FrameBgHovered];
        palette.controlSurfaceActive = theme.colors[ImGuiCol_FrameBgActive];
        return palette;
    }

    const float surfaceAlpha = ComputeSurfaceAlphaMultiplier(transparency);
    const float popupAlpha = ComputePopupAlphaMultiplier(transparency);
    const ImVec4& windowBg = theme.colors[ImGuiCol_WindowBg];
    const ImVec4& childBg = theme.colors[ImGuiCol_ChildBg];
    const ImVec4& popupBg = theme.colors[ImGuiCol_PopupBg];
    const ImVec4& header = theme.colors[ImGuiCol_Header];
    const ImVec4& border = theme.colors[ImGuiCol_Border];
    const ImVec4& separator = theme.colors[ImGuiCol_Separator];
    const ImVec4& frameBg = theme.colors[ImGuiCol_FrameBg];
    const ImVec4& frameBgHovered = theme.colors[ImGuiCol_FrameBgHovered];
    const ImVec4& frameBgActive = theme.colors[ImGuiCol_FrameBgActive];
    const bool isLightTheme = Luminance(windowBg) >= 0.5f;
    const ImVec4 baseSurface = Blend(windowBg, childBg, isLightTheme ? 0.14f : 0.22f);
    const float controlAlpha = std::clamp(surfaceAlpha + 0.08f, 0.0f, 1.0f);
    const float drawerAlpha = std::clamp(surfaceAlpha + 0.08f, 0.0f, 1.0f);
    const ImVec4 sharedSurface = ApplyAlphaMultiplier(baseSurface, surfaceAlpha);

    palette.appSurface = sharedSurface;
    palette.panelSurface = sharedSurface;
    palette.chromeSurface = sharedSurface;
    palette.drawerSurface = ApplyAlphaMultiplier(Blend(baseSurface, popupBg, isLightTheme ? 0.04f : 0.08f), drawerAlpha);
    palette.drawerSurfaceTransparent = palette.drawerSurface;
    palette.drawerSurfaceTransparent.w = 0.0f;
    palette.popupSurface = ApplyAlphaMultiplier(Blend(popupBg, baseSurface, 0.22f), popupAlpha);
    palette.border = ApplyAlphaMultiplier(border, std::clamp(surfaceAlpha * 0.58f, 0.10f, 0.34f));
    palette.separator = ApplyAlphaMultiplier(separator, std::clamp(surfaceAlpha * 0.42f, 0.08f, 0.28f));
    palette.controlSurface = ApplyAlphaMultiplier(Blend(frameBg, baseSurface, 0.18f), controlAlpha);
    palette.controlSurfaceHovered = ApplyAlphaMultiplier(Blend(frameBgHovered, header, 0.16f), std::clamp(controlAlpha + 0.05f, 0.0f, 1.0f));
    palette.controlSurfaceActive = ApplyAlphaMultiplier(Blend(frameBgActive, header, 0.14f), std::clamp(controlAlpha + 0.08f, 0.0f, 1.0f));
    return palette;
}

ImVec2 Blend(const ImVec2& from, const ImVec2& to, float t) {
    const float clampedT = std::clamp(t, 0.0f, 1.0f);
    return ImVec2(
        from.x + (to.x - from.x) * clampedT,
        from.y + (to.y - from.y) * clampedT);
}

const char* GraphVisualModeToString(GraphVisualMode mode) {
    switch (mode) {
        case GraphVisualMode::Classic: return "Classic";
        case GraphVisualMode::BlackNodes: return "BlackNodes";
        case GraphVisualMode::SpotlightPrototype: return "SpotlightPrototype";
    }
    return "Classic";
}

GraphVisualMode GraphVisualModeFromString(const std::string& value) {
    if (value == "BlackNodes" || value == "Black Nodes") {
        return GraphVisualMode::BlackNodes;
    }
    if (value == "SpotlightPrototype" ||
        value == "Soft Spotlight Prototype" ||
        value == "FuturistGlassPrototype" ||
        value == "Futurist Glass Prototype") {
        return GraphVisualMode::SpotlightPrototype;
    }
    if (value == "Classic") {
        return GraphVisualMode::Classic;
    }
    return GraphVisualMode::Classic;
}

json WriteVec2(const ImVec2& value) {
    return json::array({ value.x, value.y });
}

json WriteVec4(const ImVec4& value) {
    return json::array({ value.x, value.y, value.z, value.w });
}

bool ReadVec2(const json& value, ImVec2& outValue) {
    if (!value.is_array() || value.size() < 2) {
        return false;
    }

    outValue.x = value[0].get<float>();
    outValue.y = value[1].get<float>();
    return true;
}

bool ReadVec4(const json& value, ImVec4& outValue) {
    if (!value.is_array() || value.size() < 4) {
        return false;
    }

    outValue.x = value[0].get<float>();
    outValue.y = value[1].get<float>();
    outValue.z = value[2].get<float>();
    outValue.w = value[3].get<float>();
    return true;
}

json WriteColorArray(const std::array<ImVec4, ImGuiCol_COUNT>& colors) {
    json result = json::array();
    for (const ImVec4& color : colors) {
        result.push_back(WriteVec4(color));
    }
    return result;
}

void ReadColorArray(
    const json& value,
    std::array<ImVec4, ImGuiCol_COUNT>& outColors,
    const std::array<ImVec4, ImGuiCol_COUNT>& fallbackColors) {
    outColors = fallbackColors;
    if (!value.is_array()) {
        return;
    }

    const std::size_t count = std::min<std::size_t>(value.size(), outColors.size());
    for (std::size_t index = 0; index < count; ++index) {
        ImVec4 color = outColors[index];
        if (ReadVec4(value[index], color)) {
            outColors[index] = color;
        }
    }
}

json ThemeStyleToJson(const ThemeStyleValues& style) {
    json result = json::object();
    result["alpha"] = style.alpha;
    result["windowPadding"] = WriteVec2(style.windowPadding);
    result["framePadding"] = WriteVec2(style.framePadding);
    result["cellPadding"] = WriteVec2(style.cellPadding);
    result["itemSpacing"] = WriteVec2(style.itemSpacing);
    result["itemInnerSpacing"] = WriteVec2(style.itemInnerSpacing);
    result["indentSpacing"] = style.indentSpacing;
    result["scrollbarSize"] = style.scrollbarSize;
    result["grabMinSize"] = style.grabMinSize;
    result["windowBorderSize"] = style.windowBorderSize;
    result["childBorderSize"] = style.childBorderSize;
    result["popupBorderSize"] = style.popupBorderSize;
    result["frameBorderSize"] = style.frameBorderSize;
    result["tabBorderSize"] = style.tabBorderSize;
    result["windowRounding"] = style.windowRounding;
    result["childRounding"] = style.childRounding;
    result["frameRounding"] = style.frameRounding;
    result["popupRounding"] = style.popupRounding;
    result["scrollbarRounding"] = style.scrollbarRounding;
    result["grabRounding"] = style.grabRounding;
    result["tabRounding"] = style.tabRounding;
    result["windowTitleAlign"] = WriteVec2(style.windowTitleAlign);
    result["buttonTextAlign"] = WriteVec2(style.buttonTextAlign);
    result["selectableTextAlign"] = WriteVec2(style.selectableTextAlign);
    result["antiAliasedLines"] = style.antiAliasedLines;
    result["antiAliasedFill"] = style.antiAliasedFill;
    result["curveTessellationTol"] = style.curveTessellationTol;
    return result;
}

ThemeStyleValues ThemeStyleFromJson(const json& value, const ThemeStyleValues& fallback) {
    ThemeStyleValues style = fallback;
    if (!value.is_object()) {
        return style;
    }

    style.alpha = value.value("alpha", style.alpha);
    ReadVec2(value.value("windowPadding", WriteVec2(style.windowPadding)), style.windowPadding);
    ReadVec2(value.value("framePadding", WriteVec2(style.framePadding)), style.framePadding);
    ReadVec2(value.value("cellPadding", WriteVec2(style.cellPadding)), style.cellPadding);
    ReadVec2(value.value("itemSpacing", WriteVec2(style.itemSpacing)), style.itemSpacing);
    ReadVec2(value.value("itemInnerSpacing", WriteVec2(style.itemInnerSpacing)), style.itemInnerSpacing);
    style.indentSpacing = value.value("indentSpacing", style.indentSpacing);
    style.scrollbarSize = value.value("scrollbarSize", style.scrollbarSize);
    style.grabMinSize = value.value("grabMinSize", style.grabMinSize);
    style.windowBorderSize = value.value("windowBorderSize", style.windowBorderSize);
    style.childBorderSize = value.value("childBorderSize", style.childBorderSize);
    style.popupBorderSize = value.value("popupBorderSize", style.popupBorderSize);
    style.frameBorderSize = value.value("frameBorderSize", style.frameBorderSize);
    style.tabBorderSize = value.value("tabBorderSize", style.tabBorderSize);
    style.windowRounding = value.value("windowRounding", style.windowRounding);
    style.childRounding = value.value("childRounding", style.childRounding);
    style.frameRounding = value.value("frameRounding", style.frameRounding);
    style.popupRounding = value.value("popupRounding", style.popupRounding);
    style.scrollbarRounding = value.value("scrollbarRounding", style.scrollbarRounding);
    style.grabRounding = value.value("grabRounding", style.grabRounding);
    style.tabRounding = value.value("tabRounding", style.tabRounding);
    ReadVec2(value.value("windowTitleAlign", WriteVec2(style.windowTitleAlign)), style.windowTitleAlign);
    ReadVec2(value.value("buttonTextAlign", WriteVec2(style.buttonTextAlign)), style.buttonTextAlign);
    ReadVec2(value.value("selectableTextAlign", WriteVec2(style.selectableTextAlign)), style.selectableTextAlign);
    style.antiAliasedLines = value.value("antiAliasedLines", style.antiAliasedLines);
    style.antiAliasedFill = value.value("antiAliasedFill", style.antiAliasedFill);
    style.curveTessellationTol = value.value("curveTessellationTol", style.curveTessellationTol);
    return style;
}

json ThemeDefinitionToJson(const ThemeDefinition& theme) {
    json result = json::object();
    result[kIdKey] = theme.id;
    result[kNameKey] = theme.displayName;
    result[kThemeKey] = json::object();
    result[kThemeKey]["textScale"] = theme.textScale;
    result[kThemeKey]["colors"] = WriteColorArray(theme.colors);
    result[kThemeKey]["style"] = ThemeStyleToJson(theme.style);
    return result;
}

json BackgroundImageEntryToJson(const BackgroundImageEntry& entry) {
    json result = json::object();
    result[kIdKey] = entry.id;
    result[kNameKey] = entry.displayName;
    result[kBackgroundImagePathKey] = entry.path;
    return result;
}

bool BackgroundImageEntryFromJson(const json& value, BackgroundImageEntry& outEntry) {
    if (!value.is_object()) {
        return false;
    }
    outEntry.id = value.value(kIdKey, std::string());
    outEntry.displayName = value.value(kNameKey, std::string());
    outEntry.path = value.value(kBackgroundImagePathKey, std::string());
    if (outEntry.path.empty()) {
        return false;
    }
    if (outEntry.id.empty()) {
        outEntry.id = MakeBackgroundImageIdFromPath(outEntry.path);
    }
    if (outEntry.displayName.empty()) {
        outEntry.displayName = std::filesystem::path(outEntry.path).stem().string();
    }
    return true;
}

ThemeDefinition ThemeDefinitionFromJson(
    const json& value,
    const ThemeDefinition& fallback,
    std::string* errorMessage = nullptr) {
    ThemeDefinition theme = fallback;
    if (!value.is_object()) {
        if (errorMessage) {
            *errorMessage = "Theme preset payload is not an object.";
        }
        return theme;
    }

    theme.id = value.value(kIdKey, theme.id);
    theme.displayName = value.value(kNameKey, theme.displayName);
    if (value.contains("readOnly")) {
        theme.readOnly = value.value("readOnly", theme.readOnly);
    }

    const json* themeNode = nullptr;
    if (value.contains(kThemeKey) && value[kThemeKey].is_object()) {
        themeNode = &value[kThemeKey];
    } else {
        themeNode = &value;
    }

    if (themeNode->contains("textScale")) {
        theme.textScale = themeNode->value("textScale", theme.textScale);
    }
    if (themeNode->contains("colors")) {
        ReadColorArray((*themeNode)["colors"], theme.colors, fallback.colors);
    }
    if (themeNode->contains("style") && (*themeNode)["style"].is_object()) {
        theme.style = ThemeStyleFromJson((*themeNode)["style"], fallback.style);
    } else {
        theme.style = fallback.style;
    }

    if (theme.id.empty()) {
        theme.id = fallback.id;
    }
    if (theme.displayName.empty()) {
        theme.displayName = fallback.displayName;
    }
    theme.textScale = std::clamp(theme.textScale, kMinTextScale, kMaxTextScale);
    return theme;
}

std::string NormalizeName(const std::string& name) {
    std::string result;
    result.reserve(name.size());
    bool lastWasSpace = false;
    for (unsigned char ch : name) {
        if (std::isalnum(ch)) {
            result.push_back(static_cast<char>(std::tolower(ch)));
            lastWasSpace = false;
        } else if (!lastWasSpace) {
            result.push_back('-');
            lastWasSpace = true;
        }
    }

    while (!result.empty() && result.front() == '-') {
        result.erase(result.begin());
    }
    while (!result.empty() && result.back() == '-') {
        result.pop_back();
    }
    if (result.empty()) {
        result = "preset";
    }
    return result;
}

std::unordered_set<std::string> CollectPresetIds(const AppearanceLibrary& library) {
    std::unordered_set<std::string> ids;
    for (const ThemeDefinition& preset : MakeFactoryThemes()) {
        if (!preset.id.empty()) {
            ids.insert(preset.id);
        }
    }
    for (const ThemeDefinition& preset : library.customPresets) {
        if (!preset.id.empty()) {
            ids.insert(preset.id);
        }
    }
    return ids;
}

std::unordered_set<std::string> CollectPresetNames(const AppearanceLibrary& library) {
    std::unordered_set<std::string> names;
    for (const ThemeDefinition& preset : MakeFactoryThemes()) {
        if (!preset.displayName.empty()) {
            names.insert(preset.displayName);
        }
    }
    for (const ThemeDefinition& preset : library.customPresets) {
        if (!preset.displayName.empty()) {
            names.insert(preset.displayName);
        }
    }
    return names;
}

std::string MakeUniqueId(const std::string& baseId, const std::unordered_set<std::string>& usedIds) {
    std::string candidate = baseId.empty() ? std::string("preset") : baseId;
    if (usedIds.find(candidate) == usedIds.end()) {
        return candidate;
    }

    for (int suffix = 2; suffix < 1000; ++suffix) {
        const std::string attempt = candidate + "-" + std::to_string(suffix);
        if (usedIds.find(attempt) == usedIds.end()) {
            return attempt;
        }
    }

    return candidate + "-generated";
}

std::string MakeUniqueDisplayName(const std::string& baseName, const std::unordered_set<std::string>& usedNames) {
    std::string candidate = baseName.empty() ? std::string("Custom Theme") : baseName;
    if (usedNames.find(candidate) == usedNames.end()) {
        return candidate;
    }

    for (int suffix = 2; suffix < 1000; ++suffix) {
        const std::string attempt = candidate + " " + std::to_string(suffix);
        if (usedNames.find(attempt) == usedNames.end()) {
            return attempt;
        }
    }

    return candidate + " Copy";
}

std::filesystem::path GetSettingsPath() {
    return AppPaths::GetSettingsFilePath();
}

std::filesystem::path GetWorkingDirectoryPath() {
    return AppPaths::GetSettingsDirectory();
}

std::string NormalizeExtension(std::string extension) {
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (extension.empty()) {
        extension = ".png";
    }
    if (extension.front() != '.') {
        extension.insert(extension.begin(), '.');
    }
    return extension;
}

std::filesystem::path ResolveManagedBackgroundImagePath(const std::string& storedPath) {
    if (storedPath.empty()) {
        return {};
    }

    std::filesystem::path path(storedPath);
    if (path.is_absolute()) {
        return path;
    }

    const std::filesystem::path workingDirectory = GetWorkingDirectoryPath();
    return workingDirectory.empty() ? path : (workingDirectory / path);
}

std::string MakeBackgroundImageIdFromPath(const std::string& storedPath) {
    std::string base = NormalizeName(std::filesystem::path(storedPath).stem().string());
    return base.empty() ? std::string("background") : base;
}

std::string MakeUniqueBackgroundImageId(const std::string& baseId, const AppearanceLibrary& library) {
    std::unordered_set<std::string> usedIds;
    for (const BackgroundImageEntry& entry : library.backgroundImages) {
        if (!entry.id.empty()) {
            usedIds.insert(entry.id);
        }
    }
    return MakeUniqueId(baseId.empty() ? std::string("background") : baseId, usedIds);
}

std::string MakeUniqueBackgroundImageDisplayName(const std::string& baseName, const AppearanceLibrary& library) {
    std::unordered_set<std::string> usedNames;
    for (const BackgroundImageEntry& entry : library.backgroundImages) {
        if (!entry.displayName.empty()) {
            usedNames.insert(entry.displayName);
        }
    }
    return MakeUniqueDisplayName(baseName.empty() ? std::string("Background Image") : baseName, usedNames);
}

std::filesystem::path MakeUniqueManagedBackgroundImagePath(const std::filesystem::path& sourcePath) {
    const std::string extension = NormalizeExtension(sourcePath.extension().string());
    const std::string stem = NormalizeName(sourcePath.stem().string());
    const std::string baseName = stem.empty() ? std::string(kManagedBackgroundImageBaseName) : stem;
    const std::filesystem::path directory = GetWorkingDirectoryPath();
    for (int suffix = 1; suffix < 1000; ++suffix) {
        const std::string candidateName = suffix == 1
            ? baseName + extension
            : baseName + "-" + std::to_string(suffix) + extension;
        const std::filesystem::path candidate = directory / candidateName;
        std::error_code ec;
        if (!std::filesystem::exists(candidate, ec) || ec) {
            return candidate;
        }
    }
    return directory / (baseName + "-generated" + extension);
}

BackgroundImageEntry MakeBackgroundImageEntry(
    const std::string& storedPath,
    const std::string& displayName,
    const AppearanceLibrary& library) {
    BackgroundImageEntry entry;
    entry.path = storedPath;
    entry.id = MakeUniqueBackgroundImageId(MakeBackgroundImageIdFromPath(storedPath), library);
    entry.displayName = MakeUniqueBackgroundImageDisplayName(
        displayName.empty() ? std::filesystem::path(storedPath).stem().string() : displayName,
        library);
    return entry;
}

const BackgroundImageEntry* FindBackgroundImageById(const AppearanceLibrary& library, const std::string& imageId) {
    for (const BackgroundImageEntry& entry : library.backgroundImages) {
        if (entry.id == imageId) {
            return &entry;
        }
    }
    return nullptr;
}

BackgroundImageEntry* FindBackgroundImageById(AppearanceLibrary& library, const std::string& imageId) {
    for (BackgroundImageEntry& entry : library.backgroundImages) {
        if (entry.id == imageId) {
            return &entry;
        }
    }
    return nullptr;
}

bool BackgroundImagePathInLibrary(const AppearanceLibrary& library, const std::string& storedPath) {
    return std::any_of(
        library.backgroundImages.begin(),
        library.backgroundImages.end(),
        [&](const BackgroundImageEntry& entry) { return entry.path == storedPath; });
}

bool ThemeDefinitionEquals(const ThemeDefinition& lhs, const ThemeDefinition& rhs) {
    return ThemeDefinitionToJson(lhs) == ThemeDefinitionToJson(rhs);
}

ThemeStyleValues BlendThemeStyle(const ThemeStyleValues& from, const ThemeStyleValues& to, float t) {
    ThemeStyleValues result;
    result.alpha = from.alpha + (to.alpha - from.alpha) * t;
    result.windowPadding = Blend(from.windowPadding, to.windowPadding, t);
    result.framePadding = Blend(from.framePadding, to.framePadding, t);
    result.cellPadding = Blend(from.cellPadding, to.cellPadding, t);
    result.itemSpacing = Blend(from.itemSpacing, to.itemSpacing, t);
    result.itemInnerSpacing = Blend(from.itemInnerSpacing, to.itemInnerSpacing, t);
    result.indentSpacing = from.indentSpacing + (to.indentSpacing - from.indentSpacing) * t;
    result.scrollbarSize = from.scrollbarSize + (to.scrollbarSize - from.scrollbarSize) * t;
    result.grabMinSize = from.grabMinSize + (to.grabMinSize - from.grabMinSize) * t;
    result.windowBorderSize = from.windowBorderSize + (to.windowBorderSize - from.windowBorderSize) * t;
    result.childBorderSize = from.childBorderSize + (to.childBorderSize - from.childBorderSize) * t;
    result.popupBorderSize = from.popupBorderSize + (to.popupBorderSize - from.popupBorderSize) * t;
    result.frameBorderSize = from.frameBorderSize + (to.frameBorderSize - from.frameBorderSize) * t;
    result.tabBorderSize = from.tabBorderSize + (to.tabBorderSize - from.tabBorderSize) * t;
    result.windowRounding = from.windowRounding + (to.windowRounding - from.windowRounding) * t;
    result.childRounding = from.childRounding + (to.childRounding - from.childRounding) * t;
    result.frameRounding = from.frameRounding + (to.frameRounding - from.frameRounding) * t;
    result.popupRounding = from.popupRounding + (to.popupRounding - from.popupRounding) * t;
    result.scrollbarRounding = from.scrollbarRounding + (to.scrollbarRounding - from.scrollbarRounding) * t;
    result.grabRounding = from.grabRounding + (to.grabRounding - from.grabRounding) * t;
    result.tabRounding = from.tabRounding + (to.tabRounding - from.tabRounding) * t;
    result.windowTitleAlign = Blend(from.windowTitleAlign, to.windowTitleAlign, t);
    result.buttonTextAlign = Blend(from.buttonTextAlign, to.buttonTextAlign, t);
    result.selectableTextAlign = Blend(from.selectableTextAlign, to.selectableTextAlign, t);
    result.antiAliasedLines = t < 0.5f ? from.antiAliasedLines : to.antiAliasedLines;
    result.antiAliasedFill = t < 0.5f ? from.antiAliasedFill : to.antiAliasedFill;
    result.curveTessellationTol = from.curveTessellationTol + (to.curveTessellationTol - from.curveTessellationTol) * t;
    return result;
}

ThemeDefinition BlendThemeDefinition(const ThemeDefinition& from, const ThemeDefinition& to, float t) {
    ThemeDefinition result = to;
    result.displayName = t < 1.0f ? from.displayName : to.displayName;
    result.readOnly = to.readOnly;
    result.textScale = from.textScale + (to.textScale - from.textScale) * t;
    for (std::size_t index = 0; index < result.colors.size(); ++index) {
        result.colors[index] = Blend(from.colors[index], to.colors[index], t);
    }
    result.style = BlendThemeStyle(from.style, to.style, t);
    return result;
}

void ApplyCommonThemeStyle(ThemeDefinition& theme) {
    theme.style.alpha = 1.0f;
    theme.style.windowPadding = ImVec2(16.0f, 16.0f);
    theme.style.framePadding = ImVec2(10.0f, 6.0f);
    theme.style.cellPadding = ImVec2(8.0f, 6.0f);
    theme.style.itemSpacing = ImVec2(10.0f, 8.0f);
    theme.style.itemInnerSpacing = ImVec2(8.0f, 6.0f);
    theme.style.indentSpacing = 18.0f;
    theme.style.scrollbarSize = 12.0f;
    theme.style.grabMinSize = 10.0f;
    theme.style.windowBorderSize = 1.0f;
    theme.style.childBorderSize = 1.0f;
    theme.style.popupBorderSize = 1.0f;
    theme.style.frameBorderSize = 0.0f;
    theme.style.tabBorderSize = 0.0f;
    theme.style.windowRounding = 12.0f;
    theme.style.childRounding = 10.0f;
    theme.style.frameRounding = 8.0f;
    theme.style.popupRounding = 12.0f;
    theme.style.scrollbarRounding = 12.0f;
    theme.style.grabRounding = 8.0f;
    theme.style.tabRounding = 8.0f;
    theme.style.windowTitleAlign = ImVec2(0.0f, 0.5f);
    theme.style.buttonTextAlign = ImVec2(0.5f, 0.5f);
    theme.style.selectableTextAlign = ImVec2(0.0f, 0.0f);
    theme.style.antiAliasedLines = true;
    theme.style.antiAliasedFill = true;
    theme.style.curveTessellationTol = 1.25f;
}

ThemeDefinition BuildDarkTheme() {
    ThemeDefinition theme;
    theme.id = kFactoryThemeToken;
    theme.displayName = "Dark";
    theme.readOnly = true;
    theme.textScale = 1.0f;
    theme.colors.fill(MakeColor(0.08f, 0.09f, 0.10f, 1.0f));

    const ImVec4 background = MakeColor(0.043f, 0.047f, 0.057f, 1.0f);
    const ImVec4 windowBackground = MakeColor(0.079f, 0.085f, 0.101f, 1.0f);
    const ImVec4 surfaceBackground = MakeColor(0.094f, 0.102f, 0.121f, 1.0f);
    const ImVec4 surfaceElevated = MakeColor(0.110f, 0.119f, 0.141f, 1.0f);
    const ImVec4 border = MakeColor(0.182f, 0.207f, 0.258f, 0.92f);
    const ImVec4 accent = MakeColor(0.392f, 0.612f, 0.962f, 1.0f);
    const ImVec4 text = MakeColor(0.928f, 0.941f, 0.968f, 1.0f);
    const ImVec4 textMuted = MakeColor(0.584f, 0.623f, 0.699f, 1.0f);

    theme.colors[ImGuiCol_Text] = text;
    theme.colors[ImGuiCol_TextDisabled] = textMuted;
    theme.colors[ImGuiCol_WindowBg] = windowBackground;
    theme.colors[ImGuiCol_ChildBg] = surfaceBackground;
    theme.colors[ImGuiCol_PopupBg] = windowBackground;
    theme.colors[ImGuiCol_Border] = border;
    theme.colors[ImGuiCol_BorderShadow] = MakeColor(0.0f, 0.0f, 0.0f, 0.0f);
    theme.colors[ImGuiCol_FrameBg] = surfaceBackground;
    theme.colors[ImGuiCol_FrameBgHovered] = surfaceElevated;
    theme.colors[ImGuiCol_FrameBgActive] = Blend(surfaceElevated, accent, 0.22f);
    theme.colors[ImGuiCol_TitleBg] = windowBackground;
    theme.colors[ImGuiCol_TitleBgActive] = surfaceElevated;
    theme.colors[ImGuiCol_TitleBgCollapsed] = windowBackground;
    theme.colors[ImGuiCol_MenuBarBg] = windowBackground;
    theme.colors[ImGuiCol_ScrollbarBg] = windowBackground;
    theme.colors[ImGuiCol_ScrollbarGrab] = border;
    theme.colors[ImGuiCol_ScrollbarGrabHovered] = accent;
    theme.colors[ImGuiCol_ScrollbarGrabActive] = Blend(accent, surfaceElevated, 0.2f);
    theme.colors[ImGuiCol_CheckMark] = accent;
    theme.colors[ImGuiCol_SliderGrab] = Blend(accent, windowBackground, 0.15f);
    theme.colors[ImGuiCol_SliderGrabActive] = accent;
    theme.colors[ImGuiCol_Button] = surfaceBackground;
    theme.colors[ImGuiCol_ButtonHovered] = surfaceElevated;
    theme.colors[ImGuiCol_ButtonActive] = Blend(surfaceElevated, accent, 0.24f);
    theme.colors[ImGuiCol_Header] = surfaceBackground;
    theme.colors[ImGuiCol_HeaderHovered] = surfaceElevated;
    theme.colors[ImGuiCol_HeaderActive] = Blend(surfaceElevated, accent, 0.20f);
    theme.colors[ImGuiCol_Separator] = border;
    theme.colors[ImGuiCol_SeparatorHovered] = accent;
    theme.colors[ImGuiCol_SeparatorActive] = accent;
    theme.colors[ImGuiCol_ResizeGrip] = border;
    theme.colors[ImGuiCol_ResizeGripHovered] = accent;
    theme.colors[ImGuiCol_ResizeGripActive] = accent;
    theme.colors[ImGuiCol_Tab] = windowBackground;
    theme.colors[ImGuiCol_TabHovered] = surfaceElevated;
    theme.colors[ImGuiCol_TabActive] = surfaceBackground;
    theme.colors[ImGuiCol_TabUnfocused] = windowBackground;
    theme.colors[ImGuiCol_TabUnfocusedActive] = surfaceBackground;
    theme.colors[ImGuiCol_DockingPreview] = accent;
    theme.colors[ImGuiCol_DockingEmptyBg] = background;
    theme.colors[ImGuiCol_PlotLines] = accent;
    theme.colors[ImGuiCol_PlotLinesHovered] = accent;
    theme.colors[ImGuiCol_PlotHistogram] = accent;
    theme.colors[ImGuiCol_PlotHistogramHovered] = accent;
    theme.colors[ImGuiCol_TableHeaderBg] = surfaceBackground;
    theme.colors[ImGuiCol_TableBorderStrong] = border;
    theme.colors[ImGuiCol_TableBorderLight] = Blend(border, background, 0.45f);
    theme.colors[ImGuiCol_TableRowBg] = windowBackground;
    theme.colors[ImGuiCol_TableRowBgAlt] = surfaceBackground;
    theme.colors[ImGuiCol_TextSelectedBg] = Blend(accent, background, 0.82f);
    theme.colors[ImGuiCol_DragDropTarget] = accent;
    theme.colors[ImGuiCol_NavHighlight] = accent;
    theme.colors[ImGuiCol_NavWindowingHighlight] = Blend(accent, background, 0.3f);
    theme.colors[ImGuiCol_NavWindowingDimBg] = MakeColor(0.02f, 0.02f, 0.03f, 0.65f);
    theme.colors[ImGuiCol_ModalWindowDimBg] = MakeColor(0.02f, 0.02f, 0.03f, 0.55f);
    ApplyCommonThemeStyle(theme);

    return theme;
}

ThemeDefinition BuildLightTheme() {
    ThemeDefinition theme;
    theme.id = kLightPresetId;
    theme.displayName = "Light";
    theme.readOnly = true;
    theme.textScale = 1.0f;

    const ImVec4 background = MakeColor(0.955f, 0.958f, 0.964f, 1.0f);
    const ImVec4 windowBackground = MakeColor(0.986f, 0.988f, 0.991f, 1.0f);
    const ImVec4 surfaceBackground = MakeColor(0.930f, 0.936f, 0.946f, 1.0f);
    const ImVec4 surfaceElevated = MakeColor(0.898f, 0.909f, 0.926f, 1.0f);
    const ImVec4 border = MakeColor(0.680f, 0.708f, 0.745f, 0.92f);
    const ImVec4 accent = MakeColor(0.180f, 0.463f, 0.792f, 1.0f);
    const ImVec4 text = MakeColor(0.122f, 0.141f, 0.169f, 1.0f);
    const ImVec4 textMuted = MakeColor(0.357f, 0.397f, 0.443f, 1.0f);

    theme.colors.fill(ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    theme.colors[ImGuiCol_Text] = text;
    theme.colors[ImGuiCol_TextDisabled] = textMuted;
    theme.colors[ImGuiCol_WindowBg] = windowBackground;
    theme.colors[ImGuiCol_ChildBg] = background;
    theme.colors[ImGuiCol_PopupBg] = windowBackground;
    theme.colors[ImGuiCol_Border] = border;
    theme.colors[ImGuiCol_BorderShadow] = MakeColor(0.0f, 0.0f, 0.0f, 0.0f);
    theme.colors[ImGuiCol_FrameBg] = surfaceBackground;
    theme.colors[ImGuiCol_FrameBgHovered] = surfaceElevated;
    theme.colors[ImGuiCol_FrameBgActive] = Blend(surfaceElevated, accent, 0.16f);
    theme.colors[ImGuiCol_TitleBg] = windowBackground;
    theme.colors[ImGuiCol_TitleBgActive] = surfaceBackground;
    theme.colors[ImGuiCol_TitleBgCollapsed] = windowBackground;
    theme.colors[ImGuiCol_MenuBarBg] = windowBackground;
    theme.colors[ImGuiCol_ScrollbarBg] = background;
    theme.colors[ImGuiCol_ScrollbarGrab] = border;
    theme.colors[ImGuiCol_ScrollbarGrabHovered] = accent;
    theme.colors[ImGuiCol_ScrollbarGrabActive] = Blend(accent, surfaceElevated, 0.12f);
    theme.colors[ImGuiCol_CheckMark] = accent;
    theme.colors[ImGuiCol_SliderGrab] = Blend(accent, background, 0.10f);
    theme.colors[ImGuiCol_SliderGrabActive] = accent;
    theme.colors[ImGuiCol_Button] = surfaceBackground;
    theme.colors[ImGuiCol_ButtonHovered] = surfaceElevated;
    theme.colors[ImGuiCol_ButtonActive] = Blend(surfaceElevated, accent, 0.18f);
    theme.colors[ImGuiCol_Header] = surfaceBackground;
    theme.colors[ImGuiCol_HeaderHovered] = surfaceElevated;
    theme.colors[ImGuiCol_HeaderActive] = Blend(surfaceElevated, accent, 0.16f);
    theme.colors[ImGuiCol_Separator] = border;
    theme.colors[ImGuiCol_SeparatorHovered] = accent;
    theme.colors[ImGuiCol_SeparatorActive] = accent;
    theme.colors[ImGuiCol_ResizeGrip] = border;
    theme.colors[ImGuiCol_ResizeGripHovered] = accent;
    theme.colors[ImGuiCol_ResizeGripActive] = accent;
    theme.colors[ImGuiCol_Tab] = windowBackground;
    theme.colors[ImGuiCol_TabHovered] = surfaceElevated;
    theme.colors[ImGuiCol_TabActive] = surfaceBackground;
    theme.colors[ImGuiCol_TabUnfocused] = windowBackground;
    theme.colors[ImGuiCol_TabUnfocusedActive] = surfaceBackground;
    theme.colors[ImGuiCol_DockingPreview] = accent;
    theme.colors[ImGuiCol_DockingEmptyBg] = background;
    theme.colors[ImGuiCol_PlotLines] = accent;
    theme.colors[ImGuiCol_PlotLinesHovered] = accent;
    theme.colors[ImGuiCol_PlotHistogram] = accent;
    theme.colors[ImGuiCol_PlotHistogramHovered] = accent;
    theme.colors[ImGuiCol_TableHeaderBg] = surfaceBackground;
    theme.colors[ImGuiCol_TableBorderStrong] = border;
    theme.colors[ImGuiCol_TableBorderLight] = Blend(border, background, 0.35f);
    theme.colors[ImGuiCol_TableRowBg] = windowBackground;
    theme.colors[ImGuiCol_TableRowBgAlt] = background;
    theme.colors[ImGuiCol_TextSelectedBg] = Blend(accent, background, 0.82f);
    theme.colors[ImGuiCol_DragDropTarget] = accent;
    theme.colors[ImGuiCol_NavHighlight] = accent;
    theme.colors[ImGuiCol_NavWindowingHighlight] = Blend(accent, background, 0.25f);
    theme.colors[ImGuiCol_NavWindowingDimBg] = MakeColor(0.86f, 0.88f, 0.91f, 0.40f);
    theme.colors[ImGuiCol_ModalWindowDimBg] = MakeColor(0.86f, 0.88f, 0.91f, 0.28f);
    ApplyCommonThemeStyle(theme);

    return theme;
}

ThemeDefinition BuildSolarizedTheme() {
    ThemeDefinition theme;
    theme.id = kSolarizedPresetId;
    theme.displayName = "Solarized";
    theme.readOnly = true;
    theme.textScale = 1.0f;

    const ImVec4 base03 = MakeColor(0.000f, 0.169f, 0.212f, 1.0f);
    const ImVec4 base02 = MakeColor(0.027f, 0.212f, 0.259f, 1.0f);
    const ImVec4 base01 = MakeColor(0.345f, 0.431f, 0.459f, 1.0f);
    const ImVec4 base00 = MakeColor(0.396f, 0.482f, 0.514f, 1.0f);
    const ImVec4 base0 = MakeColor(0.514f, 0.580f, 0.588f, 1.0f);
    const ImVec4 base1 = MakeColor(0.576f, 0.631f, 0.631f, 1.0f);
    const ImVec4 base2 = MakeColor(0.933f, 0.910f, 0.835f, 1.0f);
    const ImVec4 base3 = MakeColor(0.992f, 0.965f, 0.890f, 1.0f);
    const ImVec4 yellow = MakeColor(0.710f, 0.537f, 0.000f, 1.0f);
    const ImVec4 orange = MakeColor(0.796f, 0.294f, 0.086f, 1.0f);
    const ImVec4 red = MakeColor(0.863f, 0.196f, 0.184f, 1.0f);
    const ImVec4 magenta = MakeColor(0.827f, 0.212f, 0.510f, 1.0f);
    const ImVec4 violet = MakeColor(0.424f, 0.443f, 0.769f, 1.0f);
    const ImVec4 blue = MakeColor(0.149f, 0.545f, 0.824f, 1.0f);
    const ImVec4 cyan = MakeColor(0.165f, 0.631f, 0.596f, 1.0f);
    const ImVec4 green = MakeColor(0.522f, 0.600f, 0.000f, 1.0f);

    theme.colors.fill(base02);
    theme.colors[ImGuiCol_Text] = base0;
    theme.colors[ImGuiCol_TextDisabled] = base01;
    theme.colors[ImGuiCol_WindowBg] = base03;
    theme.colors[ImGuiCol_ChildBg] = base02;
    theme.colors[ImGuiCol_PopupBg] = base02;
    theme.colors[ImGuiCol_Border] = base01;
    theme.colors[ImGuiCol_BorderShadow] = MakeColor(0.0f, 0.0f, 0.0f, 0.0f);
    theme.colors[ImGuiCol_FrameBg] = base02;
    theme.colors[ImGuiCol_FrameBgHovered] = Blend(base02, base01, 0.30f);
    theme.colors[ImGuiCol_FrameBgActive] = Blend(base02, blue, 0.18f);
    theme.colors[ImGuiCol_TitleBg] = base02;
    theme.colors[ImGuiCol_TitleBgActive] = base01;
    theme.colors[ImGuiCol_TitleBgCollapsed] = base02;
    theme.colors[ImGuiCol_MenuBarBg] = base02;
    theme.colors[ImGuiCol_ScrollbarBg] = base03;
    theme.colors[ImGuiCol_ScrollbarGrab] = base01;
    theme.colors[ImGuiCol_ScrollbarGrabHovered] = blue;
    theme.colors[ImGuiCol_ScrollbarGrabActive] = Blend(blue, base1, 0.12f);
    theme.colors[ImGuiCol_CheckMark] = cyan;
    theme.colors[ImGuiCol_SliderGrab] = blue;
    theme.colors[ImGuiCol_SliderGrabActive] = cyan;
    theme.colors[ImGuiCol_Button] = base02;
    theme.colors[ImGuiCol_ButtonHovered] = Blend(base02, blue, 0.25f);
    theme.colors[ImGuiCol_ButtonActive] = Blend(base02, cyan, 0.22f);
    theme.colors[ImGuiCol_Header] = base02;
    theme.colors[ImGuiCol_HeaderHovered] = Blend(base02, blue, 0.25f);
    theme.colors[ImGuiCol_HeaderActive] = Blend(base02, cyan, 0.18f);
    theme.colors[ImGuiCol_Separator] = base01;
    theme.colors[ImGuiCol_SeparatorHovered] = blue;
    theme.colors[ImGuiCol_SeparatorActive] = cyan;
    theme.colors[ImGuiCol_ResizeGrip] = base01;
    theme.colors[ImGuiCol_ResizeGripHovered] = blue;
    theme.colors[ImGuiCol_ResizeGripActive] = cyan;
    theme.colors[ImGuiCol_Tab] = base03;
    theme.colors[ImGuiCol_TabHovered] = base02;
    theme.colors[ImGuiCol_TabActive] = base02;
    theme.colors[ImGuiCol_TabUnfocused] = base03;
    theme.colors[ImGuiCol_TabUnfocusedActive] = base02;
    theme.colors[ImGuiCol_DockingPreview] = blue;
    theme.colors[ImGuiCol_DockingEmptyBg] = base03;
    theme.colors[ImGuiCol_PlotLines] = cyan;
    theme.colors[ImGuiCol_PlotLinesHovered] = yellow;
    theme.colors[ImGuiCol_PlotHistogram] = blue;
    theme.colors[ImGuiCol_PlotHistogramHovered] = orange;
    theme.colors[ImGuiCol_TableHeaderBg] = base02;
    theme.colors[ImGuiCol_TableBorderStrong] = base01;
    theme.colors[ImGuiCol_TableBorderLight] = Blend(base01, base03, 0.30f);
    theme.colors[ImGuiCol_TableRowBg] = base03;
    theme.colors[ImGuiCol_TableRowBgAlt] = base02;
    theme.colors[ImGuiCol_TextSelectedBg] = Blend(blue, base03, 0.76f);
    theme.colors[ImGuiCol_DragDropTarget] = yellow;
    theme.colors[ImGuiCol_NavHighlight] = blue;
    theme.colors[ImGuiCol_NavWindowingHighlight] = Blend(blue, base03, 0.35f);
    theme.colors[ImGuiCol_NavWindowingDimBg] = MakeColor(0.00f, 0.10f, 0.14f, 0.68f);
    theme.colors[ImGuiCol_ModalWindowDimBg] = MakeColor(0.00f, 0.10f, 0.14f, 0.58f);
    theme.colors[ImGuiCol_Text] = base0;
    theme.colors[ImGuiCol_TextDisabled] = base01;
    theme.colors[ImGuiCol_PlotLinesHovered] = orange;
    theme.colors[ImGuiCol_DragDropTarget] = orange;
    theme.colors[ImGuiCol_CheckMark] = cyan;
    theme.colors[ImGuiCol_SliderGrab] = blue;
    theme.colors[ImGuiCol_SliderGrabActive] = cyan;
    ApplyCommonThemeStyle(theme);
    theme.style.windowBorderSize = 1.0f;
    theme.style.childBorderSize = 1.0f;
    theme.style.popupBorderSize = 1.0f;
    theme.style.frameBorderSize = 0.0f;
    theme.style.tabBorderSize = 0.0f;
    return theme;
}

ThemeDefinition BuildSolarizedLightTheme() {
    ThemeDefinition theme;
    theme.id = kSolarizedLightPresetId;
    theme.displayName = "Solarized Light";
    theme.readOnly = true;
    theme.textScale = 1.0f;

    const ImVec4 base03 = MakeColor(0.000f, 0.169f, 0.212f, 1.0f);
    const ImVec4 base02 = MakeColor(0.027f, 0.212f, 0.259f, 1.0f);
    const ImVec4 base01 = MakeColor(0.345f, 0.431f, 0.459f, 1.0f);
    const ImVec4 base00 = MakeColor(0.396f, 0.482f, 0.514f, 1.0f);
    const ImVec4 base0 = MakeColor(0.514f, 0.580f, 0.588f, 1.0f);
    const ImVec4 base1 = MakeColor(0.576f, 0.631f, 0.631f, 1.0f);
    const ImVec4 base2 = MakeColor(0.933f, 0.910f, 0.835f, 1.0f);
    const ImVec4 base3 = MakeColor(0.992f, 0.965f, 0.890f, 1.0f);
    const ImVec4 yellow = MakeColor(0.710f, 0.537f, 0.000f, 1.0f);
    const ImVec4 orange = MakeColor(0.796f, 0.294f, 0.086f, 1.0f);
    const ImVec4 blue = MakeColor(0.149f, 0.545f, 0.824f, 1.0f);
    const ImVec4 cyan = MakeColor(0.165f, 0.631f, 0.596f, 1.0f);
    const ImVec4 green = MakeColor(0.522f, 0.600f, 0.000f, 1.0f);

    const ImVec4 page = base3;
    const ImVec4 panel = base2;
    const ImVec4 control = Blend(base2, base1, 0.10f);
    const ImVec4 controlHover = Blend(base2, blue, 0.16f);
    const ImVec4 controlActive = Blend(base2, cyan, 0.22f);
    const ImVec4 border = Blend(base1, base01, 0.22f);
    const ImVec4 borderLight = Blend(base0, page, 0.48f);
    const ImVec4 selected = Blend(blue, page, 0.78f);

    theme.colors.fill(panel);
    theme.colors[ImGuiCol_Text] = base00;
    theme.colors[ImGuiCol_TextDisabled] = base1;
    theme.colors[ImGuiCol_WindowBg] = page;
    theme.colors[ImGuiCol_ChildBg] = panel;
    theme.colors[ImGuiCol_PopupBg] = page;
    theme.colors[ImGuiCol_Border] = MakeColor(border.x, border.y, border.z, 0.92f);
    theme.colors[ImGuiCol_BorderShadow] = MakeColor(0.0f, 0.0f, 0.0f, 0.0f);
    theme.colors[ImGuiCol_FrameBg] = control;
    theme.colors[ImGuiCol_FrameBgHovered] = controlHover;
    theme.colors[ImGuiCol_FrameBgActive] = controlActive;
    theme.colors[ImGuiCol_TitleBg] = panel;
    theme.colors[ImGuiCol_TitleBgActive] = Blend(panel, base1, 0.18f);
    theme.colors[ImGuiCol_TitleBgCollapsed] = panel;
    theme.colors[ImGuiCol_MenuBarBg] = panel;
    theme.colors[ImGuiCol_ScrollbarBg] = page;
    theme.colors[ImGuiCol_ScrollbarGrab] = MakeColor(border.x, border.y, border.z, 0.86f);
    theme.colors[ImGuiCol_ScrollbarGrabHovered] = blue;
    theme.colors[ImGuiCol_ScrollbarGrabActive] = cyan;
    theme.colors[ImGuiCol_CheckMark] = cyan;
    theme.colors[ImGuiCol_SliderGrab] = blue;
    theme.colors[ImGuiCol_SliderGrabActive] = cyan;
    theme.colors[ImGuiCol_Button] = control;
    theme.colors[ImGuiCol_ButtonHovered] = controlHover;
    theme.colors[ImGuiCol_ButtonActive] = controlActive;
    theme.colors[ImGuiCol_Header] = Blend(panel, blue, 0.08f);
    theme.colors[ImGuiCol_HeaderHovered] = controlHover;
    theme.colors[ImGuiCol_HeaderActive] = controlActive;
    theme.colors[ImGuiCol_Separator] = MakeColor(border.x, border.y, border.z, 0.85f);
    theme.colors[ImGuiCol_SeparatorHovered] = blue;
    theme.colors[ImGuiCol_SeparatorActive] = cyan;
    theme.colors[ImGuiCol_ResizeGrip] = MakeColor(border.x, border.y, border.z, 0.65f);
    theme.colors[ImGuiCol_ResizeGripHovered] = blue;
    theme.colors[ImGuiCol_ResizeGripActive] = cyan;
    theme.colors[ImGuiCol_Tab] = page;
    theme.colors[ImGuiCol_TabHovered] = Blend(panel, blue, 0.14f);
    theme.colors[ImGuiCol_TabActive] = panel;
    theme.colors[ImGuiCol_TabUnfocused] = page;
    theme.colors[ImGuiCol_TabUnfocusedActive] = Blend(panel, page, 0.28f);
    theme.colors[ImGuiCol_DockingPreview] = blue;
    theme.colors[ImGuiCol_DockingEmptyBg] = page;
    theme.colors[ImGuiCol_PlotLines] = blue;
    theme.colors[ImGuiCol_PlotLinesHovered] = orange;
    theme.colors[ImGuiCol_PlotHistogram] = cyan;
    theme.colors[ImGuiCol_PlotHistogramHovered] = yellow;
    theme.colors[ImGuiCol_TableHeaderBg] = Blend(panel, base1, 0.12f);
    theme.colors[ImGuiCol_TableBorderStrong] = MakeColor(border.x, border.y, border.z, 0.88f);
    theme.colors[ImGuiCol_TableBorderLight] = MakeColor(borderLight.x, borderLight.y, borderLight.z, 0.72f);
    theme.colors[ImGuiCol_TableRowBg] = page;
    theme.colors[ImGuiCol_TableRowBgAlt] = panel;
    theme.colors[ImGuiCol_TextSelectedBg] = selected;
    theme.colors[ImGuiCol_DragDropTarget] = orange;
    theme.colors[ImGuiCol_NavHighlight] = blue;
    theme.colors[ImGuiCol_NavWindowingHighlight] = Blend(blue, base02, 0.15f);
    theme.colors[ImGuiCol_NavWindowingDimBg] = MakeColor(base03.x, base03.y, base03.z, 0.16f);
    theme.colors[ImGuiCol_ModalWindowDimBg] = MakeColor(base02.x, base02.y, base02.z, 0.22f);
    theme.colors[ImGuiCol_TextLink] = blue;
    theme.colors[ImGuiCol_TabDimmed] = page;
    theme.colors[ImGuiCol_TabDimmedSelected] = Blend(panel, page, 0.22f);
    theme.colors[ImGuiCol_TabDimmedSelectedOverline] = cyan;
    theme.colors[ImGuiCol_TabSelectedOverline] = green;

    ApplyCommonThemeStyle(theme);
    theme.style.windowBorderSize = 1.0f;
    theme.style.childBorderSize = 1.0f;
    theme.style.popupBorderSize = 1.0f;
    theme.style.frameBorderSize = 1.0f;
    theme.style.tabBorderSize = 1.0f;
    theme.style.windowRounding = 10.0f;
    theme.style.childRounding = 9.0f;
    theme.style.frameRounding = 7.0f;
    theme.style.tabRounding = 8.0f;

    return theme;
}

ThemeDefinition BuildYellowDarkTheme() {
    ThemeDefinition theme;
    theme.id = kYellowDarkPresetId;
    theme.displayName = "Yellow Dark";
    theme.readOnly = true;
    theme.textScale = 1.0f;

    const ImVec4 background = MakeColor(0.102f, 0.094f, 0.078f, 1.0f);
    const ImVec4 windowBackground = MakeColor(0.141f, 0.133f, 0.110f, 1.0f);
    const ImVec4 surfaceBackground = MakeColor(0.180f, 0.169f, 0.141f, 1.0f);
    const ImVec4 surfaceElevated = MakeColor(0.220f, 0.208f, 0.173f, 1.0f);
    const ImVec4 border = MakeColor(0.322f, 0.298f, 0.243f, 0.92f);
    const ImVec4 accent = MakeColor(1.000f, 0.831f, 0.000f, 1.0f);
    const ImVec4 accentDarker = MakeColor(0.922f, 0.765f, 0.000f, 1.0f);
    const ImVec4 text = MakeColor(0.961f, 0.949f, 0.902f, 1.0f);
    const ImVec4 textMuted = MakeColor(0.639f, 0.620f, 0.541f, 1.0f);

    theme.colors.fill(background);
    theme.colors[ImGuiCol_Text] = text;
    theme.colors[ImGuiCol_TextDisabled] = textMuted;
    theme.colors[ImGuiCol_WindowBg] = windowBackground;
    theme.colors[ImGuiCol_ChildBg] = surfaceBackground;
    theme.colors[ImGuiCol_PopupBg] = windowBackground;
    theme.colors[ImGuiCol_Border] = border;
    theme.colors[ImGuiCol_BorderShadow] = MakeColor(0.0f, 0.0f, 0.0f, 0.0f);
    theme.colors[ImGuiCol_FrameBg] = surfaceBackground;
    theme.colors[ImGuiCol_FrameBgHovered] = surfaceElevated;
    theme.colors[ImGuiCol_FrameBgActive] = Blend(surfaceElevated, accentDarker, 0.22f);
    theme.colors[ImGuiCol_TitleBg] = windowBackground;
    theme.colors[ImGuiCol_TitleBgActive] = surfaceElevated;
    theme.colors[ImGuiCol_TitleBgCollapsed] = windowBackground;
    theme.colors[ImGuiCol_MenuBarBg] = windowBackground;
    theme.colors[ImGuiCol_ScrollbarBg] = windowBackground;
    theme.colors[ImGuiCol_ScrollbarGrab] = border;
    theme.colors[ImGuiCol_ScrollbarGrabHovered] = accent;
    theme.colors[ImGuiCol_ScrollbarGrabActive] = accentDarker;
    theme.colors[ImGuiCol_CheckMark] = accent;
    theme.colors[ImGuiCol_SliderGrab] = Blend(accent, windowBackground, 0.15f);
    theme.colors[ImGuiCol_SliderGrabActive] = accentDarker;
    theme.colors[ImGuiCol_Button] = surfaceBackground;
    theme.colors[ImGuiCol_ButtonHovered] = surfaceElevated;
    theme.colors[ImGuiCol_ButtonActive] = Blend(surfaceElevated, accentDarker, 0.24f);
    theme.colors[ImGuiCol_Header] = surfaceBackground;
    theme.colors[ImGuiCol_HeaderHovered] = surfaceElevated;
    theme.colors[ImGuiCol_HeaderActive] = Blend(surfaceElevated, accentDarker, 0.20f);
    theme.colors[ImGuiCol_Separator] = border;
    theme.colors[ImGuiCol_SeparatorHovered] = accent;
    theme.colors[ImGuiCol_SeparatorActive] = accentDarker;
    theme.colors[ImGuiCol_ResizeGrip] = border;
    theme.colors[ImGuiCol_ResizeGripHovered] = accent;
    theme.colors[ImGuiCol_ResizeGripActive] = accentDarker;
    theme.colors[ImGuiCol_Tab] = windowBackground;
    theme.colors[ImGuiCol_TabHovered] = surfaceElevated;
    theme.colors[ImGuiCol_TabActive] = surfaceBackground;
    theme.colors[ImGuiCol_TabUnfocused] = windowBackground;
    theme.colors[ImGuiCol_TabUnfocusedActive] = surfaceBackground;
    theme.colors[ImGuiCol_DockingPreview] = accent;
    theme.colors[ImGuiCol_DockingEmptyBg] = background;
    theme.colors[ImGuiCol_PlotLines] = accent;
    theme.colors[ImGuiCol_PlotLinesHovered] = accentDarker;
    theme.colors[ImGuiCol_PlotHistogram] = accent;
    theme.colors[ImGuiCol_PlotHistogramHovered] = accentDarker;
    theme.colors[ImGuiCol_TableHeaderBg] = surfaceBackground;
    theme.colors[ImGuiCol_TableBorderStrong] = border;
    theme.colors[ImGuiCol_TableBorderLight] = Blend(border, background, 0.45f);
    theme.colors[ImGuiCol_TableRowBg] = windowBackground;
    theme.colors[ImGuiCol_TableRowBgAlt] = surfaceBackground;
    theme.colors[ImGuiCol_TextSelectedBg] = Blend(accent, background, 0.82f);
    theme.colors[ImGuiCol_DragDropTarget] = accent;
    theme.colors[ImGuiCol_NavHighlight] = accent;
    theme.colors[ImGuiCol_NavWindowingHighlight] = Blend(accent, background, 0.3f);
    theme.colors[ImGuiCol_NavWindowingDimBg] = MakeColor(0.02f, 0.02f, 0.03f, 0.65f);
    theme.colors[ImGuiCol_ModalWindowDimBg] = MakeColor(0.02f, 0.02f, 0.03f, 0.55f);
    theme.colors[ImGuiCol_TextLink] = accent;
    theme.colors[ImGuiCol_TabDimmed] = windowBackground;
    theme.colors[ImGuiCol_TabDimmedSelected] = Blend(surfaceBackground, windowBackground, 0.22f);
    theme.colors[ImGuiCol_TabDimmedSelectedOverline] = accentDarker;
    theme.colors[ImGuiCol_TabSelectedOverline] = accent;
    ApplyCommonThemeStyle(theme);

    return theme;
}

ThemeDefinition BuildYellowLightTheme() {
    ThemeDefinition theme;
    theme.id = kYellowLightPresetId;
    theme.displayName = "Yellow Light";
    theme.readOnly = true;
    theme.textScale = 1.0f;

    const ImVec4 background = MakeColor(0.992f, 0.988f, 0.957f, 1.0f);
    const ImVec4 windowBackground = MakeColor(1.000f, 0.992f, 0.973f, 1.0f);
    const ImVec4 surfaceBackground = MakeColor(0.961f, 0.949f, 0.882f, 1.0f);
    const ImVec4 surfaceElevated = MakeColor(0.922f, 0.902f, 0.820f, 1.0f);
    const ImVec4 border = MakeColor(0.820f, 0.796f, 0.659f, 0.92f);
    const ImVec4 accent = MakeColor(1.000f, 0.831f, 0.000f, 1.0f);
    const ImVec4 accentDarker = MakeColor(0.922f, 0.765f, 0.000f, 1.0f);
    const ImVec4 text = MakeColor(0.220f, 0.204f, 0.157f, 1.0f);
    const ImVec4 textMuted = MakeColor(0.522f, 0.486f, 0.392f, 1.0f);

    theme.colors.fill(background);
    theme.colors[ImGuiCol_Text] = text;
    theme.colors[ImGuiCol_TextDisabled] = textMuted;
    theme.colors[ImGuiCol_WindowBg] = windowBackground;
    theme.colors[ImGuiCol_ChildBg] = surfaceBackground;
    theme.colors[ImGuiCol_PopupBg] = windowBackground;
    theme.colors[ImGuiCol_Border] = border;
    theme.colors[ImGuiCol_BorderShadow] = MakeColor(0.0f, 0.0f, 0.0f, 0.0f);
    theme.colors[ImGuiCol_FrameBg] = surfaceBackground;
    theme.colors[ImGuiCol_FrameBgHovered] = surfaceElevated;
    theme.colors[ImGuiCol_FrameBgActive] = Blend(surfaceElevated, accentDarker, 0.16f);
    theme.colors[ImGuiCol_TitleBg] = windowBackground;
    theme.colors[ImGuiCol_TitleBgActive] = surfaceBackground;
    theme.colors[ImGuiCol_TitleBgCollapsed] = windowBackground;
    theme.colors[ImGuiCol_MenuBarBg] = windowBackground;
    theme.colors[ImGuiCol_ScrollbarBg] = background;
    theme.colors[ImGuiCol_ScrollbarGrab] = border;
    theme.colors[ImGuiCol_ScrollbarGrabHovered] = accent;
    theme.colors[ImGuiCol_ScrollbarGrabActive] = accentDarker;
    theme.colors[ImGuiCol_CheckMark] = accent;
    theme.colors[ImGuiCol_SliderGrab] = Blend(accent, background, 0.10f);
    theme.colors[ImGuiCol_SliderGrabActive] = accentDarker;
    theme.colors[ImGuiCol_Button] = surfaceBackground;
    theme.colors[ImGuiCol_ButtonHovered] = surfaceElevated;
    theme.colors[ImGuiCol_ButtonActive] = Blend(surfaceElevated, accentDarker, 0.18f);
    theme.colors[ImGuiCol_Header] = surfaceBackground;
    theme.colors[ImGuiCol_HeaderHovered] = surfaceElevated;
    theme.colors[ImGuiCol_HeaderActive] = Blend(surfaceElevated, accentDarker, 0.16f);
    theme.colors[ImGuiCol_Separator] = border;
    theme.colors[ImGuiCol_SeparatorHovered] = accent;
    theme.colors[ImGuiCol_SeparatorActive] = accentDarker;
    theme.colors[ImGuiCol_ResizeGrip] = border;
    theme.colors[ImGuiCol_ResizeGripHovered] = accent;
    theme.colors[ImGuiCol_ResizeGripActive] = accentDarker;
    theme.colors[ImGuiCol_Tab] = windowBackground;
    theme.colors[ImGuiCol_TabHovered] = surfaceElevated;
    theme.colors[ImGuiCol_TabActive] = surfaceBackground;
    theme.colors[ImGuiCol_TabUnfocused] = windowBackground;
    theme.colors[ImGuiCol_TabUnfocusedActive] = surfaceBackground;
    theme.colors[ImGuiCol_DockingPreview] = accent;
    theme.colors[ImGuiCol_DockingEmptyBg] = background;
    theme.colors[ImGuiCol_PlotLines] = accent;
    theme.colors[ImGuiCol_PlotLinesHovered] = accentDarker;
    theme.colors[ImGuiCol_PlotHistogram] = accent;
    theme.colors[ImGuiCol_PlotHistogramHovered] = accentDarker;
    theme.colors[ImGuiCol_TableHeaderBg] = surfaceBackground;
    theme.colors[ImGuiCol_TableBorderStrong] = border;
    theme.colors[ImGuiCol_TableBorderLight] = Blend(border, background, 0.35f);
    theme.colors[ImGuiCol_TableRowBg] = windowBackground;
    theme.colors[ImGuiCol_TableRowBgAlt] = background;
    theme.colors[ImGuiCol_TextSelectedBg] = Blend(accent, background, 0.82f);
    theme.colors[ImGuiCol_DragDropTarget] = accent;
    theme.colors[ImGuiCol_NavHighlight] = accent;
    theme.colors[ImGuiCol_NavWindowingHighlight] = Blend(accent, background, 0.25f);
    theme.colors[ImGuiCol_NavWindowingDimBg] = MakeColor(0.86f, 0.88f, 0.91f, 0.40f);
    theme.colors[ImGuiCol_ModalWindowDimBg] = MakeColor(0.86f, 0.88f, 0.91f, 0.28f);
    theme.colors[ImGuiCol_TextLink] = accent;
    theme.colors[ImGuiCol_TabDimmed] = windowBackground;
    theme.colors[ImGuiCol_TabDimmedSelected] = Blend(surfaceBackground, windowBackground, 0.22f);
    theme.colors[ImGuiCol_TabDimmedSelectedOverline] = accentDarker;
    theme.colors[ImGuiCol_TabSelectedOverline] = accent;
    ApplyCommonThemeStyle(theme);

    return theme;
}

std::vector<ThemeDefinition> BuildFactoryThemesInternal() {
    return { BuildDarkTheme(), BuildLightTheme(), BuildSolarizedTheme(), BuildSolarizedLightTheme(), BuildYellowDarkTheme(), BuildYellowLightTheme() };
}

ThemeDefinition BuildFactoryTheme() {
    return BuildDarkTheme();
}

json ThemePresetRecordToJson(const ThemeDefinition& theme) {
    json result = json::object();
    result[kIdKey] = theme.id;
    result[kNameKey] = theme.displayName;
    result[kThemeKey] = ThemeDefinitionToJson(theme)[kThemeKey];
    return result;
}

bool ThemePresetRecordFromJson(
    const json& value,
    const ThemeDefinition& fallback,
    ThemeDefinition& outTheme,
    std::string* errorMessage = nullptr) {
    if (!value.is_object()) {
        if (errorMessage) {
            *errorMessage = "Preset record is not an object.";
        }
        return false;
    }

    outTheme = ThemeDefinitionFromJson(value, fallback, errorMessage);
    outTheme.readOnly = false;
    if (outTheme.id.empty()) {
        outTheme.id = fallback.id;
    }
    if (outTheme.displayName.empty()) {
        outTheme.displayName = fallback.displayName;
    }
    return true;
}

json AppearanceLibraryToJson(const AppearanceLibrary& library) {
    json root = json::object();
    root[kVersionKey] = kAppearanceSettingsVersion;
    root[kAppearanceKey] = json::object();
    const ViewportTilingSettings viewportTiling = RenderTiling::NormalizeSettings(library.viewportTiling);
    root[kAppearanceKey][kActivePresetIdKey] = library.activePresetId.empty() ? std::string(kDefaultPresetToken) : library.activePresetId;
    root[kAppearanceKey][kGraphVisualModeKey] = GraphVisualModeToString(library.graphVisualMode);
    root[kAppearanceKey][kGraphSpotlightHaloOutlinesKey] = library.graphSpotlightHaloOutlines;
    root[kAppearanceKey][kGraphDottedMaskLinksKey] = library.graphDottedMaskLinks;
    root[kAppearanceKey][kGraphLineOpacityKey] = ClampUnit(library.graphLineOpacity);
    root[kAppearanceKey][kViewportTilingKey] = json::object();
    root[kAppearanceKey][kViewportTilingKey][kViewportTilingModeKey] =
        RenderTiling::ViewportTilingModeToString(viewportTiling.mode);
    root[kAppearanceKey][kViewportTilingKey][kViewportTilingTileSizeKey] = viewportTiling.tileSize;
    root[kAppearanceKey][kViewportTilingKey][kViewportTilingHaloPixelsKey] = viewportTiling.haloPixels;
    root[kAppearanceKey][kViewportTilingKey][kViewportTilingAutoThresholdKey] =
        viewportTiling.autoPixelThresholdMegapixels;
    root[kAppearanceKey][kViewportTilingKey][kViewportTilingProgressiveKey] = viewportTiling.progressive;
    root[kAppearanceKey][kViewportTilingKey][kViewportTilingDebugOverlayKey] = viewportTiling.debugOverlay;
    root[kAppearanceKey][kBackgroundImageEnabledKey] = library.backgroundImageEnabled;
    root[kAppearanceKey][kBackgroundImagePathKey] = library.backgroundImagePath;
    root[kAppearanceKey][kBackgroundImagesKey] = json::array();
    for (const BackgroundImageEntry& entry : library.backgroundImages) {
        if (!entry.path.empty()) {
            root[kAppearanceKey][kBackgroundImagesKey].push_back(BackgroundImageEntryToJson(entry));
        }
    }
    root[kAppearanceKey][kBackgroundImageStrengthKey] = ClampUnit(library.backgroundImageStrength);
    root[kAppearanceKey][kUiSurfaceTransparencyKey] = ClampUnit(library.uiSurfaceTransparency);
    root[kAppearanceKey][kPresetsKey] = json::array();
    for (const ThemeDefinition& preset : library.customPresets) {
        root[kAppearanceKey][kPresetsKey].push_back(ThemePresetRecordToJson(preset));
    }
    return root;
}

bool LoadAppearanceLibraryFromJson(const json& root, AppearanceLibrary& outLibrary, bool* outNeedsMigration = nullptr) {
    outLibrary = {};
    outLibrary.activePresetId = kDefaultPresetToken;
    outLibrary.graphVisualMode = GraphVisualMode::Classic;
    outLibrary.graphSpotlightHaloOutlines = false;
    outLibrary.graphDottedMaskLinks = false;
    outLibrary.graphLineOpacity = 1.0f;
    outLibrary.viewportTiling = RenderTiling::NormalizeSettings({});
    outLibrary.backgroundImageEnabled = false;
    outLibrary.backgroundImagePath.clear();
    outLibrary.backgroundImageStrength = 0.58f;
    outLibrary.uiSurfaceTransparency = 0.18f;
    outLibrary.backgroundImages.clear();
    outLibrary.customPresets.clear();

    if (!root.is_object()) {
        return false;
    }

    const int version = root.value(kVersionKey, 0);
    const ThemeDefinition factoryTheme = MakeFactoryPremiumDarkStudioTheme();

    if (version == 1) {
        if (outNeedsMigration) {
            *outNeedsMigration = true;
        }
        const json appearance = root.value(kAppearanceKey, json::object());
        const std::string legacyToken = appearance.value(kLegacyThemeKey, std::string(kFactoryThemeToken));
        (void)legacyToken;
        outLibrary.activePresetId = kDefaultPresetToken;
        outLibrary.graphVisualMode = GraphVisualMode::Classic;
        outLibrary.graphSpotlightHaloOutlines = false;
        outLibrary.graphDottedMaskLinks = false;
        outLibrary.graphLineOpacity = 1.0f;
        outLibrary.viewportTiling = RenderTiling::NormalizeSettings({});
        return true;
    }

    if (version != 2 &&
        version != 3 &&
        version != 4 &&
        version != 5 &&
        version != 6 &&
        version != static_cast<int>(kAppearanceSettingsVersion)) {
        return false;
    }
    if (version < static_cast<int>(kAppearanceSettingsVersion) && outNeedsMigration) {
        *outNeedsMigration = true;
    }

    const json appearance = root.value(kAppearanceKey, json::object());
    outLibrary.activePresetId = appearance.value(kActivePresetIdKey, std::string(kDefaultPresetToken));
    outLibrary.graphVisualMode = GraphVisualModeFromString(
        appearance.value(kGraphVisualModeKey, std::string(GraphVisualModeToString(GraphVisualMode::Classic))));
    outLibrary.graphSpotlightHaloOutlines = appearance.value(kGraphSpotlightHaloOutlinesKey, false);
    outLibrary.graphDottedMaskLinks = appearance.value(kGraphDottedMaskLinksKey, false);
    outLibrary.graphLineOpacity = ClampUnit(appearance.value(kGraphLineOpacityKey, 1.0f));
    ViewportTilingSettings viewportTiling;
    const json viewportTilingJson = appearance.value(kViewportTilingKey, json::object());
    if (viewportTilingJson.is_object()) {
        viewportTiling.mode = RenderTiling::ViewportTilingModeFromString(
            viewportTilingJson.value(kViewportTilingModeKey, std::string("Auto")));
        viewportTiling.tileSize = viewportTilingJson.value(kViewportTilingTileSizeKey, viewportTiling.tileSize);
        viewportTiling.haloPixels = viewportTilingJson.value(kViewportTilingHaloPixelsKey, viewportTiling.haloPixels);
        viewportTiling.autoPixelThresholdMegapixels =
            viewportTilingJson.value(kViewportTilingAutoThresholdKey, viewportTiling.autoPixelThresholdMegapixels);
        viewportTiling.progressive =
            viewportTilingJson.value(kViewportTilingProgressiveKey, viewportTiling.progressive);
        viewportTiling.debugOverlay =
            viewportTilingJson.value(kViewportTilingDebugOverlayKey, viewportTiling.debugOverlay);
    }
    outLibrary.viewportTiling = RenderTiling::NormalizeSettings(viewportTiling);
    outLibrary.backgroundImageEnabled = appearance.value(kBackgroundImageEnabledKey, false);
    outLibrary.backgroundImagePath = appearance.value(kBackgroundImagePathKey, std::string());
    outLibrary.backgroundImageStrength = ClampUnit(appearance.value(kBackgroundImageStrengthKey, 0.58f));
    outLibrary.uiSurfaceTransparency = ClampUnit(appearance.value(kUiSurfaceTransparencyKey, 0.18f));
    const json backgroundImages = appearance.value(kBackgroundImagesKey, json::array());
    if (backgroundImages.is_array()) {
        std::unordered_set<std::string> usedIds;
        std::unordered_set<std::string> usedNames;
        for (const json& entryValue : backgroundImages) {
            BackgroundImageEntry entry;
            if (!BackgroundImageEntryFromJson(entryValue, entry)) {
                continue;
            }
            entry.id = MakeUniqueId(NormalizeName(entry.id), usedIds);
            usedIds.insert(entry.id);
            entry.displayName = MakeUniqueDisplayName(entry.displayName, usedNames);
            usedNames.insert(entry.displayName);
            outLibrary.backgroundImages.push_back(std::move(entry));
        }
    }
    if (!outLibrary.backgroundImagePath.empty() && !BackgroundImagePathInLibrary(outLibrary, outLibrary.backgroundImagePath)) {
        outLibrary.backgroundImages.push_back(MakeBackgroundImageEntry(
            outLibrary.backgroundImagePath,
            std::filesystem::path(outLibrary.backgroundImagePath).stem().string(),
            outLibrary));
        if (outNeedsMigration) {
            *outNeedsMigration = true;
        }
    }

    const json presets = appearance.value(kPresetsKey, json::array());
    if (!presets.is_array()) {
        return false;
    }

    std::unordered_set<std::string> usedIds = CollectPresetIds(outLibrary);
    std::unordered_set<std::string> usedNames = CollectPresetNames(outLibrary);

    for (const json& presetValue : presets) {
        ThemeDefinition preset = factoryTheme;
        if (!ThemePresetRecordFromJson(presetValue, factoryTheme, preset, nullptr)) {
            continue;
        }

        preset.readOnly = false;
        preset.id = MakeUniqueId(NormalizeName(preset.id), usedIds);
        usedIds.insert(preset.id);
        preset.displayName = MakeUniqueDisplayName(preset.displayName, usedNames);
        usedNames.insert(preset.displayName);
        outLibrary.customPresets.push_back(std::move(preset));
    }

    if (version < 7) {
        const bool oldGraphDefaults =
            outLibrary.graphVisualMode == GraphVisualMode::BlackNodes &&
            outLibrary.graphDottedMaskLinks;
        if (outLibrary.activePresetId == kFactoryThemeToken) {
            outLibrary.activePresetId = kDefaultPresetToken;
        }
        if (oldGraphDefaults) {
            outLibrary.graphVisualMode = GraphVisualMode::Classic;
            outLibrary.graphDottedMaskLinks = false;
        }
    }

    const auto factoryThemes = MakeFactoryThemes();
    const bool activePresetFound = std::any_of(
        factoryThemes.begin(),
        factoryThemes.end(),
        [&](const ThemeDefinition& preset) { return preset.id == outLibrary.activePresetId; }) ||
        std::any_of(
            outLibrary.customPresets.begin(),
            outLibrary.customPresets.end(),
            [&](const ThemeDefinition& preset) { return preset.id == outLibrary.activePresetId; });
    if (!activePresetFound) {
        outLibrary.activePresetId = kDefaultPresetToken;
    }

    return true;
}

ThemeDefinition ResolveActiveTheme(
    const AppearanceLibrary& library,
    const ThemeDefinition& factoryTheme) {
    const auto factoryThemes = MakeFactoryThemes();
    for (const ThemeDefinition& preset : factoryThemes) {
        if (library.activePresetId == preset.id) {
            return preset;
        }
    }

    const ThemeDefinition* activePreset = FindPresetById(library, library.activePresetId);
    if (activePreset != nullptr) {
        return *activePreset;
    }

    return factoryTheme;
}

std::string GetSettingsFileToken() {
    return AppPaths::GetSettingsFilePath().filename().string();
}

} // namespace

ThemeDefinition MakeFactoryPremiumDarkStudioTheme() {
    return BuildFactoryTheme();
}

const char* GraphVisualModeLabel(GraphVisualMode mode) {
    switch (mode) {
        case GraphVisualMode::Classic: return "Classic";
        case GraphVisualMode::BlackNodes: return "Black Nodes";
        case GraphVisualMode::SpotlightPrototype: return "Soft Spotlight Prototype";
    }
    return "Classic";
}

const char* GraphVisualModeDescription(GraphVisualMode mode) {
    switch (mode) {
        case GraphVisualMode::Classic:
            return "Use the default stable node graph rendering.";
        case GraphVisualMode::BlackNodes:
            return "Use near-black nodes with theme-derived accents.";
        case GraphVisualMode::SpotlightPrototype:
            return "Use the experimental graph where nodes read as soft lighter spots in the canvas.";
    }
    return "";
}

std::vector<ThemeDefinition> MakeFactoryThemes() {
    return BuildFactoryThemesInternal();
}

ThemeDefinition CloneTheme(const ThemeDefinition& theme) {
    return theme;
}

bool AreThemesEquivalent(const ThemeDefinition& lhs, const ThemeDefinition& rhs) {
    return ThemeDefinitionEquals(lhs, rhs);
}

std::string MakePresetIdFromName(const std::string& displayName, const AppearanceLibrary& library) {
    const std::unordered_set<std::string> usedIds = CollectPresetIds(library);
    return MakeUniqueId(NormalizeName(displayName), usedIds);
}

std::string MakeUniquePresetName(const std::string& baseName, const AppearanceLibrary& library) {
    const std::unordered_set<std::string> usedNames = CollectPresetNames(library);
    return MakeUniqueDisplayName(baseName, usedNames);
}

const ThemeDefinition* FindPresetById(const AppearanceLibrary& library, const std::string& presetId) {
    for (const ThemeDefinition& preset : library.customPresets) {
        if (preset.id == presetId) {
            return &preset;
        }
    }
    return nullptr;
}

ThemeDefinition* FindPresetById(AppearanceLibrary& library, const std::string& presetId) {
    for (ThemeDefinition& preset : library.customPresets) {
        if (preset.id == presetId) {
            return &preset;
        }
    }
    return nullptr;
}

bool LoadAppearanceLibrary(AppearanceLibrary& outLibrary) {
    outLibrary = {};
    outLibrary.activePresetId = kDefaultPresetToken;
    outLibrary.customPresets.clear();

    const std::filesystem::path settingsPath = GetSettingsPath();
    std::error_code ec;
    if (!std::filesystem::exists(settingsPath, ec) || ec) {
        return false;
    }

    std::ifstream file(settingsPath);
    if (!file.is_open()) {
        return false;
    }

    const json root = json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return false;
    }

    bool needsMigration = false;
    if (!LoadAppearanceLibraryFromJson(root, outLibrary, &needsMigration)) {
        return false;
    }

    if (needsMigration) {
        SaveAppearanceLibrary(outLibrary);
    }

    return true;
}

bool SaveAppearanceLibrary(const AppearanceLibrary& library) {
    const std::filesystem::path settingsPath = GetSettingsPath();
    std::error_code ec;
    const std::filesystem::path parentPath = settingsPath.parent_path();
    if (!parentPath.empty()) {
        std::filesystem::create_directories(parentPath, ec);
        if (ec) {
            return false;
        }
    }

    const json root = AppearanceLibraryToJson(library);

    std::ofstream file(settingsPath, std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file << root.dump(2) << '\n';
    return file.good();
}

bool LoadThemePresetFile(const std::filesystem::path& path, ThemeDefinition& outTheme, std::string* errorMessage) {
    std::ifstream file(path);
    if (!file.is_open()) {
        if (errorMessage) {
            *errorMessage = "Unable to open theme preset file.";
        }
        return false;
    }

    const json root = json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        if (errorMessage) {
            *errorMessage = "Theme preset file is not valid JSON.";
        }
        return false;
    }

    const ThemeDefinition factoryTheme = MakeFactoryPremiumDarkStudioTheme();
    const json* recordNode = nullptr;
    if (root.contains(kPresetKey) && root[kPresetKey].is_object()) {
        recordNode = &root[kPresetKey];
    } else {
        recordNode = &root;
    }

    ThemeDefinition loadedTheme = factoryTheme;
    if (!ThemePresetRecordFromJson(*recordNode, factoryTheme, loadedTheme, errorMessage)) {
        return false;
    }

    const std::unordered_set<std::string> noUsedIds;
    loadedTheme.id = MakeUniqueId(NormalizeName(loadedTheme.id), noUsedIds);
    loadedTheme.displayName = loadedTheme.displayName.empty() ? "Custom Theme" : loadedTheme.displayName;
    loadedTheme.readOnly = false;
    outTheme = loadedTheme;
    return true;
}

bool SaveThemePresetFile(const std::filesystem::path& path, const ThemeDefinition& theme, std::string* errorMessage) {
    std::error_code ec;
    const std::filesystem::path parentPath = path.parent_path();
    if (!parentPath.empty()) {
        std::filesystem::create_directories(parentPath, ec);
        if (ec) {
            if (errorMessage) {
                *errorMessage = "Unable to create the preset export folder.";
            }
            return false;
        }
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        if (errorMessage) {
            *errorMessage = "Unable to write the theme preset file.";
        }
        return false;
    }

    json root = json::object();
    root[kVersionKey] = kThemePresetFileVersion;
    root[kPresetKey] = ThemePresetRecordToJson(theme);
    file << root.dump(2) << '\n';
    if (!file.good() && errorMessage) {
        *errorMessage = "Failed while writing the theme preset file.";
    }
    return file.good();
}

bool UseDarkIconsForCurrentTheme(const AppearanceManager* appearance) {
    if (appearance == nullptr) {
        return false;
    }
    return Luminance(appearance->GetWorkingTheme().colors[ImGuiCol_WindowBg]) >= 0.52f;
}

ImU32 ResolveThemedMonochromeIconTint(const AppearanceManager* appearance, bool emphasized, bool hovered) {
    if (UseDarkIconsForCurrentTheme(appearance)) {
        return emphasized
            ? IM_COL32(18, 22, 26, 255)
            : (hovered ? IM_COL32(48, 54, 60, 232) : IM_COL32(104, 110, 118, 188));
    }
    return emphasized
        ? IM_COL32(255, 255, 255, 255)
        : (hovered ? IM_COL32(220, 220, 220, 230) : IM_COL32(150, 150, 150, 165));
}

AppearanceManager::AppearanceManager()
    : m_FactoryTheme(),
      m_WorkingTheme(),
      m_Library() {
    m_FactoryThemes = MakeFactoryThemes();
    if (!m_FactoryThemes.empty()) {
        m_FactoryTheme = m_FactoryThemes.front();
    } else {
        m_FactoryTheme = MakeFactoryPremiumDarkStudioTheme();
    }
    m_Library = {};
    m_WorkingTheme = ResolveActiveTheme(m_Library, m_FactoryTheme);
}

bool AppearanceManager::Load() {
    m_FactoryThemes = MakeFactoryThemes();
    m_FactoryTheme = m_FactoryThemes.front();
    m_WorkingTheme = m_FactoryTheme;
    m_Library = {};
    m_BackgroundImageRevision = 0;
    m_BackgroundImageRuntimeStatus.clear();
    m_ThemeTransitionActive = false;

    if (!LoadAppearanceLibrary(m_Library)) {
        m_Library = {};
        m_Library.activePresetId = kDefaultPresetToken;
        m_Library.customPresets.clear();
        m_WorkingTheme = ResolveActiveTheme(m_Library, m_FactoryTheme);
        TouchRevision();
        return false;
    }

    m_WorkingTheme = ResolveActiveTheme(m_Library, m_FactoryTheme);
    TouchRevision();
    return true;
}

bool AppearanceManager::Save() const {
    return SaveAppearanceLibrary(m_Library);
}

void AppearanceManager::TouchRevision() {
    ++m_Revision;
    if (m_Revision == 0) {
        m_Revision = 1;
    }
}

void AppearanceManager::StartThemeTransition(const ThemeDefinition& targetTheme, double nowSeconds) {
    m_ThemeTransitionFrom = m_WorkingTheme;
    m_ThemeTransitionTo = targetTheme;
    m_ThemeTransitionTo.readOnly = targetTheme.readOnly;
    m_ThemeTransitionStartTime = nowSeconds;
    m_ThemeTransitionActive = true;
    m_WorkingTheme = m_ThemeTransitionFrom;
    TouchRevision();
}

void AppearanceManager::FinishThemeTransition() {
    if (!m_ThemeTransitionActive) {
        return;
    }
    m_WorkingTheme = m_ThemeTransitionTo;
    m_WorkingTheme.readOnly = m_ThemeTransitionTo.readOnly;
    m_ThemeTransitionActive = false;
    TouchRevision();
}

bool AppearanceManager::UpdateThemeTransition(double nowSeconds) {
    if (!m_ThemeTransitionActive) {
        return false;
    }
    const double elapsed = nowSeconds - m_ThemeTransitionStartTime;
    const float linearT = static_cast<float>(std::clamp(elapsed / std::max(0.001, m_ThemeTransitionDuration), 0.0, 1.0));
    const float easedT = 1.0f - std::pow(1.0f - linearT, 3.0f);
    if (linearT >= 1.0f) {
        FinishThemeTransition();
        return true;
    }
    m_WorkingTheme = BlendThemeDefinition(m_ThemeTransitionFrom, m_ThemeTransitionTo, easedT);
    TouchRevision();
    return true;
}

bool AppearanceManager::IsThemeTransitionActive() const {
    return m_ThemeTransitionActive;
}

const ThemeDefinition& AppearanceManager::GetFactoryTheme() const {
    return m_FactoryTheme;
}

const std::vector<ThemeDefinition>& AppearanceManager::GetFactoryThemes() const {
    return m_FactoryThemes;
}

const ThemeDefinition& AppearanceManager::GetWorkingTheme() const {
    return m_WorkingTheme;
}

ThemeDefinition& AppearanceManager::EditWorkingTheme() {
    return m_WorkingTheme;
}

const AppearanceLibrary& AppearanceManager::GetLibrary() const {
    return m_Library;
}

const ThemeDefinition* AppearanceManager::GetPresetById(const std::string& presetId) const {
    for (const ThemeDefinition& preset : m_FactoryThemes) {
        if (preset.id == presetId) {
            return &preset;
        }
    }
    return FindPresetById(m_Library, presetId);
}

const ThemeDefinition* AppearanceManager::GetActivePreset() const {
    return GetPresetById(m_Library.activePresetId);
}

const std::string& AppearanceManager::GetActivePresetId() const {
    return m_Library.activePresetId;
}

std::string AppearanceManager::GetActivePresetDisplayName() const {
    return m_WorkingTheme.displayName;
}

GraphVisualMode AppearanceManager::GetGraphVisualMode() const {
    return m_Library.graphVisualMode;
}

bool AppearanceManager::GetGraphSpotlightHaloOutlines() const {
    return m_Library.graphSpotlightHaloOutlines;
}

bool AppearanceManager::GetGraphDottedMaskLinks() const {
    return m_Library.graphDottedMaskLinks;
}

float AppearanceManager::GetGraphLineOpacity() const {
    return ClampUnit(m_Library.graphLineOpacity);
}

const ViewportTilingSettings& AppearanceManager::GetViewportTilingSettings() const {
    return m_Library.viewportTiling;
}

bool AppearanceManager::GetBackgroundImageEnabled() const {
    return m_Library.backgroundImageEnabled;
}

bool AppearanceManager::GetSeamlessSurfaceStylingEnabled() const {
    return !m_Library.backgroundImagePath.empty();
}

const std::string& AppearanceManager::GetBackgroundImagePath() const {
    return m_Library.backgroundImagePath;
}

const std::vector<BackgroundImageEntry>& AppearanceManager::GetBackgroundImages() const {
    return m_Library.backgroundImages;
}

float AppearanceManager::GetBackgroundImageStrength() const {
    return ClampUnit(m_Library.backgroundImageStrength);
}

float AppearanceManager::GetUiSurfaceTransparency() const {
    return ClampUnit(m_Library.uiSurfaceTransparency);
}

float AppearanceManager::GetUiSurfaceAlphaMultiplier() const {
    if (!GetSeamlessSurfaceStylingEnabled()) {
        return 1.0f;
    }
    return ComputeSurfaceAlphaMultiplier(m_Library.uiSurfaceTransparency);
}

RuntimeSurfacePalette AppearanceManager::GetRuntimeSurfacePalette() const {
    return BuildRuntimeSurfacePalette(m_WorkingTheme, m_Library.uiSurfaceTransparency, GetSeamlessSurfaceStylingEnabled());
}

std::uint64_t AppearanceManager::GetRevision() const {
    return m_Revision;
}

std::uint64_t AppearanceManager::GetBackgroundImageRevision() const {
    return m_BackgroundImageRevision;
}

const std::string& AppearanceManager::GetBackgroundImageRuntimeStatus() const {
    return m_BackgroundImageRuntimeStatus;
}

std::filesystem::path AppearanceManager::GetResolvedBackgroundImagePath() const {
    return ResolveManagedBackgroundImagePath(m_Library.backgroundImagePath);
}

ImVec4 AppearanceManager::GetEffectiveWindowBackgroundColor() const {
    return GetRuntimeSurfacePalette().appSurface;
}

ImVec4 AppearanceManager::GetEffectiveChildBackgroundColor() const {
    return GetRuntimeSurfacePalette().panelSurface;
}

ImVec4 AppearanceManager::GetEffectivePopupBackgroundColor() const {
    return GetRuntimeSurfacePalette().popupSurface;
}

bool AppearanceManager::ActivePresetIsFactory() const {
    return std::any_of(
        m_FactoryThemes.begin(),
        m_FactoryThemes.end(),
        [&](const ThemeDefinition& preset) { return preset.id == m_Library.activePresetId; });
}

bool AppearanceManager::HasUnsavedChanges() const {
    const ThemeDefinition* activePreset = GetActivePreset();
    if (activePreset == nullptr) {
        return true;
    }
    return !AreThemesEquivalent(m_WorkingTheme, *activePreset);
}

bool AppearanceManager::SelectPresetById(const std::string& presetId) {
    const ThemeDefinition* preset = GetPresetById(presetId);
    if (preset == nullptr) {
        return false;
    }

    if (preset->id == m_Library.activePresetId) {
        return true;
    }

    m_Library.activePresetId = preset->id;
    StartThemeTransition(*preset, ImGui::GetTime());
    return Save();
}

bool AppearanceManager::SetGraphVisualMode(GraphVisualMode mode) {
    if (m_Library.graphVisualMode == mode) {
        return true;
    }
    m_Library.graphVisualMode = mode;
    TouchRevision();
    return Save();
}

bool AppearanceManager::SetGraphSpotlightHaloOutlines(bool enabled) {
    if (m_Library.graphSpotlightHaloOutlines == enabled) {
        return true;
    }
    m_Library.graphSpotlightHaloOutlines = enabled;
    TouchRevision();
    return Save();
}

bool AppearanceManager::SetGraphDottedMaskLinks(bool enabled) {
    if (m_Library.graphDottedMaskLinks == enabled) {
        return true;
    }
    m_Library.graphDottedMaskLinks = enabled;
    TouchRevision();
    return Save();
}

bool AppearanceManager::SetGraphLineOpacity(float opacity) {
    const float clamped = ClampUnit(opacity);
    if (std::abs(m_Library.graphLineOpacity - clamped) < 0.0005f) {
        return true;
    }
    m_Library.graphLineOpacity = clamped;
    TouchRevision();
    return Save();
}

bool AppearanceManager::SetViewportTilingSettings(const ViewportTilingSettings& settings) {
    const ViewportTilingSettings normalized = RenderTiling::NormalizeSettings(settings);
    const ViewportTilingSettings current = RenderTiling::NormalizeSettings(m_Library.viewportTiling);
    if (current.mode == normalized.mode &&
        current.tileSize == normalized.tileSize &&
        current.haloPixels == normalized.haloPixels &&
        current.autoPixelThresholdMegapixels == normalized.autoPixelThresholdMegapixels &&
        current.progressive == normalized.progressive &&
        current.debugOverlay == normalized.debugOverlay) {
        return true;
    }
    m_Library.viewportTiling = normalized;
    TouchRevision();
    return Save();
}

bool AppearanceManager::SetBackgroundImageEnabled(bool enabled) {
    if (m_Library.backgroundImageEnabled == enabled) {
        return true;
    }
    m_Library.backgroundImageEnabled = enabled;
    TouchRevision();
    return Save();
}

bool AppearanceManager::SetBackgroundImageStrength(float strength) {
    const float clamped = ClampUnit(strength);
    if (std::abs(m_Library.backgroundImageStrength - clamped) < 0.0005f) {
        return true;
    }
    m_Library.backgroundImageStrength = clamped;
    TouchRevision();
    return Save();
}

bool AppearanceManager::SetUiSurfaceTransparency(float transparency) {
    const float clamped = ClampUnit(transparency);
    if (std::abs(m_Library.uiSurfaceTransparency - clamped) < 0.0005f) {
        return true;
    }
    m_Library.uiSurfaceTransparency = clamped;
    TouchRevision();
    return Save();
}

bool AppearanceManager::ImportBackgroundImageFromPath(const std::filesystem::path& sourcePath, std::string* errorMessage) {
    std::error_code ec;
    if (sourcePath.empty() || !std::filesystem::exists(sourcePath, ec) || ec) {
        if (errorMessage) {
            *errorMessage = "Selected background image file does not exist.";
        }
        m_BackgroundImageRuntimeStatus = errorMessage ? *errorMessage : "Selected background image file does not exist.";
        return false;
    }

    const std::filesystem::path normalizedSource = sourcePath.lexically_normal();
    for (const BackgroundImageEntry& entry : m_Library.backgroundImages) {
        const std::filesystem::path existingPath = ResolveManagedBackgroundImagePath(entry.path);
        if (existingPath.empty()) {
            continue;
        }
        const bool sameManagedPath =
            normalizedSource == existingPath.lexically_normal() ||
            (std::filesystem::equivalent(sourcePath, existingPath, ec) && !ec);
        ec.clear();
        if (sameManagedPath) {
            m_Library.backgroundImagePath = entry.path;
            m_Library.backgroundImageEnabled = true;
            m_BackgroundImageRuntimeStatus.clear();
            ++m_BackgroundImageRevision;
            TouchRevision();
            return Save();
        }
    }

    const std::filesystem::path destinationPath = MakeUniqueManagedBackgroundImagePath(sourcePath);
    if (destinationPath.empty()) {
        if (errorMessage) {
            *errorMessage = "Unable to resolve the app background image path.";
        }
        m_BackgroundImageRuntimeStatus = errorMessage ? *errorMessage : "Unable to resolve the app background image path.";
        return false;
    }

    if (!destinationPath.parent_path().empty()) {
        std::filesystem::create_directories(destinationPath.parent_path(), ec);
        if (ec) {
            if (errorMessage) {
                *errorMessage = "Unable to create the background image folder.";
            }
            m_BackgroundImageRuntimeStatus = errorMessage ? *errorMessage : "Unable to create the background image folder.";
            return false;
        }
    }

    bool sourceAlreadyManaged = false;
    const std::filesystem::path normalizedDestination = destinationPath.lexically_normal();
    if (normalizedSource == normalizedDestination) {
        sourceAlreadyManaged = true;
    }

    if (!sourceAlreadyManaged) {
        std::filesystem::copy_file(sourcePath, destinationPath, std::filesystem::copy_options::none, ec);
    }
    if (ec) {
        if (errorMessage) {
            *errorMessage = "Unable to copy the selected background image into the app folder.";
        }
        m_BackgroundImageRuntimeStatus = errorMessage ? *errorMessage : "Unable to copy the selected background image into the app folder.";
        return false;
    }

    const std::string fileName = destinationPath.filename().string();
    m_Library.backgroundImages.push_back(MakeBackgroundImageEntry(fileName, sourcePath.stem().string(), m_Library));
    m_Library.backgroundImagePath = fileName;
    m_Library.backgroundImageEnabled = true;
    m_BackgroundImageRuntimeStatus.clear();
    ++m_BackgroundImageRevision;
    TouchRevision();
    return Save();
}

bool AppearanceManager::SelectBackgroundImageById(const std::string& imageId) {
    const BackgroundImageEntry* entry = FindBackgroundImageById(m_Library, imageId);
    if (entry == nullptr || entry->path.empty()) {
        return false;
    }
    if (m_Library.backgroundImagePath == entry->path && m_Library.backgroundImageEnabled) {
        return true;
    }
    m_Library.backgroundImagePath = entry->path;
    m_Library.backgroundImageEnabled = true;
    m_BackgroundImageRuntimeStatus.clear();
    ++m_BackgroundImageRevision;
    TouchRevision();
    return Save();
}

bool AppearanceManager::RemoveBackgroundImageById(const std::string& imageId, std::string* errorMessage) {
    auto it = std::find_if(
        m_Library.backgroundImages.begin(),
        m_Library.backgroundImages.end(),
        [&](const BackgroundImageEntry& entry) { return entry.id == imageId; });
    if (it == m_Library.backgroundImages.end()) {
        return false;
    }

    const bool removingActive = it->path == m_Library.backgroundImagePath;
    const std::filesystem::path managedPath = ResolveManagedBackgroundImagePath(it->path);
    std::error_code ec;
    if (!managedPath.empty()) {
        std::filesystem::remove(managedPath, ec);
        if (ec) {
            if (errorMessage) {
                *errorMessage = "Unable to remove the managed background image file.";
            }
            m_BackgroundImageRuntimeStatus = errorMessage ? *errorMessage : "Unable to remove the managed background image file.";
            return false;
        }
    }

    m_Library.backgroundImages.erase(it);
    if (removingActive) {
        if (!m_Library.backgroundImages.empty()) {
            m_Library.backgroundImagePath = m_Library.backgroundImages.front().path;
            m_Library.backgroundImageEnabled = true;
        } else {
            m_Library.backgroundImagePath.clear();
            m_Library.backgroundImageEnabled = false;
        }
        ++m_BackgroundImageRevision;
    }
    m_BackgroundImageRuntimeStatus.clear();
    TouchRevision();
    return Save();
}

bool AppearanceManager::ClearBackgroundImage(std::string* errorMessage) {
    (void)errorMessage;
    if (!m_Library.backgroundImageEnabled) {
        return true;
    }
    m_Library.backgroundImageEnabled = false;
    m_BackgroundImageRuntimeStatus.clear();
    ++m_BackgroundImageRevision;
    TouchRevision();
    return Save();
}

void AppearanceManager::SetBackgroundImageRuntimeStatus(std::string statusMessage) {
    m_BackgroundImageRuntimeStatus = std::move(statusMessage);
}

bool AppearanceManager::ResetWorkingTheme() {
    const ThemeDefinition* activePreset = GetActivePreset();
    if (activePreset == nullptr) {
        m_WorkingTheme = m_FactoryTheme;
        m_ThemeTransitionActive = false;
        TouchRevision();
        return true;
    }

    m_WorkingTheme = *activePreset;
    m_ThemeTransitionActive = false;
    TouchRevision();
    return true;
}

bool AppearanceManager::SaveWorkingTheme() {
    if (ActivePresetIsFactory()) {
        return false;
    }

    ThemeDefinition* preset = FindPresetById(m_Library, m_Library.activePresetId);
    if (preset == nullptr) {
        return false;
    }

    m_WorkingTheme.readOnly = false;
    m_ThemeTransitionActive = false;
    *preset = m_WorkingTheme;
    preset->id = m_Library.activePresetId;
    preset->readOnly = false;
    TouchRevision();
    return Save();
}

bool AppearanceManager::SaveWorkingThemeAsNew(std::string displayName) {
    displayName = MakeUniquePresetName(displayName, m_Library);
    const std::string presetId = MakePresetIdFromName(displayName, m_Library);

    ThemeDefinition preset = m_WorkingTheme;
    preset.id = presetId;
    preset.displayName = displayName;
    preset.readOnly = false;

    m_Library.customPresets.push_back(preset);
    m_Library.activePresetId = presetId;
    m_WorkingTheme = preset;
    m_ThemeTransitionActive = false;
    TouchRevision();
    return Save();
}

bool AppearanceManager::DuplicateWorkingTheme() {
    return SaveWorkingThemeAsNew(m_WorkingTheme.displayName + " Copy");
}

bool AppearanceManager::ImportPreset(const std::filesystem::path& path, std::string* errorMessage) {
    ThemeDefinition importedTheme = m_FactoryTheme;
    if (!LoadThemePresetFile(path, importedTheme, errorMessage)) {
        return false;
    }

    importedTheme.displayName = MakeUniquePresetName(importedTheme.displayName, m_Library);
    importedTheme.id = MakePresetIdFromName(importedTheme.displayName, m_Library);
    importedTheme.readOnly = false;

    m_Library.customPresets.push_back(importedTheme);
    m_Library.activePresetId = importedTheme.id;
    m_WorkingTheme = importedTheme;
    m_ThemeTransitionActive = false;
    TouchRevision();
    return Save();
}

bool AppearanceManager::ExportWorkingTheme(const std::filesystem::path& path, std::string* errorMessage) const {
    return SaveThemePresetFile(path, m_WorkingTheme, errorMessage);
}

void AppearanceManager::ApplyCurrentTheme(ImGuiIO& io, ImGuiStyle& style) const {
    ImGui::StyleColorsDark(&style);
    const RuntimeSurfacePalette palette = GetRuntimeSurfacePalette();
    const bool seamlessSurfaceStylingEnabled = GetSeamlessSurfaceStylingEnabled();

    // Premium hardcoded style values for a clean, consistent modern look
    style.Alpha = 1.0f;
    style.WindowPadding = ImVec2(16.0f, 16.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.WindowRounding = 12.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 12.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabRounding = 8.0f;
    style.TabRounding = 8.0f;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.0f);
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;
    style.CurveTessellationTol = 1.25f;

    for (int index = 0; index < ImGuiCol_COUNT; ++index) {
        style.Colors[index] = m_WorkingTheme.colors[static_cast<std::size_t>(index)];
    }

    if (seamlessSurfaceStylingEnabled) {
        style.Colors[ImGuiCol_WindowBg] = palette.appSurface;
        style.Colors[ImGuiCol_ChildBg] = palette.panelSurface;
        style.Colors[ImGuiCol_PopupBg] = palette.popupSurface;
        style.Colors[ImGuiCol_Header] = palette.chromeSurface;
        style.Colors[ImGuiCol_HeaderHovered] = palette.controlSurfaceHovered;
        style.Colors[ImGuiCol_HeaderActive] = palette.controlSurfaceActive;
        style.Colors[ImGuiCol_FrameBg] = palette.controlSurface;
        style.Colors[ImGuiCol_FrameBgHovered] = palette.controlSurfaceHovered;
        style.Colors[ImGuiCol_FrameBgActive] = palette.controlSurfaceActive;
        style.Colors[ImGuiCol_Border] = palette.border;
        style.Colors[ImGuiCol_Separator] = palette.separator;
        style.Colors[ImGuiCol_SeparatorHovered] = palette.border;
        style.Colors[ImGuiCol_SeparatorActive] = palette.border;
        style.Colors[ImGuiCol_DockingEmptyBg] = palette.appSurface;
    }

    io.FontGlobalScale = 1.0f; // Hardcode global font scale for consistent typography
}

void AppearanceManager::SetupFonts(ImGuiIO& io) const {
    if (io.Fonts == nullptr) {
        return;
    }

    if (io.Fonts->Fonts.Size > 0) {
        io.FontDefault = io.Fonts->Fonts[0];
        return;
    }

    ImFontConfig fontConfig;
    fontConfig.OversampleH = 3;
    fontConfig.OversampleV = 2;
    fontConfig.PixelSnapH = true;
    fontConfig.FontDataOwnedByAtlas = false;

    ImFont* font = io.Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(EmbeddedCompositeFont::kRobotoMediumTtf),
        static_cast<int>(EmbeddedCompositeFont::kRobotoMediumTtfSize),
        16.0f,
        &fontConfig);

    if (font == nullptr) {
        font = io.Fonts->AddFontDefault();
    }

    io.FontDefault = font;
}

ImVec4 AppearanceManager::GetClearColor() const {
    return GetRuntimeSurfacePalette().appSurface;
}

} // namespace StackAppearance
