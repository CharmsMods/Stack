#include "AppearanceTheme.h"

#include "Composite/EmbeddedCompositeFont.h"
#include "Persistence/StackBinaryFormat.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_set>

namespace StackAppearance {
namespace {

using json = StackBinaryFormat::json;

constexpr const char* kSettingsFileName = "StackSettings.json";
constexpr const char* kVersionKey = "version";
constexpr const char* kAppearanceKey = "appearance";
constexpr const char* kActivePresetIdKey = "activePresetId";
constexpr const char* kPresetsKey = "presets";
constexpr const char* kPresetKey = "preset";
constexpr const char* kIdKey = "id";
constexpr const char* kNameKey = "name";
constexpr const char* kThemeKey = "theme";
constexpr const char* kLegacyThemeKey = "themePreset";
constexpr const char* kFactoryThemeToken = "premium-dark-studio";
constexpr float kMinTextScale = 0.75f;
constexpr float kMaxTextScale = 1.60f;

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

ImVec2 Blend(const ImVec2& from, const ImVec2& to, float t) {
    const float clampedT = std::clamp(t, 0.0f, 1.0f);
    return ImVec2(
        from.x + (to.x - from.x) * clampedT,
        from.y + (to.y - from.y) * clampedT);
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
    std::error_code ec;
    const std::filesystem::path current = std::filesystem::current_path(ec);
    if (ec) {
        return std::filesystem::path(kSettingsFileName);
    }
    return current / kSettingsFileName;
}

bool ThemeDefinitionEquals(const ThemeDefinition& lhs, const ThemeDefinition& rhs) {
    return ThemeDefinitionToJson(lhs) == ThemeDefinitionToJson(rhs);
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

std::vector<ThemeDefinition> BuildFactoryThemesInternal() {
    return { BuildDarkTheme(), BuildLightTheme(), BuildSolarizedTheme(), BuildSolarizedLightTheme() };
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
    root[kAppearanceKey][kActivePresetIdKey] = library.activePresetId.empty() ? std::string(kFactoryThemeToken) : library.activePresetId;
    root[kAppearanceKey][kPresetsKey] = json::array();
    for (const ThemeDefinition& preset : library.customPresets) {
        root[kAppearanceKey][kPresetsKey].push_back(ThemePresetRecordToJson(preset));
    }
    return root;
}

bool LoadAppearanceLibraryFromJson(const json& root, AppearanceLibrary& outLibrary, bool* outNeedsMigration = nullptr) {
    outLibrary = {};
    outLibrary.activePresetId = kFactoryThemeToken;
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
        outLibrary.activePresetId = kFactoryThemeToken;
        return true;
    }

    if (version != static_cast<int>(kAppearanceSettingsVersion)) {
        return false;
    }

    const json appearance = root.value(kAppearanceKey, json::object());
    outLibrary.activePresetId = appearance.value(kActivePresetIdKey, std::string(kFactoryThemeToken));

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
        outLibrary.activePresetId = kFactoryThemeToken;
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
    return kSettingsFileName;
}

} // namespace

ThemeDefinition MakeFactoryPremiumDarkStudioTheme() {
    return BuildFactoryTheme();
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
    outLibrary.activePresetId = kFactoryThemeToken;
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
    m_WorkingTheme = m_FactoryTheme;
}

bool AppearanceManager::Load() {
    m_FactoryThemes = MakeFactoryThemes();
    m_FactoryTheme = m_FactoryThemes.front();
    m_WorkingTheme = m_FactoryTheme;
    m_Library = {};

    if (!LoadAppearanceLibrary(m_Library)) {
        m_Library.activePresetId = kFactoryThemeToken;
        m_Library.customPresets.clear();
        return false;
    }

    m_WorkingTheme = ResolveActiveTheme(m_Library, m_FactoryTheme);
    return true;
}

bool AppearanceManager::Save() const {
    return SaveAppearanceLibrary(m_Library);
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
    m_WorkingTheme = *preset;
    m_WorkingTheme.readOnly = preset->readOnly;
    return Save();
}

bool AppearanceManager::ResetWorkingTheme() {
    const ThemeDefinition* activePreset = GetActivePreset();
    if (activePreset == nullptr) {
        m_WorkingTheme = m_FactoryTheme;
        return true;
    }

    m_WorkingTheme = *activePreset;
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
    *preset = m_WorkingTheme;
    preset->id = m_Library.activePresetId;
    preset->readOnly = false;
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
    return Save();
}

bool AppearanceManager::ExportWorkingTheme(const std::filesystem::path& path, std::string* errorMessage) const {
    return SaveThemePresetFile(path, m_WorkingTheme, errorMessage);
}

void AppearanceManager::ApplyCurrentTheme(ImGuiIO& io, ImGuiStyle& style) const {
    ImGui::StyleColorsDark(&style);

    style.Alpha = m_WorkingTheme.style.alpha;
    style.WindowPadding = m_WorkingTheme.style.windowPadding;
    style.FramePadding = m_WorkingTheme.style.framePadding;
    style.CellPadding = m_WorkingTheme.style.cellPadding;
    style.ItemSpacing = m_WorkingTheme.style.itemSpacing;
    style.ItemInnerSpacing = m_WorkingTheme.style.itemInnerSpacing;
    style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
    style.IndentSpacing = m_WorkingTheme.style.indentSpacing;
    style.ScrollbarSize = m_WorkingTheme.style.scrollbarSize;
    style.GrabMinSize = m_WorkingTheme.style.grabMinSize;
    style.WindowBorderSize = m_WorkingTheme.style.windowBorderSize;
    style.ChildBorderSize = m_WorkingTheme.style.childBorderSize;
    style.PopupBorderSize = m_WorkingTheme.style.popupBorderSize;
    style.FrameBorderSize = m_WorkingTheme.style.frameBorderSize;
    style.TabBorderSize = m_WorkingTheme.style.tabBorderSize;
    style.WindowRounding = m_WorkingTheme.style.windowRounding;
    style.ChildRounding = m_WorkingTheme.style.childRounding;
    style.FrameRounding = m_WorkingTheme.style.frameRounding;
    style.PopupRounding = m_WorkingTheme.style.popupRounding;
    style.ScrollbarRounding = m_WorkingTheme.style.scrollbarRounding;
    style.GrabRounding = m_WorkingTheme.style.grabRounding;
    style.TabRounding = m_WorkingTheme.style.tabRounding;
    style.WindowTitleAlign = m_WorkingTheme.style.windowTitleAlign;
    style.ButtonTextAlign = m_WorkingTheme.style.buttonTextAlign;
    style.SelectableTextAlign = m_WorkingTheme.style.selectableTextAlign;
    style.AntiAliasedLines = m_WorkingTheme.style.antiAliasedLines;
    style.AntiAliasedFill = m_WorkingTheme.style.antiAliasedFill;
    style.CurveTessellationTol = m_WorkingTheme.style.curveTessellationTol;

    for (int index = 0; index < ImGuiCol_COUNT; ++index) {
        style.Colors[index] = m_WorkingTheme.colors[static_cast<std::size_t>(index)];
    }

    io.FontGlobalScale = std::clamp(m_WorkingTheme.textScale, kMinTextScale, kMaxTextScale);
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
    return m_WorkingTheme.colors[ImGuiCol_DockingEmptyBg];
}

} // namespace StackAppearance
