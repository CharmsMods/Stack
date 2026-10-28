// Native input through the complete production folder-card renderer. Only
// display-only material/theme services are doubles; no files, GL or RAW decode.
#include <imgui.h>
#include <imgui_internal.h>
#include "Editor/Internal/RawLab/RawLabGalleryFolderCards.h"
#include "App/AppHeaderStyle.h"
#include <algorithm>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
struct Hit { std::string label; ImGuiID id; ImRect rectangle; };
std::vector<Hit> hits;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}
namespace ImGui {
bool RecordedInvisibleButton(const char* label, const ImVec2& size, ImGuiButtonFlags flags = 0) {
    const bool clicked = InvisibleButton(label, size, flags);
    hits.push_back({label, GetItemID(), {GetItemRectMin(), GetItemRectMax()}});
    return clicked;
}
}
namespace Stack::LibraryUi { bool ReducedMotion() { return true; } }
namespace Stack::Header {
ImVec4 Blend(ImVec4 a, ImVec4 b, float t) {
    return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t};
}
void Approach(float& value, float target, float, float) { value = target; }
void ApproachMotion(float& value, float target, float) { value = target; }
}
namespace Stack::Renderer {
bool FolderPocketMaterial::Begin(ImDrawList*, ImVec2, ImVec4, ImVec4, float, ImVec2) { return false; }
void FolderPocketMaterial::End(ImDrawList*) {}
void FolderPocketMaterial::Shutdown() {}
}

// Observe native item rectangles without changing the production input path.
// Override the source only for an old-production negative control.
#ifndef STACK_FOLDER_CARDS_SOURCE
#define STACK_FOLDER_CARDS_SOURCE "Editor/Internal/RawLab/RawLabGalleryFolderCards.cpp"
#endif
#define InvisibleButton RecordedInvisibleButton
#include STACK_FOLDER_CARDS_SOURCE
#undef InvisibleButton

