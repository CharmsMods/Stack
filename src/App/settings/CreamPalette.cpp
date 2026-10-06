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
CreamPalette Make(const char* id, const char* name, std::array<unsigned, 6> swatches) {
    CreamPalette p;
    p.id = id; p.name = name;
    p.seeds[0] = Hex(swatches[0]);
    for (int i = 1; i < 6; ++i) p.seeds[i+1] = Hex(swatches[i]);
    p.seeds[5]=Hex(0x727B55); p.seeds[6]=Hex(0x35483D);
    return p;
}
CreamPalette MakeMonochromeDark() {
    CreamPalette p;
    p.id = kMonochromeDarkPaletteId;
    p.name = "Dark";
    p.colorPolicy = PaletteColorPolicy::Monochrome;
    p.seeds = {Hex(0xE0E0E0), Hex(0x000000), Hex(0xB8B8B8), Hex(0xA0A0A0),
        Hex(0x181818), Hex(0x909090), Hex(0x0A0A0A), Hex(0xC8C8C8), Hex(0x787878)};
    return p;
}
CreamPalette MakeMonochromeGray() {
    CreamPalette p = MakeMonochromeDark();
    p.id = kMonochromeGrayPaletteId;
    p.name = "Gray";
    p.seeds[1] = Hex(0x454545);
    return p;
}

