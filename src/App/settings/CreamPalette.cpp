#include "CreamPalette.h"
#include <algorithm>
#include <cmath>

namespace StackAppearance {
namespace {
ImVec4 Hex(unsigned value) {
    return ImVec4(((value >> 16) & 255) / 255.0f, ((value >> 8) & 255) / 255.0f, (value & 255) / 255.0f, 1);
}
ImVec4 Mix(ImVec4 a, ImVec4 b, float t) {
    return ImVec4(a.x + (b.x-a.x)*t, a.y + (b.y-a.y)*t, a.z + (b.z-a.z)*t, 1);
}
CreamPalette MakeMonochromeDark() {
    CreamPalette p;
    p.id = kMonochromeDarkPaletteId;
    p.name = "Neutral";
    p.colorPolicy = PaletteColorPolicy::Monochrome;
    p.seeds = {Hex(0xE0E0E0), Hex(0x000000), Hex(0xB8B8B8), Hex(0xA0A0A0),
        Hex(0x181818), Hex(0x909090), Hex(0x0A0A0A), Hex(0xC8C8C8), Hex(0x787878)};
    return p;
}
ResolvedCreamPalette ResolveMonochromePalette(const CreamPalette& p) {
    ResolvedCreamPalette r{};
    r.colorPolicy = PaletteColorPolicy::Monochrome;
    const float brightness = std::isfinite(p.brightness) ? std::clamp(p.brightness, 0.f, 1.f) : 0.f;
    const auto shade = [brightness](unsigned dark, unsigned gray) { return Mix(Hex(dark), Hex(gray), brightness); };
    // Share the same representable base across float clears and packed ImGui
    // geometry; fractional near-black values otherwise cross byte boundaries.
    const float base = std::round(brightness * 36.f) / 255.f;
    const ImVec4 black(base,base,base,1.f), panel=black, raised=shade(0x181818,0x2D2D2D);
    const ImVec4 popup=shade(0x101010,0x2D2D2D);
    const ImVec4 control=shade(0x141414,0x333333), hover=shade(0x202020,0x3F3F3F), active=shade(0x2A2A2A,0x4B4B4B);
    const ImVec4 text=Hex(0xE0E0E0), muted=shade(0xA8A8A8,0xC0C0C0), disabled=shade(0x737373,0x959595);
    const ImVec4 border=shade(0x383838,0x565656), focus=Hex(0xC8C8C8), subtle=Hex(0x242424);
    auto surface=[&](ImVec4 background, ImVec4 foreground, ImVec4 hovered, ImVec4 pressed) {
        SurfaceColors role{};
        role.background=background; role.foreground=foreground; role.mutedForeground=muted;
        role.border=border; role.hovered=hovered; role.active=pressed; role.focus=focus;
        role.disabledBackground=control; role.disabledForeground=disabled;
        return role;
    };
    r.canvas=black; r.surface=raised; r.text=text; r.mutedText=muted; r.border=border;
    r.focus=focus; r.selection=Hex(0xB0B0B0); r.error=Hex(0xD0D0D0);
    r.workspace=surface(black,text,subtle,active);
    r.panel=surface(panel,text,hover,active);
    r.node=surface(raised,text,hover,active);
    r.control=surface(control,text,hover,active);
    r.popup=surface(popup,text,hover,active);
    r.navigation=surface(panel,text,hover,active);
    r.section=surface(raised,text,hover,active);
    r.navigationAccent=Hex(0xA0A0A0); r.enabledAccent=Hex(0xB8B8B8);
    for (size_t i=0;i<r.groups.size();++i) {
        const float level=0.055f+static_cast<float>(i)*0.012f;
        const ImVec4 group=Mix(ImVec4(level,level,level,1.0f), Hex(0x303030 + static_cast<unsigned>(i)*0x020202), brightness);
        r.groups[i]=surface(group,text,hover,active);
    }
    r.primaryAction=surface(shade(0x303030,0x454545),Hex(0xF0F0F0),shade(0x3A3A3A,0x4A4A4A),shade(0x464646,0x505050));
    r.primaryAction.mutedForeground=Hex(0xD0D0D0);
    r.primaryAction.disabledBackground=raised; r.primaryAction.disabledForeground=Hex(0x7A7A7A);
    auto& n=r.nodeAppearance;
    n.surface=shade(0x101010,0x2D2D2D); n.text=text; n.mutedText=muted; n.disabledText=disabled;
    n.number=Hex(0xD8D8D8); n.numberHovered=Hex(0xEEEEEE); n.numberActive=Hex(0xFFFFFF);
    n.reset=muted; n.resetHovered=Hex(0xD0D0D0); n.resetActive=Hex(0xF0F0F0);
    n.focus=focus; n.error=Hex(0xD8D8D8); n.selection=Hex(0xB8B8B8); n.socketHalo=black;
    n.control=r.control;
    r.imageSocket=Hex(0xD8D8D8); r.maskSocket=Hex(0xB8B8B8);
    r.analysisSocket=Hex(0x989898); r.valueSocket=Hex(0xC8C8C8); r.rawSocket=Hex(0x787878);
    r.colors.fill(muted);
    auto& c=r.colors;
    c[ImGuiCol_Text]=text; c[ImGuiCol_TextDisabled]=disabled;
    c[ImGuiCol_WindowBg]=black; c[ImGuiCol_ChildBg]=panel; c[ImGuiCol_PopupBg]=popup;
    c[ImGuiCol_Border]=border; c[ImGuiCol_BorderShadow]=ImVec4(0,0,0,0);
    c[ImGuiCol_FrameBg]=control; c[ImGuiCol_FrameBgHovered]=hover; c[ImGuiCol_FrameBgActive]=active;
    c[ImGuiCol_TitleBg]=black; c[ImGuiCol_TitleBgActive]=panel; c[ImGuiCol_TitleBgCollapsed]=black;
    c[ImGuiCol_MenuBarBg]=black; c[ImGuiCol_ScrollbarBg]=black;
    c[ImGuiCol_ScrollbarGrab]=border; c[ImGuiCol_ScrollbarGrabHovered]=Hex(0x585858); c[ImGuiCol_ScrollbarGrabActive]=Hex(0x707070);
    c[ImGuiCol_CheckMark]=r.enabledAccent; c[ImGuiCol_SliderGrab]=Hex(0x989898); c[ImGuiCol_SliderGrabActive]=focus;
    c[ImGuiCol_Button]=control; c[ImGuiCol_ButtonHovered]=hover; c[ImGuiCol_ButtonActive]=active;
    c[ImGuiCol_Header]=raised; c[ImGuiCol_HeaderHovered]=hover; c[ImGuiCol_HeaderActive]=active;
    c[ImGuiCol_Separator]=border; c[ImGuiCol_SeparatorHovered]=Hex(0x686868); c[ImGuiCol_SeparatorActive]=focus;
    c[ImGuiCol_ResizeGrip]=border; c[ImGuiCol_ResizeGripHovered]=Hex(0x787878); c[ImGuiCol_ResizeGripActive]=focus;
    c[ImGuiCol_Tab]=panel; c[ImGuiCol_TabHovered]=hover; c[ImGuiCol_TabSelected]=raised;
    c[ImGuiCol_TabSelectedOverline]=Hex(0x989898); c[ImGuiCol_TabDimmed]=black;
    c[ImGuiCol_TabDimmedSelected]=panel; c[ImGuiCol_TabDimmedSelectedOverline]=Hex(0x686868);
    c[ImGuiCol_DockingPreview]=ImVec4(0.65f,0.65f,0.65f,0.28f); c[ImGuiCol_DockingEmptyBg]=black;
    c[ImGuiCol_PlotLines]=Hex(0xB0B0B0); c[ImGuiCol_PlotLinesHovered]=Hex(0xEEEEEE);
    c[ImGuiCol_PlotHistogram]=Hex(0x909090); c[ImGuiCol_PlotHistogramHovered]=Hex(0xD8D8D8);
    c[ImGuiCol_TableHeaderBg]=raised; c[ImGuiCol_TableBorderStrong]=border; c[ImGuiCol_TableBorderLight]=subtle;
    c[ImGuiCol_TableRowBg]=ImVec4(0,0,0,0); c[ImGuiCol_TableRowBgAlt]=ImVec4(1,1,1,0.025f);
    c[ImGuiCol_TextSelectedBg]=ImVec4(0.55f,0.55f,0.55f,0.35f);
    c[ImGuiCol_DragDropTarget]=focus; c[ImGuiCol_NavCursor]=focus;
    c[ImGuiCol_NavWindowingHighlight]=Hex(0xC8C8C8); c[ImGuiCol_NavWindowingDimBg]=ImVec4(0,0,0,0.72f);
    c[ImGuiCol_ModalWindowDimBg]=ImVec4(0,0,0,0.72f);
    return r;
}
}
CreamPalette::CreamPalette() : seeds{Hex(0xF2E3C2), Hex(0x3B3028), Hex(0xD9B26F),
    Hex(0xB86F3C), Hex(0x7E4E2D), Hex(0x5E6B4E), Hex(0x2F3A2F), Hex(0x668A9E), Hex(0x5E9690)} {}
const std::array<const char*, 9>& CreamSeedNames() {
    static const std::array<const char*, 9> names{"Cream", "Dark base", "Ochre", "Rust", "Walnut", "Olive", "Forest", "Dusty blue", "Muted teal"};
    return names;
}
std::vector<CreamPalette> FactoryCreamPalettes() {
    return {MakeMonochromeDark()};
}
ResolvedCreamPalette ResolveCreamPalette(const CreamPalette& p) {
    return ResolveMonochromePalette(p);
}
bool EqualCreamPalettes(const CreamPalette& a, const CreamPalette& b) {
    if (a.brightness != b.brightness || a.id != b.id || a.name != b.name || a.numberAccent != b.numberAccent || a.resetAccent != b.resetAccent || a.colorPolicy != b.colorPolicy) return false;
    for (size_t i=0;i<a.seeds.size();++i)
        if (a.seeds[i].x != b.seeds[i].x || a.seeds[i].y != b.seeds[i].y || a.seeds[i].z != b.seeds[i].z) return false;
    return true;
}
bool IsMonochrome(const ImVec4& color, float tolerance) {
    return std::abs(color.x-color.y)<=tolerance && std::abs(color.y-color.z)<=tolerance;
}
ImVec4 ResolveSemanticUiColor(const ResolvedCreamPalette& palette, SemanticUiColor role, ImVec4 fullColor) {
    if (palette.colorPolicy!=PaletteColorPolicy::Monochrome) return fullColor;
    switch (role) {
        case SemanticUiColor::Error: return ImVec4(0.86f,0.86f,0.86f,fullColor.w);
        case SemanticUiColor::Warning: return ImVec4(0.72f,0.72f,0.72f,fullColor.w);
        case SemanticUiColor::Success: return ImVec4(0.66f,0.66f,0.66f,fullColor.w);
        case SemanticUiColor::Info: return ImVec4(0.78f,0.78f,0.78f,fullColor.w);
        case SemanticUiColor::Selection: return ImVec4(0.82f,0.82f,0.82f,fullColor.w);
        case SemanticUiColor::ChannelRed: return ImVec4(0.90f,0.90f,0.90f,fullColor.w);
        case SemanticUiColor::ChannelGreen: return ImVec4(0.75f,0.75f,0.75f,fullColor.w);
        case SemanticUiColor::ChannelBlue: return ImVec4(0.60f,0.60f,0.60f,fullColor.w);
        case SemanticUiColor::ChannelAlpha: return ImVec4(0.48f,0.48f,0.48f,fullColor.w);
    }
    return fullColor;
}
float CreamContrastRatio(ImVec4 a, ImVec4 b) {
    auto luminance = [](ImVec4 c) {
        auto linear = [](float v) { return v <= 0.04045f ? v/12.92f : std::pow((v+0.055f)/1.055f,2.4f); };
        return 0.2126f*linear(c.x)+0.7152f*linear(c.y)+0.0722f*linear(c.z);
    };
    const float x=luminance(a), y=luminance(b);
    return (std::max(x,y)+0.05f)/(std::min(x,y)+0.05f);
}
bool CreamPaletteHasReadableText(const ResolvedCreamPalette& p) {
    const auto& n=p.nodeAppearance;
    for (auto color : {n.text,n.mutedText,n.number,n.numberHovered,n.numberActive,n.reset,n.resetHovered,n.resetActive,n.error})
        if (CreamContrastRatio(color,n.surface)<4.5f) return false;
    if (CreamContrastRatio(n.disabledText,n.surface)<3.0f) return false;
    for (const auto* role : {&p.workspace,&p.panel,&p.node,&p.control,&p.popup,&p.primaryAction,&p.navigation,&p.section,
        &p.groups[0],&p.groups[1],&p.groups[2],&p.groups[3],&p.groups[4],&p.groups[5]}) {
        for (const auto background : {role->background,role->hovered,role->active}) {
            if (CreamContrastRatio(role->foreground,background)<4.5f ||
                CreamContrastRatio(role->mutedForeground,background)<4.5f) return false;
        }
        if (CreamContrastRatio(role->disabledForeground,role->disabledBackground)<3.0f) return false;
    }
    return true;
}
}
