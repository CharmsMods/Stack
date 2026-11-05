#include "Editor/Internal/RawLab/RawLabColorSurface.h"
#include "Editor/Internal/RawLab/RawLabColorProjection.h"
#include "Editor/NodeEditors/ColorWarpEditorState.h"
#include "Editor/NodeEditors/ColorWarpFeedback.h"
#include "Raw/ColorWarpMath.h"
#include "Renderer/GLHelpers.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
void DrawColorWarpCloud(RawLabColorSurfaceArgs& args,ImVec2 minimum,float size) {
    auto& state=*args.state;const auto& scope=args.scope;
    const auto inputKey=scope.graphInputFingerprint?scope.graphInputFingerprint:args.cloudInputFingerprint;
    bool rebuild=false;
    if(scope.valid && scope.stage==RawDevelopmentGraphScopeStage::ColorWarpInput &&
        scope.width>0 && scope.height>0 && scope.pixels.size()>=std::size_t(scope.width)*scope.height*3 &&
        (state.inputKey!=inputKey || state.workingSpace!=args.workingSpace || !state.cloudReady)) {
        state.points.clear();state.points.reserve(4096);
        const int width=std::min(64,scope.width),height=std::min(64,scope.height);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            const int sx=std::min(scope.width-1,int((x+.5f)*scope.width/width));
            const int sy=std::min(scope.height-1,int((y+.5f)*scope.height/height));
            const auto offset=(std::size_t(sy)*scope.width+sx)*3;
            ColorWarpEditorState::Point point;
            point.rgb={scope.pixels[offset],scope.pixels[offset+1],scope.pixels[offset+2]};
            point.input=RawRecipe::WorkingRgbToColorWarpCoordinate(point.rgb,args.workingSpace);
            point.u=(sx+.5f)/scope.width;point.v=(sy+.5f)/scope.height;
            if(std::isfinite(point.input.lightness) && std::isfinite(point.input.a) && std::isfinite(point.input.b))state.points.push_back(point);
        }
        state.inputKey=inputKey;state.workingSpace=args.workingSpace;state.cloudReady=true;rebuild=true;
    }
    auto feedback=args.feedback?args.feedback->Current():nullptr;
    const auto feedbackKey=feedback && args.feedback?args.feedback->Key():0;
    const bool live=args.ui.colorWarpLiveCloud, preview=args.ui.colorWarpDiagnosticLocked;
    const int selected=args.ui.selectedColorWarpPin;
    rebuild|=state.cloudRevision!=args.nodeRevision || state.cloudLocalRevision!=state.localRevision ||
        state.cloudFeedbackKey!=feedbackKey || state.cloudSelection!=selected || state.cloudLive!=live || state.cloudPreview!=preview;
    if(!EnsureRawLabColorCloudRenderer(state.cloud))return;
    if(rebuild) {
        std::vector<RawLabColorCloudGpuVertex> vertices;vertices.reserve(state.points.size());
        std::vector<RawRecipe::RawColorWarpMaskSample> masks;
        const bool spatial=RawRecipe::UsesColorWarpSpatialMasks(args.colorWarp);
        const auto* selectedPin=selected>=0 && selected<int(args.colorWarp.pins.size())?&args.colorWarp.pins[selected]:nullptr;
        const auto selectedRegion=selectedPin?std::find_if(args.colorWarp.regions.begin(),args.colorWarp.regions.end(),
            [&](const auto& r){return r.id==selectedPin->regionId;}):args.colorWarp.regions.end();
        const bool selectedSpatial=selectedRegion!=args.colorWarp.regions.end() && selectedRegion->spatialMode!=RawRecipe::RawColorWarpSpatialMode::AllMatches;
        RawRecipe::RawColorWarpPin membership;
        if(selectedPin){membership=*selectedPin;membership.enabled=true;membership.strength=1;}
        for(const auto& point:state.points) {
            if(feedback)feedback->SampleMasks(args.colorWarp,point.input,point.u,point.v,masks);
            float caught=0;
            if(preview && selectedPin && (!selectedSpatial || feedback)) {
                caught=RawRecipe::EvaluateColorWarpPinWeight(membership,point.input,
                    feedback && selectedSpatial?masks[selected]:RawRecipe::RawColorWarpMaskSample{},RawRecipe::ColorWarpPinSpatialSupport(args.colorWarp,membership));
            }
            auto rgb=point.rgb;auto coordinate=point.input;
            if(live && (!spatial || feedback)) {
                rgb=RawRecipe::ApplyPreparedColorWarp(args.colorWarp,rgb,args.workingSpace,spatial && feedback?&masks:nullptr);
                coordinate=RawRecipe::WorkingRgbToColorWarpCoordinate(rgb,args.workingSpace);
            }
            const auto normalized=RawRecipe::ColorWarpSelectionCoordinate(coordinate);
            const auto projected=ProjectColorDisc(normalized[0]*.7f,normalized[1]*.7f);
            const auto display=ColorDiscDisplayRgb(rgb,args.workingSpace);
            if(std::isfinite(projected.x) && std::isfinite(projected.y))vertices.push_back({projected.x,projected.y,
                display[0],display[1],display[2],coordinate.sceneEv,caught});
        }
        GLint previous=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&previous);
        glBindBuffer(GL_ARRAY_BUFFER,state.cloud.colorWarpCloudVertexBuffer);
        glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(vertices.size()*sizeof(RawLabColorCloudGpuVertex)),vertices.data(),GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER,GLuint(previous));
        state.cloud.colorWarpCloudGpuPointCount=int(vertices.size());
        state.cloudRevision=args.nodeRevision;state.cloudLocalRevision=state.localRevision;state.cloudFeedbackKey=feedbackKey;
        state.cloudSelection=selected;state.cloudLive=live;state.cloudPreview=preview;
    }
    // Copy presentation coordinates into each draw command: a floating widget
    // and graph card can use the same node buffer without overwriting geometry.
    auto request=state.cloud;
    const auto* viewport=ImGui::GetWindowViewport();const auto scale=ImGui::GetIO().DisplayFramebufferScale;
    request.colorWarpCloudCanvasMinX=minimum.x;request.colorWarpCloudCanvasMinY=minimum.y;
    request.colorWarpCloudCanvasWidth=request.colorWarpCloudCanvasHeight=size;
    request.colorWarpCloudDisplayPosX=viewport->Pos.x;request.colorWarpCloudDisplayPosY=viewport->Pos.y;
    request.colorWarpCloudDisplayWidth=viewport->Size.x;request.colorWarpCloudDisplayHeight=viewport->Size.y;
    request.colorWarpCloudFramebufferScaleX=scale.x;request.colorWarpCloudFramebufferScaleY=scale.y;
    request.colorWarpCloudViewCenterA=request.colorWarpCloudViewCenterB=0;
    request.colorWarpCloudViewHalfWidth=request.colorWarpCloudViewHalfHeight=ColorDiscExtent;
    request.colorWarpCloudPointScale=.55f*std::min(scale.x,scale.y);
    request.colorWarpCloudShowSelectedCatch=preview;
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddCallback(DrawRawLabColorCloudCallback,&request,sizeof(request));
    draw->AddCallback(ImDrawCallback_ResetRenderState,nullptr);
}
}
