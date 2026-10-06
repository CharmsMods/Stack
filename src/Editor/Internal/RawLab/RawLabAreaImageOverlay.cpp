#include "Editor/Internal/RawLab/RawLabAreaImage.h"
#include "Editor/Internal/RawLab/RawLabAreaMask.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Stack::Editor::RawLabInternal {
namespace {
using namespace RawRecipe;
void Mix(std::size_t& key,std::size_t value) {key^=value+0x9e3779b9u+(key<<6)+(key>>2);}
void DrawCoverage(EditorModuleTypes::RawZoneAreaUiState& ui,const RawZoneArea& area,
    const RawDevelopmentRecipe& recipe,const ImRect& image,
    const std::shared_ptr<const RawZoneAreaMaskPreview>& coverage,const RawLabAreaImageMappings* mappings) {
    if (!coverage) return;
    // Coverage remains visible while painting, including a zero-gain area.
    const bool effect=ui.effectPreview && !(ui.active && ui.gestureMode!=0);
    const auto guide=ui.neutralGuide;
    std::size_t key=reinterpret_cast<std::size_t>(coverage.get());
    Mix(key,coverage->maskFingerprint);Mix(key,effect);
    if (effect) {
        Mix(key,ZoneAreaGainFingerprint(area));Mix(key,recipe.localRange.enabled);
        Mix(key,std::hash<float>{}(recipe.localRange.strength));
        Mix(key,reinterpret_cast<std::size_t>(guide.get()));
    }
    if (key!=ui.overlayKey || !ui.overlayTexture) {
        const int width=coverage->width,height=coverage->height;
        std::array<float,513> gain{};
        if (effect) for (std::size_t i=0;i<gain.size();++i)
            gain[i]=area.offsetEv+ZoneAreaCurveGain(area,-32.0f+float(i)/8.0f);
        std::vector<unsigned char> pixels(coverage->coverage.size()*4);
        for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
            const auto index=std::size_t(y)*width+x;
            float alpha=coverage->coverage[index],delta=area.offsetEv;
            if (effect) {
                if (guide && guide->Valid()) {
                    const int xx=std::min(guide->width-1,int((x+.5f)*guide->width/width));
                    const int yy=std::min(guide->height-1,int((height-y-.5f)*guide->height/height));
                    const float ev=guide->pixels[std::size_t(yy)*guide->width+xx].sceneEv;
                    const float position=std::isfinite(ev) ? std::clamp((ev+32)*8,0.0f,512.0f) : 0;
                    const int lo=int(position),hi=std::min(512,lo+1);
                    delta=gain[lo]+(gain[hi]-gain[lo])*(position-lo);
                }
                alpha*=std::min(1.0f,std::abs(delta)*recipe.localRange.strength);
                if (!area.enabled || !recipe.localRange.enabled) alpha=0;
            }
            pixels[index*4]=effect && delta<0 ? 95 : 248;
            pixels[index*4+1]=effect && delta<0 ? 169 : 220;
            pixels[index*4+2]=effect && delta<0 ? 245 : 64;
            pixels[index*4+3]=static_cast<unsigned char>(std::lround(std::clamp(alpha,0.0f,1.0f)*130));
        }
        const Renderer::GLState::PixelUnpackState unpack;
        unpack.ConfigureTightCpuUpload();
        if (ui.overlayTexture && ui.overlayWidth==width && ui.overlayHeight==height) {
            glBindTexture(GL_TEXTURE_2D,ui.overlayTexture);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        } else {
            if (ui.overlayTexture) glDeleteTextures(1,&ui.overlayTexture);
            ui.overlayTexture=GLHelpers::CreateTextureFromPixels(pixels.data(),width,height,4);
            ui.overlayWidth=width;ui.overlayHeight=height;
        }
        unpack.Restore();ui.overlayKey=key;
    }
    if (mappings) {
        const auto position = [&](float u,float v) {
            const auto p = mappings->measurement.Canvas({u,v});
            return ImVec2(image.Min.x+p.x*image.GetWidth(),image.Min.y+p.y*image.GetHeight());
        };
        ImGui::GetWindowDrawList()->AddImageQuad((ImTextureID)(intptr_t)ui.overlayTexture,
            position(0,0),position(1,0),position(1,1),position(0,1),{0,1},{1,1},{1,0},{0,0});
        return;
    }
    const auto& crop=recipe.cropRotation;
    const ImVec2 uv0(crop.cropEnabled ? crop.cropX : 0,crop.cropEnabled ? 1-crop.cropY : 1);
    const ImVec2 uv1(crop.cropEnabled ? crop.cropX+crop.cropWidth : 1,crop.cropEnabled ? 1-crop.cropY-crop.cropHeight : 0);
    ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)ui.overlayTexture,image.Min,image.Max,uv0,uv1);
}
void DrawPendingStroke(const RawZoneArea& area,const RawCropRotationRecipe& transform,
    const RawZoneAreaMaskPreview* mask,const ImRect& image,const RawLabImageMapping* mapping) {
    if (mask && mask->maskFingerprint==ZoneAreaMaskFingerprint(area)) return;
    auto* draw=ImGui::GetWindowDrawList();
    auto position=[&](const RawZoneBrushPoint& p) {
        if (mapping) {
            const auto d=mapping->Canvas({p.u,p.v});
            return ImVec2(image.Min.x+d.x*image.GetWidth(),image.Min.y+d.y*image.GetHeight());
        }
        const auto d=ZoneAreaDisplayPoint(p.u,p.v,transform,true);
        return ImVec2(image.Min.x+d.u*image.GetWidth(),image.Min.y+d.v*image.GetHeight());
    };
    const std::size_t first=std::max(mask && mask->strokeCount ? mask->strokeCount-1 : 0,
        area.strokes.size()>8 ? area.strokes.size()-8 : 0);
    for (std::size_t i=first;i<area.strokes.size();++i) {
        const auto& stroke=area.strokes[i];
        if (stroke.path.empty()) continue;
        std::size_t start=(mask && i+1==mask->strokeCount && mask->lastStrokePointCount) ? mask->lastStrokePointCount-1 : 0;
        if (start>=stroke.path.size()) continue;
        // This thin trace acknowledges input while coverage is being evaluated.
        // It does not pretend that a guided selection has already completed.
        const auto color=stroke.erase ? IM_COL32(255,245,220,165) : IM_COL32(255,225,75,165);
        if (start+1==stroke.path.size() && (!mask || i>=mask->strokeCount))
            draw->AddCircleFilled(position(stroke.path[start]),3,color);
        const auto step=std::max<std::size_t>(1,(stroke.path.size()-start+255)/256);
        for (std::size_t p=start;p+1<stroke.path.size();p+=step)
            draw->AddLine(position(stroke.path[p]),position(stroke.path[std::min(p+step,stroke.path.size()-1)]),color,2);
    }
}
}
void DrawRawLabAreaImage(EditorModuleTypes::RawZoneAreaUiState& ui,
    const RawRecipe::RawDevelopmentRecipe& recipe,const RawDevelopmentGraphScopeReadback& scope,
    const ImVec2& minimum,const ImVec2& maximum,const Async::ActivityMetadata& activity,
    const RawLabAreaImageMappings* mappings) {
    const auto area=std::find_if(recipe.localRange.areas.begin(),recipe.localRange.areas.end(),
        [&](const auto& a){return a.id==ui.selectedId;});
    if (area==recipe.localRange.areas.end()) return;
    const auto mapping = mappings ? mappings->area(area->id) : std::nullopt;
    if (mappings && !mapping) return;
    const ImRect image(minimum,maximum);
    ImRect hit=image;hit.ClipWith(ImGui::GetCurrentWindow()->ClipRect);
    const auto coverage=ResolveRawLabAreaMask(ui,*area,recipe,scope,activity);
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(hit.Min,hit.Max,true);
    if (ui.showMask) {
        const bool current=!mappings || (scope.valid && std::any_of(scope.zoneAreas.begin(),scope.zoneAreas.end(),[&](const auto& stats) {
            return stats.areaId==area->id && stats.maskFingerprint==ZoneAreaMaskFingerprint(*area) && stats.maskPreview;
        }));
        if(current) DrawCoverage(ui,*area,recipe,image,coverage,mappings);
        if(ui.maskPending || !current) DrawPendingStroke(*area,recipe.cropRotation,current ? coverage.get() : nullptr,
            image,mapping ? &*mapping : nullptr);
    }
    const auto& io=ImGui::GetIO();
    if (ui.active || ImGui::IsMouseHoveringRect(hit.Min,hit.Max)) {
        const int mode=ui.active ? ui.gestureMode : ui.mode;
        if (mode!=0) {
            if (ui.showCursor) {
                if (mapping) {
                    const auto center=mapping->Local({(io.MousePos.x-image.Min.x)/image.GetWidth(),(io.MousePos.y-image.Min.y)/image.GetHeight()});
                    ImVec2 previous;
                    for (int i=0;i<=64;++i) {
                        const float angle=6.28318530718f*i/64;
                        const auto p=mapping->Canvas({center.x+std::cos(angle)*ui.radius/std::max(1.f,area->sourceAspect),
                            center.y+std::sin(angle)*ui.radius/std::max(1.f,1.f/area->sourceAspect)});
                        const ImVec2 current{image.Min.x+p.x*image.GetWidth(),image.Min.y+p.y*image.GetHeight()};
                        if (i) draw->AddLine(previous,current,IM_COL32(248,233,164,180),1);
                        previous=current;
                    }
                } else {
                    const auto& crop=recipe.cropRotation;
                    const float w=image.GetWidth()/(crop.cropEnabled ? crop.cropWidth : 1);
                    const float h=image.GetHeight()/(crop.cropEnabled ? crop.cropHeight : 1);
                    draw->AddCircle(io.MousePos,ui.radius*std::min(w,h),IM_COL32(248,233,164,180),64,1);
                }
            }
            ImGui::SetMouseCursor(ui.showCursor ? ImGuiMouseCursor_None : ImGuiMouseCursor_Arrow);
        } else {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            char text[64];std::snprintf(text,sizeof(text),"%+.2f EV",area->offsetEv);
            const ImVec2 p(io.MousePos.x+16,io.MousePos.y-22);
            draw->AddText(ImVec2(p.x+1,p.y+1),IM_COL32(0,0,0,200),text);
            draw->AddText(p,IM_COL32(255,255,255,255),text);
        }
    }
    draw->PopClipRect();
}
}
