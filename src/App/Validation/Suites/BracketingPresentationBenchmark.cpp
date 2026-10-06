#include "Raw/Bracketing/Processor.h"
#include "BracketingDiagnosticInputs.h"
#include "Editor/Bracketing/ProcessingPresentation.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include "Renderer/GLLoader.h"
#include <GLFW/glfw3.h>
#include <future>
#include <fstream>
#include <chrono>
#include <iostream>
#include <thread>

namespace Stack::Validation {
bool BenchmarkBracketingPresentation(const std::filesystem::path& source,const std::filesystem::path& directory) {
    using namespace Raw::Bracketing;using Clock=std::chrono::steady_clock;
    if(!glfwInit())return false;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto* window=glfwCreateWindow(1280,720,"Processing scene benchmark",nullptr,nullptr);
    if(!window){glfwTerminate();return false;}glfwMakeContextCurrent(window);
    if(!LoadGLFunctions()){glfwDestroyWindow(window);glfwTerminate();return false;}
    bool passed=false;
    {
        Editor::ProcessingSceneRenderer renderer;
        try {
            std::filesystem::create_directories(directory);
            ProcessingRequest request;std::string error;
            if(!LoadBracketingDiagnosticFolder(source,request,error))throw std::runtime_error(error);
            const auto budget=Raw::Mfd::ResolveMfdProcessingMemoryBudget(0,Raw::Mfd::QueryPhysicalMemorySnapshot());
            if(!budget.valid)throw std::runtime_error(budget.message);
            request.memoryBudgetBytes=budget.budgetBytes;request.automaticMemoryBudget=true;
            request.workerCount=ResolveBracketingWorkerCount(request.memoryBudgetBytes);
            request.cacheDirectory=directory/"prepared";request.removeCacheOnRelease=true;
            // Measure the common temporal/HDR blend independently of registration variability.
            request.recipe.alignmentMode=AlignmentMode::FixedCoordinates;
            auto mailbox=std::make_shared<Editor::ProcessingMailbox>(1);
            request.reportPresentation=[mailbox](const ProcessingProgress& event){mailbox->Publish(event);};
            auto prepared=Process(request);if(prepared.status!=BracketingResult::Status::Completed)throw std::runtime_error(prepared.message);
            request.analysis=prepared.analysis;
            std::vector<Editor::ProcessingCard> cards;
            for(std::size_t g=0;g<request.recipe.groups.size();++g)for(const auto& frame:request.recipe.groups[g].frames)
                cards.push_back({frame.id,frame.id,unsigned(g),{.4f,.7f,.75f,1}});
            auto clockStart=Clock::now();
            const auto now=[&]{return std::chrono::duration<double>(Clock::now()-clockStart).count();};
            renderer.Draw(*mailbox->Read(),cards,{1920,1080},{.1f,.09f,.08f,1},now(),false);glFinish();
            std::vector<double> baseline,animated;bool identical=true;
            for(unsigned pair=0;pair<5;++pair)for(unsigned side=0;side<2;++side) {
                const bool show=bool((pair+side)%2);
                auto run=request;if(!show)run.reportPresentation={};
                const auto started=Clock::now();
                auto future=std::async(std::launch::async,[run]{return Process(run);});
                while(future.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) {
                    if(show)renderer.Draw(*mailbox->Read(),cards,{1920,1080},{.1f,.09f,.08f,1},now(),false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                auto result=future.get();
                const double elapsed=std::chrono::duration<double>(Clock::now()-started).count();
                if(result.status!=BracketingResult::Status::Completed)throw std::runtime_error(result.message);
                identical&=*result.raw->normalizedMosaicBuffer==*prepared.raw->normalizedMosaicBuffer;
                (show?animated:baseline).push_back(elapsed);
                std::cout<<(show?"Animated":"Baseline")<<" blend "<<elapsed<<" s\n"<<std::flush;
            }
            auto median=[](std::vector<double> values){std::sort(values.begin(),values.end());return values[values.size()/2];};
            const double base=median(baseline),scene=median(animated),overhead=scene/base-1;
            passed=identical&&overhead<=.05&&renderer.TextureBytes()<64ull*1024*1024;
            nlohmann::json report={{"scope","Cached full-resolution Standard blend; CPU processor, live OpenGL scene. Alignment/decode excluded."},
                {"captures",request.sources.size()},{"baselineSeconds",baseline},{"animatedSeconds",animated},
                {"baselineMedian",base},{"animatedMedian",scene},{"overheadPercent",overhead*100},
                {"identicalPixels",identical},{"sceneBytes",renderer.TextureBytes()},{"passed",passed}};
            std::ofstream(directory/"benchmark.json")<<report.dump(2);
            std::cout<<report.dump(2)<<'\n';
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';}
    }
    glfwDestroyWindow(window);glfwTerminate();return passed;
}
}
