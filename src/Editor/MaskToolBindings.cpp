#include "Editor/MaskToolBindings.h"
#include "App/AppPaths.h"
#include <fstream>
#include <cmath>
namespace Stack::Editor {
using Tool=MaskAuthoring::Tool;
namespace {
using Keys=std::array<ImGuiKey,8>;
const Keys defaults{ImGuiKey_C,ImGuiKey_B,ImGuiKey_E,ImGuiKey_R,ImGuiKey_L,ImGuiKey_M,ImGuiKey_I,ImGuiKey_G};
auto File(){return AppPaths::GetSettingsDirectory()/"mask-bindings.json";}
Keys& Bindings(){
    static Keys keys=[]{
        Keys result=defaults;
        try {std::ifstream f(File());if(f){auto j=nlohmann::json::parse(f);
            for(size_t i=0;i<result.size();++i){int c=j.at(i);if(c<'A'||c>'Z')return defaults;result[i]=ImGuiKey(ImGuiKey_A+c-'A');}
            auto ordered=result;std::sort(ordered.begin(),ordered.end());if(std::adjacent_find(ordered.begin(),ordered.end())!=ordered.end())return defaults;
        }} catch(const std::exception&){}return result;
    }();return keys;
}
}
const char* MaskToolName(Tool tool){static const char* names[]={"Draw Values","Brush","Erase","Radial","Linear","Rectangle","Sample Circle","Edge Guide"};return names[int(tool)];}
ImGuiKey MaskToolKey(Tool tool){return Bindings()[int(tool)];}
bool MaskToolChordDown(){
    for(auto key:Bindings())if(ImGui::IsKeyDown(key))return true;
    return false;
}
bool BindMaskTool(Tool tool,ImGuiKey key){
    if(key<ImGuiKey_A || key>ImGuiKey_Z)return false;
    auto proposed=Bindings();auto other=std::find(proposed.begin(),proposed.end(),key);
    if(other!=proposed.end())std::swap(*other,proposed[int(tool)]);else proposed[int(tool)]=key;
    auto j=nlohmann::json::array();for(auto entry:proposed)j.push_back(int('A'+entry-ImGuiKey_A));
    std::ofstream f(File());if(!(f<<j.dump()) || !f.flush())return false;Bindings()=proposed;return true;
}
void DrawMaskGlyph(ImDrawList* d,Tool t,ImVec2 c,float r,ImU32 ink){
    const auto p=[&](float x,float y){return ImVec2(c.x+x*r,c.y+y*r);};
    const auto line=[&](float x,float y,float u,float v){d->AddLine(p(x,y),p(u,v),ink,1.6f);};
    switch(t){
    case Tool::Values: line(-.9f,.6f,-.45f,-.3f);line(-.45f,-.3f,0,.3f);line(0,.3f,.65f,-.65f);d->AddCircleFilled(p(.65f,-.65f),r*.22f,ink);break;
    case Tool::Brush: d->AddCircle(c,r*.78f,ink,24,1.5f);d->AddCircleFilled(c,r*.34f,ink);break;
    case Tool::Erase: d->AddQuad(p(-.85f,.25f),p(.25f,-.85f),p(.9f,-.2f),p(-.2f,.9f),ink,1.5f);line(-.5f,-.1f,.15f,.55f);break;
    case Tool::Radial: d->AddEllipse(c,{r,r*.69f},ink,0,24,1.6f);d->AddEllipse(c,{r*.51f,r*.33f},ink,0,24,1.3f);break;
    case Tool::Linear: for(int i=-1;i<=1;++i)line(i*.45f-.45f,.72f,i*.45f+.45f,-.72f);break;
    case Tool::Rectangle:d->AddRect(p(-.85f,-.65f),p(.85f,.65f),ink,0,0,1.6f);break;
    case Tool::Circle:d->AddCircle(c,r*.72f,ink,24,1.5f);line(0,-1,0,-.45f);line(-1,0,-.45f,0);break;
    case Tool::Edge:line(-.65f,.55f,.65f,-.55f);d->AddCircleFilled(p(-.65f,.55f),r*.22f,ink);d->AddCircle(p(.65f,-.55f),r*.24f,ink,12,1.5f);break;
    }
}
}

