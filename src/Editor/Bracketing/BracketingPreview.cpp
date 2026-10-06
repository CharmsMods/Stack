#include "BracketingSession.h"
#include "Raw/MultiFrameDenoise/Reliability.h"
#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <cmath>

namespace Stack::Editor {
void DrawPanoramaPreview(BracketingSession&,const ImVec2&);
namespace {
bool HardAlignmentRejection(std::uint16_t bits) {
    constexpr auto mask=
        Raw::Mfd::ReliabilityRejectMask(Raw::Mfd::ReliabilityRejectBit::AlignmentInvalid)|
        Raw::Mfd::ReliabilityRejectMask(Raw::Mfd::ReliabilityRejectBit::SampleInvalid)|
        Raw::Mfd::ReliabilityRejectMask(Raw::Mfd::ReliabilityRejectBit::PatchRejected)|
        Raw::Mfd::ReliabilityRejectMask(Raw::Mfd::ReliabilityRejectBit::NonFiniteEvidence);
    return (bits&mask)!=0;
}
std::pair<unsigned,unsigned> AlignmentCell(
    const Raw::Bracketing::AlignmentInspection& inspection,double rawX,double rawY) {
    const double rawWidth=std::max(1u,inspection.rawWidth);
    const double rawHeight=std::max(1u,inspection.rawHeight);
    return {
        std::min(inspection.width-1,static_cast<unsigned>(std::max(0.0,rawX)*inspection.width/rawWidth)),
        std::min(inspection.height-1,static_cast<unsigned>(std::max(0.0,rawY)*inspection.height/rawHeight))};
}
}

BracketingSession::~BracketingSession() {
    activityNotifier.CancelActivity(activity, "The bracket session closed before result delivery.");
    if(texture)glDeleteTextures(1,&texture);
}
void DrawBracketingPreview(BracketingSession& ui,const ImVec2& available) {
    if(ui.result&&ui.result->panorama){DrawPanoramaPreview(ui,available);return;}
    auto& p=ui.detailMode?ui.detail:ui.preview;const auto groups=ui.recipe.groups.size();
    if(!groups) return;
    ui.selectedGroup=std::clamp(ui.selectedGroup,0,static_cast<int>(groups)-1);
    if(!p.width||!p.height||p.resultRgb.empty()||p.sourceRgb.size()!=static_cast<std::size_t>(p.width)*p.height*groups*3) {
        ImGui::TextWrapped("The bracket preview appears after the captures have been prepared.");return;
    }
    const float controlsTop=ImGui::GetCursorScreenPos().y;
    const char* views[]={"Result","Original","Mask","Matched","Group","Compare","Alignment"};
    for(int v=0;v<7;++v) {
        if(v&&ImGui::GetItemRectMax().x+ImGui::CalcTextSize(views[v]).x+ImGui::GetFrameHeight()*2<ImGui::GetWindowPos().x+ImGui::GetWindowContentRegionMax().x)ImGui::SameLine();
        if(ImGui::RadioButton(views[v],ui.view==v))ui.view=v;
    }
    if(ImGui::Button(ui.detailMode?"Back to overview":"Fit")) {ui.zoom=1;ui.panX=ui.panY=0;ui.detailMode=false;ui.textureIdentity.clear();return;}
    ImGui::SameLine();if(!ui.detailMode&&ImGui::Button("Native detail at cursor")) ui.requestDetail=true;
    if(ui.detailJob) {ImGui::SameLine();ImGui::TextDisabled("Loading native pixels...");}
    ImGui::SameLine();ImGui::Checkbox("Clipping",&ui.showClipping);
    ImGui::SetNextItemWidth(160);ImGui::SliderFloat("Viewing EV",&ui.inspectionEv,-10,10,"%+.1f");
    ImGui::SameLine();if(ImGui::SmallButton("Reset view EV"))ui.inspectionEv=0;
    if(ui.view==2) {ImGui::SameLine();ImGui::Checkbox("Selected group only",&ui.selectedMask);}
    if(ui.view==5) {ImGui::SetNextItemWidth(160);ImGui::SliderFloat("Split: result | matched original",&ui.compareSplit,0,1,"%.2f");}
    std::string frame=ui.hoverFrame.empty()?ui.selectedFrame:ui.hoverFrame;
    if(frame.empty()&&!ui.recipe.groups[ui.selectedGroup].frames.empty())frame=ui.recipe.groups[ui.selectedGroup].frames.front().id;
    const int view=ui.hoverFrame.empty()?ui.view:(ui.view==3||ui.view==5?ui.view:1);
    const auto* originals=&p.originals;
    if(!ui.detailMode&&ui.result&&ui.result->analysis) originals=&ui.result->analysis->originals;
    const auto found=originals->find(frame);
    const auto* original=found==originals->end()?nullptr:found->second.get();
    const bool needsOriginal=view==1||view==3||view==5;
    const auto native=UpdateBracketingInspection(ui,view,frame);
    if(native&&needsOriginal)original=native.get();
    const unsigned imageWidth=native?native->width:p.width,imageHeight=native?native->height:p.height;
    const Raw::Bracketing::AlignmentInspection* alignmentInspection=nullptr;
    if(view==6&&ui.result&&ui.result->analysis) {
        const auto inspected=ui.result->analysis->alignmentInspection.find(frame);
        if(inspected!=ui.result->analysis->alignmentInspection.end()) alignmentInspection=inspected->second.get();
    }
    if(needsOriginal) {
        const auto label=ui.frameLabels.find(frame);
        ImGui::TextWrapped("%s / %s",view==1?"Captured brightness":"Exposure matched",label==ui.frameLabels.end()?"Select a capture":label->second.c_str());
        if(!original) {ImGui::TextWrapped("This capture is not prepared. Enable it to inspect its original.");return;}
    } else if(view==6) ImGui::TextWrapped("Alignment confidence / %s",ui.frameLabels.count(frame)?ui.frameLabels.at(frame).c_str():"select a capture");
    else ImGui::TextDisabled(view==2?"Actual contributions after clipping and fallback":"Merged result or denoised group / common exposure");
    ImGui::TextDisabled(ui.detailMode?(ui.result&&ui.result->reconstructionInspection?
        "Native reconstruction pixels / original enlarged to the same scene area":"Native sensor pixels / identical demosaic / no resizing at 1x"):
        native?"Full-quality inspection / zoom to examine detail":ui.inspectionJob?"Loading full-quality pixels...":
        view==2||view==6?"Mask overview / native detail available at cursor":"Preview / full-quality pixels load automatically");
    if(native) {ImGui::SameLine();ImGui::TextDisabled("%u x %u",imageWidth,imageHeight);}
    if(!ui.inspectionError.empty()&&!ui.detailMode)ImGui::TextDisabled("%s",ui.inspectionError.c_str());
    const auto identity=ui.storedRecipe+std::to_string(view)+std::to_string(ui.selectedGroup)+std::to_string(ui.detailMode)+frame+
        std::to_string(ui.inspectionEv)+std::to_string(ui.showClipping)+std::to_string(ui.selectedMask)+std::to_string(ui.compareSplit)+
        "/"+std::to_string(imageWidth)+"x"+std::to_string(imageHeight);
    if(!ui.texture||ui.textureIdentity!=identity) {
        std::vector<unsigned char> rgba(native?0:static_cast<std::size_t>(imageWidth)*imageHeight*4);
        for(std::size_t output=0;!native&&output<static_cast<std::size_t>(imageWidth)*imageHeight;++output) {
            const auto x=output%imageWidth,y=output/imageWidth;
            const auto i=(y*p.height/imageHeight)*p.width+x*p.width/imageWidth;
            float color[3]{};
            const bool originalPixel=needsOriginal&&(view!=5||static_cast<float>(x)/imageWidth>=ui.compareSplit);
            std::size_t originalIndex=0;
            if(originalPixel) originalIndex=(y*original->height/imageHeight)*original->width+x*original->width/imageWidth;
            if(view==6) {
                float confidence=0;std::uint16_t rejection=1;
                if(alignmentInspection&&alignmentInspection->width&&alignmentInspection->height) {
                    const double rawX=p.sensorOriginX+(i%p.width)*p.sensorStepX;
                    const double rawY=p.sensorOriginY+(i/p.width)*p.sensorStepY;
                    const auto [ax,ay]=AlignmentCell(*alignmentInspection,rawX,rawY);
                    const auto ai=static_cast<std::size_t>(ay)*alignmentInspection->width+ax;
                    confidence=alignmentInspection->confidence[ai];rejection=alignmentInspection->rejectionBits[ai];
                }
                if(HardAlignmentRejection(rejection)) {
                    color[0]=.95f;color[1]=.18f;color[2]=.08f;
                } else if(rejection||confidence<.2f) {
                    color[0]=.95f;color[1]=.68f;color[2]=.08f;
                } else {
                    color[0]=.05f;color[1]=.2f+.8f*confidence;color[2]=.35f+.55f*confidence;
                }
            } else if(view==2) {
                for(std::size_t g=0;g<groups;++g) {
                    if(ui.selectedMask&&g!=ui.selectedGroup)continue;
                    const auto c=BracketColor(ui.recipe,g);const auto w=p.contributions[i*groups+g];color[0]+=c.x*w;color[1]+=c.y*w;color[2]+=c.z*w;
                }
            } else {
                float camera[3]{};
                const float gain=std::exp2(ui.inspectionEv)*(originalPixel&&view!=1?static_cast<float>(original->exposureScale):1.f);
                for(unsigned c=0;c<3;++c) camera[c]=(originalPixel?original->rgb[originalIndex*3+c]:native&&view!=5?native->rgb[output*3+c]:view==4?p.sourceRgb[(i*groups+ui.selectedGroup)*3+c]:p.resultRgb[i*3+c])*gain*
                    p.metadata.cameraWhiteBalance[c]/std::max(1e-6f,p.metadata.cameraWhiteBalance[1]);
                for(unsigned c=0;c<3;++c) {
                    float v=0;for(unsigned channel=0;channel<3;++channel)v+=p.metadata.cameraToSrgb[c*3+channel]*camera[channel];
                    v=std::max(0.f,v);v/=1+v;color[c]=v<=.0031308f?v*12.92f:1.055f*std::pow(v,1/2.4f)-.055f;
                }
                const bool clipped=originalPixel?original->clipped[originalIndex]!=0:native&&view!=5?native->clipped[output]!=0:i<p.diagnostics.size()&&(p.diagnostics[i]&Raw::Bracketing::Preview::Unrecoverable);
                if(ui.showClipping&&clipped) {color[0]=1;color[1]=0;color[2]=1;}
            }
            for(unsigned c=0;c<3;++c)rgba[output*4+c]=static_cast<unsigned char>(std::clamp(color[c],0.f,1.f)*255+.5f);
            rgba[output*4+3]=255;
        }
        if(!ui.texture)glGenTextures(1,&ui.texture);
        GLint previous=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&previous);
        glBindTexture(GL_TEXTURE_2D,ui.texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,ui.detailMode?GL_NEAREST:native?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,ui.detailMode?GL_NEAREST:GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,imageWidth,imageHeight,0,GL_RGBA,GL_UNSIGNED_BYTE,native?ui.inspectionPixels.data():rgba.data());
        if(native&&!ui.detailMode)glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D,previous);ui.textureIdentity=identity;
    }
    const float controlsHeight=ImGui::GetCursorScreenPos().y-controlsTop+(5+(view==2?groups:0)+(ui.showClipping?1:0))*ImGui::GetTextLineHeightWithSpacing();
    const ImVec2 area(std::max(1.f,available.x),std::max(80.f,available.y-controlsHeight));
    const auto start=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##BracketImage",area,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle);
    const auto& io=ImGui::GetIO();const bool hovered=ImGui::IsItemHovered();
    if(hovered&&io.MouseWheel) ui.zoom=std::clamp(ui.zoom*std::pow(1.15f,io.MouseWheel),1.f,32.f);
    if(hovered&&ImGui::IsMouseDragging(2)) {ui.panX+=io.MouseDelta.x;ui.panY+=io.MouseDelta.y;}
    const float scale=(ui.detailMode?1.f:std::min(area.x/p.width,area.y/p.height))*ui.zoom;
    const ImVec2 size(p.width*scale,p.height*scale);
    const ImVec2 pos(start.x+(area.x-size.x)*.5f+ui.panX,start.y+(area.y-size.y)*.5f+ui.panY);
    auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(start,{start.x+area.x,start.y+area.y},true);
    draw->AddImage(static_cast<ImTextureID>(ui.texture),pos,{pos.x+size.x,pos.y+size.y});draw->PopClipRect();
    if(hovered) {ui.probeX=std::clamp((io.MousePos.x-pos.x)/size.x,0.f,.99999f);ui.probeY=std::clamp((io.MousePos.y-pos.y)/size.y,0.f,.99999f);}
    const auto pixel=static_cast<std::size_t>(ui.probeY*p.height)*p.width+static_cast<unsigned>(ui.probeX*p.width);
    ImGui::Text("Scene %.2f EV",p.guideEv[pixel]);
    const auto g=static_cast<std::size_t>(ui.selectedGroup);
    ImGui::Text(ui.recipe.automatic ? "%s: curve average %.1f%%, actual %.1f%%" : "%s: requested %.1f%%, actual %.1f%%",ui.recipe.groups[g].name.c_str(),p.requested[pixel*groups+g]*100,p.contributions[pixel*groups+g]*100);
    if((!ui.result||!ui.result->reconstructionInspection||ui.interactiveRaw)&&
        p.samples.size()==static_cast<std::size_t>(p.width)*p.height*4*groups) {
        double support=0;
        for(unsigned c=0;c<4;++c) support+=p.samples[(pixel*4+c)*groups+g].support*.25;
        ImGui::Text("Effective frame support %.1f",support);
    }
    if(view==6&&alignmentInspection&&alignmentInspection->width&&alignmentInspection->height) {
        const double rawX=p.sensorOriginX+(pixel%p.width)*p.sensorStepX;
        const double rawY=p.sensorOriginY+(pixel/p.width)*p.sensorStepY;
        const auto [ax,ay]=AlignmentCell(*alignmentInspection,rawX,rawY);
        const auto ai=static_cast<std::size_t>(ay)*alignmentInspection->width+ax;
        const auto confidence=alignmentInspection->confidence[ai];
        const auto rejection=alignmentInspection->rejectionBits[ai];
        if(HardAlignmentRejection(rejection))
            ImGui::Text("Rejected / alignment confidence %.1f%%",confidence*100);
        else if(rejection||confidence<.2f)
            ImGui::Text("Uncertain, reduced weight / alignment confidence %.1f%%",confidence*100);
        else ImGui::Text("Accepted / alignment confidence %.1f%%",confidence*100);
        ImGui::TextDisabled("Cyan = accepted, amber = uncertain, red = rejected.");
    }
    const auto diagnostic=pixel<p.diagnostics.size()?p.diagnostics[pixel]:0;
    if(diagnostic&Raw::Bracketing::Preview::Invalid)
        ImGui::TextUnformatted("No valid measurement for at least one sample here.");
    else if(diagnostic&Raw::Bracketing::Preview::Unrecoverable) {
        if(diagnostic&Raw::Bracketing::Preview::ShortestExposure)
            ImGui::TextUnformatted("Unrecoverable clipping here. The shortest effective exposure supplies the marked finite sample.");
        else ImGui::TextUnformatted("Unrecoverable clipping here. The mask includes a retained clipped sample.");
    }
    else if(diagnostic&Raw::Bracketing::Preview::FixedReference)
        ImGui::TextUnformatted("Local alignment rejected an alternate measurement; the fixed reference supplies this result.");
    else if(diagnostic&Raw::Bracketing::Preview::Fallback)
        ImGui::TextUnformatted("Fallback here: requested sources unusable; another enabled source supplies the sample.");
    else if(diagnostic&Raw::Bracketing::Preview::AlignmentRejected)
        ImGui::TextUnformatted("An uncertain alternate was rejected here; remaining reliable measurements were renormalized.");
    else ImGui::TextDisabled("Actual contributions exclude unusable samples. RAW display mapping does not affect this mask.");
    if(view==2) for(std::size_t group=0;group<groups;++group) {
        double average=0;for(std::size_t sample=group;sample<p.contributions.size();sample+=groups)average+=p.contributions[sample];
        average/=static_cast<double>(p.width)*p.height;
        ImGui::TextColored(BracketColor(ui.recipe,group),ui.recipe.automatic ? "%s: curve average %.1f%% / cursor %.1f%% actual; view average %.1f%%" : "%s: cursor %.1f%% requested / %.1f%% actual; view average %.1f%%",ui.recipe.groups[group].name.c_str(),p.requested[pixel*groups+group]*100,p.contributions[pixel*groups+group]*100,average*100);
    }
    if(ui.showClipping)ImGui::TextDisabled("Magenta = sensor clipping, not display white. Result marks unrecoverable clipping.");
    ImGui::TextDisabled("Scroll to zoom. Middle-drag to pan. Viewing EV does not change the merge.");
}
} // namespace Stack::Editor
