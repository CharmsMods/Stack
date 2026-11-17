#include "Editor/NodeEditors/HslSelectionEditor.h"
#include "Editor/NodeEditors/RawWorkspaceEditContext.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/GLHelpers.h"
#include <limits>

namespace Stack::Editor {
using namespace RawRecipe;
namespace {
std::size_t CoverageKey(const HslSelection& s) {
    std::size_t key=1;const auto hash=[&](const auto& v){key^=std::hash<std::decay_t<decltype(v)>>{}(v)+0x9e3779b9+(key<<6)+(key>>2);};
    hash(s.enabled);hash(s.hue);hash(s.hueWidth);hash(s.hueSoftness);hash(s.allColors);hash(s.areaEnabled);hash(s.areaSoftness);
    for(const auto* list:{&s.bands,&s.excludedBands}){hash(list->size());for(const auto& b:*list){hash(b.low);hash(b.high);hash(b.softness);}}
    hash(s.outline.size());for(const auto& p:s.outline){hash(p[0]);hash(p[1]);}return key;
}
bool AddBand(std::vector<HslBrightnessBand>& bands,float ev) {
    // Union overlapping sample cores only. In particular, do not bridge the
    // dark and bright populations when a second stroke is added.
    HslBrightnessBand next{std::max(-32.f,ev-.15f),std::min(32.f,ev+.15f),.35f};
    for(auto it=bands.begin();it!=bands.end();) {
        if(next.low<=it->high && next.high>=it->low){next.low=std::min(next.low,it->low);next.high=std::max(next.high,it->high);next.softness=std::max(next.softness,it->softness);it=bands.erase(it);}else ++it;
    }
    if(bands.size()>=kMaxHslBands)return false;bands.push_back(next);return true;
}
void UpdatePreview(HslSelectionEditorState& state,const HslSelection& selection,const RawWorkspaceEditContext& context,
    const RawDevelopmentGraphScopeReadback& scope,std::size_t input,const std::function<bool(std::function<void()>)>& submit) {
    if(state.preview==0){if(state.job)state.job->cancelled=true;return;}
    if(state.coverageRevision!=context.nodeRevision || state.coverageSelection!=selection.id) {
        state.coverageKey=CoverageKey(selection);state.coverageRevision=context.nodeRevision;state.coverageSelection=selection.id;
    }
    const auto matches=[&](const auto& job){return !job.cancelled && job.input==input && job.revision==state.coverageKey && job.selection==selection.id && job.mode==state.preview;};
    if(state.job && matches(*state.job)) {
        if(!state.job->done)return;
        const auto& job=*state.job;
        if(job.failed){state.status="Selection preview unavailable. Toggle preview to retry.";return;}
        if(state.overlay && state.overlayInput==input && state.overlayRevision==state.coverageKey && state.overlaySelection==selection.id && state.overlayMode==state.preview)return;
        state.overlay.Reset(GLHelpers::CreateTextureFromPixels(job.pixels.data(),job.width,job.height,4));
        if(!state.overlay){state.status="Selection preview could not be uploaded.";state.job->failed=true;return;}
        state.overlayInput=input;state.overlayRevision=state.coverageKey;state.overlaySelection=selection.id;state.overlayMode=state.preview;state.matching=job.matching;return;
    }
    if(state.job && !state.job->done){state.job->cancelled=true;return;}
    auto job=std::make_shared<HslSelectionEditorState::PreviewJob>();job->input=input;job->revision=state.coverageKey;job->selection=selection.id;job->mode=state.preview;
    const auto space=context.recipe.technical.workingSpace;
    // Only a changed input or selection copies samples and submits analysis.
    const bool submitted=submit([job,selection,scope,space]() {
        try {
            job->width=scope.width;job->height=scope.height;const auto count=std::size_t(scope.width)*scope.height;job->pixels.resize(count*4);
            for(std::size_t i=0;i<count;++i) {
                if((i&1023)==0 && job->cancelled)break;
                const auto c=WorkingRgbToColorWarpCoordinate({scope.pixels[i*3],scope.pixels[i*3+1],scope.pixels[i*3+2]},space);
                const float w=HslSelectionWeight(selection,c,{(float(i%scope.width)+.5f)/scope.width,(float(i/scope.width)+.5f)/scope.height});
                if(w>.01f)++job->matching;
                auto* pixel=job->pixels.data()+i*4;
                if(job->mode==2){pixel[0]=pixel[1]=pixel[2]=static_cast<unsigned char>(std::lround(w*255));pixel[3]=255;}
                else {pixel[0]=48;pixel[1]=210;pixel[2]=224;pixel[3]=static_cast<unsigned char>(std::lround(w*145));}
            }
        }catch(...){job->failed=true;}
        job->done=true;
    });
    if(!submitted){job->failed=true;job->done=true;}state.job=std::move(job);
}
}
bool DrawHslSelectionImage(HslSelectionEditorState& state,const RawWorkspaceEditContext& context,
    const RawDevelopmentGraphScopeReadback& scope,std::size_t inputKey,const ImRect& rect,bool pointerAllowed,
    const std::function<bool(std::function<void()>)>& submit) {
    auto found=std::find_if(context.recipe.colorWarp.hslSelections.begin(),context.recipe.colorWarp.hslSelections.end(),[&](const auto& s){return s.id==state.selected;});
    if(found==context.recipe.colorWarp.hslSelections.end()){state.EndInteraction();state.selected.clear();state.preview=0;return false;}
    const auto& selection=*found;
    if(ImGui::GetIO().AppFocusLost){state.EndInteraction();return false;}
    if(!context.canEdit || !selection.enabled || !context.recipe.colorWarp.enabled){state.EndInteraction();return false;}
    const bool ready=scope.valid && scope.stage==RawDevelopmentGraphScopeStage::ColorWarpInput && scope.width>0 && scope.height>0 && scope.pixels.size()>=std::size_t(scope.width)*scope.height*3;
    const auto& crop=context.recipe.cropRotation;
    const float ox=crop.cropEnabled?crop.cropX:0,oy=crop.cropEnabled?crop.cropY:0,sx=crop.cropEnabled?crop.cropWidth:1,sy=crop.cropEnabled?crop.cropHeight:1;
    const auto point=[&](ImVec2 p){return std::array<float,2>{std::clamp(ox+(p.x-rect.Min.x)/rect.GetWidth()*sx,0.f,1.f),std::clamp(oy+(p.y-rect.Min.y)/rect.GetHeight()*sy,0.f,1.f)};};
    const auto screen=[&](std::array<float,2> p){return ImVec2(rect.Min.x+(p[0]-ox)/sx*rect.GetWidth(),rect.Min.y+(p[1]-oy)/sy*rect.GetHeight());};
    auto* draw=ImGui::GetWindowDrawList();
    if(ready)UpdatePreview(state,selection,context,scope,inputKey,submit);
    else if(state.preview)state.status="Waiting for current image tones";
    if(state.preview && state.overlay && state.overlayInput==inputKey && state.overlayRevision==state.coverageKey && state.overlaySelection==selection.id && state.overlayMode==state.preview)
        draw->AddImage((ImTextureID)(intptr_t)state.overlay.Get(),rect.Min,rect.Max,{ox,oy},{ox+sx,oy+sy});
    if(state.drawing && (state.gestureRevision!=context.nodeRevision || (state.gestureInput && (!ready || state.gestureInput!=inputKey)))){state.EndInteraction();state.status="Image input changed; draw the selection again.";return true;}
    if(ImGui::IsKeyPressed(ImGuiKey_Escape) && (state.drawing || state.tool!=HslImageTool::None)){state.EndInteraction();return true;}
    const bool hovered=pointerAllowed && rect.Contains(ImGui::GetIO().MousePos) && ImRect(draw->GetClipRectMin(),draw->GetClipRectMax()).Contains(ImGui::GetIO().MousePos);
    const bool sampling=state.tool==HslImageTool::Sample || state.tool==HslImageTool::Exclude;
    const auto mouse=ImGui::GetIO().MousePos;
    const auto appendPath=[&] {
        if(state.path.empty())return;
        const auto from=state.path.back(),to=point(mouse);const auto a=screen(from),b=screen(to);
        const float distance=std::hypot(b.x-a.x,b.y-a.y);if(distance<1.f)return;
        const int steps=std::max(1,int(std::ceil(distance/3.f)));
        for(int i=1;i<=steps;++i) {
            if(state.path.size()>=2048){state.pathOverflow=true;break;}
            const float t=float(i)/steps;state.path.push_back({from[0]+(to[0]-from[0])*t,from[1]+(to[1]-from[1])*t});
        }
    };
    if(hovered && state.tool!=HslImageTool::None) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        const auto ink=ImGui::GetColorU32(ImGuiCol_Text);
        if(sampling){draw->AddCircle(mouse,6,ink,20,1.5f);draw->AddLine({mouse.x-10,mouse.y},{mouse.x+10,mouse.y},ink);draw->AddLine({mouse.x,mouse.y-10},{mouse.x,mouse.y+10},ink);}
        else {draw->AddCircleFilled(mouse,2.5f,ink);draw->AddLine(mouse,{mouse.x+9,mouse.y-9},ink,2);}
    }
    if(hovered && ImGui::IsMouseClicked(0) && state.tool!=HslImageTool::None && !state.pending) {
        if(sampling && !ready){state.status="Waiting for current image tones";return true;}
        state.draft=selection;state.path.clear();state.path.push_back(point(mouse));state.gestureInput=sampling?inputKey:0;state.gestureRevision=context.nodeRevision;state.drawing=true;state.handle=-1;state.pathOverflow=false;
        if(state.tool==HslImageTool::EditOutline) {
            float distance=12.f;for(int i=0;i<int(selection.outline.size());++i){const auto p=screen(selection.outline[i]);const float d=std::hypot(p.x-mouse.x,p.y-mouse.y);if(d<distance){distance=d;state.handle=i;}}
            if(state.handle<0){state.drawing=false;state.draft.reset();}
        }
    }
    if(state.drawing && ImGui::IsMouseDown(0)) {
        if(state.tool==HslImageTool::EditOutline)state.draft->outline[state.handle]=point(mouse);
        else appendPath();
    }
    if(state.drawing && ImGui::IsMouseReleased(0)) {
        if(state.tool!=HslImageTool::EditOutline)appendPath();
        bool accepted=!state.pathOverflow;state.drawing=false;
        if(sampling) {
            auto& target=*state.draft;auto& bands=state.tool==HslImageTool::Exclude?target.excludedBands:target.bands;
            // First sample establishes the center; later strokes widen the
            // explicit color range while retaining separate brightness bands.
            bool first=bands.empty();
            for(const auto& p:state.path) {
                const int x=std::clamp(int(p[0]*scope.width),0,scope.width-1),y=std::clamp(int(p[1]*scope.height),0,scope.height-1);const auto i=(std::size_t(y)*scope.width+x)*3;
                const auto c=WorkingRgbToColorWarpCoordinate({scope.pixels[i],scope.pixels[i+1],scope.pixels[i+2]},context.recipe.technical.workingSpace);
                if(!std::isfinite(c.sceneEv) || c.lightness<=0)continue;
                if(state.tool==HslImageTool::Sample) {
                    const float hue=ColorWarpHue(c.a,c.b);
                    if(first){target.hue=hue;target.hueWidth=8;first=false;}
                    else target.hueWidth=std::min(180.f,std::max(target.hueWidth,std::abs(ColorWarpHueDelta(hue-target.hue))+3));
                }
                if(!AddBand(bands,c.sceneEv)){accepted=false;state.status="Too many separate brightness ranges. Use a shorter stroke or remove a range.";break;}
            }
        }else if(state.tool==HslImageTool::Outline) {
            // Remove nearly collinear points without changing the boundary's
            // intent. Refuse an over-complex path instead of truncating it.
            auto points=state.path;
            for(std::size_t i=1;i+1<points.size();) {
                const auto a=screen(points[i-1]),b=screen(points[i]),c=screen(points[i+1]);
                const float area=std::abs((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x));
                if(area/std::max(1.f,std::hypot(c.x-a.x,c.y-a.y))<1.2f)points.erase(points.begin()+i);else ++i;
            }
            if(points.size()<3 || points.size()>kMaxHslOutlinePoints){accepted=false;state.status="Draw a closed area with a simpler outline.";}
            else {state.draft->outline=std::move(points);state.draft->areaEnabled=true;state.tool=HslImageTool::EditOutline;}
        }
        if(state.pathOverflow)state.status="The stroke is too long. Use shorter strokes.";
        state.pending=accepted;if(!accepted)state.draft.reset();else state.status.clear();state.path.clear();
    }
    const auto& outline=state.draft && state.tool==HslImageTool::EditOutline?state.draft->outline:selection.outline;
    if(selection.areaEnabled || state.tool==HslImageTool::EditOutline) {
        draw->PushClipRect(rect.Min,rect.Max,true);
        for(std::size_t i=0;i<outline.size();++i) {
            const auto a=screen(outline[i]),b=screen(outline[(i+1)%outline.size()]);draw->AddLine(a,b,ImGui::GetColorU32(ImGuiCol_Text,.85f),1.5f);
            if(state.tool==HslImageTool::EditOutline)draw->AddCircleFilled(a,3.f,ImGui::GetColorU32(ImGuiCol_Text));
        }draw->PopClipRect();
    }
    if(state.drawing && state.path.size()>1)for(std::size_t i=1;i<state.path.size();++i)draw->AddLine(screen(state.path[i-1]),screen(state.path[i]),ImGui::GetColorU32(ImGuiCol_Text),2.f);
    return state.tool!=HslImageTool::None || state.preview!=0 || state.pending;
}
}