namespace {
using Cards = Stack::Editor::RawLabInternal::GalleryFolderCards;
using Folder = Stack::RawWorkspace::SourceFolderCard;
struct Fixture {
    Cards cards;
    std::vector<Folder> folders;
    Cards::Action action;
    Fixture() {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr; io.LogFilename = nullptr;
        io.DisplaySize = {1000,800}; io.DeltaTime = 1.f/60;
        unsigned char* pixels; int width,height;
        io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    }
    ~Fixture() { ImGui::DestroyContext(); }
    void Frame() {
        hits.clear(); ImGui::NewFrame();
        ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({950,750});
        ImGui::Begin("Folder identity test");
        action = cards.Render(folders,{900,700},{.1f,.1f,.1f,1},
            [](const auto&,const auto&) { return Cards::Texture{7,120,80}; });
        ImGui::End(); ImGui::Render();
    }
    std::vector<Hit> Items(const char* label) const {
        std::vector<Hit> result;
        for (const auto& item : hits) if (item.label == label) result.push_back(item);
        return result;
    }
    void Click(ImVec2 point) {
        auto& io = ImGui::GetIO();
        io.AddMousePosEvent(point.x,point.y); Frame();
        io.AddMouseButtonEvent(0,true); Frame();
        io.AddMouseButtonEvent(0,false); Frame();
    }
};

void FolderClicks(bool special, bool reversed) {
    Fixture test;
    test.folders.resize(2);
    test.folders[0].root = special ? "/photos/A###same" : "/photos/ordinary-A";
    test.folders[1].root = special ? "/photos/B###same" : "/photos/ordinary-B";
    if (reversed) std::reverse(test.folders.begin(),test.folders.end());
    test.Frame(); test.Frame();
    auto items = test.Items("Open folder gallery");
    Require(items.size()==2,"both folder arrows must be submitted");
    // Actual second-card release failed before the raw-byte identity fix.
    test.Click(items[1].rectangle.GetCenter());
    Require(test.action.openFolder==test.folders[1].root,"second folder arrow opened no/wrong folder");
    test.Click(test.Items("Open folder gallery")[0].rectangle.GetCenter());
    Require(test.action.openFolder==test.folders[0].root,"first folder arrow opened no/wrong folder");
    Require(items[0].id!=items[1].id,"distinct folder paths shared an input ID");
    std::reverse(test.folders.begin(),test.folders.end());
    test.Frame(); test.Frame();
    auto reordered = test.Items("Open folder gallery");
    Require(reordered[0].id==items[1].id && reordered[1].id==items[0].id,
        "folder IDs followed presentation slots rather than paths");
    // A temporarily absent card returning to the list has the same scope.
    const auto retained = test.folders[1];
    test.folders.erase(test.folders.begin()+1); test.Frame();
    test.folders.push_back(retained); test.Frame();
    Require(test.Items("Open folder gallery")[1].id==items[0].id,
        "returning folder changed identity");
}

void ClippedRows() {
    Fixture test;
    for (int index=0;index<18;++index) {
        Folder folder;folder.root="/photos/folder-"+std::to_string(index);
        test.folders.push_back(std::move(folder));
    }
    test.Frame();test.Frame();
    const auto first=test.Items("Open folder gallery").front().id;
    ImGuiWindow* content=nullptr;
    for (auto* window : ImGui::GetCurrentContext()->Windows)
        if (std::strstr(window->Name,"/FolderCards_")) content=window;
    Require(content && content->ScrollMax.y>0,"folder rows must be scrollable");
    ImGui::SetScrollY(content,content->ScrollMax.y);test.Frame();test.Frame();
    const auto clipped=test.Items("Open folder gallery");
    Require(std::none_of(clipped.begin(),clipped.end(),[&](const Hit& hit){return hit.id==first;}),
        "first folder was not clipped after scrolling");
    ImGui::SetScrollY(content,0.f);test.Frame();test.Frame();
    Require(test.Items("Open folder gallery").front().id==first,"scrolling changed folder identity");
}

void PreviewClicks(bool special, bool reversed) {
    Fixture test;
    test.folders.resize(1); auto& folder = test.folders[0]; folder.root="/photos/origin";
    folder.previews.resize(2);
    folder.previews[0].absolutePath = special ? "/photos/origin/A###same.raw" : "/photos/origin/ordinary-A.raw";
    folder.previews[1].absolutePath = special ? "/photos/origin/B###same.raw" : "/photos/origin/ordinary-B.raw";
    if (reversed) std::reverse(folder.previews.begin(),folder.previews.end());
    test.Frame(); test.Frame();
    test.Click(test.Items("Expand or collapse folder")[0].rectangle.GetCenter());
    test.Frame();
    auto pages = test.Items("Open image");
    Require(pages.size()==2,"expanded folder must submit both image pages");
    // Production submits front-to-back: the second hit belongs to preview 0.
    test.Click(pages[1].rectangle.GetCenter());
    Require(test.action.openImage && test.action.openImage->path==folder.previews[0].absolutePath &&
        test.action.openImage->sourceFolder==folder.root,"rear preview opened no/wrong source or owner");
    test.Click(test.Items("Open image")[0].rectangle.GetCenter());
    Require(test.action.openImage && test.action.openImage->path==folder.previews[1].absolutePath,
        "front preview opened no/wrong source");
    Require(pages[0].id!=pages[1].id,"distinct source paths shared an input ID");
    std::reverse(folder.previews.begin(),folder.previews.end()); test.Frame();
    auto reordered = test.Items("Open image");
    Require(reordered[0].id==pages[1].id && reordered[1].id==pages[0].id,
        "preview IDs followed slots after catalog reorder");
    // Replacing a page during a held click cannot transfer that gesture.
    auto point = reordered[0].rectangle.GetCenter(); auto& io=ImGui::GetIO();
    io.AddMousePosEvent(point.x,point.y); test.Frame(); io.AddMouseButtonEvent(0,true); test.Frame();
    folder.previews[1].absolutePath="/photos/origin/replacement.raw";
    test.Frame(); io.AddMouseButtonEvent(0,false); test.Frame();
    Require(!test.action.openImage,"held click transferred to a replacement preview");
}
}
int main() {
    try {
        for (bool special : {false,true}) for (bool reversed : {false,true}) {
            FolderClicks(special,reversed); PreviewClicks(special,reversed);
        }
        ClippedRows();
        std::cout<<"Folder-card identity tests passed\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