ResolvedCreamPalette ResolveMonochromePalette(const CreamPalette& p);
ResolvedCreamPalette ResolveMonochromeGrayPalette(const CreamPalette& p) {
    ResolvedCreamPalette r = ResolveMonochromePalette(p);
    const ImVec4 gray = Hex(0x454545), panel = gray, raised = Hex(0x525252);
    const ImVec4 control = Hex(0x565656), hover = Hex(0x606060), active = Hex(0x686868);
    const ImVec4 text = Hex(0xFFFFFF), muted = Hex(0xF4F4F4), disabled = Hex(0xC8C8C8);
    const ImVec4 border = Hex(0x8A8A8A), focus = Hex(0xFFFFFF);
    auto surface = [&](ImVec4 background) {
        SurfaceColors role{};
        role.background = background; role.foreground = text; role.mutedForeground = muted;
        role.border = border; role.hovered = hover; role.active = active; role.focus = focus;
        role.disabledBackground = control; role.disabledForeground = disabled;
        return role;
    };
    r.canvas = gray; r.surface = raised; r.text = text; r.mutedText = muted;
    r.border = border; r.focus = focus; r.selection = Hex(0xB0B0B0);
    r.workspace = surface(gray); r.panel = surface(panel); r.node = surface(raised);
    r.control = surface(control); r.popup = surface(panel); r.navigation = surface(panel);
    r.section = surface(raised);
    for (size_t i = 0; i < r.groups.size(); ++i)
        r.groups[i] = surface(Hex(0x535353 + static_cast<unsigned>(i) * 0x030303));
    r.navigationAccent = focus; r.enabledAccent = focus;
    r.primaryAction = surface(Hex(0x646464));
    r.primaryAction.disabledBackground = panel;
    auto& n = r.nodeAppearance;
    n.surface = raised; n.text = text; n.mutedText = muted; n.disabledText = disabled;
    n.number = text; n.numberHovered = text; n.numberActive = text;
    n.reset = muted; n.resetHovered = text; n.resetActive = text;
    n.focus = focus; n.error = text; n.selection = border; n.socketHalo = gray;
    n.control = r.control;
    r.imageSocket = text; r.maskSocket = border; r.analysisSocket = muted;
    r.valueSocket = focus; r.rawSocket = disabled;
    auto& c = r.colors;
    c.fill(gray);
    c[ImGuiCol_Text] = text; c[ImGuiCol_TextDisabled] = disabled;
    c[ImGuiCol_WindowBg] = gray; c[ImGuiCol_ChildBg] = panel; c[ImGuiCol_PopupBg] = panel;
    c[ImGuiCol_Border] = border; c[ImGuiCol_BorderShadow] = ImVec4(0,0,0,0);
    c[ImGuiCol_FrameBg] = control; c[ImGuiCol_FrameBgHovered] = hover; c[ImGuiCol_FrameBgActive] = active;
    c[ImGuiCol_TitleBg] = gray; c[ImGuiCol_TitleBgActive] = panel; c[ImGuiCol_TitleBgCollapsed] = gray;
    c[ImGuiCol_MenuBarBg] = gray; c[ImGuiCol_ScrollbarBg] = gray;
    c[ImGuiCol_ScrollbarGrab] = border; c[ImGuiCol_ScrollbarGrabHovered] = muted; c[ImGuiCol_ScrollbarGrabActive] = focus;
    c[ImGuiCol_CheckMark] = focus; c[ImGuiCol_SliderGrab] = border; c[ImGuiCol_SliderGrabActive] = focus;
    c[ImGuiCol_Button] = control; c[ImGuiCol_ButtonHovered] = hover; c[ImGuiCol_ButtonActive] = active;
    c[ImGuiCol_Header] = raised; c[ImGuiCol_HeaderHovered] = hover; c[ImGuiCol_HeaderActive] = active;
    c[ImGuiCol_Separator] = border; c[ImGuiCol_SeparatorHovered] = muted; c[ImGuiCol_SeparatorActive] = focus;
    c[ImGuiCol_ResizeGrip] = border; c[ImGuiCol_ResizeGripHovered] = muted; c[ImGuiCol_ResizeGripActive] = focus;
    c[ImGuiCol_Tab] = panel; c[ImGuiCol_TabHovered] = hover; c[ImGuiCol_TabSelected] = raised;
    c[ImGuiCol_TabSelectedOverline] = focus; c[ImGuiCol_TabDimmed] = gray;
    c[ImGuiCol_TabDimmedSelected] = panel; c[ImGuiCol_TabDimmedSelectedOverline] = border;
    c[ImGuiCol_DockingPreview] = ImVec4(0.1f,0.1f,0.1f,0.28f); c[ImGuiCol_DockingEmptyBg] = gray;
    c[ImGuiCol_PlotLines] = focus; c[ImGuiCol_PlotLinesHovered] = text;
    c[ImGuiCol_PlotHistogram] = border; c[ImGuiCol_PlotHistogramHovered] = text;
    c[ImGuiCol_TableHeaderBg] = raised; c[ImGuiCol_TableBorderStrong] = border; c[ImGuiCol_TableBorderLight] = muted;
    c[ImGuiCol_TableRowBg] = ImVec4(0,0,0,0); c[ImGuiCol_TableRowBgAlt] = ImVec4(1,1,1,0.04f);
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.15f,0.15f,0.15f,0.30f);
    c[ImGuiCol_DragDropTarget] = focus; c[ImGuiCol_NavCursor] = focus;
    c[ImGuiCol_NavWindowingHighlight] = focus; c[ImGuiCol_NavWindowingDimBg] = ImVec4(0,0,0,0.45f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0,0,0,0.45f);
    return r;
}

