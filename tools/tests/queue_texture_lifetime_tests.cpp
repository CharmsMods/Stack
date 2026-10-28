#include "Renderer/GLLoader.h"
#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#include <imgui_impl_opengl3.h>
#include "Queue/QueueView.h"
#include "App/AppHeaderStyle.h"
#include "App/settings/PrimaryAction.h"
#include "Library/LibraryControls.h"
#include <unordered_map>
#include <array>
#include <iostream>
#include <cstring>
#include <limits>
#include <unordered_set>
#include <cstdlib>
ImVec2 removePoint, thumbPoint;
namespace ImGui { bool RecordedButton(const char* label,const ImVec2& size=ImVec2()) {bool p=Button(label,size);if(std::strcmp(label,"Remove")==0){auto lo=GetItemRectMin(),hi=GetItemRectMax();removePoint={(lo.x+hi.x)/2,(lo.y+hi.y)/2};}return p;} }
namespace Stack::Header { ImVec4 Blend(ImVec4 a,ImVec4 b,float t){return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t};} }
namespace Stack::LibraryUi { void DrawIcon(Icon,ImVec2,float,ImU32){} }
namespace StackAppearance {bool PrimaryActionButton(const char* label,const ImVec2& size,const SurfaceColors*){return ImGui::Button(label,size);} }
#define Button RecordedButton
#include "Queue/QueueView.cpp"
#undef Button

