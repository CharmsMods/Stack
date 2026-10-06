#pragma once
#include <array>
#include <string>
#include <vector>
#include <imgui.h>

namespace StackAppearance {
inline constexpr const char* kMonochromeDarkPaletteId = "monochrome-dark";
inline constexpr const char* kMonochromeGrayPaletteId = "monochrome-gray";
// Future dark palettes should resolve these same roles. Mixing modes belongs
// here, not in individual widgets; no dark-mode interpolation is enabled yet.
enum class NodeAccent { Blue, Teal, Olive };
enum class PaletteColorPolicy { FullColor, Monochrome };
struct CreamPalette {
    std::string id = "harvest-cream";
    std::string name = "Harvest Cream";
    std::array<ImVec4, 9> seeds;
    NodeAccent numberAccent = NodeAccent::Blue;
    NodeAccent resetAccent = NodeAccent::Teal;
    PaletteColorPolicy colorPolicy = PaletteColorPolicy::FullColor;
    CreamPalette();
};
struct SurfaceColors {
    ImVec4 background, foreground, mutedForeground, border;
    ImVec4 hovered, active, focus, disabledBackground, disabledForeground;
};
struct NodeAppearance {
    ImVec4 surface, text, mutedText, disabledText;
    ImVec4 number, numberHovered, numberActive;
    ImVec4 reset, resetHovered, resetActive;
    ImVec4 focus, error, selection, socketHalo;
    SurfaceColors control;
};
enum class RetroAccent { Neutral, Olive, Blue, Teal, Ochre, Rust, Count };
struct ResolvedCreamPalette {
    PaletteColorPolicy colorPolicy = PaletteColorPolicy::FullColor;
    NodeAppearance nodeAppearance;
    SurfaceColors navigation, section;
    ImVec4 navigationAccent, enabledAccent;
    std::array<SurfaceColors, static_cast<size_t>(RetroAccent::Count)> groups;
    SurfaceColors workspace, panel, node, control, popup, primaryAction;
    ImVec4 selection;
    std::array<ImVec4, ImGuiCol_COUNT> colors;
    ImVec4 canvas, surface, text, mutedText, border, focus, error;
    ImVec4 imageSocket, maskSocket, analysisSocket, valueSocket, rawSocket;
};
enum class SemanticUiColor { Info, Success, Warning, Error, Selection, ChannelRed, ChannelGreen, ChannelBlue, ChannelAlpha };
const std::array<const char*, 9>& CreamSeedNames();
std::vector<CreamPalette> FactoryCreamPalettes();
ResolvedCreamPalette ResolveCreamPalette(const CreamPalette& palette);
bool EqualCreamPalettes(const CreamPalette& a, const CreamPalette& b);
float CreamContrastRatio(ImVec4 a, ImVec4 b);
bool CreamPaletteHasReadableText(const ResolvedCreamPalette& palette);
bool IsMonochrome(const ImVec4& color, float tolerance = 0.0005f);
ImVec4 ResolveSemanticUiColor(const ResolvedCreamPalette& palette, SemanticUiColor role, ImVec4 fullColor);
} // namespace StackAppearance