ResolvedCreamPalette ResolveMonochromePalette(const CreamPalette& p) {
    ResolvedCreamPalette r{};
    r.colorPolicy = PaletteColorPolicy::Monochrome;
    const ImVec4 black=Hex(0x000000), panel=Hex(0x0A0A0A), raised=Hex(0x181818);
    const ImVec4 control=Hex(0x141414), hover=Hex(0x202020), active=Hex(0x2A2A2A);
    const ImVec4 text=Hex(0xE0E0E0), muted=Hex(0xA8A8A8), disabled=Hex(0x737373);
    const ImVec4 border=Hex(0x383838), focus=Hex(0xC8C8C8), subtle=Hex(0x242424);
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
    r.popup=surface(panel,text,hover,active);
    r.navigation=surface(panel,text,hover,active);
    r.section=surface(raised,text,hover,active);
    r.navigationAccent=Hex(0xA0A0A0); r.enabledAccent=Hex(0xB8B8B8);
    for (size_t i=0;i<r.groups.size();++i) {
        const float level=0.055f+static_cast<float>(i)*0.012f;
        const ImVec4 group=ImVec4(level,level,level,1.0f);
        r.groups[i]=surface(group,text,hover,active);
    }
    r.primaryAction=surface(Hex(0x303030),Hex(0xF0F0F0),Hex(0x3A3A3A),Hex(0x464646));
    r.primaryAction.mutedForeground=Hex(0xD0D0D0);
    r.primaryAction.disabledBackground=Hex(0x181818); r.primaryAction.disabledForeground=Hex(0x7A7A7A);
    auto& n=r.nodeAppearance;
    n.surface=Hex(0x101010); n.text=text; n.mutedText=muted; n.disabledText=disabled;
    n.number=Hex(0xD8D8D8); n.numberHovered=Hex(0xEEEEEE); n.numberActive=Hex(0xFFFFFF);
    n.reset=muted; n.resetHovered=Hex(0xD0D0D0); n.resetActive=Hex(0xF0F0F0);
    n.focus=focus; n.error=Hex(0xD8D8D8); n.selection=Hex(0xB8B8B8); n.socketHalo=black;
    n.control=r.control;
    r.imageSocket=Hex(0xD8D8D8); r.maskSocket=Hex(0xB8B8B8);
    r.analysisSocket=Hex(0x989898); r.valueSocket=Hex(0xC8C8C8); r.rawSocket=Hex(0x787878);
    r.colors.fill(muted);
    auto& c=r.colors;
    c[ImGuiCol_Text]=text; c[ImGuiCol_TextDisabled]=disabled;
    c[ImGuiCol_WindowBg]=black; c[ImGuiCol_ChildBg]=panel; c[ImGuiCol_PopupBg]=panel;
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
    return {CreamPalette{},
        Make("avocado-kitchen", "Avocado Kitchen", {0xF4E8C9,0xD7C58A,0x9B9E4A,0x6F7435,0xA45D3C,0x4B3A2A}),
        Make("desert-lounge", "Desert Lounge", {0xEFE0C3,0xD6A56A,0xB76D47,0x8B4F3B,0x7B7352,0x3C3A34}),
        Make("mustard-walnut", "Mustard & Walnut", {0xF5E8C8,0xD0A23A,0xA56D2B,0x7A4C2A,0x5E4A3A,0x2F2A24}),
        MakeMonochromeDark(), MakeMonochromeGray()};
}
ResolvedCreamPalette ResolveCreamPalette(const CreamPalette& p) {
    if (p.id == kMonochromeGrayPaletteId) return ResolveMonochromeGrayPalette(p);
    if (p.colorPolicy==PaletteColorPolicy::Monochrome) return ResolveMonochromePalette(p);
    ResolvedCreamPalette r{};
    r.colorPolicy=PaletteColorPolicy::FullColor;
    const auto& s = p.seeds;
    const ImVec4 cream=Mix(s[0],s[0],0);
    const ImVec4 dark=Mix(s[1],s[1],0);
    r.text=cream;
    r.canvas=Mix(Mix(dark,s[6],0.15f),ImVec4(0,0,0,1),0.15f);
    r.surface=Mix(dark,cream,0.08f);
    r.mutedText=Mix(cream,r.surface,0.12f);
    r.border=Mix(dark,cream,0.35f);
    r.focus=s[2]; r.focus.w=1;
    r.selection=Mix(s[3],cream,0.22f);
    r.error=Mix(s[3],cream,0.30f);
    auto surface=[&](ImVec4 background, ImVec4 foreground) {
        SurfaceColors role{};
        role.background=background; role.foreground=foreground;
        role.mutedForeground=Mix(foreground,background,0.12f);
        role.border=r.border; role.focus=r.focus;
        role.hovered=Mix(background,s[2],0.08f);
        role.active=Mix(background,s[3],0.12f);
        role.disabledBackground=background;
        role.disabledForeground=role.mutedForeground;
        return role;
    };
    r.workspace=surface(r.canvas,cream);
    r.panel=surface(dark,cream);
    r.node=surface(r.surface,cream);
    r.popup=surface(r.surface,cream);
    r.control=surface(Mix(dark,ImVec4(0,0,0,1),0.10f),cream);
    const std::array<ImVec4,6> accents{r.border,s[5],s[7],s[8],s[2],s[3]};
    for (size_t i=0;i<accents.size();++i) {
        auto& group=r.groups[i];
        group=surface(Mix(r.node.background,accents[i],i==0 ? 0.0f : (i==4 ? 0.12f : 0.24f)),cream);
        group.border=Mix(accents[i],cream,0.22f);
        group.hovered=Mix(group.background,accents[i],0.03f);
        group.active=Mix(group.background,accents[i],0.06f);
    }
    r.navigation=surface(Mix(dark,s[7],0.30f),cream);
    r.navigation.hovered=Mix(dark,s[7],0.38f);
    r.navigation.active=Mix(dark,s[7],0.45f);
    r.navigationAccent=Mix(s[7],cream,0.32f);
    r.section=surface(Mix(dark,s[8],0.24f),cream);
    r.section.hovered=Mix(dark,s[8],0.32f);
    r.section.active=Mix(dark,s[8],0.40f);
    r.enabledAccent=Mix(s[5],cream,0.42f);
    r.primaryAction=surface(cream,dark);
    r.primaryAction.hovered=Mix(cream,s[2],0.18f);
    r.primaryAction.active=Mix(cream,s[2],0.32f);
    r.primaryAction.focus=r.focus;
    r.primaryAction.mutedForeground=Mix(dark,cream,0.12f);
    r.primaryAction.border=dark;
    r.primaryAction.disabledBackground=Mix(cream,dark,0.15f);
    r.primaryAction.disabledForeground=Mix(dark,cream,0.10f);
    // Node content has its own light surface. Do not reuse application text or controls.
    auto readable=[&](ImVec4 accent, ImVec4 background, float contrast) {
        const ImVec4 target = CreamContrastRatio(dark,background) > CreamContrastRatio(cream,background) ? dark : cream;
        for (int i=0;i<=100;++i) {
            const auto color=Mix(accent,target,i/100.0f);
            if (CreamContrastRatio(color,background)>=contrast) return color;
        }
        return target;
    };
    auto accentSeed=[&](NodeAccent accent) { return s[accent==NodeAccent::Blue ? 7 : accent==NodeAccent::Teal ? 8 : 5]; };
    auto& n=r.nodeAppearance;
    n.surface=cream; n.text=dark; n.mutedText=Mix(dark,cream,0.20f);
    n.disabledText=Mix(dark,cream,0.36f);
    n.number=readable(accentSeed(p.numberAccent),cream,4.5f);
    n.numberHovered=Mix(n.number,dark,0.18f); n.numberActive=Mix(n.number,dark,0.30f);
    n.reset=n.mutedText; n.resetHovered=readable(accentSeed(p.resetAccent),cream,4.5f);
    n.resetActive=Mix(n.resetHovered,dark,0.25f);
    n.focus=n.numberHovered; n.error=readable(s[3],cream,4.5f);
    n.selection=readable(s[3],cream,3.0f); n.socketHalo=cream;
    n.control=surface(Mix(cream,dark,0.07f),dark);
    n.control.hovered=Mix(cream,dark,0.12f); n.control.active=Mix(cream,dark,0.18f);
    n.control.mutedForeground=n.mutedText; n.control.disabledForeground=n.disabledText;
    n.control.border=Mix(dark,cream,0.52f); n.control.focus=n.focus;
    r.imageSocket=readable(s[8],r.canvas,3.0f);
    r.maskSocket=readable(s[5],r.canvas,3.0f); r.analysisSocket=readable(s[7],r.canvas,3.0f);
    r.valueSocket=readable(s[2],r.canvas,3.0f);
    r.rawSocket=readable(s[6],r.canvas,3.0f);
    // Start every slot from a warm neutral so new ImGui slots cannot introduce
    // the old blue/dark palette. Override surfaces and semantic marks below.
    r.colors.fill(r.mutedText);
    auto& c = r.colors;
    c[ImGuiCol_Text] = r.text; c[ImGuiCol_TextDisabled] = r.mutedText;
    c[ImGuiCol_WindowBg] = r.canvas; c[ImGuiCol_ChildBg] = r.panel.background;
    c[ImGuiCol_PopupBg] = r.popup.background;
    c[ImGuiCol_Border] = r.border; c[ImGuiCol_BorderShadow] = ImVec4(0,0,0,0);
    for (auto slot : {ImGuiCol_FrameBg, ImGuiCol_Button, ImGuiCol_Header, ImGuiCol_Tab,
        ImGuiCol_TitleBg, ImGuiCol_TitleBgCollapsed, ImGuiCol_MenuBarBg, ImGuiCol_ScrollbarBg})
        c[slot] = r.panel.background;
    c[ImGuiCol_FrameBg] = r.control.background;
    for (auto slot : {ImGuiCol_FrameBgHovered, ImGuiCol_ButtonHovered, ImGuiCol_HeaderHovered,
        ImGuiCol_TabHovered, ImGuiCol_TabSelected, ImGuiCol_TitleBgActive})
        c[slot] = r.panel.hovered;
    c[ImGuiCol_FrameBgHovered] = r.control.hovered;
    for (auto slot : {ImGuiCol_FrameBgActive, ImGuiCol_ButtonActive, ImGuiCol_HeaderActive})
        c[slot] = r.panel.active;
    c[ImGuiCol_FrameBgActive] = r.control.active;
    for (auto slot : {ImGuiCol_CheckMark, ImGuiCol_SliderGrab, ImGuiCol_SliderGrabActive,
        ImGuiCol_NavCursor, ImGuiCol_TabSelectedOverline, ImGuiCol_DragDropTarget}) c[slot] = r.focus;
    c[ImGuiCol_CheckMark]=r.enabledAccent;
    c[ImGuiCol_Header]=r.section.background;
    c[ImGuiCol_HeaderHovered]=r.section.hovered;
    c[ImGuiCol_HeaderActive]=r.section.active;
    c[ImGuiCol_TabHovered]=r.navigation.hovered;
    c[ImGuiCol_TabSelected]=r.navigation.background;
    c[ImGuiCol_TabSelectedOverline]=r.navigationAccent;
    c[ImGuiCol_TextSelectedBg] = Mix(dark, s[2], 0.25f);
    c[ImGuiCol_Separator] = Mix(r.surface, r.text, 0.25f);
    c[ImGuiCol_SeparatorHovered] = c[ImGuiCol_SeparatorActive] = r.focus;
    c[ImGuiCol_ScrollbarGrab] = Mix(r.surface, r.text, 0.35f);
    c[ImGuiCol_ScrollbarGrabHovered] = c[ImGuiCol_ScrollbarGrabActive] = r.mutedText;
    c[ImGuiCol_TabDimmed] = r.panel.background; c[ImGuiCol_TabDimmedSelected] = c[ImGuiCol_TabSelected];
    c[ImGuiCol_TabDimmedSelectedOverline] = r.mutedText;
    c[ImGuiCol_TableHeaderBg] = c[ImGuiCol_Header];
    c[ImGuiCol_TableBorderStrong] = c[ImGuiCol_TableBorderLight] = c[ImGuiCol_Separator];
    c[ImGuiCol_TableRowBg] = ImVec4(0,0,0,0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(r.text.x,r.text.y,r.text.z,0.04f);
    c[ImGuiCol_DockingEmptyBg] = r.canvas;
    c[ImGuiCol_DockingPreview] = ImVec4(r.focus.x,r.focus.y,r.focus.z,0.25f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(r.canvas.x,r.canvas.y,r.canvas.z,0.55f);
    c[ImGuiCol_NavWindowingDimBg] = c[ImGuiCol_ModalWindowDimBg];
    c[ImGuiCol_NavWindowingHighlight] = r.focus;
    c[ImGuiCol_PlotLines] = r.imageSocket; c[ImGuiCol_PlotLinesHovered] = r.focus;
    c[ImGuiCol_PlotHistogram] = r.valueSocket; c[ImGuiCol_PlotHistogramHovered] = r.focus;
    return r;
}
bool EqualCreamPalettes(const CreamPalette& a, const CreamPalette& b) {
    if (a.id != b.id || a.name != b.name || a.numberAccent != b.numberAccent || a.resetAccent != b.resetAccent || a.colorPolicy != b.colorPolicy) return false;
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
