#include "App/Validation/Suites/RawViewportValidationFixture.h"
#include "Editor/EditorRenderWorker.h"
#include "Renderer/RawDevelopmentStageCachePolicy.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace Stack::Validation {
bool ValidateRawZoneAreaWorker(GLFWwindow* sharedWindow) {
    EditorRenderWorker worker;
    if(!worker.Initialize(sharedWindow)) return false;
    bool ok=false;
    try {
        auto require=[](bool condition,const char* message){if(!condition) throw std::runtime_error(message);};
        EditorRenderWorker::Snapshot snapshot;
        snapshot.width=128;snapshot.height=96;snapshot.channels=4;snapshot.outputConnected=true;
        snapshot.rawWorkspace.sourceKey="zone-worker";snapshot.rawWorkspace.fullFrameWidth=128;snapshot.rawWorkspace.fullFrameHeight=96;
        snapshot.rawWorkspace.hasRecipe=true;
        snapshot.rawWorkspace.graphScopeStage=RawDevelopmentGraphScopeStage::LocalRangeInput;snapshot.rawWorkspace.graphScopeMaxDimension=64;
        auto& recipe=snapshot.rawWorkspace.recipe;recipe=RawRecipe::MakeDefaultRecipe("viewport-region-validation");
        recipe.rgbDenoise.enabled=false;recipe.localRange.enabled=true;
        RawRecipe::RawZoneArea area;area.id="worker-paint";area.sourceAspect=128.0f/96;
        RawRecipe::RawZoneBrushStroke stroke;stroke.radius=.2f;stroke.path={{.3f,.4f}};area.strokes={stroke};recipe.localRange.areas={area};
        RenderGraphNode source;source.nodeId=1;source.kind=RenderGraphNodeKind::RawDevelopment;
        source.rawDevelopment.embeddedRawData=MakeViewportValidationRaw(128,96);snapshot.graph.nodes.push_back(source);
        RenderGraphNode output;output.nodeId=2;output.kind=RenderGraphNodeKind::Output;snapshot.graph.nodes.push_back(output);
        snapshot.graph.links.push_back({1,"imageOut",2,"imageIn"});snapshot.graph.outputNodeId=2;snapshot.graph.outputSocketId="imageOut";
        for(int pass=0;pass<3;++pass) {
            snapshot.generation=pass+1;snapshot.previewMaxDimension=pass==0?64:0;
            snapshot.rawWorkspace.analysisRequested=pass!=0;
            snapshot.rawRenderPurpose=pass==0?RawRenderPurpose::InteractivePresentation:RawRenderPurpose::AnalysisScopes;
            snapshot.telemetry.interactionActive=pass==0;
            if(pass==2) recipe.localRange.areas[0].strokes[0].path={{.7f,.6f}};
            snapshot.graph.nodes[0].rawDevelopment.recipe=recipe;
            snapshot.rawWorkspace.graphScopeInputFingerprint=Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe,64).rawPlacement;
            require(worker.Submit(snapshot),"Area worker request rejected");
            EditorRenderWorker::Result result;
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
            bool ready=false;
            while(std::chrono::steady_clock::now()<deadline) {
                if(worker.TryConsumeCompleted(result)) {ready=true;break;}
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            require(ready && result.success,"Area worker render did not complete");
            const auto& scope=result.rawWorkspace.graphScopeReadback;
            require(scope.valid && scope.zoneAreas.size()==1 && scope.zoneAreas[0].valid,"Worker dropped area statistics");
            require(scope.zoneAreas[0].fullResolution==(pass!=0),"Worker mislabeled preview/full statistics");
            require(scope.zoneAreas[0].maskFingerprint==RawRecipe::ZoneAreaMaskFingerprint(recipe.localRange.areas[0]),"Worker returned stale mask statistics");
            require(result.rawWorkspace.analysisCaptured==(pass!=0),"Preview statistics started a full analysis request");
            result.outputTexture.Reset();result.rawWorkspace.localRangeOverlayTexture.Reset();
        }
        // Keep authoring while the render worker coalesces older snapshots.
        snapshot.previewMaxDimension=64;snapshot.rawWorkspace.analysisRequested=false;
        snapshot.rawRenderPurpose=RawRenderPurpose::InteractivePresentation;snapshot.telemetry.interactionActive=true;
        recipe.localRange.areas[0].strokes[0].followEdges=true;
        for (int sample=0;sample<30;++sample) {
            recipe.localRange.areas[0].strokes[0].path.push_back({.7f-float(sample)*.005f,.6f});
            snapshot.generation=4+sample;
            snapshot.graph.nodes[0].rawDevelopment.recipe=recipe;
            snapshot.rawWorkspace.graphScopeInputFingerprint=Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe,64).rawPlacement;
            require(worker.Submit(snapshot),"Continuous guided stroke request rejected");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        bool latest=false;EditorRenderWorker::Result result;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while (std::chrono::steady_clock::now()<deadline) {
            if (worker.TryConsumeCompleted(result) && result.generation==snapshot.generation) {latest=true;break;}
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        require(latest && result.success,"Continuous guided rendering did not converge to last mouse sample");
        const auto& scope=result.rawWorkspace.graphScopeReadback;
        require(scope.zoneGuide && scope.zoneGuide->Valid(),"Guided render did not publish its neutral image guide");
        require(scope.zoneAreas.size()==1 && scope.zoneAreas[0].maskPreview &&
            scope.zoneAreas[0].maskFingerprint==RawRecipe::ZoneAreaMaskFingerprint(recipe.localRange.areas[0]),
            "Worker published coverage from a superseded stroke");
        result.outputTexture.Reset();result.rawWorkspace.localRangeOverlayTexture.Reset();
        ok=true;
    } catch(const std::exception& error) {std::cerr<<"Zones worker validation failed: "<<error.what()<<'\n';}
    worker.Shutdown();
    if(ok) std::cout<<"Zones preview and asynchronous full-resolution worker validation passed.\n";
    return ok;
}
}
