// SPDX-License-Identifier: GPL-3.0-only
#include "GlassChrome.h"
#include "Glass/ControlMaterial.h"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace Stack::Renderer {
namespace {
std::atomic<std::uint64_t> nextGroup{1};
float Linear(float c){return c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);}
}
void GlassChrome::BeginFrame(bool requested){
    if(!requested&&enabled)Shutdown();
    enabled=requested;++frame;backdrops.clear();objects.clear();inks.clear();compoundCount=0;
    activeBackdrop=nullptr;captureValid=inkReady=false;gpu.BeginFrame();
}
GlassChrome::Backdrop* GlassChrome::Resolve(SurfaceHandle h){
    return h.owner==this&&h.frame==frame&&h.index<backdrops.size()?&backdrops[h.index]:nullptr;
}
GlassChrome::SurfaceHandle GlassChrome::AddBackdrop(ImDrawList* draw,ImVec2 min,ImVec2 max,ImVec4 tint,
    const LightingFrame*,std::uint64_t revision){
    if(!enabled||!Error().empty()||!draw||!std::isfinite(min.x)||!std::isfinite(min.y)||
        !std::isfinite(max.x)||!std::isfinite(max.y)||max.x<=min.x||max.y<=min.y||
        ImGui::GetWindowViewport()!=ImGui::GetMainViewport())return {};
    const auto* v=ImGui::GetMainViewport();
    const ImVec2 low=draw->GetClipRectMin(),high=draw->GetClipRectMax();
    backdrops.emplace_back();auto& b=backdrops.back();
    b.owner=this;b.min=min;b.max=max;b.tint=tint;b.displayPos=v->Pos;b.displaySize=v->Size;
    b.scale=ImGui::GetIO().DisplayFramebufferScale;b.clip={low.x,low.y,high.x,high.y};
    b.group=nextGroup.fetch_add(1);b.revision=revision;BuildLayout(b);
    if(b.layout.width<=0||b.layout.height<=0||Glass::MaterialRenderer::RequiredBytes(b.layout.width,b.layout.height)>Glass::MaterialRenderer::BudgetBytes){
        backdrops.pop_back();return {};
    }
    draw->AddCallback(BackdropCallback,&b);draw->AddCallback(ImDrawCallback_ResetRenderState,nullptr);
    return {this,backdrops.size()-1,frame};
}
void GlassChrome::BuildLayout(Backdrop& b){
    auto& l=b.layout;l.framebufferWidth=int(b.displaySize.x*b.scale.x);l.framebufferHeight=int(b.displaySize.y*b.scale.y);
    if(b.scale.x<=0||b.scale.y<=0||l.framebufferWidth<=0||l.framebufferHeight<=0)return;
    const float left=std::max({b.min.x,b.clip.x,b.displayPos.x}),right=std::min({b.max.x,b.clip.z,b.displayPos.x+b.displaySize.x});
    const float top=std::max({b.min.y,b.clip.y,b.displayPos.y}),bottom=std::min({b.max.y,b.clip.w,b.displayPos.y+b.displaySize.y});
    if(right<=left||bottom<=top)return;
    const int pad=int(std::ceil(b.padding));
    l.x=std::max(0,int(std::floor((left-b.displayPos.x)*b.scale.x))-pad);
    l.y=std::max(0,int(std::floor(l.framebufferHeight-(bottom-b.displayPos.y)*b.scale.y))-pad);
    l.width=std::min(l.framebufferWidth,int(std::ceil((right-b.displayPos.x)*b.scale.x))+pad)-l.x;
    l.height=std::min(l.framebufferHeight,int(std::ceil(l.framebufferHeight-(top-b.displayPos.y)*b.scale.y))+pad)-l.y;
    // Reserve a complete captured rectangle, not uninitialized texture capacity.
    // Small motion stays in the same size bucket without changing source density.
    l.width=std::min(l.framebufferWidth,((l.width+31)/32)*32);
    l.height=std::min(l.framebufferHeight,((l.height+31)/32)*32);
    l.x=std::min(l.x,l.framebufferWidth-l.width);l.y=std::min(l.y,l.framebufferHeight-l.height);
}
bool GlassChrome::AddObject(ImDrawList* draw,SurfaceHandle handle,ImVec2 min,ImVec2 max,ImVec4 tint,float radius,
    float elevation,MaterialRole role,float opacity,const Interaction* interaction,const JoinGroup* join){
    auto* b=Resolve(handle);
    if(!b||!draw||!enabled||!Error().empty()||ImGui::GetWindowViewport()!=ImGui::GetMainViewport()||
        !std::isfinite(radius)||!std::isfinite(min.x)||!std::isfinite(min.y)||!std::isfinite(max.x)||!std::isfinite(max.y)||max.x<=min.x||max.y<=min.y)return false;
    const float sx=b->scale.x,sy=b->scale.y,scale=std::min(sx,sy);
    if(std::abs(sx-sy)>1.e-5f)return false; // Tensor is defined in an isotropic UI metric.
    const float w=(max.x-min.x)*sx,h=(max.y-min.y)*sy;
    auto preset=Glass::ClearControlMaterial(role==MaterialRole::RegularSurface?std::min(14.f*scale,.5f*std::min(w,h)):.5f*std::min(w,h));
    if(!preset)return false;
    auto m=*preset;
    if(role==MaterialRole::RegularSurface){m.tintAmount=.16f;m.tintLinear={Linear(tint.x),Linear(tint.y),Linear(tint.z)};m.roughnessLod=std::log2(std::max(1.f,9.f*scale));m.causticGain=0;}
    const float press=interaction?std::clamp(interaction->press,-.12f,1.12f):std::clamp((1.f-elevation)/.8f,0.f,1.f);
    m.contactGain=.022f*std::max(0.f,press);
    // Match v0.3's optical response after uniform visual growth is applied.
    m.crownHeightPx*=1.f-.08f*press;
    m.edge.reachPx*=1.f+.06f*press;
    Glass::MaterialEffects effects;effects.clearWater=true;effects.opacity=std::clamp(opacity,0.f,1.f);
    if(interaction){
        m.tilt={interaction->tilt.x,-interaction->tilt.y};m.contactLocalPx={interaction->contact.x*sx,-interaction->contact.y*sy};
        effects.logStrain={interaction->strain.x,-interaction->strain.y};
        if(interaction->contactGain>=0)m.contactGain=interaction->contactGain;
        m.shadowSigmaPx*=1.f+.20f*interaction->elevation;
        m.shadowOffsetPx=m.shadowOffsetPx*(1.f+.20f*interaction->elevation);
    }
    if(!stack::glass::finite(effects.logStrain) || std::hypot(effects.logStrain.x,effects.logStrain.y)>.30001f)return false;
    const float strainReach=std::hypot(w*.5f,h*.5f)*std::expm1(std::hypot(effects.logStrain.x,effects.logStrain.y));
    const float filter=std::max(4.f,std::exp2(m.roughnessLod)*2.f);
    const float joinWidth=join && join->set && std::isfinite(join->width)?std::clamp(join->width*scale,0.f,.5f*std::min(w,h)):0.f;
    const float apron=std::ceil(strainReach+joinWidth+std::max({m.edge.reachPx+filter,2.4f*m.shadowSigmaPx+std::abs(m.shadowOffsetPx.y),3.f*m.causticWidthPx}));
    const float oldPadding=b->padding;b->padding=std::max(b->padding,apron);BuildLayout(*b);
    if(Glass::MaterialRenderer::RequiredBytes(b->layout.width,b->layout.height)>Glass::MaterialRenderer::BudgetBytes){b->padding=oldPadding;BuildLayout(*b);return false;}
    const auto clipMin=draw->GetClipRectMin(),clipMax=draw->GetClipRectMax();
    const float x=(min.x-b->displayPos.x)*sx,y=b->layout.framebufferHeight-(max.y-b->displayPos.y)*sy;
    const float left=std::max({b->min.x,b->clip.x,clipMin.x,b->displayPos.x}),right=std::min({b->max.x,b->clip.z,clipMax.x,b->displayPos.x+b->displaySize.x});
    const float top=std::max({b->min.y,b->clip.y,clipMin.y,b->displayPos.y}),bottom=std::min({b->max.y,b->clip.w,clipMax.y,b->displayPos.y+b->displaySize.y});
    const int l=std::max(int(std::floor(x-apron)),int(std::ceil((left-b->displayPos.x)*sx)));
    const int r=std::min(int(std::ceil(x+w+apron)),int(std::floor((right-b->displayPos.x)*sx)));
    const int low=std::max(int(std::floor(y-apron)),int(std::ceil(b->layout.framebufferHeight-(bottom-b->displayPos.y)*sy)));
    const int high=std::min(int(std::ceil(y+h+apron)),int(std::floor(b->layout.framebufferHeight-(top-b->displayPos.y)*sy)));
    if(r<=l||high<=low)return false;
    if(b->remainingObjects==0) {b->left=l;b->bottom=low;b->right=r;b->top=high;}
    else {b->left=std::min(b->left,l);b->bottom=std::min(b->bottom,low);b->right=std::max(b->right,r);b->top=std::max(b->top,high);}
    ++b->remainingObjects;
    objects.push_back({this,b,m,{x+w*.5f,y+h*.5f},{w*.5f,h*.5f},std::clamp(radius*scale,0.f,.5f*std::min(w,h)),
        std::isfinite(opacity)?std::clamp(opacity,0.f,1.f):1.f,l,low,r-l,high-low,effects});
    if(joinWidth>0)objects.back().join={join->set,join->item,joinWidth};
    objects.back().clip={left,top,right,bottom};
    draw->AddCallback(ObjectCallback,&objects.back());draw->AddCallback(ImDrawCallback_ResetRenderState,nullptr);return true;
}
bool GlassChrome::BeginInk(ImDrawList* draw,SurfaceHandle handle,ImVec2 min,ImVec2 max,ImGuiID id){
    const auto* b=Resolve(handle);if(!b||!draw||!enabled||!Error().empty()||max.x<=min.x||max.y<=min.y||inks.size()>=MaxInkItems)return false;
    const auto low=draw->GetClipRectMin(),high=draw->GetClipRectMax();
    if(max.x<=low.x||min.x>=high.x||max.y<=low.y||min.y>=high.y)return false;
    inks.push_back({this,b,min,max,id,MaxInkItems,{low.x,low.y,high.x,high.y},false});draw->AddCallback(InkCallback,&inks.back());return true;
}
void GlassChrome::EndInk(ImDrawList* draw){draw->AddCallback(EndInkCallback,nullptr);draw->AddCallback(ImDrawCallback_ResetRenderState,nullptr);}
}
