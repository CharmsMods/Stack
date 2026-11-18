#include "Raw/ColorWarpHsl.h"
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
    auto feedback=args.feedback?args.feedback->Current():nullptr;
    const auto feedbackKey=feedback && args.feedback?args.feedback->Key():0;
    const bool live=args.ui.colorWarpLiveCloud, preview=args.ui.colorWarpDiagnosticLocked;
    const int selected=args.ui.selectedColorWarpPin;
    if(!EnsureRawLabColorCloudRenderer(state.cloud))return;
    ColorWarpCloudDemand demand;
    demand.inputKey=inputKey;demand.feedbackKey=feedbackKey;
    demand.nodeRevision=args.nodeRevision;demand.localRevision=state.localRevision;
    demand.workingSpace=args.workingSpace;demand.selected=selected;
    demand.live=live;demand.preview=preview;
    auto vertices=state.cloudPreparation.Update(scope,args.colorWarp,std::move(feedback),demand,args.submitCloudWork);
    if(state.cloudPreparation.InputChanged())state.cloud.colorWarpCloudGpuPointCount=0;
    if(vertices) {
        // The background job never touches GL. Publish a bounded finished
        // buffer here while the previous one remains usable during preparation.
        GLint previous=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&previous);
        glBindBuffer(GL_ARRAY_BUFFER,state.cloud.colorWarpCloudVertexBuffer);
        glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(vertices->size()*sizeof(RawLabColorCloudGpuVertex)),vertices->data(),GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER,GLuint(previous));
        state.cloud.colorWarpCloudGpuPointCount=int(vertices->size());
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
    request.colorWarpCloudShowSelectedCatch=preview && state.cloudPreparation.SelectionCurrent();
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddCallback(DrawRawLabColorCloudCallback,&request,sizeof(request));
    draw->AddCallback(ImDrawCallback_ResetRenderState,nullptr);
}
}
