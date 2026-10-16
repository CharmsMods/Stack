#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabImageMapping.h"
#include "Project/GraphImageContracts.h"
#include "App/WorkspacePresentation.h"
#include "Notifications/NotificationPresenter.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

using namespace Stack;
using namespace Stack::Project;

void EditorModule::RenderRawAssetCaptureOverlay(const ImVec2& minimum,const ImVec2& maximum,
    const RawRecipe::RawCropRotationRecipe& crop) {
    auto& capture=m_RawAssetCapture;
    if(capture.pending && capture.application) {
        const auto clip=ImGui::GetCurrentWindow()->ClipRect;
        ImGui::GetWindowDrawList()->AddText({std::max(minimum.x,clip.Min.x)+12,std::max(minimum.y,clip.Min.y)+12},
            IM_COL32_WHITE,"Preparing mask...");
        if(ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if(capture.cancellation)capture.cancellation->store(true);
            capture.pending=false;capture.application.reset();++capture.serial;capture.status="Capture canceled.";
        }
        return;
    }
    if (!capture.armed || !capture.target) return;
    const auto cancel=[&](const char* reason) {
        if (capture.cancellation) capture.cancellation->store(true);
        capture.armed=capture.drawing=capture.feedbackNeeded=capture.actionMenu=capture.openMenu=false;
        capture.feedback.reset();capture.feedbackOpacity=0;capture.status=reason;
    };
    const auto available=QueryRawOperationMaskAvailability();
    if (!available.target || !(*available.target==*capture.target) || Workspace::IsPreview() || !RawAssetCaptureInputMatches()) {
        cancel("Capture canceled because its image or operation changed.");return;
    }
    if (ImGui::GetIO().AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape)) {cancel("Capture canceled.");return;}
    const auto* layer=FindRawAdjustmentLayer(m_Project->rawLayers.State(),capture.target->layerId);
    const auto* operation=SelectedEditingNode();
    const auto* input=layer && operation?layer->graph.FindInputLink(operation->id,"imageIn"):nullptr;
    if (!input) {cancel("Capture canceled because its image input is missing.");return;}
    if (m_GraphOutputDescriptionRenderRevision!=m_RenderRevision) BuildGraphSnapshot();
    const auto found=m_GraphOutputDescriptions.find("raw/"+layer->id+"/"+
        EditorNodeGraph::GraphOutputIdentity(input->fromNodeId,input->fromSocketId));
    using namespace NodeMath;
    const ValueDescriptor* descriptor=found==m_GraphOutputDescriptions.end()?nullptr:&found->second.descriptor;
    using Mapping=Editor::RawLabInternal::RawLabImageMapping;
    const auto mapping=descriptor && descriptor->spatial.state==KnowledgeState::Known &&
        m_LastGraphOutputSemanticDescriptor.spatial.state==KnowledgeState::Known?
        Mapping::FromSpatial(descriptor->spatial.value,m_LastGraphOutputSemanticDescriptor.spatial.value,crop):std::nullopt;
    const auto workingSpace=descriptor?RawOperationInputWorkingSpace(*descriptor):std::nullopt;
    const bool scene=workingSpace &&
        descriptor->transfer.state==KnowledgeState::Known && descriptor->transfer.value.kind==TransferKind::Linear &&
        descriptor->reference.state==KnowledgeState::Known && descriptor->reference.value==ReferenceState::Scene;
    // Capture reads unassociated RGB and excludes transparent pixels. RAW
    // preparation declares straight alpha even when every pixel is opaque.
    const bool captureAlpha=descriptor && descriptor->alpha.state==KnowledgeState::Known &&
        (descriptor->alpha.value==AlphaMode::Opaque || descriptor->alpha.value==AlphaMode::Straight);
    auto* draw=ImGui::GetWindowDrawList();
    ImRect visible(minimum,maximum);visible.ClipWith(ImGui::GetCurrentWindow()->ClipRect);
    const ImVec2 label(visible.Min.x+12,visible.Min.y+12);
    if (!scene || !captureAlpha || !mapping) {
        const char* reason=!scene?"Capture needs a scene-linear sRGB or Rec.2020 input.":
            !captureAlpha?"Capture needs RGB with opaque or straight alpha.":
            "Capture needs known image coordinates.";
        draw->AddText(label,IM_COL32_WHITE,reason);return;
    }
    const auto& extent=descriptor->spatial.value.fullWindow;
    if (extent.width<=0 || extent.height<=0 || extent.width>std::numeric_limits<int>::max() || extent.height>std::numeric_limits<int>::max()) {
        draw->AddText(label,IM_COL32_WHITE,"Native capture dimensions are unavailable.");return;
    }
    capture.width=static_cast<int>(extent.width);capture.height=static_cast<int>(extent.height);
    capture.workingSpace=*workingSpace;
    const ImVec2 size(maximum.x-minimum.x,maximum.y-minimum.y);
    if (size.x<=1 || size.y<=1) return;
    const auto local=[&](ImVec2 screen) {return mapping->Local({(screen.x-minimum.x)/size.x,(screen.y-minimum.y)/size.y});};
    const auto screen=[&](const std::array<float,2>& uv) {const auto p=mapping->Canvas({uv[0],uv[1]});
        return ImVec2(minimum.x+p.x*size.x,minimum.y+p.y*size.y);};
    auto& io=ImGui::GetIO();const auto mouse=io.MousePos;
    const bool pointerValid=ImGui::IsMousePosValid(&mouse);
    const bool hovered=pointerValid && ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive();
    const auto uv=pointerValid?local(mouse):ImVec2(-1,-1);
    const bool inside=visible.Contains(mouse) && uv.x>=0 && uv.x<=1 && uv.y>=0 && uv.y<=1;
    const double now=ImGui::GetTime();
    if(capture.openMenu) {ImGui::OpenPopup("Stroke actions");capture.openMenu=false;}
    if(ImGui::BeginPopup("Stroke actions")) {
        const auto choose=[&](const char* name,RawAssetCapture::Output output,bool useCurve=false,int application=0) {
            if(!ImGui::MenuItem(name)) return;
            SubmitRawAssetCapture(output,application!=0,useCurve);
            ImGui::CloseCurrentPopup();
        };
        const auto* node=SelectedEditingNode();
        if(node && node->kind==EditorNodeGraph::NodeKind::RawOperation &&
           (node->rawOperation.kind==RawRecipe::GraphOperationKind::LocalEv ||
            (node->rawOperation.kind==RawRecipe::GraphOperationKind::ToneCurve &&
             RawRecipe::ReadSceneTone(node->rawOperation.parameters).direct)))
            choose("Select these tones on the curve",RawAssetCapture::Output::Samples,true);
        choose("Save sampled pixels",RawAssetCapture::Output::Samples);
        ImGui::Separator();
        choose("Save drawn-path mask",RawAssetCapture::Output::SpatialMask);
        choose("Save brightness-match mask",RawAssetCapture::Output::LumaMatch);
        choose("Save color-match mask",RawAssetCapture::Output::ColorMatch);
        choose("Save brightness-and-color mask",RawAssetCapture::Output::BothMatch);
        if(ImGui::BeginMenu("Create and apply mask")) {
            const bool background=capture.target->layerId==Stack::Project::kRawBackgroundId;
            ImGui::TextDisabled(background ? "New %s after this tool, starting at defaults" : "Apply to %s",
                capture.target->displayLabel.c_str());
            choose("Drawn path",RawAssetCapture::Output::SpatialMask,false,1);
            choose("Matching brightness",RawAssetCapture::Output::LumaMatch,false,1);
            choose("Matching color",RawAssetCapture::Output::ColorMatch,false,1);
            choose("Matching brightness and color",RawAssetCapture::Output::BothMatch,false,1);
            ImGui::EndMenu();
        }
        if(ImGui::MenuItem("Draw an exclusion stroke")) {capture.excludeStroke=true;capture.actionMenu=false;ImGui::CloseCurrentPopup();}
        if(ImGui::BeginMenu("Save texture")) {
            choose("Luminance along this path",RawAssetCapture::Output::LumaStrip);
            choose("Chroma along this path",RawAssetCapture::Output::ChromaStrip);
            choose("Color strip / palette",RawAssetCapture::Output::ColorStrip);
            choose("Full input luminance image",RawAssetCapture::Output::LumaImage);
            choose("Full input chroma image",RawAssetCapture::Output::ChromaImage);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        ImGui::SliderFloat("Match tolerance",&capture.selection.outerFeather,0.f,2.f,"%.2f");
        ImGui::Checkbox("Repeat this capture action",&capture.repeat);
        if(ImGui::MenuItem("Cancel")) {cancel("Capture canceled.");ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
    } else if(capture.actionMenu && !capture.openMenu && !ImGui::IsPopupOpen("Stroke actions")) {
        cancel("Capture canceled.");return;
    }
    if(capture.actionMenu || capture.pending) return;
    if (hovered && inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        capture.selection.start={uv.x,uv.y};capture.selection.end=capture.selection.start;
        capture.selection.radiusU=capture.selection.radiusV=0;
        if(capture.selection.shape==RawAssetCapture::Shape::Stroke) {
            auto& path=capture.excludeStroke?capture.selection.exclusion:capture.selection.path;
            path.clear();path.push_back({uv.x,uv.y});
        }
        capture.pointerExited=false;capture.drawing=true;capture.feedbackDirty=true;capture.lastActivity=now;
    }
    if (capture.drawing) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            cancel("Capture canceled because pointer input was interrupted.");return;
        }
        const bool moving=pointerValid && (std::abs(io.MouseDelta.x)>.05f || std::abs(io.MouseDelta.y)>.05f);
        if (pointerValid) {
            const std::array<float,2> end{std::clamp(uv.x,0.f,1.f),std::clamp(uv.y,0.f,1.f)};
            const float radius=std::min(std::hypot(mapping->sourceAspect,1.f),
                std::hypot((uv.x-capture.selection.start[0])*mapping->sourceAspect,uv.y-capture.selection.start[1]));
            if (capture.selection.end!=end || capture.selection.radiusV!=radius) capture.feedbackDirty=true;
            capture.selection.end=end;capture.selection.radiusU=radius/mapping->sourceAspect;capture.selection.radiusV=radius;
            if(!inside)capture.pointerExited=true;
            if(capture.selection.shape==RawAssetCapture::Shape::Stroke && inside && !capture.pointerExited) {
                auto& path=capture.excludeStroke?capture.selection.exclusion:capture.selection.path;
                if(path.empty() || std::hypot((end[0]-path.back()[0])*size.x,(end[1]-path.back()[1])*size.y)>.75f)path.push_back(end);
            }
        }
        const float wheel=ImGui::IsMouseDown(ImGuiMouseButton_Left)?io.MouseWheel:0;
        if (std::abs(wheel)>.0001f) {
            const float step=.025f*wheel;
            if(capture.selection.shape==RawAssetCapture::Shape::Stroke)
                capture.selection.strokeRadius=std::clamp(capture.selection.strokeRadius*std::exp2(wheel*.2f),.001f,.2f);
            else {
                if (!io.KeyCtrl) capture.selection.innerFeather=std::clamp(capture.selection.innerFeather+step,0.f,.5f);
                if (!io.KeyShift) capture.selection.outerFeather=std::clamp(capture.selection.outerFeather+step,0.f,2.f);
            }
            capture.feedbackDirty=true;
            ImGui::SetKeyOwner(ImGuiKey_MouseWheelY,ImGui::GetCurrentWindow()->ID);
        }
        if (moving || std::abs(wheel)>.0001f) capture.lastActivity=now;
        if (capture.selection.shape!=RawAssetCapture::Shape::Stroke && capture.feedback && capture.feedbackDirty && now-capture.lastMeasurement>=1.0/30.0) {
            try {
                const auto range=RawAssetCapture::Measure(*capture.feedback,capture.selection);
                if (range.count) {
                    const auto qualifier=RawAssetCapture::MakeQualifier(range,capture.selection);
                    capture.feedbackValid=capture.feedbackRenderer.Render(capture.feedback,qualifier,capture.status);
                } else {capture.feedbackValid=false;capture.status="Draw a larger circle to sample pixels.";}
            } catch (const std::exception& failure) {capture.status=failure.what();capture.feedbackValid=false;}
            capture.feedbackDirty=false;capture.lastMeasurement=now;
        }
        const float idle=static_cast<float>(std::clamp((now-capture.lastActivity-.18)/.22,0.0,1.0));
        const bool reducedMotion=Notifications::SystemReducedMotion();
        const float target=capture.feedbackValid?(reducedMotion?(now-capture.lastActivity<=.18?1.f:0.f):1-idle*idle*(3-2*idle)):0;
        if (reducedMotion) capture.feedbackOpacity=target;
        else capture.feedbackOpacity+=(target-capture.feedbackOpacity)*(1-std::exp(-40.f*std::max(0.f,io.DeltaTime)));
        if (std::abs(target-capture.feedbackOpacity)<.005f) capture.feedbackOpacity=target;
        if (capture.feedbackValid && capture.feedback && capture.feedbackRenderer.Texture() && capture.feedbackOpacity>.002f) {
            const auto textureUv=[&](ImVec2 p) {const auto q=mapping->Local(p);return ImVec2(q.x,1-q.y);};
            draw->PushClipRect(minimum,maximum,true);
            draw->AddImageQuad((ImTextureID)(intptr_t)capture.feedbackRenderer.Texture(),
                minimum,{maximum.x,minimum.y},maximum,{minimum.x,maximum.y},
                textureUv({0,0}),textureUv({1,0}),textureUv({1,1}),textureUv({0,1}),
                IM_COL32(255,255,255,int(std::clamp(capture.feedbackOpacity,0.f,1.f)*255)));
            draw->PopClipRect();
        }
        if (capture.selection.shape!=RawAssetCapture::Shape::Stroke && !capture.feedback) draw->AddText(label,IM_COL32_WHITE,
            capture.feedbackNeeded?"Preparing live mask feedback...":capture.status.c_str());
        else if (!capture.feedbackValid && !capture.status.empty()) draw->AddText(label,IM_COL32_WHITE,capture.status.c_str());
        char controls[180];
        std::snprintf(controls,sizeof(controls),"Inner %.0f%%  Outer %.0f%%\nWheel: both   Shift: inner   Ctrl: outer",
            capture.selection.innerFeather*100,capture.selection.outerFeather*100);
        if(capture.selection.shape==RawAssetCapture::Shape::Stroke)
            std::snprintf(controls,sizeof(controls),"Release to choose an action. Wheel: path width %.1f%%. Escape cancels.",capture.selection.strokeRadius*200);
        const ImVec2 controlsAt(label.x,label.y+32);
        draw->AddRectFilled({controlsAt.x-5,controlsAt.y-3},{controlsAt.x+ImGui::CalcTextSize(controls).x+5,controlsAt.y+ImGui::CalcTextSize(controls).y+3},IM_COL32(0,0,0,150),4);
        draw->AddText(controlsAt,IM_COL32_WHITE,controls);
        const auto a=screen(capture.selection.start),b=screen(capture.selection.end);
        draw->PushClipRect(minimum,maximum,true);
        if(capture.selection.shape==RawAssetCapture::Shape::Stroke) {
            for(const auto* path:{&capture.selection.path,&capture.selection.exclusion}) {
                for(size_t i=1;i<path->size();++i)draw->AddLine(screen((*path)[i-1]),screen((*path)[i]),path==&capture.selection.path?IM_COL32_WHITE:IM_COL32(255,140,100,255),2);
            }
        } else if (capture.selection.shape==RawAssetCapture::Shape::Line) {
            draw->AddLine(a,b,IM_COL32(0,0,0,220),4);draw->AddLine(a,b,IM_COL32_WHITE,2);
        } else {
            for (int stroke=0;stroke<2;++stroke) {
                for (int i=0;i<=64;++i) {
                    const float angle=6.28318530718f*i/64;
                    draw->PathLineTo(screen({capture.selection.start[0]+capture.selection.radiusU*std::cos(angle),
                        capture.selection.start[1]+capture.selection.radiusV*std::sin(angle)}));
                }
                draw->PathStroke(stroke?IM_COL32_WHITE:IM_COL32(0,0,0,220),ImDrawFlags_Closed,stroke?2.f:4.f);
            }
        }
        draw->PopClipRect();
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            capture.drawing=false;
            if(capture.selection.shape==RawAssetCapture::Shape::Stroke) {
                if(capture.repeat && !capture.excludeStroke) {
                    SubmitRawAssetCapture(capture.lastOutput,capture.lastMaskApplication!=0,capture.lastUseCurve);
                } else {capture.actionMenu=capture.openMenu=true;}
                capture.excludeStroke=false;
            } else {
                capture.armed=capture.feedbackNeeded=false;
                capture.pending=true;++capture.serial;capture.status="Capturing native image values...";
                capture.assetsTab=true;m_RawSettingsPanelRequested=true;
            }
        }
    }
    if (hovered && inside) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        for (int stroke=0;stroke<2;++stroke) {
            const auto color=stroke?IM_COL32_WHITE:IM_COL32(0,0,0,230);const float thickness=stroke?1.5f:3.5f;
            draw->AddLine({mouse.x-7,mouse.y},{mouse.x+7,mouse.y},color,thickness);
            draw->AddLine({mouse.x,mouse.y-7},{mouse.x,mouse.y+7},color,thickness);
            if (capture.selection.shape==RawAssetCapture::Shape::Circle) draw->AddCircle(mouse,11,color,32,thickness);
            else draw->AddLine({mouse.x+10,mouse.y+10},{mouse.x+23,mouse.y+23},color,thickness);
        }
    }
}
