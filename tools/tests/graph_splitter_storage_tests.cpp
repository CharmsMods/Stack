#include "imgui.h"
#include "imgui_internal.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <tuple>
static bool counting=false;
static std::size_t allocations=0,liveAllocations=0;
static void* Allocate(std::size_t n,void*) {if(counting)++allocations;void*p=std::malloc(n);if(p)++liveAllocations;return p;}
static void Free(void*p,void*) {if(p)--liveAllocations;std::free(p);}
static void Callback(const ImDrawList*,const ImDrawCmd*){}
static void Begin(ImDrawList* list,int variant) {
    list->_ResetForNewFrame();list->PushTextureID(ImTextureID(1));
    list->PushClipRect({float(variant),float(variant)},{1600,1000});
}
static void Cards(ImDrawList* list,int count,int variant) {
    for(int i=0;i<count;++i) {
        float x=float(i%8)*180+variant,y=float(i/8)*100;
        list->AddRectFilled({x,y},{x+150,y+80},0xffffffff,8);
        list->AddText({x+10,y+10},0xffffffff,"Exposure and color");
        if(i%7==0)list->AddImage(ImTextureID(2+variant),{x,y+40},{x+25,y+65});
    }
    if(count)list->AddCallback(Callback,reinterpret_cast<void*>(99));
}
static void Wires(ImDrawList* list,int count) {
    for(int i=0;i<count;++i)list->AddLine({0,float(i)*10},{1500,float(i)*10},0xffabcdef,2);
}
static void End(ImDrawList* list){list->PopClipRect();list->PopTextureID();}
struct Owner {
// PRODUCTION_MEMBER
    void Draw(ImDrawList* drawList,int nodes,int variant) {
        Begin(drawList,variant);
// PRODUCTION_BINDING
// PRODUCTION_SPLIT
// PRODUCTION_FRONT
        Cards(drawList,nodes,variant);
// PRODUCTION_BACK
        Wires(drawList,nodes);
// PRODUCTION_MERGE
        End(drawList);
    }
};
static void Reference(ImDrawList* list,int nodes,int variant) {
    Begin(list,variant);ImDrawListSplitter splitter;
    splitter.Split(list,2);splitter.SetCurrentChannel(list,1);Cards(list,nodes,variant);
    splitter.SetCurrentChannel(list,0);Wires(list,nodes);splitter.Merge(list);End(list);
}
static auto Fields(const ImDrawCmd& c) {
    return std::make_tuple(c.ClipRect.x,c.ClipRect.y,c.ClipRect.z,c.ClipRect.w,c.GetTexID(),
        c.VtxOffset,c.IdxOffset,c.ElemCount,c.UserCallback,c.UserCallbackData,
        c.UserCallbackDataSize,c.UserCallbackDataOffset);
}
static bool Same(const ImDrawList& a,const ImDrawList& b) {
    if(a.VtxBuffer.Size!=b.VtxBuffer.Size||a.IdxBuffer.Size!=b.IdxBuffer.Size||a.CmdBuffer.Size!=b.CmdBuffer.Size)return false;
    for(int i=0;i<a.VtxBuffer.Size;++i) {
        const auto&x=a.VtxBuffer[i];const auto&y=b.VtxBuffer[i];
        if(x.pos.x!=y.pos.x||x.pos.y!=y.pos.y||x.uv.x!=y.uv.x||x.uv.y!=y.uv.y||x.col!=y.col)return false;
    }
    for(int i=0;i<a.IdxBuffer.Size;++i)if(a.IdxBuffer[i]!=b.IdxBuffer[i])return false;
    for(int i=0;i<a.CmdBuffer.Size;++i)if(Fields(a.CmdBuffer[i])!=Fields(b.CmdBuffer[i]))return false;
    return true;
}
int main() {
    ImGui::SetAllocatorFunctions(Allocate,Free);ImGui::CreateContext();
    auto&io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1600,1000};
    unsigned char*p;int w,h;io.Fonts->GetTexDataAsRGBA32(&p,&w,&h);ImGui::NewFrame();
    {ImDrawList warm(ImGui::GetDrawListSharedData());Reference(&warm,64,0);}
    const auto baselineLive=liveAllocations;bool passed=true;
    {
        Owner first,second;
        ImDrawList a(ImGui::GetDrawListSharedData()),b(ImGui::GetDrawListSharedData()),reference(ImGui::GetDrawListSharedData());
        int frame=0;
        for(int nodes:{64,64,0,1,128,16,0,64,64}) {
            for(int owner=0;owner<2;++owner) {
                auto&instance=owner?second:first;auto*list=(frame+owner)%2?&a:&b;
                instance.Draw(list,nodes,owner);Reference(&reference,nodes,owner);
                passed=Same(*list,reference)&&passed;
            }
            ++frame;
        }
        first.Draw(&a,64,0);first.Draw(&a,64,0);
        allocations=0;counting=true;first.Draw(&a,64,0);counting=false;
        if(allocations!=0){std::cerr<<"Warm graph splitter reallocates "<<allocations<<" times\n";passed=false;}
        Owner moved=std::move(first);moved.Draw(&a,64,0);Reference(&reference,64,0);passed=Same(a,reference)&&passed;
    }
    if(liveAllocations!=baselineLive){std::cerr<<"Owner destruction retained channel buffers\n";passed=false;}
    ImGui::EndFrame();ImGui::DestroyContext();
    if(!passed)return 1;
    std::cout<<"Graph splitter command/vertex/index parity, owner lifetime and warm allocation checks passed.\n";
}
