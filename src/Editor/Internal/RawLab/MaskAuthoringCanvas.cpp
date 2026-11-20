#include "Editor/EditorModule.h"
#include "Editor/MaskToolBindings.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include "App/WorkspacePresentation.h"
#include <imgui_internal.h>

using namespace Stack;
using namespace Stack::MaskAuthoring;
using Phase=Editor::MaskAuthoringSession::Phase;

namespace {
void RefreshTexture(Editor::MaskAuthoringSession& s){
    if(!s.textureDirty || !s.preview)return;
    s.textureDirty=false;const auto& data=*s.preview;
    std::vector<unsigned char> pixels(data.values.size()*4);
    for(size_t i=0;i<data.values.size();++i){
        const auto v=static_cast<unsigned char>(std::round(255*std::clamp(data.values[i],0.f,1.f)));
        pixels[4*i]=s.gray?v:255;pixels[4*i+1]=s.gray?v:74;pixels[4*i+2]=s.gray?v:122;pixels[4*i+3]=s.gray?255:v/2;
    }
    Renderer::GLState::TextureBinding binding(GL_TEXTURE_2D,GL_TEXTURE_BINDING_2D);
    Renderer::GLState::PixelUnpackState unpack;unpack.ConfigureTightCpuUpload();
    s.texture.Reset(GLHelpers::CreateTextureFromPixels(pixels.data(),data.width,data.height,4,false));
    unpack.Restore();binding.Restore();
}
}
void EditorModule::RenderMaskCanvas(const ImRect& bounds,const RawRecipe::RawCropRotationRecipe& crop){
    if(Workspace::IsPreview() || bounds.GetWidth()<=0 || bounds.GetHeight()<=0)return;
    auto& s=m_MaskSession;const auto& io=ImGui::GetIO();
    ImRect visible=bounds;visible.ClipWith(ImGui::GetCurrentWindow()->ClipRect);
    const bool hovered=visible.Contains(io.MousePos) && ImGui::IsWindowHovered() && !io.WantTextInput &&
        !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId);
    if(s.phase==Phase::Idle){
        if(hovered && !io.KeyCtrl && !io.KeyAlt && !io.KeyShift && ImGui::IsMouseClicked(0)) {
            for(int i=0;i<=int(Tool::Edge);++i)if(ImGui::IsKeyDown(Editor::MaskToolKey(Tool(i)))){StartMaskTool(Tool(i));break;}
        }
        return;
    }
    if(!MaskOwnerCurrent() || io.AppFocusLost){CancelMaskAuthoring();return;}
    if(s.phase==Phase::Destination){DrawMaskPickerTarget(visible,"Save to Graph",{});return;}
    ResolveMaskSource(crop);UpdateMaskPreview();RefreshTexture(s);
    const auto screen=[&](Point p){const auto uv=s.mapping.Canvas({p[0],p[1]});
        return ImVec2(bounds.Min.x+uv.x*bounds.GetWidth(),bounds.Min.y+uv.y*bounds.GetHeight());};
    const auto uv=s.mapping.Local({(io.MousePos.x-bounds.Min.x)/bounds.GetWidth(),(io.MousePos.y-bounds.Min.y)/bounds.GetHeight()});
    const Point point{std::clamp(uv.x,0.f,1.f),std::clamp(uv.y,0.f,1.f)};
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(visible.Min,visible.Max,true);
    if(s.texture.Get())draw->AddImageQuad((ImTextureID)(intptr_t)s.texture.Get(),screen({0,0}),screen({1,0}),screen({1,1}),screen({0,1}),
        {0,0},{1,0},{1,1},{0,1});
    const float aspect=s.field?float(s.field->width)/s.field->height:s.mapping.sourceAspect;
    const auto rim=[&](Point p,float radius){p[0]+=radius*std::min(1.f,1/aspect);return screen(p);};
    const auto distance=[](ImVec2 a,ImVec2 b){return std::hypot(a.x-b.x,a.y-b.y);};
    auto selected=[&]()->Mark*{return s.selected>=0 && s.selected<int(s.recipe.marks.size())?&s.recipe.marks[s.selected]:nullptr;};
    const auto canDraw=s.phase==Phase::Drawing && s.field && !ImGui::IsKeyDown(ImGuiKey_Space) && !IsRawImageNavigationActive();
    if(canDraw && hovered && !io.KeyCtrl && !io.KeyAlt && !io.KeyShift && !s.dragging) {
        for(int i=0;i<8;++i)if(Tool(i)!=Tool::Circle && ImGui::IsKeyPressed(Editor::MaskToolKey(Tool(i)),false))s.tool=Tool(i);
    }
    const Tool tool=canDraw && ImGui::IsKeyDown(Editor::MaskToolKey(Tool::Circle))?Tool::Circle:s.tool;
    if(canDraw && hovered && io.MouseWheel && (io.KeyCtrl || io.KeyShift)) {
        Mark* active=selected();if(active && active->tool!=tool)active=nullptr;
        if(io.KeyCtrl){s.radius=std::clamp((active?active->radius:s.radius)*std::exp2(io.MouseWheel*.18f),.001f,1.f);if(active)active->radius=s.radius;}
        if(io.KeyShift){s.feather=std::clamp((active?active->feather:s.feather)+io.MouseWheel*.05f,0.f,1.f);if(active)active->feather=s.feather;}
        if(active){if(io.KeyCtrl && active->tool==Tool::Circle)active->samples.clear();s.Dirty();}ImGui::SetKeyOwner(ImGuiKey_MouseWheelY,ImGui::GetCurrentWindow()->ID);
    }
    if(canDraw && hovered && ImGui::GetFrameCount()>s.acceptAfter && ImGui::IsMouseClicked(0) && !io.KeyCtrl && !io.KeyAlt){
        s.dragHandle=-1;
        if(auto* mark=selected();mark && mark->tool==tool && !mark->points.empty()){
            if(tool==Tool::Circle && distance(rim(mark->points.front(),mark->radius),io.MousePos)<8)s.dragHandle=2;
            else if(mark->points.size()>1 && distance(screen(mark->points.back()),io.MousePos)<7)s.dragHandle=1;
            else if(distance(screen(mark->points.front()),io.MousePos)<7)s.dragHandle=0;
        }
        if(s.dragHandle<0 && s.recipe.marks.size()<4096){
            Mark m;m.tool=tool;m.role=s.role;m.radius=s.radius;m.feather=s.feather;m.points.push_back(point);
            s.recipe.marks.push_back(std::move(m));s.selected=int(s.recipe.marks.size())-1;s.dragHandle=3;
        }
        s.dragStart=point;s.dragging=s.dragHandle>=0;
    }
    if(s.dragging && selected() && ImGui::IsMousePosValid()){
        auto& m=*selected();auto& points=m.points;
        const auto delta=Point{point[0]-s.dragStart[0],point[1]-s.dragStart[1]};
        if(delta[0]!=0 || delta[1]!=0){
            m.samples.clear();
            if(s.dragHandle==0){
                if(m.tool==Tool::Edge)points.front()=point;
                else for(auto& p:points){p[0]=std::clamp(p[0]+delta[0],0.f,1.f);p[1]=std::clamp(p[1]+delta[1],0.f,1.f);}
            }else if(m.tool==Tool::Circle){
                const auto a=points.front();m.radius=std::clamp(std::hypot((point[0]-a[0])*aspect,point[1]-a[1])/std::min(1.f,aspect),.001f,1.f);
            }else if(s.dragHandle==1){points.back()=point;}
            else if(m.tool==Tool::Brush || m.tool==Tool::Erase || m.tool==Tool::Values){
                if(points.size()<65536 && distance(screen(points.back()),screen(point))>=.7f)points.push_back(point);
            }else if(points.size()==1)points.push_back(point);else points.back()=point;
            s.dragStart=point;
        }
        if(!ImGui::IsMouseDown(0)){
            s.dragging=false;
            if(s.dragHandle==3 && points.size()==1 && (m.tool==Tool::Edge || m.tool==Tool::Linear ||
                    m.tool==Tool::Radial || m.tool==Tool::Rectangle)){
                s.recipe.marks.erase(s.recipe.marks.begin()+s.selected);s.selected=-1;
            } else s.Dirty();
        }
    }
    const ImU32 white=IM_COL32(246,248,255,225);
    for(int i=0;i<int(s.recipe.marks.size());++i){
        const auto& m=s.recipe.marks[i];if(m.points.empty())continue;
        const bool active=s.selected==i;
        if(!active && m.tool!=Tool::Edge && m.tool!=Tool::Circle)continue;
        const auto a=screen(m.points.front()),b=screen(m.points.back());
        const auto color=active?white:IM_COL32(180,190,205,175);
        if(m.tool==Tool::Circle){
            const auto handle=rim(m.points.front(),m.radius);draw->AddCircle(a,distance(a,handle),color,48,active?2.f:1.f);
            if(active)draw->AddCircleFilled(handle,4,color);
        }else if(m.tool==Tool::Radial || m.tool==Tool::Rectangle){
            const ImVec2 r{std::max(1.f,std::abs(a.x-b.x)),std::max(1.f,std::abs(a.y-b.y))};
            if(m.tool==Tool::Radial)draw->AddEllipse(a,r,color,0,48,1.5f);
            else draw->AddRect({a.x-r.x,a.y-r.y},{a.x+r.x,a.y+r.y},color);
        }else if(m.tool==Tool::Edge || m.tool==Tool::Linear){draw->AddLine(a,b,color,2);draw->AddCircle(b,5,color);}
        else if(active){
            const size_t stride=std::max<size_t>(1,m.points.size()/512);
            for(size_t j=stride;j<m.points.size();j+=stride)draw->AddLine(screen(m.points[j-stride]),screen(m.points[j]),color,1.4f);
        }
        if(active){draw->AddCircleFilled(a,4,color);if(m.points.size()>1)draw->AddCircleFilled(b,3,color);}
    }
    if(canDraw && hovered && (tool==Tool::Brush || tool==Tool::Erase || tool==Tool::Circle)){
        const float r=distance(screen(point),rim(point,s.radius));draw->AddCircle(io.MousePos,r,white,48,1.f);
        if(tool!=Tool::Circle)draw->AddCircle(io.MousePos,r*(1-s.feather),IM_COL32(255,255,255,120),48);
    }
    draw->PopClipRect();DrawMaskPalette();
}

