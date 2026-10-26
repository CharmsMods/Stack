#!/usr/bin/env python3
"""Generate actual ImGui draw-geometry checks for production scope loops.

Only the four UI layout/query endpoints are stand-ins. The scope draw methods
are extracted from production, use real immutable analysis and real ImDrawList,
and are compared with the original per-rectangle drawing path. No GPU or UI.
"""
import argparse
from pathlib import Path


def method(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    scopes = (root / "src/Editor/UI/EditorScopes.cpp").read_text()
    methods = "\n".join(method(scopes, signature) for signature in (
        "void EditorScopes::DrawVectorscope()", "void EditorScopes::DrawRGBParade()"))
    methods = methods.replace("ImGui::", "ScopeUi::")
    text = r'''
#include "Editor/UI/ScopeRectBatch.h"
#include "Editor/GraphScopeData.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
namespace ScopeUi {
ImVec2 available,position,lastDummy;
int dummyCount=0;
ImDrawList* draw=nullptr;
ImVec2 GetContentRegionAvail(){return available;}
ImVec2 GetCursorScreenPos(){return position;}
ImDrawList* GetWindowDrawList(){return draw;}
void Dummy(ImVec2 size){lastDummy=size;++dummyCount;}
}
struct EditorScopes {
    std::shared_ptr<const GraphScopeData> m_Data;
    void DrawVectorscope();void DrawRGBParade();
};
''' + methods + r'''
void Setup(ImDrawList& draw,bool offsets,int prefix){
    draw._ResetForNewFrame();
    if(offsets)draw.Flags|=ImDrawListFlags_AllowVtxOffset;
    draw.PushTextureID(ImTextureRef(ImTextureID(17)));
    draw.PushClipRect({10.25f,20.125f},{450.5f,380.75f});
    for(int i=0;i<prefix;++i)
        draw.AddRectFilled({float(i),1},{float(i)+1.5f,2.5f},IM_COL32(1,2,3,4));
    // Earlier graph geometry need not end on a four-vertex rectangle boundary.
    draw.PrimReserve(3,3);draw.PrimVtx({1,2},{.125f,.25f},IM_COL32_WHITE);
    draw.PrimVtx({3,4},{.125f,.25f},IM_COL32_WHITE);draw.PrimVtx({5,6},{.125f,.25f},IM_COL32_WHITE);
}
void Suffix(ImDrawList& draw){
    draw.PushClipRect({5,8},{12,13});draw.PushTextureID(ImTextureRef(ImTextureID(19)));
    draw.AddRectFilled({2,2},{2,2},IM_COL32(3,4,5,6));
    draw.AddRectFilled({8,9},{3,2},IM_COL32(6,7,8,9));
}
void Compare(const ImDrawList& a,const ImDrawList& b){
    Require(a.VtxBuffer.Size==b.VtxBuffer.Size,"vertex count differs");
    Require(a.IdxBuffer.Size==b.IdxBuffer.Size,"index count differs");
    Require(a.CmdBuffer.Size==b.CmdBuffer.Size,"command count differs");
    Require(!std::memcmp(a.VtxBuffer.Data,b.VtxBuffer.Data,a.VtxBuffer.Size*sizeof(ImDrawVert)),"vertices differ");
    Require(!std::memcmp(a.IdxBuffer.Data,b.IdxBuffer.Data,a.IdxBuffer.Size*sizeof(ImDrawIdx)),"indices differ");
    for(int i=0;i<a.CmdBuffer.Size;++i){const auto& x=a.CmdBuffer[i];const auto& y=b.CmdBuffer[i];
        Require(!std::memcmp(&x.ClipRect,&y.ClipRect,sizeof(ImVec4)),"clip differs");
        Require(x.ElemCount==y.ElemCount && x.IdxOffset==y.IdxOffset && x.VtxOffset==y.VtxOffset,
            "command offsets/elements differ");
        Require(x.GetTexID()==y.GetTexID() && x.UserCallback==y.UserCallback,"texture/callback differs");
    }
    Require(a._VtxCurrentIdx==b._VtxCurrentIdx,"post-batch current index differs");
}
struct Rect{ImVec2 minimum,maximum;ImU32 color;};
Rect Mark(int i,bool transparent){
    const float x=float(i%211)-8.25f,y=float(i%157)+.375f;
    return {{x,y},{i%11?x+1.5f:x,i%13?y+1.5f:y-1},
        transparent || i%7==0?IM_COL32(34,56,78,0):IM_COL32(i%256,123,21,i%256)};
}
void Rectangles(int prefix,int count,bool offsets,bool transparent=false){
    ImDrawListSharedData shared;shared.TexUvWhitePixel={.125f,.25f};
    ImDrawList baseline(&shared),batched(&shared);
    Setup(baseline,offsets,prefix);Setup(batched,offsets,prefix);
    for(int i=0;i<count;++i){const auto r=Mark(i,transparent);baseline.AddRectFilled(r.minimum,r.maximum,r.color);}
    {Stack::Editor::ScopeRectBatch marks(batched);
        for(int i=0;i<count;++i){const auto r=Mark(i,transparent);marks.Add(r.minimum,r.maximum,r.color);}}
    Suffix(baseline);Suffix(batched);Compare(baseline,batched);
}
void OriginalVector(const GraphScopeData& data,ImDrawList& draw,ImVec2 size,ImVec2 pos){
    const float side=std::min(size.x,size.y);
    const ImVec2 center(pos.x+side/2,pos.y+side/2);
    draw.AddCircle(center,side/2,IM_COL32(100,100,100,255),64);
    draw.AddLine({center.x-side/2,center.y},{center.x+side/2,center.y},IM_COL32(50,50,50,255));
    draw.AddLine({center.x,center.y-side/2},{center.x,center.y+side/2},IM_COL32(50,50,50,255));
    for(const auto& point:data.VectorPoints){
        const float x=center.x+point.u*(side*.8f),y=center.y-point.v*(side*.8f);
        draw.AddRectFilled({x,y},{x+1.5f,y+1.5f},IM_COL32(255,255,255,40));
    }
    ScopeUi::Dummy({side,side});
}
void OriginalParade(const GraphScopeData& data,ImDrawList& draw,ImVec2 size,ImVec2 pos){
    if(data.ParadeData.empty())return;
    const float colWidth=size.x/data.ParadeData.size(),rowHeight=size.y/256.f;
    for(std::size_t x=0;x<data.ParadeData.size();++x)for(int i=0;i<256;++i){
        const float r=data.ParadeData[x].r[i],g=data.ParadeData[x].g[i],b=data.ParadeData[x].b[i];
        if(r>0)draw.AddRectFilled({pos.x+x*colWidth,pos.y+(255-i)*rowHeight},
            {pos.x+(x+1)*colWidth,pos.y+(255-i+1)*rowHeight},IM_COL32(255,0,0,(int)std::min(255.f,r*50)));
        if(g>0)draw.AddRectFilled({pos.x+x*colWidth,pos.y+(255-i)*rowHeight},
            {pos.x+(x+1)*colWidth,pos.y+(255-i+1)*rowHeight},IM_COL32(0,255,0,(int)std::min(255.f,g*50)));
        if(b>0)draw.AddRectFilled({pos.x+x*colWidth,pos.y+(255-i)*rowHeight},
            {pos.x+(x+1)*colWidth,pos.y+(255-i+1)*rowHeight},IM_COL32(0,0,255,(int)std::min(255.f,b*50)));
    }
    ScopeUi::Dummy(size);
}
void ScopePlots(std::shared_ptr<const GraphScopeData> data,bool vectors,ImVec2 size,int prefix){
    ImDrawListSharedData shared;shared.TexUvWhitePixel={.375f,.625f};shared.SetCircleTessellationMaxError(.3f);
    ImDrawList baseline(&shared),batched(&shared);Setup(baseline,true,prefix);Setup(batched,true,prefix);
    ScopeUi::available=size;ScopeUi::position={33.125f,-7.75f};ScopeUi::dummyCount=0;
    if(vectors)OriginalVector(*data,baseline,size,ScopeUi::position);
    else OriginalParade(*data,baseline,size,ScopeUi::position);
    const int dummyCount=ScopeUi::dummyCount;const auto dummySize=ScopeUi::lastDummy;
    ScopeUi::draw=&batched;ScopeUi::dummyCount=0;EditorScopes scope{data};
    if(vectors)scope.DrawVectorscope();else scope.DrawRGBParade();
    Require(dummyCount==ScopeUi::dummyCount && (!dummyCount ||
        (dummySize.x==ScopeUi::lastDummy.x && dummySize.y==ScopeUi::lastDummy.y)),"scope layout changed");
    Suffix(baseline);Suffix(batched);Compare(baseline,batched);
}
int main()try{
    for(int prefix:{0,16382,16383})for(int count:{0,1,255,256,257,32768})Rectangles(prefix,count,true);
    for(int prefix:{0,17})for(int count:{0,1,257,2048})Rectangles(prefix,count,false);
    Rectangles(16383,257,true,true);
    std::vector<unsigned char> pixels(256*256*4);
    for(std::size_t i=0;i<pixels.size()/4;++i){pixels[i*4]=i%256;pixels[i*4+1]=(i/256)%256;
        pixels[i*4+2]=(i*17+i/256)%256;pixels[i*4+3]=255;}
    const auto dense=AnalyzeGraphScopePixels(pixels,256,256);
    const auto small=AnalyzeGraphScopePixels({12,34,56,255,88,99,77,255},2,1);
    const auto empty=AnalyzeGraphScopePixels({},0,0);
    for(const auto& data:{empty,small,dense})for(bool vectors:{false,true})
        for(const auto& size:{ImVec2(237.5f,189.25f),ImVec2(0,0),ImVec2(-2,-1)})ScopePlots(data,vectors,size,16382);
    std::cout<<"Scope batching: real ImGui vertices/indices/commands,16-bit rollover, alpha, degenerate sizes, clipping, "
        "backend fallback and actual scope geometry match ("<<sizeof(ImDrawIdx)*8<<"-bit indices).\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
'''
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text)


if __name__ == "__main__":
    main()
