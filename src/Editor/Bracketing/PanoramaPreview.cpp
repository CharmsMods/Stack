#include "BracketingSession.h"
#include "Raw/Bracketing/Panorama/Panorama.h"
#include "Renderer/GLHelpers.h"
#include <cmath>
#include <algorithm>

namespace Stack::Editor {
void DrawPanoramaPreview(BracketingSession& ui,const ImVec2& available) {
    auto& p=ui.detailMode?ui.detail:ui.preview;if(!p.width||!p.height)return;
    if(ui.view!=0&&ui.view!=1&&ui.view!=2&&ui.view!=6)ui.view=0;
    const float top=ImGui::GetCursorScreenPos().y;
    if(ImGui::RadioButton("Result",ui.view==0))ui.view=0;ImGui::SameLine();
    if(ImGui::RadioButton("Original",ui.view==1)){ui.view=1;ui.detailMode=false;return;}ImGui::SameLine();
    if(ImGui::RadioButton("Coverage",ui.view==2))ui.view=2;ImGui::SameLine();
    if(ImGui::RadioButton("Source regions",ui.view==6))ui.view=6;
    ImGui::SetNextItemWidth(140);ImGui::SliderFloat("Viewing EV",&ui.inspectionEv,-10,10,"%+.1f");
    if(ImGui::Button("Fit")){ui.detailMode=false;ui.zoom=1;ui.panX=ui.panY=0;ui.textureIdentity.clear();return;}
    ImGui::SameLine();if(!ui.detailMode&&ui.view!=1&&ImGui::Button("Native detail at cursor"))ui.requestDetail=true;
    std::string frame=ui.hoverFrame.empty()?ui.selectedFrame:ui.hoverFrame;
    if(frame.empty()&&!ui.result->panorama->cameras.empty())frame=ui.result->panorama->cameras.front().frameId;
    if(ui.view==1&&!ui.result->analysis&&!ui.preparationFailed)ui.preparationRequested=true;
    auto native=UpdateBracketingInspection(ui,ui.view,frame);
    const Raw::Bracketing::CapturePreview* original=nullptr;
    if(ui.view==1&&ui.result->analysis){auto found=ui.result->analysis->originals.find(frame);if(found!=ui.result->analysis->originals.end())original=found->second.get();}
    if(ui.view==1&&!native&&!original){ImGui::TextWrapped(ui.preparationFailed?"Source inspection could not be prepared.":"Preparing the source for inspection...");return;}
    const unsigned width=native?native->width:original?original->width:p.width,height=native?native->height:original?original->height:p.height;
    ImGui::Text("%u x %u / %s",width,height,ui.detailMode?"native pixels":native?"full-quality inspection":"overview");
    const auto identity=ui.result->identity+"/pano/"+std::to_string(ui.view)+frame+std::to_string(ui.detailMode)+std::to_string(ui.inspectionEv)+
        std::to_string(width)+"x"+std::to_string(height)+std::to_string(p.sensorOriginX)+":"+std::to_string(p.sensorOriginY);
    if(!ui.texture||ui.textureIdentity!=identity) {
        std::vector<unsigned char> rgba(native?0:std::size_t(width)*height*4);
        for(unsigned y=0;!native&&y<height;++y)for(unsigned x=0;x<width;++x) {
            const auto q=std::size_t(y)*width+x;float color[3]{};const float alpha=original?1:p.coverage.empty()?1:p.coverage[q];
            if(ui.view==2){color[0]=color[1]=color[2]=alpha;}
            else if(ui.view==6) {
                const auto& layout=*ui.result->panorama;const auto px=std::min(layout.width-1,unsigned(p.sensorOriginX+x*p.sensorStepX)),py=std::min(layout.height-1,unsigned(p.sensorOriginY+y*p.sensorStepY));
                const auto owner=layout.ownership?(*layout.ownership)[std::size_t(py)*layout.width+px]:65535;
                if(owner<layout.cameras.size()){float h=std::fmod(owner*.618034f,1.f);ImGui::ColorConvertHSVtoRGB(h,.65f,.85f,color[0],color[1],color[2]);}
            }else {
                const auto& pixels=original?original->rgb:p.resultRgb;float camera[3]{};
                for(int c=0;c<3;++c)camera[c]=pixels[q*3+c]*std::exp2(ui.inspectionEv)*p.metadata.cameraWhiteBalance[c]/std::max(1e-6f,p.metadata.cameraWhiteBalance[1]);
                for(int c=0;c<3;++c){float v=0;for(int k=0;k<3;++k)v+=p.metadata.cameraToSrgb[c*3+k]*camera[k];v=std::max(0.f,v);v/=1+v;color[c]=v<=.0031308f?v*12.92f:1.055f*std::pow(v,1/2.4f)-.055f;}
            }
            for(int c=0;c<3;++c)rgba[q*4+c]=std::uint8_t(std::clamp(color[c],0.f,1.f)*255+.5f);rgba[q*4+3]=std::uint8_t((ui.view==2?1:alpha)*255+.5f);
        }
        if(!ui.texture)glGenTextures(1,&ui.texture);GLint previous=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&previous);
        glBindTexture(GL_TEXTURE_2D,ui.texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,ui.detailMode?GL_NEAREST:GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,native?ui.inspectionPixels.data():rgba.data());glBindTexture(GL_TEXTURE_2D,previous);ui.textureIdentity=identity;
    }
    const float used=ImGui::GetCursorScreenPos().y-top;const ImVec2 area(std::max(1.f,available.x),std::max(80.f,available.y-used-35));
    auto start=ImGui::GetCursorScreenPos();ImGui::InvisibleButton("##PanoramaImage",area,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle);
    const auto& io=ImGui::GetIO();if(ImGui::IsItemHovered()){if(io.MouseWheel)ui.zoom=std::clamp(ui.zoom*std::pow(1.15f,io.MouseWheel),1.f,32.f);if(ImGui::IsMouseDragging(2)){ui.panX+=io.MouseDelta.x;ui.panY+=io.MouseDelta.y;}}
    const float scale=(ui.detailMode?1.f:std::min(area.x/width,area.y/height))*ui.zoom;const ImVec2 size(width*scale,height*scale);
    const ImVec2 pos(start.x+(area.x-size.x)*.5f+ui.panX,start.y+(area.y-size.y)*.5f+ui.panY);
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(start,{start.x+area.x,start.y+area.y},true);
    for(float y=start.y;y<start.y+area.y;y+=16)for(float x=start.x;x<start.x+area.x;x+=16)draw->AddRectFilled({x,y},{std::min(x+16,start.x+area.x),std::min(y+16,start.y+area.y)},(int((x-start.x)/16)+int((y-start.y)/16))%2?IM_COL32(65,65,65,255):IM_COL32(85,85,85,255));
    draw->AddImage(static_cast<ImTextureID>(ui.texture),pos,{pos.x+size.x,pos.y+size.y});draw->PopClipRect();
    if(ImGui::IsItemHovered()&&!ui.detailMode&&ui.view!=1){ui.probeX=std::clamp((io.MousePos.x-pos.x)/size.x,0.f,.99999f);ui.probeY=std::clamp((io.MousePos.y-pos.y)/size.y,0.f,.99999f);}
    if(!ui.inspectionError.empty())ImGui::TextWrapped("%s",ui.inspectionError.c_str());
}
}
