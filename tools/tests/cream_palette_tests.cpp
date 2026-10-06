#include "App/settings/CreamPalettePersistence.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace StackAppearance;
void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        for (const auto& palette : FactoryCreamPalettes()) {
            auto resolved=ResolveCreamPalette(palette);
            int roleIndex=0;
            for (const auto* role : {&resolved.workspace,&resolved.panel,&resolved.node,&resolved.control,&resolved.popup,&resolved.primaryAction,&resolved.navigation,&resolved.section,&resolved.groups[0],&resolved.groups[1],&resolved.groups[2],&resolved.groups[3],&resolved.groups[4],&resolved.groups[5]}) {
                for (auto bg : {role->background,role->hovered,role->active})
                    if (CreamContrastRatio(role->mutedForeground,bg)<4.5f) std::cerr<<palette.name<<" role "<<roleIndex<<" muted "<<CreamContrastRatio(role->mutedForeground,bg)<<"\n";
                ++roleIndex;
            }
            Require(CreamPaletteHasReadableText(resolved), (palette.name+" has insufficient text contrast").c_str());
            for (auto slot : {ImGuiCol_WindowBg,ImGuiCol_ChildBg,ImGuiCol_PopupBg,ImGuiCol_FrameBg,ImGuiCol_Button})
                Require(resolved.colors[slot].w==1,"Control surfaces must be opaque");
            Require(CreamContrastRatio(resolved.text,resolved.surface)>4.5f,"Node text contrast");
            Require(resolved.workspace.background.x < resolved.node.background.x,"Nodes remain raised above workspace");
            if (palette.id != kMonochromeGrayPaletteId)
                Require(resolved.node.background.x < resolved.text.x,"Cream foreground on dark nodes");
            Require(CreamContrastRatio(resolved.focus,resolved.panel.background)>=3.0f,"Visible focus outline");
            for (const auto* role : {&resolved.workspace,&resolved.panel,&resolved.node,&resolved.control,&resolved.popup,&resolved.primaryAction}) {
                Require(CreamContrastRatio(role->foreground,role->background)>=4.5f,"Surface pair contrast");
                Require(CreamContrastRatio(role->foreground,role->hovered)>=4.5f,"Hover contrast");
                Require(CreamContrastRatio(role->foreground,role->active)>=4.5f,"Pressed contrast");
                Require(CreamContrastRatio(role->disabledForeground,role->disabledBackground)>=3.0f,"Disabled contrast");
            }
            const auto original=EncodeCreamPalette(palette);
            ResolveCreamPalette(palette);
            Require(original==EncodeCreamPalette(palette),"Role resolution preserves saved seed values");
            CreamPalette decoded;
            Require(DecodeCreamPalette(EncodeCreamPalette(palette),decoded) && EqualCreamPalettes(palette,decoded),"Palette round trip");
            if (palette.colorPolicy==PaletteColorPolicy::Monochrome) {
                Require((palette.id==kMonochromeDarkPaletteId && palette.name=="Dark") ||
                    (palette.id==kMonochromeGrayPaletteId && palette.name=="Gray"),"Stable monochrome palette identity");
                for (const auto& color : resolved.colors) Require(IsMonochrome(color),"Every monochrome ImGui role is monochrome");
                for (auto color : {resolved.canvas,resolved.surface,resolved.text,resolved.mutedText,resolved.border,
                        resolved.focus,resolved.error,resolved.imageSocket,resolved.maskSocket,resolved.analysisSocket,
                        resolved.valueSocket,resolved.rawSocket})
                    Require(IsMonochrome(color),"Every monochrome semantic role is monochrome");
            }
        }
        auto legacy = EncodeCreamPalette(FactoryCreamPalettes()[1]);
        legacy["seeds"].erase(legacy["seeds"].begin()+7, legacy["seeds"].end());
        CreamPalette migrated;
        Require(DecodeCreamPalette(legacy,migrated),"Load seven-seed palettes");
        auto expanded = EncodeCreamPalette(migrated);
        for (int i=0;i<7;++i) Require(legacy["seeds"][i]==expanded["seeds"][i],"Preserve all original seeds");
        Require(expanded["seeds"].size()==9,"Add blue and teal defaults");
        legacy.erase("numberAccent"); legacy.erase("resetAccent"); legacy.erase("colorPolicy");
        Require(DecodeCreamPalette(legacy,migrated) && migrated.numberAccent==NodeAccent::Blue && migrated.resetAccent==NodeAccent::Teal && migrated.colorPolicy==PaletteColorPolicy::FullColor,"Old settings receive independent accent defaults");
        for (auto starter : FactoryCreamPalettes()) {
            if (starter.colorPolicy==PaletteColorPolicy::Monochrome) continue;
            for (auto number : {NodeAccent::Blue,NodeAccent::Teal,NodeAccent::Olive}) {
                for (auto reset : {NodeAccent::Blue,NodeAccent::Teal,NodeAccent::Olive}) {
                    starter.numberAccent=number; starter.resetAccent=reset;
                    const auto palette=ResolveCreamPalette(starter);
                    const auto& n=palette.nodeAppearance;
                    Require(CreamPaletteHasReadableText(palette),"All node accent combinations remain readable");
                    Require(n.surface.x==starter.seeds[0].x && n.surface.w==1,"Opaque cream node interior");
                    for (auto bg : {n.control.background,n.control.hovered,n.control.active})
                        Require(CreamContrastRatio(n.text,bg)>=4.5f,"Embedded control contrast");
                    for (auto socket : {palette.imageSocket,palette.maskSocket,palette.valueSocket,palette.rawSocket,palette.analysisSocket})
                        Require(CreamContrastRatio(socket,palette.canvas)>=3.0f,"Socket contrast on canvas");
                    CreamPalette restored;
                    Require(DecodeCreamPalette(EncodeCreamPalette(starter),restored) && EqualCreamPalettes(starter,restored),"Independent accent settings round-trip");
                    auto other=starter; other.resetAccent=reset==NodeAccent::Blue ? NodeAccent::Teal : NodeAccent::Blue;
                    Require(!EqualCreamPalettes(other,starter),"Reset choice participates in preview equality");
                    const auto altered=ResolveCreamPalette(other);
                    Require(altered.nodeAppearance.number.x==n.number.x && altered.nodeAppearance.number.y==n.number.y,"Reset choice never changes number color");
                }
            }
        }
        auto bad=EncodeCreamPalette(CreamPalette{}); bad["seeds"][0][0]=2;
        CreamPalette decoded;
        Require(!DecodeCreamPalette(bad,decoded),"Reject invalid color input");
        auto unreadable=CreamPalette{}; unreadable.seeds[1]=unreadable.seeds[0];
        Require(!CreamPaletteHasReadableText(ResolveCreamPalette(unreadable)),"Warn about unreadable custom colors");
        const auto dir=std::filesystem::temp_directory_path()/"stack-cream-palette-tests";
        std::filesystem::create_directories(dir);
        const auto path=dir/"appearance.json";
        Require(WriteAppearanceAtomically(path,EncodeCreamPalette(CreamPalette{})),"Initial atomic save");
        const auto second=FactoryCreamPalettes()[1];
        Require(WriteAppearanceAtomically(path,EncodeCreamPalette(second)),"Replace existing saved palette");
        std::ifstream file(path); nlohmann::json saved; file>>saved; file.close();
        Require(DecodeCreamPalette(saved,decoded) && EqualCreamPalettes(decoded,second),"Reload saved replacement");
        std::filesystem::remove(path); std::filesystem::remove(dir);
        std::cout<<"Cream palette checks passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
