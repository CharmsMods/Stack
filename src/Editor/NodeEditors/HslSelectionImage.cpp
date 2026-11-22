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
    hash(s.sourceChroma);hash(s.chromaReach);hash(s.enabled);hash(s.hue);hash(s.hueWidth);hash(s.hueSoftness);hash(s.allColors);hash(s.neutralOnly);hash(s.areaEnabled);hash(s.areaSoftness);
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
// A circle is measured in displayed pixels. Convert each axis independently for
// the source raster so zoom, crop and aspect ratio preserve the sampled region.
bool SampleWarpCircle(HslSelection& pick,const RawDevelopmentGraphScopeReadback& scope,
    Raw::RawWorkingSpace space,std::array<float,2> center,float rx,float ry) {
    if(rx<=0 || ry<=0)return false;
    std::vector<float> hues,chroma,ev;
    float hueX=0,hueY=0;
    constexpr int steps=12;
    for(int y=-steps;y<=steps;++y)for(int x=-steps;x<=steps;++x) {
        if(x*x+y*y>steps*steps)continue;
        const float u=center[0]+rx*x/steps,v=center[1]+ry*y/steps;
        if(u<0 || v<0 || u>=1 || v>=1)continue;
        const auto i=(std::size_t(int(v*scope.height))*scope.width+int(u*scope.width))*3;
        const auto color=WorkingRgbToColorWarpCoordinate({scope.pixels[i],scope.pixels[i+1],scope.pixels[i+2]},space);
        if(color.lightness<=0 || !std::isfinite(color.sceneEv))continue;
        const auto ab=ColorWarpSelectionCoordinate(color);
        const float c=std::hypot(ab[0],ab[1]);
        chroma.push_back(c);ev.push_back(color.sceneEv);
        if(c>.02f){hues.push_back(ColorWarpHue(ab[0],ab[1]));hueX+=ab[0];hueY+=ab[1];}
    }
    if(ev.empty())return false;
    std::sort(chroma.begin(),chroma.end());
    std::sort(ev.begin(),ev.end());
    const auto low=ev.size()/20,high=ev.size()-1-low;
    pick.sourceChroma=chroma[ev.size()/2];
    pick.chromaReach=std::clamp(std::max(pick.sourceChroma-chroma[low],chroma[high]-pick.sourceChroma)+.035f,.06f,4.f);
    pick.neutralOnly=hues.empty();
    pick.hue=ColorWarpHue(hueX,hueY);
    for(float& h:hues)h=std::abs(ColorWarpHueDelta(h-pick.hue));
    std::sort(hues.begin(),hues.end());
    pick.hueWidth=hues.empty()?180.f:std::clamp(hues[hues.size()-1-hues.size()/20]+10.f,12.f,180.f);
    pick.bands={{ev[low]-.45f,ev[high]+.45f,.45f}};
    pick.outline.clear();
    for(int i=0;i<64;++i) {
        const float a=i*6.2831853f/64;
        pick.outline.push_back({std::clamp(center[0]+rx*std::cos(a),0.f,1.f),
            std::clamp(center[1]+ry*std::sin(a),0.f,1.f)});
    }
    pick.warpMask=pick.areaEnabled;
    return true;
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
            auto qualifier=selection;
            const bool wired=qualifier.areaEnabled && qualifier.warpMask;
            if(wired)qualifier.areaEnabled=false;
            const auto mask=std::find_if(scope.warpPickMasks.begin(),scope.warpPickMasks.end(),
                [&](const auto& plane){return plane.id==selection.id;});
            for(std::size_t i=0;i<count;++i) {
                if((i&1023)==0 && job->cancelled)break;
                const auto c=WorkingRgbToColorWarpCoordinate({scope.pixels[i*3],scope.pixels[i*3+1],scope.pixels[i*3+2]},space);
                const std::array<float,2> uv{(float(i%scope.width)+.5f)/scope.width,(float(i/scope.width)+.5f)/scope.height};
                float w=HslSelectionWeight(qualifier,c,uv);
                if(wired) {
                    if(mask==scope.warpPickMasks.end() || mask->coverage.empty())w=0;
                    else {
                        const auto& m=*mask;
                        const int x=std::min(m.width-1,int(uv[0]*m.width)),y=std::min(m.height-1,int(uv[1]*m.height));
                        w*=m.coverage[std::size_t(y)*m.width+x]/255.f;
                    }
                }
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
    if(ImGui::IsKeyDown(ImGuiKey_H) && !ImGui::GetIO().WantTextInput && state.preview==0) {
        state.preview=1;state.momentaryPreview=true;
    } else if(!ImGui::IsKeyDown(ImGuiKey_H) && state.momentaryPreview) {
        state.preview=0;state.momentaryPreview=false;
    }
    const auto previewKey=selection.warpMask?context.nodeRevision^inputKey:inputKey;
    if(ready)UpdatePreview(state,selection,context,scope,previewKey,submit);
    else if(state.preview)state.status="Waiting for current image tones";
    if(state.preview && state.overlay && state.overlayInput==previewKey && state.overlayRevision==state.coverageKey && state.overlaySelection==selection.id && state.overlayMode==state.preview)
        draw->AddImage((ImTextureID)(intptr_t)state.overlay.Get(),rect.Min,rect.Max,{ox,oy},{ox+sx,oy+sy});
    if(state.drawing && (state.gestureRevision!=context.nodeRevision || (state.gestureInput && (!ready || state.gestureInput!=inputKey)))){state.EndInteraction();state.status="Image input changed; draw the selection again.";return true;}
    if(ImGui::IsKeyPressed(ImGuiKey_Escape) && (state.drawing || state.tool!=HslImageTool::None)){state.EndInteraction();return true;}
    const bool hovered=pointerAllowed && rect.Contains(ImGui::GetIO().MousePos) && ImRect(draw->GetClipRectMin(),draw->GetClipRectMax()).Contains(ImGui::GetIO().MousePos);
    const bool sampling=state.tool==HslImageTool::Sample || state.tool==HslImageTool::Exclude;
    const auto mouse=ImGui::GetIO().MousePos;
    const bool warpSample=state.tool==HslImageTool::Sample && selection.sourceChroma>=0;
    const auto circleRadius=[&] {
        const auto origin=screen(state.path.front());
        return std::hypot(mouse.x-origin.x,mouse.y-origin.y);
    };
    const auto appendPath=[&] {
        if(warpSample)return;
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
        if(warpSample && circleRadius()>=4.f) {
            const float radius=circleRadius();
            accepted=SampleWarpCircle(*state.draft,scope,context.recipe.technical.workingSpace,state.path.front(),
                sx*radius/rect.GetWidth(),sy*radius/rect.GetHeight());
            if(!accepted)state.status="The circle contains no usable color samples.";
        }else if(sampling) {
            auto& target=*state.draft;auto& bands=state.tool==HslImageTool::Exclude?target.excludedBands:target.bands;
            // First sample establishes the center; later strokes widen the
            // explicit color range while retaining separate brightness bands.
            bool first=bands.empty();
            if(first && state.path.size()==1 && state.tool==HslImageTool::Sample) {
                // Pick the representative sample from a small neighborhood. Reject the
                // opposite side of an edge using the clicked pixel as the reference.
                const auto p=state.path.front();
                const int cx=std::clamp(int(p[0]*scope.width),0,scope.width-1);
                const int cy=std::clamp(int(p[1]*scope.height),0,scope.height-1);
                const auto colorAt=[&](int x,int y) {
                    const auto i=(std::size_t(y)*scope.width+x)*3;
                    return WorkingRgbToColorWarpCoordinate({scope.pixels[i],scope.pixels[i+1],scope.pixels[i+2]},context.recipe.technical.workingSpace);
                };
                const auto center=colorAt(cx,cy);
                const auto ref=ColorWarpSelectionCoordinate(center);
                struct Sample {int x,y;float ev,hue;};
                std::array<Sample,25> samples{};int count=0;
                for(int y=std::max(0,cy-2);y<=std::min(scope.height-1,cy+2);++y)
                    for(int x=std::max(0,cx-2);x<=std::min(scope.width-1,cx+2);++x) {
                        const auto sample=colorAt(x,y);
                        const auto ab=ColorWarpSelectionCoordinate(sample);
                        if(std::abs(sample.sceneEv-center.sceneEv)<=.7f && std::hypot(ab[0]-ref[0],ab[1]-ref[1])<.065f)
                            samples[count++]={x,y,sample.sceneEv,ColorWarpHue(sample.a,sample.b)};
                    }
                std::sort(samples.begin(),samples.begin()+count,[](const auto& a,const auto& b){return a.ev<b.ev;});
                if(count) {
                    const auto median=samples[count/2];
                    state.path[0]={(median.x+.5f)/scope.width,(median.y+.5f)/scope.height};
                    target.hue=median.hue;
                    if(target.sourceChroma>=0) {
                        const auto ab=ColorWarpSelectionCoordinate(colorAt(median.x,median.y));
                        target.sourceChroma=std::hypot(ab[0],ab[1]);
                        // Seed an editable local outline from the first color boundary
                        // along each ray. Sampling work runs only on pick release.
                        target.outline.clear();
                        for(int ray=0;ray<40;++ray) {
                            const float angle=float(ray)*6.2831853f/40;
                            const float dx=std::cos(angle),dy=std::sin(angle);
                            float px=median.x+.5f,py=median.y+.5f;
                            for(int step=1;step<=std::max(scope.width,scope.height);++step) {
                                const float x=median.x+.5f+dx*step,y=median.y+.5f+dy*step;
                                if(x<0 || y<0 || x>=scope.width || y>=scope.height)break;
                                const auto test=colorAt(int(x),int(y));
                                const auto chroma=ColorWarpSelectionCoordinate(test);
                                if(std::hypot(chroma[0]-ab[0],chroma[1]-ab[1])>.12f || std::abs(test.sceneEv-median.ev)>2.5f)break;
                                px=x;py=y;
                            }
                            target.outline.push_back({px/scope.width,py/scope.height});
                        }
                        target.areaSoftness=.002f;
                        target.warpMask=target.areaEnabled;
                    }
                    std::array<float,25> deviations{};
                    for(int i=0;i<count;++i)deviations[i]=std::abs(ColorWarpHueDelta(samples[i].hue-median.hue));
                    std::sort(deviations.begin(),deviations.begin()+count);
                    target.hueWidth=std::clamp(deviations[count*4/5]+8.f,12.f,65.f);
                    first=false;
                    target.neutralOnly=!target.allColors && NeutralRangeWeight(colorAt(median.x,median.y))>.85f;
                    const float reach=target.sourceChroma>=0?1.f:.18f;
                    bands.push_back({samples[count/5].ev-reach,samples[count-1-count/5].ev+reach,.45f});
                }
            }
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
            else {state.draft->outline=std::move(points);state.draft->areaEnabled=true;if(state.draft->sourceChroma>=0) {
                    state.draft->warpMask=true;state.tool=HslImageTool::None;
                }else state.tool=HslImageTool::EditOutline;}
        }
        if(state.pathOverflow)state.status="The stroke is too long. Use shorter strokes.";
        if(sampling && state.path.size()==1 && accepted)state.tool=HslImageTool::None;
        state.pending=accepted;if(!accepted)state.draft.reset();else state.status.clear();state.path.clear();
    }
    const auto& outline=state.draft && state.tool==HslImageTool::EditOutline?state.draft->outline:selection.outline;
    if((selection.areaEnabled && !selection.warpMask) || state.tool==HslImageTool::EditOutline) {
        draw->PushClipRect(rect.Min,rect.Max,true);
        for(std::size_t i=0;i<outline.size();++i) {
            const auto a=screen(outline[i]),b=screen(outline[(i+1)%outline.size()]);draw->AddLine(a,b,ImGui::GetColorU32(ImGuiCol_Text,.85f),1.5f);
            if(state.tool==HslImageTool::EditOutline)draw->AddCircleFilled(a,3.f,ImGui::GetColorU32(ImGuiCol_Text));
        }draw->PopClipRect();
    }
    if(state.drawing && warpSample && !state.path.empty() && circleRadius()>=4.f)
        draw->AddCircle(screen(state.path.front()),circleRadius(),ImGui::GetColorU32(ImGuiCol_Text),64,1.5f);
    if(state.drawing && state.path.size()>1)for(std::size_t i=1;i<state.path.size();++i)draw->AddLine(screen(state.path[i-1]),screen(state.path[i]),ImGui::GetColorU32(ImGuiCol_Text),2.f);
    return state.tool!=HslImageTool::None || state.preview!=0 || state.pending;
}
}
