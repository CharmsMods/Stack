#pragma once
#include "Raw/HslSelections.h"
#include "Animation/ScalarChannel.h"
#include "Editor/NodeEditors/PanelRoller.h"
#include "Renderer/ScopedGLObjects.h"
#include <atomic>
#include <memory>
#include <functional>
#include <optional>
#include <imgui_internal.h>

struct RawDevelopmentGraphScopeReadback;
namespace Stack::Editor {
struct RawWorkspaceEditContext;
enum class HslImageTool { None,Sample,Exclude,Outline,EditOutline };
struct HslSelectionEditorState {
    std::string selected,status;
    HslImageTool tool=HslImageTool::None;
    int preview=0,range=-1,handle=-1;
    bool excludeRange=false,drawing=false,pending=false,pathOverflow=false,photoRouting=false;
    std::size_t gestureInput=0;
    std::uint64_t gestureRevision=0;
    std::vector<std::array<float,2>> path;
    std::optional<RawRecipe::HslSelection> draft;
    RawRecipe::HslBrightnessBand bandBefore;
    float pressEv=0,viewLow=-12,viewHigh=8;
    PanelControlState rollers;
    Animation::ScalarChannel reveal{0},toolPosition{0},rangePosition{0},previewPosition{0};
    int lastTool=0;
    std::uint64_t coverageRevision=~std::uint64_t(0),coverageKey=0;
    std::string coverageSelection;
    std::array<float,96> profile{};
    std::uint64_t profileRevision=~std::uint64_t(0);
    std::string profileSelection;
    std::uint64_t referenceRevision=~std::uint64_t(0);
    std::string referenceSelection;
    std::array<float,3> globalReference{},ownReference{};
    struct PreviewJob {
        std::atomic<bool> done{false},cancelled{false};
        std::vector<unsigned char> pixels;
        int width=0,height=0,mode=0;
        bool failed=false;
        std::size_t matching=0,input=0;
        std::uint64_t revision=0;
        std::string selection;
    };
    std::shared_ptr<PreviewJob> job;
    Renderer::ScopedGLTexture overlay;
    std::size_t overlayInput=0,matching=0;
    std::uint64_t overlayRevision=0;
    std::string overlaySelection;
    int overlayMode=0;
    void EndGesture() {tool=HslImageTool::None;drawing=pending=pathOverflow=false;draft.reset();path.clear();handle=-1;rollers.EndInteraction();}
    void EndInteraction() {EndGesture();preview=0;if(job)job->cancelled=true;reveal.Finish();toolPosition.Finish();rangePosition.Finish();previewPosition.Finish();}
    ~HslSelectionEditorState(){if(job)job->cancelled=true;}
};
bool DrawHslSelections(RawWorkspaceEditContext& context);
bool HslNeedsAnalysis(const HslSelectionEditorState& state);
// Captures an immutable draft; the normal node editor commits it in one edit.
bool DrawHslSelectionImage(HslSelectionEditorState& state,const RawWorkspaceEditContext& context,
    const RawDevelopmentGraphScopeReadback& scope,std::size_t inputKey,const ImRect& imageRect,bool pointerAllowed,
    const std::function<bool(std::function<void()>)>& submit);
}
