#include "AppearanceTheme.h"
#include <algorithm>

namespace StackAppearance {
std::vector<ThemeDefinition> MakeCreamThemes(const std::vector<CreamPalette>& palettes) {
    std::vector<ThemeDefinition> themes;
    for (const auto& p : palettes) {
        ThemeDefinition t;
        t.id=p.id; t.displayName=p.name; t.creamPalette=p;
        t.colors=ResolveCreamPalette(p).colors; t.readOnly=true;
        themes.push_back(std::move(t));
    }
    return themes;
}
const CreamPalette& AppearanceManager::GetCreamPalette() const { return m_WorkingTheme.creamPalette; }
const ResolvedCreamPalette& AppearanceManager::GetResolvedCreamPalette() const {
    if (!m_CreamCacheValid) {
        m_ResolvedCream = ResolveCreamPalette(m_WorkingTheme.creamPalette);
        const auto& p=m_ResolvedCream;
        const auto& c=p.colors;
        m_CreamSurfaces.appSurface=p.canvas;
        m_CreamSurfaces.panelSurface=p.panel.background;
        m_CreamSurfaces.popupSurface=c[ImGuiCol_PopupBg];
        m_CreamSurfaces.chromeSurface=c[ImGuiCol_MenuBarBg];
        m_CreamSurfaces.drawerSurface=p.panel.background;
        m_CreamSurfaces.drawerSurfaceTransparent=p.panel.background;
        m_CreamSurfaces.drawerSurfaceTransparent.w=0;
        m_CreamSurfaces.border=p.border;
        m_CreamSurfaces.separator=c[ImGuiCol_Separator];
        m_CreamSurfaces.controlSurface=c[ImGuiCol_FrameBg];
        m_CreamSurfaces.controlSurfaceHovered=c[ImGuiCol_FrameBgHovered];
        m_CreamSurfaces.controlSurfaceActive=c[ImGuiCol_FrameBgActive];
        m_CreamCacheValid=true;
    }
    return m_ResolvedCream;
}
bool AppearanceManager::UsesMonochromeUi() const {
    return GetResolvedCreamPalette().colorPolicy == PaletteColorPolicy::Monochrome;
}
ImVec4 AppearanceManager::ResolveSemanticUiColor(SemanticUiColor role, ImVec4 fullColor) const {
    return StackAppearance::ResolveSemanticUiColor(GetResolvedCreamPalette(),role,fullColor);
}
void AppearanceManager::PreviewCreamPalette(const CreamPalette& palette) {
    m_ThemeTransitionActive=false;
    m_WorkingTheme.creamPalette=palette;
    m_WorkingTheme.id=palette.id;
    m_WorkingTheme.displayName=palette.name;
    m_WorkingTheme.colors=ResolveCreamPalette(palette).colors;
    TouchRevision();
}
void AppearanceManager::RevertCreamPalette() { PreviewCreamPalette(m_Library.savedCreamPalette); }
bool AppearanceManager::HasCreamPaletteChanges() const {
    return !EqualCreamPalettes(GetCreamPalette(),m_Library.savedCreamPalette);
}
bool AppearanceManager::SaveCreamPalette(const std::string& variantName) {
    auto next=m_Library;
    CreamPalette palette=GetCreamPalette();
    if (!variantName.empty()) {
        palette.name=variantName;
        unsigned index=1;
        do { palette.id="cream-custom-"+std::to_string(index++); }
        while (std::any_of(next.creamVariants.begin(),next.creamVariants.end(),[&](const CreamPalette& p){return p.id==palette.id;}));
        next.creamVariants.push_back(palette);
    } else {
        for (auto& variant : next.creamVariants) if (variant.id==palette.id) variant=palette;
    }
    next.savedCreamPalette=palette;
    if (!SaveAppearanceLibrary(next)) return false;
    m_Library=std::move(next);
    m_CreamVariantThemes=MakeCreamThemes(m_Library.creamVariants);
    PreviewCreamPalette(palette);
    return true;
}
}
