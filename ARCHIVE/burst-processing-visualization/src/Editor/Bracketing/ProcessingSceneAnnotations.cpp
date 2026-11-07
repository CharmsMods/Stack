#include "ProcessingPresentation.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Stack::Editor {
namespace {
ImU32 Ink(float alpha,float brightness=1) {return ImGui::ColorConvertFloat4ToU32({brightness,brightness,brightness,alpha});}
ImVec2 Lerp(ImVec2 a,ImVec2 b,float t){return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};}
void Dashed(ImDrawList* draw,ImVec2 a,ImVec2 b,ImU32 color,float thickness) {
    const float length=std::hypot(b.x-a.x,b.y-a.y);
    for(float d=0;d<length;d+=8)draw->AddLine(Lerp(a,b,d/std::max(1.f,length)),Lerp(a,b,std::min(length,d+3)/std::max(1.f,length)),color,thickness);
}
std::string Number(double value) {
    char text[40];if(!std::isfinite(value))return "--";
    std::snprintf(text,sizeof(text),std::abs(value)>0&&std::abs(value)<.001?"%.2e":"%.2f",value);return text;
}
}
void ProcessingSceneRenderer::Annotate(ImDrawList* draw,ImVec2 origin,ImVec2 size,const ProcessingSnapshot& snapshot,float alpha) const {
    float t=dissolveDuration_>0?float(std::clamp((drawTime_-dissolveStart_)/dissolveDuration_,0.,1.)):1.f;
    t=t*t*(3-2*t);
    if(t<1&&!previousAnnotations_.empty()) {
        ProcessingSnapshot previous;previous.evidence=previousAnnotationEvidence_;
        AnnotateLayer(draw,origin,size,previous,alpha*(1-t),previousAnnotations_);
    }
    AnnotateLayer(draw,origin,size,snapshot,alpha*t*AnnotationOpacity(),annotations_);
}
void ProcessingSceneRenderer::AnnotateLayer(ImDrawList* draw,ImVec2 origin,ImVec2 size,const ProcessingSnapshot& snapshot,float alpha,const std::vector<Annotation>& annotations) const {
    if(alpha<=.001f)return;
    const float font=ImGui::GetFontSize(),stroke=std::max(1.f,font/16);
    const auto pos=[&](ImVec2 p){return ImVec2{origin.x+p.x*size.x,origin.y+p.y*size.y};};
    const Annotation* main=nullptr;
    for(const auto& a:annotations)if(a.main)main=&a;
    for(const auto& a:annotations) {
        const auto bottom=pos(a.corners[3]);
        auto label=ImVec2{bottom.x,bottom.y+font*.65f};
        if(a.main) {
            const bool field=snapshot.evidence&&(!snapshot.evidence->vectors.empty()||!snapshot.evidence->kernels.empty());
            const auto top=pos(a.corners[0]);label={top.x,top.y-font*(field?3.2f:1.7f)};
        }
        draw->AddText(label,Ink(alpha*.7f),a.label.c_str());
        if(a.evidence>=0&&snapshot.evidence&&std::size_t(a.evidence)<snapshot.evidence->rasters.size()) {
            const auto& r=snapshot.evidence->rasters[a.evidence];
            if(r.kind!=Raw::Bracketing::EvidenceKind::Image) {
                const auto right=pos(a.corners[2]);const float y=bottom.y+font*2.05f;
                const float width=std::min(right.x-bottom.x,font*5);
                draw->AddRectFilledMultiColor({bottom.x,y},{bottom.x+width,y+3*stroke},Ink(alpha,.1f),Ink(alpha,.85f),Ink(alpha,.85f),Ink(alpha,.1f));
                std::string range=Number(r.minimum)+"  -  "+Number(r.maximum)+(r.units.empty()?"":" "+r.units);
                draw->AddText({bottom.x,y+font*.5f},Ink(alpha*.45f),range.c_str());
            }
            if(main&&!a.main) {
                auto from=pos(Lerp(main->corners[1],main->corners[2],.5f));
                auto to=pos(Lerp(a.corners[0],a.corners[3],.5f));
                from.x+=font*.5f;to.x-=font*.6f;
                const auto color=Ink(alpha*.24f);
                draw->AddBezierCubic(from,{Lerp(from,to,.45f).x,from.y},{Lerp(from,to,.55f).x,to.y},to,color,stroke);
                draw->AddCircleFilled(to,2.2f*stroke,Ink(alpha*.65f),12);
            }
        }
    }
    const auto e=snapshot.evidence;if(!e)return;
    if(main&&!e->kernels.empty()) {
        const auto& q=main->corners;
        const auto map=[&](float x,float y){return pos(Lerp(Lerp(q[0],q[1],x),Lerp(q[3],q[2],x),y));};
        for(const auto& k:e->kernels) {
            const float a=std::sqrt(std::max(0.f,k.xx)),b=a>0?k.xy/a:0,c=std::sqrt(std::max(0.f,k.yy-b*b));
            for(unsigned i=0;i<=32;++i) {
                const float t=i*6.2831853f/32;
                draw->PathLineTo(map(k.x+12*a*std::cos(t),k.y+12*(b*std::cos(t)+c*std::sin(t))));
            }
            draw->PathStroke(ImGui::ColorConvertFloat4ToU32({.72f,.88f,.96f,alpha*.6f}),ImDrawFlags_Closed,stroke);
        }
        const auto top=pos(q[0]);draw->AddText({top.x,top.y-font*1.7f},Ink(alpha*.6f),"Kernel covariance   /   size x12");
    }
    if(main&&!e->vectors.empty()) {
        const auto& q=main->corners;
        const auto map=[&](float x,float y){return pos(Lerp(Lerp(q[0],q[1],x),Lerp(q[3],q[2],x),y));};
        // Explicitly identify displacement exaggeration. Anchor positions remain exact.
        constexpr float exaggeration=6;
        for(const auto& v:e->vectors) {
            if(v.x<0||v.y<0||v.x>1||v.y>1)continue;
            auto a=map(v.x,v.y),b=map(std::clamp(v.x+v.dx*exaggeration,0.f,1.f),std::clamp(v.y+v.dy*exaggeration,0.f,1.f));
            if(v.rejected) {
                const float r=2.3f*stroke;draw->AddLine({a.x-r,a.y-r},{a.x+r,a.y+r},Ink(alpha*.35f),stroke);
                draw->AddLine({a.x+r,a.y-r},{a.x-r,a.y+r},Ink(alpha*.35f),stroke);continue;
            }
            const auto ink=ImGui::ColorConvertFloat4ToU32({.70f,.88f,.96f,alpha*(e->provisional?.5f:.8f)});
            if(e->provisional||v.confidence<.5f)Dashed(draw,a,b,ink,stroke);else draw->AddLine(a,b,ink,stroke);
            draw->AddCircleFilled(b,1.8f*stroke,ink,10);
        }
        const auto top=pos(q[0]);draw->AddText({top.x,top.y-font*1.7f},Ink(alpha*.65f),e->provisional?"Correspondence search   /   displacement x6":"Correspondence   /   displacement x6");
    }
    if(!e->curve.empty()) {
        const bool pairedImage=!e->rasters.empty();
        const float x=origin.x+size.x*.735f,y=origin.y+size.y*(pairedImage?.63f:.30f),w=size.x*.205f,h=size.y*(pairedImage?.22f:.28f);
        float xmin=e->curve.front().x,xmax=xmin,ymin=0,ymax=0;
        for(const auto& p:e->curve){xmin=std::min(xmin,p.x);xmax=std::max(xmax,p.x);ymin=std::min(ymin,p.y);ymax=std::max(ymax,p.y);}
        for(const auto& p:e->observations)if(std::isfinite(p.x)&&std::isfinite(p.y)){xmin=std::min(xmin,p.x);xmax=std::max(xmax,p.x);ymax=std::max(ymax,p.y);}
        const auto point=[&](const auto& p){return ImVec2{x+w*(p.x-xmin)/std::max(1e-9f,xmax-xmin),y+h*(1-(p.y-ymin)/std::max(1e-9f,ymax-ymin))};};
        draw->AddText({x,y-font*2.2f},Ink(alpha*.75f),e->curveLabel.c_str());
        for(int i=0;i<3;++i)draw->AddLine({x,y+h*i/2},{x+w,y+h*i/2},Ink(alpha*.12f),stroke);
        for(const auto& p:e->observations)if(std::isfinite(p.x)&&std::isfinite(p.y))draw->AddCircleFilled(point(p),2*stroke,Ink(alpha*.35f),10);
        for(std::size_t i=1;i<e->curve.size();++i)draw->AddLine(point(e->curve[i-1]),point(e->curve[i]),Ink(alpha*.85f),1.5f*stroke);
        draw->AddText({x,y+h+font*.65f},Ink(alpha*.5f),(Number(xmin)+"    "+e->xUnits+"    "+Number(xmax)).c_str());
        draw->AddText({x,y-font*1.05f},Ink(alpha*.4f),(Number(ymax)+" "+e->yUnits).c_str());
    }
    if(!e->equation.empty()) {
        const auto extent=ImGui::CalcTextSize(e->equation.c_str());
        draw->AddText({origin.x+size.x*.43f-extent.x*.5f,origin.y+size.y*.94f},Ink(alpha*.8f),e->equation.c_str());
    }
    float x=origin.x+size.x*.22f;
    for(std::size_t i=0;i<std::min<std::size_t>(4,e->metrics.size());++i) {
        const auto& metric=e->metrics[i];const float y=origin.y+size.y*.84f;
        const auto text=Number(metric.value)+(metric.units.empty()?"":" "+metric.units);
        draw->AddText({x,y},Ink(alpha*.9f),text.c_str());
        draw->AddText({x,y+font*1.4f},Ink(alpha*.42f),metric.label.c_str());x+=size.x*.125f;
    }
}
}
