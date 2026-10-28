// Complete production navigator and FolderTree, native ImGui input. Theme/icon
// services alone are display-only doubles; no files, GL, or project operations.
#include <imgui.h>
#include <imgui_internal.h>
#include "Editor/Internal/RawLab/RawLabGalleryNavigator.h"
#include "App/AppHeaderStyle.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
struct Hit { ImGuiID id; ImRect rect; };
std::vector<Hit> hits;
int textFailures = 0;
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
}
namespace ImGui {
bool RecordedButton(const char* id, const ImVec2& size) {
    const bool pressed = Button(id, size);
    if (std::strcmp(id, "##Folder") == 0)
        hits.push_back({GetItemID(), {GetItemRectMin(), GetItemRectMax()}});
    return pressed;
}
void RecordedTextEllipsis(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
    float clip, const char* text, const char* end, const ImVec2* measured) {
    const auto item = GetCurrentContext()->LastItemData.ID;
    const int begin = draw->VtxBuffer.Size;
    RenderTextEllipsis(draw, min, max, clip, text, end, measured);
    const int split = draw->VtxBuffer.Size;
    RenderTextEllipsis(draw, min, max, clip, text, text + std::strlen(text), measured);
    const int finish = draw->VtxBuffer.Size;
    bool equal = split-begin == finish-split;
    for (int i=0; equal && i<split-begin; ++i) {
        const auto& a=draw->VtxBuffer[begin+i]; const auto& b=draw->VtxBuffer[split+i];
        equal = a.pos.x==b.pos.x && a.pos.y==b.pos.y && a.uv.x==b.uv.x && a.uv.y==b.uv.y && a.col==b.col;
    }
    if (!equal || item != GetCurrentContext()->LastItemData.ID) ++textFailures;
}
}
namespace Stack::Header {
ImVec4 Blend(ImVec4 a,ImVec4 b,float t) { return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t}; }
void ApproachMotion(float& v,float t,float) {v=t;}
}
namespace Stack::LibraryUi { void DrawIcon(Icon,ImVec2,float,ImU32) {} bool ReducedMotion() {return true;} }
// INSERT_COMPLETE_NAVIGATOR
namespace {
using namespace Stack::Editor::RawLabInternal;
struct Fixture {
    GalleryFolderTree tree;
    GalleryNavigator nav;
    GalleryNavigatorAction result;
    bool disabled=false;
    Fixture() {
        ImGui::CreateContext(); auto& io=ImGui::GetIO();
        io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={800,700};io.DeltaTime=1.f/60;
        unsigned char* p;int w,h;io.Fonts->GetTexDataAsRGBA32(&p,&w,&h);
        tree.workspaceRoot="C:/photos";tree.serial=1;tree.sourceName="Source##literal";
        tree.parentPath="C:/parent###literal";tree.root.label="All";
    }
    ~Fixture() {ImGui::DestroyContext();}
    void Add(const std::string& order,const std::string& key) {
        auto& f=tree.root.children[order];f.key=f.label=key;f.countLabels={"0","0","0"};++tree.serial;
    }
    void Frame(float width=400) {
        hits.clear();ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,650});
        ImGui::Begin("Navigator");ImGui::BeginDisabled(disabled);
        result=nav.Render(tree,Stack::RawWorkspace::GalleryContentMode::Gallery,std::nullopt,{.1f,.1f,.1f,1},{});
        ImGui::EndDisabled();ImGui::End();ImGui::Render();
    }
    void Move(ImVec2 p) {ImGui::GetIO().AddMousePosEvent(p.x,p.y);Frame();}
    void Click(ImVec2 p) {Move(p);ImGui::GetIO().AddMouseButtonEvent(0,true);Frame();ImGui::GetIO().AddMouseButtonEvent(0,false);Frame();}
};
void Clicks(bool special,bool reversed) {
    Fixture f;const std::string a=special?"A###same":"Aordinary",b=special?"B###same":"Bordinary";
    f.Add(reversed?"2":"1",a);f.Add(reversed?"1":"2",b);f.Frame();f.Frame();
    Require(hits.size()==3,"three folder rows");
    const auto root=hits[0].id, first=hits[1].id, second=hits[2].id;
    for(int i=1;i<3;++i) {f.Click(hits[i].rect.GetCenter());Require(f.result.folderChanged && f.result.folder==(i==1?(reversed?b:a):(reversed?a:b)),"clicked folder owns action");}
    Require(first!=second,"folder IDs must be distinct");
    // Reorder display entries without changing literal identity.
    std::swap(f.tree.root.children["1"],f.tree.root.children["2"]);++f.tree.serial;f.Frame();
    Require(hits[0].id==root && hits[1].id==second && hits[2].id==first,"IDs survive reorder");
    // Press an entry, replace that row before release: no replacement activation.
    auto p=hits[1].rect.GetCenter();f.Move(p);ImGui::GetIO().AddMouseButtonEvent(0,true);f.Frame();
    f.tree.root.children["1"].key="replacement###same";++f.tree.serial;
    ImGui::GetIO().AddMouseButtonEvent(0,false);f.Frame();Require(!f.result.folderChanged,"held replacement must not activate");
    f.disabled=true;f.Click(hits[2].rect.GetCenter());Require(!f.result.folderChanged,"disabled row must not activate");
}
void LabelsAndOrdinaryIds() {
    Fixture f;f.Add("1","ordinary");f.Frame();f.Frame();
    // Native legacy string hash and raw-byte hash agree for ordinary identities.
    const ImGuiID seed=ImHashStr("parent");
    for (const char* key : {"ordinary", "folder/subfolder", ""})
        Require(ImHashStr(key,0,seed)==ImHashData(key,std::strlen(key),seed),"ordinary scope ID preserved");
    for(const std::string label : {std::string(),std::string("ordinary"),std::string("A##suffix"),std::string("A###suffix"),std::string(220,'W')+u8"照片##suffix"}) {
        f.tree.sourceName=f.tree.parentPath=f.tree.root.children["1"].label=label;
        for(float width : {180.f,400.f,780.f}) {f.Frame(width);f.Frame(width);}
    }
    Require(textFailures==0,"literal glyph/clip/color and LastItem parity");
}
}
int main(int argc, char**) {try {if(argc==1) for(bool special:{false,true})for(bool reversed:{false,true})Clicks(special,reversed);LabelsAndOrdinaryIds();std::cout<<"Gallery navigator identity and literal labels passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
