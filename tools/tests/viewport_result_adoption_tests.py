#!/usr/bin/env python3
"""Generate bounded CPU checks of the production viewport-result predicates.

Geometry, sampling, preferences and graph-context policies are real. The editor
and result shells contain only fields those methods read; section stores are
identity tokens, with no GL, pixel storage or OS window. The surrounding
IsCurrentGraphResult gate is also copied to check source/context and monotonic
generation ownership. Denoise continuation handoff is covered by the existing
worker lifecycle fixture, not these deliberately false continuation stand-ins.
"""
import argparse
from pathlib import Path


def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
viewport = (root / 'src/Editor/Internal/EditorModuleViewportPreview.cpp').read_text()
graph = (root / 'src/Editor/Internal/EditorModuleGraphRendering.cpp').read_text()

shell = r'''
#include "Editor/ViewportRefinementPolicy.h"
#include "Editor/GraphViewportAnalysisPolicy.h"
#include "Editor/RawRenderPurpose.h"
#include "Raw/RawViewportPreferences.h"
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace Stack::Renderer;
using Step = ViewportRefinementStep;
using StoreIdentity = std::shared_ptr<const int>;
struct EditorRenderWorker {
    struct Result {
        RawRenderPurpose rawRenderPurpose = RawRenderPurpose::ViewportRefinement;
        Stack::GraphRendering::RequestTag graphRequest;
        ViewportPreviewRequest viewportPreview;
        ViewportPreviewGeometry viewportPreviewGeometry;
        StoreIdentity viewportSections;
        std::size_t viewportContentIdentity = 0;
        std::uint64_t generation = 11;
        int viewportViewId = 1;
        bool success = true, denoisePending = false;
        std::string error;
    };
};
struct ViewState {
    ViewportPreviewRequest request;
    ViewportPreviewGeometry geometry;
    StoreIdentity sections;
    std::size_t contentIdentity = 1234, acceptedContentIdentity = 1234;
    int physicalWidth = 2000, physicalHeight = 1500;
    bool navigationActive = true;
};
struct EditorModule {
    std::array<ViewState,2> m_ViewportPreviewStates;
    bool m_RawWorkspaceRootTabActive = true, usesPreview = true;
    std::uint64_t m_RenderGeneration = 11, m_GraphAcceptedResultGeneration = 10;
    Stack::GraphRendering::RequestTag tag{true,4,0,false,3,99,5};
    Raw::ViewportPreferences preferences;
    bool UsesGraphViewportPreview() const {return usesPreview;}
    const ViewState& ActiveViewportPreviewState() const {
        return m_ViewportPreviewStates[m_RawWorkspaceRootTabActive ? 0 : 1];
    }
    const Raw::ViewportPreferences& GetRawViewportPreferences() const {return preferences;}
    Stack::GraphRendering::RequestTag CurrentGraphRenderTag() const {return tag;}
    bool IsCurrentGraphDenoiseContinuation(const EditorRenderWorker::Result&) const {return false;}
    bool IsGraphContinuationViewCurrent(const EditorRenderWorker::Result&) const {return false;}
    bool IsViewportPreviewResultCurrent(const EditorRenderWorker::Result&) const;
    bool IsCurrentGraphResult(const EditorRenderWorker::Result&) const;
    bool MayAdopt(const EditorRenderWorker::Result& result) const {
        return IsViewportPreviewResultCurrent(result) && IsCurrentGraphResult(result);
    }
};
'''

