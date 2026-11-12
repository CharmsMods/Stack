#include "GalleryBrowserMotion.h"
#include <algorithm>
namespace Stack::Editor::RawLabInternal {
namespace {
ImVec2 Map(ImVec2 p,ImRect from,ImRect to) {
    return {to.Min.x+(p.x-from.Min.x)*to.GetWidth()/std::max(1.f,from.GetWidth()),
        to.Min.y+(p.y-from.Min.y)*to.GetHeight()/std::max(1.f,from.GetHeight())};
}
ImU32 Fade(ImU32 color,float alpha) {
    return (color&~IM_COL32_A_MASK)|(ImU32(((color>>IM_COL32_A_SHIFT)&255)*alpha)<<IM_COL32_A_SHIFT);
}
ImRect Mix(ImRect a,ImRect b,float t){return {ImLerp(a.Min,b.Min,t),ImLerp(a.Max,b.Max,t)};}
}
GalleryBrowserMotion::~GalleryBrowserMotion(){Stop();}
void GalleryBrowserMotion::Stop(){if(captured_)IM_DELETE(captured_);captured_=nullptr;active_=false;waiting_=false;}
void GalleryBrowserMotion::Capture(ImDrawList* draw,ImRect surface) {
    Stop();captured_=draw->CloneOutput();source_=surface;
}
void GalleryBrowserMotion::Start(bool outward,ImRect tile,unsigned cover,int width,int height) {
    if(!outward && captured_){IM_DELETE(captured_);captured_=nullptr;}
    outward_=outward;tile_=tile;progress_=0;active_=true;waiting_=true;
    cover_=cover;coverWidth_=width;coverHeight_=height;
}
void GalleryBrowserMotion::Begin(ImRect surface,bool ready,bool reduced) {
    surface_=surface;if(!active_)return;
    if(reduced){Stop();return;}
    if(waiting_){if(!ready)return;waiting_=false;started_=ImGui::GetTime();}
    const float t=std::clamp(float((ImGui::GetTime()-started_)/.26),0.f,1.f);
    progress_=1-(1-t)*(1-t)*(1-t);
    if(t>=1){Stop();return;}
    if(tile_.GetWidth()<=0 || tile_.GetHeight()<=0) {
        const auto center=surface.GetCenter();tile_={{center.x-36,center.y-36},{center.x+36,center.y+36}};
    }
    if(!outward_) {
        const auto rect=Mix(tile_,surface_,progress_);
        ImGui::GetWindowDrawList()->AddRectFilled(rect.Min,rect.Max,ImGui::GetColorU32(ImGuiCol_WindowBg),12*(1-progress_));
    }
}
void GalleryBrowserMotion::Finish(ImDrawList* draw,int firstVertex) {
    if(!active_ || waiting_)return;
    if(!outward_) {
        const auto rect=Mix(tile_,surface_,progress_);
        for(int i=firstVertex;i<draw->VtxBuffer.Size;++i) {
            auto& vertex=draw->VtxBuffer[i];vertex.pos=Map(vertex.pos,surface_,rect);
            vertex.col=Fade(vertex.col,.3f+.7f*progress_);
        }
        for(auto& command:draw->CmdBuffer) {
            const auto low=Map({command.ClipRect.x,command.ClipRect.y},surface_,rect);
            const auto high=Map({command.ClipRect.z,command.ClipRect.w},surface_,rect);
            command.ClipRect={low.x,low.y,high.x,high.y};
        }
        if(cover_ && coverWidth_>0 && coverHeight_>0) {
            const float scale=std::min(rect.GetWidth()/coverWidth_,rect.GetHeight()/coverHeight_);
            const ImVec2 size{coverWidth_*scale,coverHeight_*scale},center=rect.GetCenter();
            draw->AddImageRounded((ImTextureID)(intptr_t)cover_,{center.x-size.x*.5f,center.y-size.y*.5f},
                {center.x+size.x*.5f,center.y+size.y*.5f},{0,0},{1,1},Fade(IM_COL32_WHITE,1-progress_),6);
        }
    } else if(captured_) {
        const auto rect=Mix(surface_,tile_,progress_);
        const float alpha=std::min(1.f,(1-progress_)*4);
        draw->AddRectFilled(rect.Min,rect.Max,ImGui::GetColorU32(ImGuiCol_WindowBg,alpha),12*progress_);
        for(const auto& command:captured_->CmdBuffer) {
            if(command.UserCallback || !command.ElemCount)continue;
            const auto low=Map({command.ClipRect.x,command.ClipRect.y},source_,rect);
            const auto high=Map({command.ClipRect.z,command.ClipRect.w},source_,rect);
            draw->PushClipRect(low,high,true);draw->PushTexture(command.TexRef);
            // Chunking keeps each reservation valid with 16-bit ImDrawIdx.
            for(unsigned offset=0;offset<command.ElemCount;) {
                const auto count=std::min(6000u,command.ElemCount-offset);
                draw->PrimReserve(int(count),int(count));
                for(unsigned i=0;i<count;++i) {
                    const auto& v=captured_->VtxBuffer[command.VtxOffset+captured_->IdxBuffer[command.IdxOffset+offset+i]];
                    draw->PrimVtx(Map(v.pos,source_,rect),v.uv,Fade(v.col,alpha));
                }
                offset+=count;
            }
            draw->PopTexture();draw->PopClipRect();
        }
    }
}
}