using Stack::Queue::Item;
static std::unordered_map<GLuint, int> deletions;
void CountDelete(GLsizei n, const GLuint* names) { for (int i=0;i<n;++i) ++deletions[names[i]]; glDeleteTextures(n,names); }
GLuint Texture(unsigned char red=255) {
    GLuint name=0;glGenTextures(1,&name);glBindTexture(GL_TEXTURE_2D,name);
    std::array<unsigned char,4> pixel{red,0,static_cast<unsigned char>(255-red),255};
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);return name;
}
// Decoder boundary only; production owner caching/replacement/eviction is extracted.
void stbi_set_flip_vertically_on_load_thread(int) {}
unsigned char* stbi_load_from_memory(const unsigned char*,int,int*w,int*h,int*c,int) {
    *w=*h=1;*c=4;auto*p=static_cast<unsigned char*>(std::malloc(4));std::memset(p,255,4);return p;
}
void stbi_image_free(void*p) {std::free(p);}
namespace GLHelpers { unsigned int CreateTextureFromPixels(const unsigned char*,int,int,int){return Texture();} }
struct Owner {
    struct TextureEntry { unsigned int texture=0;int width=1,height=1;std::shared_ptr<const std::vector<unsigned char>> bytes;int usedFrame=0; };
    struct RetiredTexture { unsigned int texture;int frame; };
    std::unordered_map<std::uint64_t,TextureEntry> m_Textures;
    std::vector<RetiredTexture> m_RetiredTextures;
    struct SnapshotModel {
        std::vector<Item> items;bool changed=true;
        bool SnapshotIfChanged(std::vector<Item>& out,std::uint64_t& revision,std::uint64_t* presentation) {
            if(!changed)return false;out=items;++revision;++*presentation;changed=false;return true;
        }
    } m_Model;
    std::vector<Item>m_FrameItems;std::uint64_t m_SnapshotRevision=0,m_PresentationRevision=0;
    int m_ThumbnailFrame=-1,m_ThumbnailLoads=0;
    Stack::Queue::QueueView m_View;std::function<void()>m_ExportRequest,m_OpenGallery,m_Stop;
    TextureEntry* EnsureTexture(const Item&);void ReleaseTexture(std::uint64_t);
    void RetireTexture(unsigned int);void ReleaseRetiredTextures();
    const std::vector<Item>& FrameItems();void Shutdown();
};
#define glDeleteTextures CountDelete
// PRODUCTION_METHODS
#undef glDeleteTextures
bool Check(bool value,const char*message){if(!value)std::cerr<<"FAIL: "<<message<<'\n';return value;}
void NewFrame(){ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();}
void Submit(){ImGui::Render();glViewport(0,0,800,600);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());}
void Draw(GLuint texture, ImVec2 low={20,20}) {ImGui::GetForegroundDrawList()->AddImage((ImTextureID)(intptr_t)texture,low,{low.x+40,low.y+40});}
bool RedAt(ImVec2 p){std::array<unsigned char,4>pixel{};glReadPixels(int(p.x),600-int(p.y),1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());return pixel[0]>240&&pixel[2]<10;}
bool LabelParity() {
    NewFrame();
    ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({800,600});
    ImGui::Begin("Queue labels", nullptr, ImGuiWindowFlags_NoDecoration);
    ImGui::Dummy({1,1}); const auto itemId = ImGui::GetItemID();
    auto* draw = ImGui::GetWindowDrawList();
    bool passed = true;
    for (const std::string label : {std::string(), std::string("ordinary.raw"),
            std::string("A##capture.raw"), std::string("C:/folder###literal/output.png"),
            std::string(u8"旅行_##éclair_📷_long_capture_name_edited_project.raw")}) {
        for (float width : {0.f, 1.f, 18.f, 70.f, 400.f}) {
            const ImVec2 at{50,50}; const float right = at.x + width;
            const ImU32 color = IM_COL32(210,190,170,200);
            const int start = draw->VtxBuffer.Size;
            Stack::Queue::Ellipsis(label.c_str(), at, right, color);
            const int split = draw->VtxBuffer.Size;
            // Direct native literal-text rendering is independent of the Queue
            // wrapper, and exercises both ellipsis and unclipped branches.
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::RenderTextEllipsis(draw, at, {right, at.y + ImGui::GetFontSize() + 2},
                right, label.data(), label.data() + label.size(), nullptr);
            ImGui::PopStyleColor();
            passed = Check(split - start == draw->VtxBuffer.Size - split,
                "Queue labels retain literal glyphs at every clipping width") && passed;
            if (split - start == draw->VtxBuffer.Size - split)
                for (int i = 0; i < split - start; ++i) {
                    const auto& a = draw->VtxBuffer[start+i]; const auto& b = draw->VtxBuffer[split+i];
                    passed = Check(a.pos.x == b.pos.x && a.pos.y == b.pos.y &&
                        a.uv.x == b.uv.x && a.uv.y == b.uv.y && a.col == b.col,
                        "Queue literal clipping preserves native glyph geometry and color") && passed;
                }
            passed = Check(ImGui::GetItemID() == itemId, "label painting cannot change item ownership") && passed;
        }
    }
    ImGui::End(); ImGui::EndFrame();
    return passed;
}
int main(){if(!glfwInit())return 2;glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);auto*w=glfwCreateWindow(800,600,"Queue lifetime",nullptr,nullptr);if(!w)return 2;glfwMakeContextCurrent(w);if(!LoadGLFunctions())return 2;ImGui::CreateContext();auto&io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={800,600};io.DeltaTime=1.f/60;ImGui_ImplOpenGL3_Init("#version 430 core");
bool passed=LabelParity();
{
    deletions.clear();
    Owner owner,other;GLuint original=Texture(),unrelated=Texture(0);owner.m_Textures[1].texture=original;other.m_Textures[1].texture=unrelated;
    Stack::Queue::QueueView view;Item item;item.id=1;item.displayName="Waiting project";std::vector<Item>items{item};view.Prepare(items,1);Stack::Queue::ExportPresentation output;
    bool removed=false;
    for(int frame=0;frame<6;++frame){if(frame>=2)io.AddMousePosEvent(removePoint.x,removePoint.y);if(frame==3)io.AddMouseButtonEvent(0,true);if(frame==4)io.AddMouseButtonEvent(0,false);NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({800,600});ImGui::Begin("Queue",nullptr,ImGuiWindowFlags_NoDecoration);
        auto action=view.RenderWorkspace(items,output,[&](const auto&,ImVec2 low,ImVec2 size){thumbPoint={low.x+size.x/2,low.y+size.y/2};ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)original,low,{low.x+size.x,low.y+size.y});});
        if(action.command==Stack::Queue::ViewCommand::Remove){owner.ReleaseTexture(action.id);owner.ReleaseTexture(action.id);removed=true;}
        ImGui::End();owner.ReleaseRetiredTextures();Submit();
        passed=Check(glIsTexture(original)&&RedAt(thumbPoint),"native Remove preserves already-submitted thumbnail pixels")&&passed;
        if(removed)break;
    }
    passed=Check(removed,"native row Remove activated")&&passed;
    NewFrame();owner.ReleaseRetiredTextures();owner.ReleaseRetiredTextures();
    passed=Check(!glIsTexture(original)&&deletions[original]==1,"hidden next-frame pump deletes exactly once")&&passed;
    passed=Check(glIsTexture(unrelated),"retirement cannot affect another owner")&&passed;
    // A driver may immediately reuse the retired numeric name. A repeated
    // pump must never delete a newly-created texture, reused name or not.
    const GLuint fresh=Texture(0);owner.m_Textures[9].texture=fresh;
    owner.ReleaseRetiredTextures();
    passed=Check(glIsTexture(fresh),"repeated pump cannot delete a newly allocated/reused name")&&passed;
    ImGui::EndFrame();owner.Shutdown();other.Shutdown();
}
{
    deletions.clear();
    Owner owner;Item item;item.id=1;item.thumbnailBytes=std::make_shared<const std::vector<unsigned char>>(1,1);
    NewFrame();const GLuint original=owner.EnsureTexture(item)->texture;Draw(original);Draw(original,{80,20});
    item.thumbnailBytes=std::make_shared<const std::vector<unsigned char>>(1,2);
    const GLuint replacement=owner.EnsureTexture(item)->texture;
    owner.ReleaseRetiredTextures();Submit();
    passed=Check(glIsTexture(original)&&glIsTexture(replacement)&&RedAt({30,30})&&RedAt({90,30}),"same-frame replacement preserves repeated submissions")&&passed;
    NewFrame();owner.ReleaseRetiredTextures();passed=Check(!glIsTexture(original)&&glIsTexture(replacement),"replacement keeps active cached texture")&&passed;ImGui::EndFrame();owner.Shutdown();
}
{
    deletions.clear();
    Owner owner;NewFrame();GLuint oldest=0;
    for(int i=0;i<128;++i){const GLuint name=Texture();owner.m_Textures[i+1].texture=name;owner.m_Textures[i+1].usedFrame=i;if(i==0)oldest=name;}
    Draw(oldest);Item item;item.id=200;item.thumbnailBytes=std::make_shared<const std::vector<unsigned char>>(1,1);owner.EnsureTexture(item);
    owner.ReleaseRetiredTextures();Submit();passed=Check(glIsTexture(oldest)&&RedAt({30,30}),"LRU eviction retains current draw")&&passed;
    NewFrame();owner.ReleaseRetiredTextures();passed=Check(!glIsTexture(oldest),"LRU name retires next frame")&&passed;ImGui::EndFrame();owner.Shutdown();
}
{
    deletions.clear();
    Owner owner;NewFrame();const GLuint name=Texture();owner.m_Textures[1].texture=name;Item item;item.id=1;owner.m_FrameItems={item};Draw(name);
    owner.FrameItems();owner.FrameItems();owner.ReleaseRetiredTextures();Submit();
    passed=Check(glIsTexture(name)&&RedAt({30,30}),"same-frame snapshot pruning preserves submitted thumbnail")&&passed;
    NewFrame();owner.ReleaseRetiredTextures();passed=Check(!glIsTexture(name),"pruned texture releases when Queue is not rendered")&&passed;ImGui::EndFrame();owner.Shutdown();
}
{
    deletions.clear();
    Owner owner;NewFrame();const GLuint retired=Texture(),active=Texture();owner.m_Textures[1].texture=retired;owner.m_Textures[2].texture=active;Draw(retired);owner.ReleaseTexture(1);Submit();
    ImGuiContext* context=ImGui::GetCurrentContext();ImGui::SetCurrentContext(nullptr);owner.ReleaseRetiredTextures();
    passed=Check(glIsTexture(retired)&&glIsTexture(active),"no ImGui context defers retirement safely")&&passed;
    owner.Shutdown();passed=Check(!glIsTexture(retired)&&!glIsTexture(active)&&deletions[retired]==1&&deletions[active]==1,"GL-context shutdown flushes active and retired names once")&&passed;
    owner.Shutdown();ImGui::SetCurrentContext(context);
}
passed=Check(glGetError()==GL_NO_ERROR,"GL lifetime tests leave no errors")&&passed;
ImGui_ImplOpenGL3_Shutdown();ImGui::DestroyContext();glfwDestroyWindow(w);glfwTerminate();if(!passed)return 1;std::cout<<"Queue texture lifetime passed (native Remove, replacement, LRU, pruning, hidden pump, owner isolation, shutdown, literal labels).\n";
}