checks = r'''
void Require(bool value,const char* message) {
    if (!value) throw std::runtime_error(message);
}
ViewportPreviewGeometry Whole(int width,int height) {
    return {8000,6000,WholeImageRegion(width,height),width,height};
}
ViewportPreviewGeometry Patch(const ImageRegion& bounds,int width,int height) {
    const auto sampled=ScaleImageRegion(bounds,width,height);
    return {8000,6000,sampled,sampled.width,sampled.height};
}
struct Fixture {
    EditorModule editor;
    EditorRenderWorker::Result result;
    ViewState& View() {return editor.m_ViewportPreviewStates[0];}
    Fixture() {
        auto& view=View();
        view.request.enabled=true;view.request.generation=7;
        view.request.visible=WholeImageRegion(8000,6000);
        view.geometry=Whole(1000,750);
        view.sections=std::make_shared<const int>(1);
        editor.m_ViewportPreviewStates[1].sections=std::make_shared<const int>(2);
        result.graphRequest=editor.tag;
        result.viewportContentIdentity=view.contentIdentity;
        result.viewportPreview=view.request;
        result.viewportPreview.generation=6;
        result.viewportPreview.refinement=Step::VisibleScreen;
        result.viewportPreviewGeometry=Whole(1800,1350);
    }
};
void MovingScreen() {
    Fixture f;
    Require(f.editor.MayAdopt(f.result),
        "A completed nearby zoom must remain usable while the camera moves");
    f.editor.preferences.mode=Raw::ViewportInteractionMode::PreserveDetail;
    f.editor.preferences.minimumDetailPercent=90;
    Require(f.editor.MayAdopt(f.result),"Moving-screen adoption must honor an exactly met detail floor");
    for (int floor:{95,100}) {
        f.editor.preferences.minimumDetailPercent=floor;
        Require(!f.editor.MayAdopt(f.result),"Moving-camera tolerance cannot violate the user's detail floor");
    }
    f.editor.preferences.mode=Raw::ViewportInteractionMode::TargetFps;
    f.View().navigationActive=false;
    Require(!f.editor.MayAdopt(f.result),"A stopped camera must require full display density");
    f.result.viewportPreviewGeometry=Whole(2000,1500);
    Require(f.editor.MayAdopt(f.result),"A display-resolution result must settle without requiring native pixels");
    const ImageRegion oldCrop{8000,6000,2000,1500,4000,3000};
    f.result.viewportPreviewGeometry=Patch(oldCrop,4000,3000);
    f.View().request.visible=oldCrop;f.View().request.visible.x+=100;
    f.View().navigationActive=true;
    Require(!f.editor.MayAdopt(f.result),"Relaxed density must never admit missing spatial coverage");
    f.View().request.empty=true;
    Require(!f.editor.MayAdopt(f.result),"An empty viewport cannot adopt a new visible frame");
}
void ForegroundAndCoverage() {
    Fixture f;
    const ImageRegion crop{8000,6000,2000,1500,4000,3000};
    f.View().request.visible=crop;f.View().geometry=Patch(crop,4000,3000);
    f.View().navigationActive=false;
    f.result.viewportPreview.generation=f.View().request.generation;
    f.result.generation=99;
    f.result.viewportPreviewGeometry=Whole(2000,1500);
    Require(!f.editor.MayAdopt(f.result),
        "A newer whole foreground raster must not replace finer pixels already covering the view");
    f.result.viewportPreviewGeometry=Whole(8000,6000);
    Require(f.editor.MayAdopt(f.result),"A larger native foreground result may improve a settled patch");
    f.result.viewportPreview.refinement=Step::NavigationCoverage;
    f.result.viewportPreview.generation=1;
    f.result.viewportPreviewGeometry=Whole(2000,1500);
    Require(f.editor.MayAdopt(f.result),
        "Same-content whole coverage must remain eligible behind a finer patch after camera movement");
    f.result.denoisePending=true;
    Require(!f.editor.MayAdopt(f.result),
        "Provisional optional coverage must not displace a settled foreground image");
    f.result.denoisePending=false;
    f.result.viewportPreviewGeometry=Patch(crop,4000,3000);
    Require(!f.editor.MayAdopt(f.result),"A partial result cannot claim whole navigation coverage");
    f.result.viewportPreviewGeometry=Whole(2000,1500);
    --f.result.viewportContentIdentity;
    Require(!f.editor.MayAdopt(f.result),"An old edit cannot provide current navigation coverage");
}
void OwnershipAndGeneration() {
    Fixture f;
    f.result.viewportPreviewGeometry=Whole(2000,1500);
    f.result.viewportViewId=2;
    Require(!f.editor.MayAdopt(f.result),"Another view cannot replace the active view's main image");
    f.result.viewportViewId=1;
    ++f.result.graphRequest.sourceIdentity;
    Require(!f.editor.MayAdopt(f.result),"Another source cannot publish even with matching camera geometry");
    f.result.graphRequest=f.editor.tag;++f.result.graphRequest.outputNodeId;
    Require(!f.editor.MayAdopt(f.result),"Another graph output cannot publish into this view");
    f.result.graphRequest=f.editor.tag;
    f.result.generation=f.editor.m_GraphAcceptedResultGeneration;
    Require(!f.editor.MayAdopt(f.result),"A previously accepted generation cannot roll presentation backward");
    f.result.generation=11;f.result.viewportContentIdentity=999;
    Require(!f.editor.MayAdopt(f.result),"Old-content screen refinement cannot publish after a new edit");
    f.result.viewportContentIdentity=f.View().contentIdentity;f.editor.usesPreview=false;
    Require(!f.editor.MayAdopt(f.result),"An inactive preview owner cannot adopt a viewport result");
    f.editor.usesPreview=true;f.result.viewportPreviewGeometry={};
    Require(!f.editor.MayAdopt(f.result),"Successful packets require valid result geometry");
}
void NativeStoreOwnership() {
    Fixture f;
    f.result.viewportPreview.refinement=Step::NativeSection;
    f.result.viewportPreviewGeometry=Patch({8000,6000,1024,1024,1024,1024},8000,6000);
    f.result.viewportSections=f.View().sections;
    Require(f.editor.MayAdopt(f.result),"Immutable native sections survive camera movement within their owner");
    const auto acceptedStore=f.result.viewportSections;
    f.result.viewportSections=std::make_shared<const int>(1);
    Require(!f.editor.MayAdopt(f.result),"A replaced native store cannot publish through a recycled identity value");
    f.result.viewportSections=acceptedStore;--f.result.viewportContentIdentity;
    Require(!f.editor.MayAdopt(f.result),"Native pixels from an old edit cannot enter the current store");
    f.result.viewportContentIdentity=f.View().contentIdentity;f.result.viewportViewId=2;
    Require(!f.editor.MayAdopt(f.result),"A native store cannot cross view ownership");
    f.result.viewportSections=f.editor.m_ViewportPreviewStates[1].sections;
    Require(f.editor.MayAdopt(f.result),"An inactive view may retain its own immutable native completion");
    f.result.viewportViewId=3;
    Require(!f.editor.MayAdopt(f.result),"An invalid view index must not access or replace another store");
}
void AuxiliaryIdentity() {
    Fixture f;
    f.result.rawRenderPurpose=RawRenderPurpose::AnalysisScopes;
    Require(f.editor.MayAdopt(f.result),"Current auxiliary input analysis must remain eligible independently of camera density");
    --f.result.graphRequest.revision;
    Require(!f.editor.MayAdopt(f.result),"Input analysis must describe the exact authored revision");
    f.result.graphRequest=f.editor.tag;++f.result.graphRequest.sourceIdentity;
    Require(!f.editor.MayAdopt(f.result),"Analysis cannot cross source identity");
    f.result.graphRequest=f.editor.tag;--f.result.viewportContentIdentity;
    Require(!f.editor.MayAdopt(f.result),"Analysis cannot reuse an old content owner");
    f.result.viewportContentIdentity=f.View().contentIdentity;f.result.viewportViewId=2;
    Require(!f.editor.MayAdopt(f.result),"Analysis cannot cross the active viewport owner");
}
int main() try {
    MovingScreen();ForegroundAndCoverage();OwnershipAndGeneration();NativeStoreOwnership();AuxiliaryIdentity();
    std::cout<<"Production viewport-result adoption, detail and ownership checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr<<error.what()<<'\n';return 1;
}
'''

args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(shell + method(viewport, 'bool EditorModule::IsViewportPreviewResultCurrent(') +
    method(graph, 'bool EditorModule::IsCurrentGraphResult(') + checks)
