#pragma once
#include "Editor/EditorModule.h"
#include "Editor/RawRenderService.h"
#include "Editor/Bracketing/BracketingSession.h"
#include <chrono>
#include <thread>
#include <iostream>

struct BracketingRenderValidationAccess {
    static void SelectSuperResolution(EditorModule& e,unsigned scale=2) {
        e.TickBracketing();
        if(!e.m_Bracketing)throw std::runtime_error("No bracket session for reconstruction selection");
        e.m_Bracketing->recipe.reconstruction=scale==2?Raw::Bracketing::ReconstructionMode::SuperResolution2x:Raw::Bracketing::ReconstructionMode::SuperResolution1x;
        ImGui::NewFrame();e.CommitBracketingEdit();ImGui::EndFrame();
    }
    static void Run(EditorModule& e, GLFWwindow* window) {
        e.m_Pipeline.Initialize();
        if (!e.m_RawRenderClientId)
            e.m_RawRenderClientId=Stack::EditorRendering::RawRenderService::Get().Acquire(window);
        if(!e.m_RawRenderClientId) throw std::runtime_error("RAW render service unavailable");
        e.m_RawWorkspaceRootTabActive=true;e.m_Project->rawPipelineActive=true;
        e.NoteRawWorkspaceProjectOpenPreview();e.MarkRenderRefreshDirty();
        WaitForIdle(e);
        const auto mergeRevision = e.m_Project->snapshot->hdrInputRevision;
        const auto mergedPixels = e.m_HdrAdoptedRawResult->rawData;
        const auto initialPreview = e.ResolveRawWorkspacePreviewContext(nullptr);
        if (!initialPreview.projectActive || !initialPreview.edit.multiFrameResult ||
            initialPreview.identity != e.GetActiveRawWorkspacePreviewIdentity())
            throw std::runtime_error("Bracket result has no source-independent RAW preview context");
        const auto renderRevision = e.m_RenderRevision;
        ImGui::NewFrame(); e.CommitBracketingEdit(); ImGui::EndFrame();
        if (e.m_Project->snapshot->hdrInputRevision != mergeRevision || e.m_Bracketing->pending ||
            e.m_RenderRevision != renderRevision)
            throw std::runtime_error("Unchanged bracket recipe scheduled reconstruction");
        for(unsigned control=0;control<2;++control) {
            EditorModule::RawWorkspaceEditContext context;
            if(!e.BeginRawWorkspaceEditContext(nullptr,context)||!context.canEdit)
                throw std::runtime_error("Bracket RAW controls are unavailable");
            if(control==0) {
                if(e.m_HdrAdoptedRawResult&&e.m_HdrAdoptedRawResult->rawData->reconstructedCameraRgb) {
                    context.recipe.rgbDenoise.enabled=true;
                    context.recipe.rgbDenoise.lumaMap.baseMultiplier=.3f;
                } else {
                    context.recipe.technical.mosaicDenoise.enabled=true;
                    context.recipe.technical.mosaicDenoise.lumaStrength=.3f;
                }
            } else {
                e.m_RawWorkspaceLabUi.activeTool=EditorModule::RawLabTool::Color;
                context.recipe.colorWarp.enabled=true;
                Stack::RawRecipe::RawColorWarpPin pin;pin.id="bracket-settle-test";pin.targetA=.025f;
                context.recipe.colorWarp.pins={pin};
            }
            ImGui::NewFrame();e.CommitRawWorkspaceEditContext(context,true,true);ImGui::EndFrame();
            ImGui::NewFrame();e.CommitRawWorkspaceEditContext(context,false,false);ImGui::EndFrame();
            if (e.m_RawWorkspaceAdaptiveGestureActive)
                throw std::runtime_error("Bracket RAW gesture did not end on unchanged release");
            WaitForIdle(e);
            if (e.m_Project->snapshot->hdrInputRevision != mergeRevision ||
                e.m_HdrAdoptedRawResult->rawData != mergedPixels || e.m_Bracketing->job)
                throw std::runtime_error("RAW editing restarted bracket reconstruction");
        }
        if (!e.m_RawWorkspaceColorWarpInputGraphScopeCache.readback.valid ||
            e.m_RawWorkspaceColorWarpInputGraphScopeCache.readback.stage != RawDevelopmentGraphScopeStage::ColorWarpInput)
            throw std::runtime_error("Bracket Color Warp did not receive its RAW input analysis");
        e.m_RawWorkspaceLabUi.previewZoom=e.m_RawWorkspaceLabUi.previewZoomTarget=2.f;
        WaitForIdle(e);
        // A connected Graph output renders its complete downstream image.
        // Only the direct RAW-stage path issues regional render requests.
        if ((e.UsesRawWorkspaceStageRender() && !e.m_RawViewportRequest.visible.Partial()) ||
            e.m_RawWorkspaceLabUi.previewZoom != 2.f)
            throw std::runtime_error("Bracket RAW viewport did not preserve zoom and request visible detail");
        e.m_RawWorkspaceLabUi.previewZoom=e.m_RawWorkspaceLabUi.previewZoomTarget=1.f;
        WaitForIdle(e);
        std::cout<<"Shared bracket RAW viewport, Color Warp analysis, gesture release and downstream-only edits passed.\n";
    }
    static void WaitForIdle(EditorModule& e) {
        // Full native denoise plus the optional four-pass viewport calibration can exceed 15 seconds.
        // This watchdog checks eventual idle, not the separate proxy-latency contract.
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
        auto nextStatus=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        unsigned idle=0;
        while(std::chrono::steady_clock::now()<deadline) {
            ImGui::GetIO().DeltaTime=1.f/60;ImGui::NewFrame();
            e.TickBracketing();e.ConsumeRenderWorkerResults();
            ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({640,480});
            ImGui::Begin("Shared RAW viewport validation",nullptr,
                ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize);
            ImVec2 imageMinimum, imageMaximum;
            e.RenderRawWorkspacePreviewCanvas(nullptr, false, &imageMinimum, &imageMaximum);
            if (!e.UsesRawWorkspaceStageRender() && e.m_RawWorkspacePresentationTexture.height > 0 &&
                imageMaximum.y > imageMinimum.y && e.HasRawWorkspaceLivePreviewForSource(e.GetActiveRawWorkspacePreviewIdentity())) {
                const float expectedAspect = float(e.m_RawWorkspacePresentationTexture.width) /
                    e.m_RawWorkspacePresentationTexture.height;
                const float shownAspect = (imageMaximum.x-imageMinimum.x)/(imageMaximum.y-imageMinimum.y);
                if (std::abs(expectedAspect-shownAspect) > .001f)
                    throw std::runtime_error("RAW viewport stretched the downstream Graph output");
            }
            ImGui::End();
            e.SubmitRenderIfReady();e.UpdateBracketingPresentation();
            if(std::chrono::steady_clock::now()>=nextStatus) {
                std::cout<<"RAW settle progress: "<<e.GetFullQualityRenderDiagnostic()<<'\n'<<std::flush;
                nextStatus=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            }
            ImGui::EndFrame();
            if(!e.IsEditorRenderBusy()&&!e.m_RenderDirty&&
                (!e.m_Bracketing||(!e.m_Bracketing->job&&!e.m_Bracketing->pending&&!e.m_Bracketing->publishRequested))&&
                !e.m_RawWorkspaceAnalysisPending&&!e.m_RawWorkspaceAnalysisRequested&&
                !e.m_RawWorkspaceFullResolutionPreviewPending&&
                e.HasRawWorkspaceLivePreviewForSource(e.GetActiveRawWorkspacePreviewIdentity())) {
                if(++idle==60) {
                    if(e.UsesRawWorkspaceStageRender() &&
                        (e.m_RawRenderSessionFullFrameWidth<=0||e.m_RawRenderSessionFullFrameHeight<=0||
                         !e.m_ViewportOutputNativeExtentVerified))
                        throw std::runtime_error("Bracket RAW has no verified native extent");
                    EditorRenderWorker::Result obsolete;
                    obsolete.generation=0;obsolete.success=true;
                    obsolete.rawWorkspace.sourceKey=e.GetActiveRawWorkspacePreviewIdentity();
                    obsolete.rawRenderPurpose=RawRenderPurpose::InteractivePresentation;
                    e.m_DeferredRenderResults.push_back(std::move(obsolete));e.m_RenderPending=true;
                    e.ConsumeRenderWorkerResults();
                    if(e.IsEditorRenderBusy()) throw std::runtime_error("Obsolete RAW completion left editor busy");
                    std::cout<<"Bracket RAW editor settled and released obsolete work; RAW stage native extent verified where applicable.\n";
                    return;
                }
            } else idle=0;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        std::cerr<<e.GetFullQualityRenderDiagnostic()<<"\n";
        throw std::runtime_error("Bracket RAW editor did not settle");
    }
};
