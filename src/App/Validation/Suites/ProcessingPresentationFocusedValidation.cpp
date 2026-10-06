#include "Raw/Bracketing/Processor.h"
#include "BracketingDiagnosticInputs.h"
#include "BracketingPresentationValidation.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include "App/settings/AppearanceTheme.h"
#include <future>
#include <fstream>
#include <thread>

namespace Stack::Validation {
bool ValidateProcessingPresentation(const std::filesystem::path& source,const std::filesystem::path& directory) {
    using namespace Raw::Bracketing;using Clock=std::chrono::steady_clock;
    bool success=false;
    if(!glfwInit())return false;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto* window=glfwCreateWindow(1280,720,"Processing evidence validation",nullptr,nullptr);
    if(!window){glfwTerminate();return false;}
    glfwMakeContextCurrent(window);
    if(!LoadGLFunctions()){glfwDestroyWindow(window);glfwTerminate();return false;}
    ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;
    StackAppearance::AppearanceManager appearance;appearance.SetupFonts(ImGui::GetIO());
    ImGui_ImplGlfw_InitForOpenGL(window,false);ImGui_ImplOpenGL3_Init("#version 430");
    try {
        std::filesystem::create_directories(directory);
        ProcessingRequest request;std::string error;
        if(!LoadBracketingDiagnosticFolder(source,request,error))throw std::runtime_error(error);
        // Anchor this observer comparison to recorded camera exposure. The test
        // checks image invariance, not whether disjoint HDR histograms can solve
        // their exposure ratio automatically.
        std::map<std::string,double> exposure;
        for(const auto& capture:request.sources) {
            Raw::RawMetadata metadata;
            if(!Raw::RawLoader::LoadMetadata(capture.path.string(),metadata)||metadata.exposureTimeSeconds<=0||metadata.isoSpeed<=0)
                throw std::runtime_error("The presentation fixture needs recorded shutter and ISO values.");
            exposure[capture.frameId]=double(metadata.exposureTimeSeconds)*metadata.isoSpeed;
        }
        for(auto& group:request.recipe.groups)for(auto& frame:group.frames) {
            frame.manualExposure=true;frame.relativeEv=std::log2(exposure.at(frame.id)/exposure.at(request.recipe.originFrameId));
        }
        const auto budget=Raw::Mfd::ResolveMfdProcessingMemoryBudget(0,Raw::Mfd::QueryPhysicalMemorySnapshot());
        if(!budget.valid)throw std::runtime_error(budget.message);
        request.memoryBudgetBytes=budget.budgetBytes;request.automaticMemoryBudget=true;
        request.workerCount=ResolveBracketingWorkerCount(request.memoryBudgetBytes);
        request.recipe.alignmentMode=AlignmentMode::AutomaticLocal;
        request.removeCacheOnRelease=true;request.cacheDirectory=directory/"baseline-cache";
        auto start=Clock::now();
        std::cout<<"Baseline real bracket, including local alignment\n"<<std::flush;
        auto baselineFuture=std::async(std::launch::async,[request]{return Process(request);});
        while(baselineFuture.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        auto baseline=baselineFuture.get();
        const auto baseSeconds=std::chrono::duration<double>(Clock::now()-start).count();
        if(baseline.status!=BracketingResult::Status::Completed)throw std::runtime_error(baseline.message);
        auto mailbox=std::make_shared<Editor::ProcessingMailbox>(1,request.recipe.originFrameId);
        std::vector<Editor::ProcessingCard> cards;
        for(std::size_t g=0;g<request.recipe.groups.size();++g)for(const auto& frame:request.recipe.groups[g].frames)
            cards.push_back({frame.id,"Capture "+std::to_string(cards.size()+1),unsigned(g),{.6f,.75f,.8f,1}});
        std::stable_sort(cards.begin(),cards.end(),[&](const auto& a,const auto& b){return a.id==request.recipe.originFrameId&&b.id!=request.recipe.originFrameId;});
        request.cacheDirectory=directory/"observed-cache";
        request.reportPresentation=[mailbox](const ProcessingProgress& event){mailbox->Publish(event);};
        Editor::ProcessingSceneRenderer renderer;
        start=Clock::now();auto future=std::async(std::launch::async,[request]{return Process(request);});
        std::cout<<"Observed real bracket with native OpenGL scene\n"<<std::flush;
        while(future.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) {
            if(auto snapshot=mailbox->Read())renderer.Draw(*snapshot,cards,{1280,720},{0,0,0,1},std::chrono::duration<double>(Clock::now()-start).count(),false);
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        auto observed=future.get();const auto observedSeconds=std::chrono::duration<double>(Clock::now()-start).count();
        if(observed.status!=BracketingResult::Status::Completed)throw std::runtime_error(observed.message);
        bool identical=*baseline.raw->normalizedMosaicBuffer==*observed.raw->normalizedMosaicBuffer;
        const auto& a=*baseline.raw->multiFrameMeasurementSidecars;const auto& b=*observed.raw->multiFrameMeasurementSidecars;
        identical&=*a.variance==*b.variance&&*a.fusionUncertaintyVariance==*b.fusionUncertaintyVariance&&
            *a.effectiveSupport==*b.effectiveSupport&&*a.validity==*b.validity&&*a.clipping==*b.clipping&&
            *a.localRejection==*b.localRejection&&*a.fallbackReason==*b.fallbackReason;
        const auto snapshot=mailbox->Read();
        Editor::ProcessingSnapshot finishing;
        auto pendingMask=std::make_shared<ProcessingEvidence>();pendingMask->provisional=true;pendingMask->rasters.resize(2);
        finishing.progress.stage=ProcessingStage::LocalAlignment;finishing.progress.occurrence=1;finishing.evidence=pendingMask;
        Editor::ProcessingDirector finishDirector;finishDirector.Select(finishing,0,false);finishDirector.Select(finishing,1,false);
        auto completeMask=std::make_shared<ProcessingEvidence>(*pendingMask);completeMask->provisional=false;
        finishing.milestones.push_back({finishing.progress,completeMask});
        for(const auto stage:{ProcessingStage::Exposure,ProcessingStage::Groups,ProcessingStage::Guide,ProcessingStage::Blend}) {
            finishing.progress.stage=stage;++finishing.progress.occurrence;finishing.milestones.push_back({finishing.progress,std::make_shared<ProcessingEvidence>()});
        }
        finishDirector.Ready(finishing,2,false);
        const auto firstFinish=finishDirector.Select(finishing,2,false);
        if(firstFinish.progress.stage!=ProcessingStage::LocalAlignment||firstFinish.evidence->provisional)
            throw std::runtime_error("The bounded ending omitted the final confidence masks.");
        BracketingPresentationValidationAccess::Handoff(request,observed);
        const bool visuals=BracketingPresentationValidationAccess::Run(window,directory,snapshot.get(),cards);
        const double overhead=observedSeconds/baseSeconds-1;
        success=identical&&visuals&&overhead<=.05;
        nlohmann::json report={{"baselineSeconds",baseSeconds},{"observedSeconds",observedSeconds},{"overheadPercent",overhead*100},
            {"identicalPixelsAndSidecars",identical},{"visualChecks",visuals},{"contributionHandoff",true},{"evidenceBytes",snapshot->EvidenceBytes()},
            {"baselineStages",baseline.analysis->stageSeconds},{"observedStages",observed.analysis->stageSeconds},
            {"sceneBytes",renderer.TextureBytes()},{"passed",success},{"scope","One CPU Automatic local Standard bracket with camera-recorded exposure ratios, observer off/on; three native scenes. Automatic exposure overlap, SR and render-owner GPU processing not benchmarked."}};
        std::ofstream(directory/"focused-report.json")<<report.dump(2);std::cout<<report.dump(2)<<'\n';
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';}
    ImGui_ImplOpenGL3_Shutdown();ImGui_ImplGlfw_Shutdown();ImGui::DestroyContext();glfwDestroyWindow(window);glfwTerminate();return success;
}
}
