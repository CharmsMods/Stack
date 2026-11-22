#include "Editor/NodeEditors/ColorGradeHover.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"

namespace Stack::Editor {
void ColorGradeHover::DrawImage(const RawDevelopmentGraphScopeReadback& scope,std::size_t identity,
    const ImRect& image,bool pointerAllowed) {
    photoFrame=-10;
    if(mapFrame<ImGui::GetFrameCount()-1 || !scope.valid || scope.stage!=RawDevelopmentGraphScopeStage::ColorWarpInput ||
       scope.width<=0 || scope.height<=0 || scope.pixels.size()/3<std::size_t(scope.width)*scope.height ||
       image.GetWidth()<=0 || image.GetHeight()<=0 || ImGui::GetIO().AppFocusLost || ImGui::IsAnyMouseDown())return;
    auto* draw=ImGui::GetWindowDrawList();
    const auto mouse=ImGui::GetIO().MousePos;
    const ImVec2 origin=crop.cropEnabled?ImVec2(crop.cropX,crop.cropY):ImVec2(0,0);
    const ImVec2 extent=crop.cropEnabled?ImVec2(crop.cropWidth,crop.cropHeight):ImVec2(1,1);
    const auto sample=[&](float u,float v) {
        const int x=std::clamp(int(u*scope.width),0,scope.width-1);
        const int y=std::clamp(int(v*scope.height),0,scope.height-1);
        const auto* rgb=&scope.pixels[(std::size_t(y)*scope.width+x)*3];
        const auto color=RawRecipe::WorkingRgbToColorWarpCoordinate({rgb[0],rgb[1],rgb[2]},space);
        return Point{color.sceneEv,RawRecipe::ColorWarpHue(color.a,color.b),RawRecipe::NeutralRangeWeight(color)>.85f};
    };
    if(overMap && mapRect.Contains(mouse)) {
        // Keep the display overlay bounded. Classify source colors only when the input changes.
        const float scale=std::min(1.f,256.f/std::max(scope.width,scope.height));
        const int w=std::max(1,int(std::round(scope.width*scale))),h=std::max(1,int(std::round(scope.height*scale)));
        const bool resized=w!=width || h!=height;
        const bool rebuild=resized || cachedInput!=identity || cachedSpace!=space;
        if(rebuild) {
            width=w;height=h;cachedInput=identity;cachedSpace=space;
            samples.resize(w*h);pixels.resize(w*h*4);
            for(int y=0;y<h;++y)for(int x=0;x<w;++x)
                samples[y*w+x]=sample((x+.5f)/w,(y+.5f)/h);
        }
        if(rebuild || !overlay || query.ev!=lastQuery.ev || query.hue!=lastQuery.hue || query.neutral!=lastQuery.neutral) {
            for(std::size_t i=0;i<samples.size();++i) {
                const auto& s=samples[i];
                auto* pixel=&pixels[4*i];
                const float brightness=std::max(0.f,1.f-std::abs(query.ev-s.ev)/1.f);
                float weight=query.neutral==s.neutral?brightness:0.f;
                if(weight>0 && !query.neutral)
                    weight*=std::max(0.f,1.f-std::abs(RawRecipe::ColorWarpHueDelta(query.hue-s.hue))/24.f);
                pixel[0]=pixel[1]=pixel[2]=s.ev>.5f?0:255;
                pixel[3]=static_cast<unsigned char>(64.f*weight*weight*(3.f-2.f*weight));
            }
            if(resized || !overlay)overlay.Reset(GLHelpers::CreateTextureFromPixels(pixels.data(),w,h,4));
            else {
                Renderer::GLState::TextureBinding binding(GL_TEXTURE_2D,GL_TEXTURE_BINDING_2D);
                Renderer::GLState::PixelUnpackState unpack;
                unpack.ConfigureTightCpuUpload();
                glBindTexture(GL_TEXTURE_2D,overlay.Get());
                glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
                unpack.Restore();binding.Restore();
            }
            lastQuery=query;
        }
        if(overlay)draw->AddImage((ImTextureID)(intptr_t)overlay.Get(),image.Min,image.Max,origin,
            {origin.x+extent.x,origin.y+extent.y});
    }else if(pointerAllowed && image.Contains(mouse) && ImRect(draw->GetClipRectMin(),draw->GetClipRectMax()).Contains(mouse)) {
        photo=sample(origin.x+extent.x*(mouse.x-image.Min.x)/image.GetWidth(),
                     origin.y+extent.y*(mouse.y-image.Min.y)/image.GetHeight());
        photoFrame=ImGui::GetFrameCount();
    }
}
}
