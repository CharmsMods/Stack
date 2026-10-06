#include "Persistence/ProjectStore.h"
#include "Raw/Bracketing/Processor.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include "Renderer/GLLoader.h"
#include "ThirdParty/stb_image_write.h"
#include "BracketingInspectionOutput.h"
#include "BracketingDiagnosticInputs.h"
#include "BracketingStageInspection.h"
#include "BracketingRawHandoffValidation.h"
#include "BracketingEvidenceExport.h"
#include "BracketingQualityChecks.h"
#include "BracketingUpgradeComparison.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <chrono>

namespace Stack::Validation {
namespace {

struct HiddenOpenGlContext {
    GLFWwindow* window=nullptr;
    ~HiddenOpenGlContext() {
        if(window) glfwDestroyWindow(window);
        if(window) glfwTerminate();
    }
    bool Initialize(std::string& error) {
        if(!glfwInit()) {error="GLFW could not initialize the diagnostic GPU context.";return false;}
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        window=glfwCreateWindow(32,32,"Bracket diagnostic",nullptr,nullptr);
        if(!window) {glfwTerminate();error="OpenGL 4.3 is unavailable for the diagnostic.";return false;}
        glfwMakeContextCurrent(window);
        if(!LoadGLFunctions()) {error="The diagnostic could not load OpenGL functions.";return false;}
        return true;
    }
};

double Quantile(const std::vector<float>& sorted,double fraction) {
    if(sorted.empty()) return 0;
    const auto index=static_cast<std::size_t>(std::clamp(fraction,0.0,1.0)*(sorted.size()-1));
    return sorted[index];
}

const Project::MultiFrameSourceSet* FindBracketSet(const Project::RawProjectSnapshot& snapshot) {
    const auto active=std::find_if(snapshot.sourceSets.begin(),snapshot.sourceSets.end(),
        [&](const auto& set){return set.sourceSetId==snapshot.activeSourceSetId&&set.settings.contains("bracketing");});
    if(active!=snapshot.sourceSets.end()) return &*active;
    const auto any=std::find_if(snapshot.sourceSets.begin(),snapshot.sourceSets.end(),
        [](const auto& set){return set.settings.contains("bracketing");});
    return any==snapshot.sourceSets.end()?nullptr:&*any;
}

bool WriteSupportImage(const Raw::RawImageData& raw,const std::filesystem::path& path,
    std::size_t enabledFrames) {
    if(!raw.multiFrameMeasurementSidecars||
       !raw.multiFrameMeasurementSidecars->effectiveSupport||
       !raw.multiFrameMeasurementSidecars->localRejection) return false;
    const auto& support=*raw.multiFrameMeasurementSidecars->effectiveSupport;
    const auto& rejected=*raw.multiFrameMeasurementSidecars->localRejection;
    const auto width=static_cast<unsigned>(raw.metadata.visibleWidth)/2;
    const auto height=static_cast<unsigned>(raw.metadata.visibleHeight)/2;
    if(!width||!height) return false;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width)*height*3);
    const double denominator=std::max(1.0,static_cast<double>(enabledFrames)-1.0);
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
        double frameSupport=0,rejection=0;
        for(unsigned dy=0;dy<2;++dy) for(unsigned dx=0;dx<2;++dx) {
            const auto source=(static_cast<std::size_t>(y)*2+dy)*raw.metadata.visibleWidth+x*2+dx;
            frameSupport+=support[source]*.25;rejection+=rejected[source]?0.25:0;
        }
        const double accepted=std::clamp((frameSupport-1.0)/denominator,0.0,1.0);
        const auto destination=(static_cast<std::size_t>(y)*width+x)*3;
        pixels[destination]=static_cast<unsigned char>(255*std::clamp(rejection,0.0,1.0));
        pixels[destination+1]=static_cast<unsigned char>(255*accepted);
        pixels[destination+2]=static_cast<unsigned char>(255*(1.0-rejection)*accepted);
    }
    return stbi_write_png(path.string().c_str(),width,height,3,pixels.data(),width*3)!=0;
}

} // namespace

bool DiagnoseBracketingProject(int argc,char** argv) {
    if(argc<2) return false;
    try {
        const std::filesystem::path projectPath=argv[0];
        const std::filesystem::path output=argv[1];
        std::filesystem::create_directories(output);
        Raw::Bracketing::ProcessingRequest request;std::string error;
        if(std::filesystem::is_directory(projectPath)&&!std::filesystem::exists(projectPath/"project.stack")) {
            if(!LoadBracketingDiagnosticFolder(projectPath,request,error)) throw std::runtime_error(error);
        } else {
        const auto opened=Project::OpenProjectStore(projectPath);
        if(!opened) throw std::runtime_error(opened.message);
        const auto* set=FindBracketSet(opened.snapshot);
        if(!set) throw std::runtime_error("The project has no Bracketing source set.");

        if(!Raw::Bracketing::Deserialize(set->settings.at("bracketing"),request.recipe,error))
            throw std::runtime_error(error);
        const auto root=Project::ResolveProjectStoreRoot(projectPath);
        const auto extraction=output/"portable-assets";
        for(std::size_t i=0;i<set->frames.size();++i) {
            const auto& frame=set->frames[i];
            const auto* asset=Project::FindEmbeddedAsset(opened.snapshot,frame.assetId);
            if(!asset) throw std::runtime_error("A bracket frame has no project-owned asset.");
            auto sourcePath=root/std::filesystem::u8path(asset->projectAssetPath);
            std::error_code pathError;
            if(!std::filesystem::is_regular_file(sourcePath,pathError)||pathError) {
                std::filesystem::create_directories(extraction);
                sourcePath=extraction/(std::to_string(i+1)+"-"+asset->originalFilename);
                if(!opened.store->CopyAssetToFile(asset->assetId,sourcePath,&error))
                    throw std::runtime_error(error);
            }
            request.sources.push_back({frame.frameId,asset->sha256,asset->byteLength,sourcePath,asset->originalFilename});
        }
        }
        const auto memory=Raw::Mfd::ResolveMfdProcessingMemoryBudget(
            0,Raw::Mfd::QueryPhysicalMemorySnapshot());
        if(!memory.valid) throw std::runtime_error(memory.message);
        request.memoryBudgetBytes=memory.budgetBytes;
        request.automaticMemoryBudget=true;
        request.workerCount=Raw::Bracketing::ResolveBracketingWorkerCount(request.memoryBudgetBytes);
        HiddenOpenGlContext openGl;
        bool useGpu=false,checkReuse=false,inspectStages=false,exportEvidence=false,inspectQuality=false;
        bool compareUpgrade=false;
        std::vector<std::array<unsigned,2>> requestedRegions;
        for(int i=2;i<argc;++i) {
            const std::string option=argv[i];
            if(option=="--gpu") useGpu=true;
            else if(option=="--check-reuse") checkReuse=true;
            else if(option=="--inspect-stages") inspectStages=true;
            else if(option=="--export-evidence") exportEvidence=true;
            else if(option=="--inspect-quality") inspectQuality=true;
            else if(option=="--compare-upgrade") compareUpgrade=true;
            else if(option=="--standard") request.recipe.reconstruction=Raw::Bracketing::ReconstructionMode::Standard;
            else if(option.rfind("--region=",0)==0) {
                const auto separator=option.find(',',9);
                if(separator==std::string::npos) throw std::runtime_error("A diagnostic region requires x,y sensor coordinates.");
                requestedRegions.push_back({static_cast<unsigned>(std::stoul(option.substr(9,separator-9))),
                    static_cast<unsigned>(std::stoul(option.substr(separator+1)))});
            }
            else if(option=="--fixed") request.recipe.alignmentMode=Raw::Bracketing::AlignmentMode::FixedCoordinates;
            else if(option=="--global") request.recipe.alignmentMode=Raw::Bracketing::AlignmentMode::AutomaticGlobal;
            else if(option=="--local") request.recipe.alignmentMode=Raw::Bracketing::AlignmentMode::AutomaticLocal;
            else throw std::runtime_error("Unknown bracket diagnostic option: "+option);
        }
        request.preferGpuRegistration=useGpu;
        if(request.recipe.reconstruction!=Raw::Bracketing::ReconstructionMode::Standard&&
            (inspectStages||compareUpgrade))
            throw std::runtime_error("Temporal mosaic inspection requires --standard. RGB reconstruction supports native result crops.");
        if(useGpu||inspectStages) {
            if(!openGl.Initialize(error)) throw std::runtime_error(error);
            request.executeOpenGlTask=[](Raw::OpenGlTask task,std::string& taskError) {
                return task&&task(taskError);
            };
        }
        request.cacheDirectory=output/"cache";
        request.removeCacheOnRelease=true;
        int lastProgress=-1;
        request.reportProgress=[&](double progress,const std::string& stage) {
            const auto percent=static_cast<int>(std::clamp(progress,0.0,1.0)*100);
            if(percent/5!=lastProgress/5) {
                lastProgress=percent;
                std::cout<<"Project diagnostic: "<<percent<<"% "<<stage<<'\n'<<std::flush;
                std::ofstream(output/"progress.txt")<<percent<<"% "<<stage<<'\n';
            }
        };
        const auto start=std::chrono::steady_clock::now();
        const auto result=Raw::Bracketing::Process(request);
        if(result.status!=Raw::Bracketing::BracketingResult::Status::Completed) {
            if(result.analysis) std::ofstream(output/"failed-analysis.json")<<
                nlohmann::json({{"recipe",Raw::Bracketing::Serialize(request.recipe)},
                    {"diagnostics",result.analysis->diagnostics}}).dump(2);
            throw std::runtime_error(result.message);
        }

        nlohmann::json report;
        report["processingSeconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        report["stageSeconds"]=result.analysis->stageSeconds;
        if(checkReuse) {
            auto cachedRequest=request;cachedRequest.analysis=result.analysis;
            const auto reuseStart=std::chrono::steady_clock::now();
            const auto reused=Raw::Bracketing::Process(cachedRequest);
            if(reused.status!=Raw::Bracketing::BracketingResult::Status::Completed||
                reused.analysis->prepared!=result.analysis->prepared||
                *reused.raw->normalizedMosaicBuffer!=*result.raw->normalizedMosaicBuffer)
                throw std::runtime_error("Cached group re-evaluation changed the result.");
            report["cachedReevaluationSeconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-reuseStart).count();
        }
        report["recipeIdentity"]=Raw::Bracketing::Identity(request.recipe);
        report["analysisIdentity"]=result.analysis->identity;
        report["alignmentMode"]=Raw::Bracketing::AlignmentModeName(request.recipe.alignmentMode);
        report["sourceCount"]=request.sources.size();
        report["workerCount"]=request.workerCount;
        report["backendPreference"]=useGpu?"gpu-with-cpu-fallback":"cpu-reference";
        report["estimatedPeakResidentBytes"]=result.analysis->estimatedPeakResidentBytes;
        report["fallbackSamples"]=result.fallbackSamples;
        report["unrecoverableSamples"]=result.unrecoverableSamples;
        report["invalidSamples"]=result.invalidSamples;
        report["diagnostics"]=result.analysis->diagnostics;
        if(inspectQuality)WriteBracketQualityChecks(request,result,output,report);
        for(const auto& [frameId,alignment]:result.analysis->alignments) {
            report["alignment"].push_back({{"frameId",frameId},
                {"translationX",alignment.translationX},{"translationY",alignment.translationY},
                {"rotationDegrees",alignment.rotationDegrees},{"localApplied",alignment.localApplied},
                {"acceptedCoverage",alignment.acceptedCoverage},{"uncertainCoverage",alignment.uncertainCoverage},
                {"rejectedCoverage",alignment.rejectedCoverage},
                {"backend",alignment.backend},{"status",alignment.message}});
        }
        const auto sidecars=result.raw->multiFrameMeasurementSidecars;
        if(sidecars&&sidecars->effectiveSupport) {
            std::vector<float> support=*sidecars->effectiveSupport;
            support.erase(std::remove_if(support.begin(),support.end(),
                [](float value){return !std::isfinite(value);}),support.end());
            std::sort(support.begin(),support.end());
            const auto supported=[&](double threshold) {
                return support.empty()?0.0:static_cast<double>(support.end()-
                    std::upper_bound(support.begin(),support.end(),static_cast<float>(threshold)))/support.size();
            };
            report["effectiveSupport"]={{"minimum",Quantile(support,0)},
                {"p10",Quantile(support,.10)},{"median",Quantile(support,.50)},
                {"p90",Quantile(support,.90)},{"maximum",Quantile(support,1)},
                {"mean",support.empty()?0:std::accumulate(support.begin(),support.end(),0.0)/support.size()},
                {"fractionAbove1_25",supported(1.25)},{"fractionAbove2",supported(2)}};
        }
        if(sidecars&&sidecars->localRejection) {
            const auto rejected=std::count_if(sidecars->localRejection->begin(),
                sidecars->localRejection->end(),[](std::uint8_t value){return value!=0;});
            report["localRejectionFraction"]=sidecars->localRejection->empty()?0:
                static_cast<double>(rejected)/sidecars->localRejection->size();
        }
        if(!WriteBracketNativeComparisons(result,output,report,requestedRegions))
            throw std::runtime_error("Could not write native bracket comparisons.");
        if(exportEvidence) WriteBracketEvidence(request,result,output/"evidence",requestedRegions);
        if(compareUpgrade) WriteBracketUpgradeComparison(request,result,output/"upgrade",requestedRegions);
        if(inspectStages) {
            WriteBracketStageInspection(request,result,output,report,requestedRegions);
            try {InspectBracketRawHandoff(result.raw,report);}
            catch(...) {std::ofstream(output/"bracketing-diagnostic.json")<<report.dump(2);throw;}
        }
        std::ofstream(output/"bracketing-diagnostic.json")<<report.dump(2);
        if(!WriteSupportImage(*result.raw,output/"effective-support.png",request.sources.size()))
            throw std::runtime_error("Could not write the effective-support diagnostic image.");
        std::cout<<"Project diagnostic passed: "<<(output/"bracketing-diagnostic.json").string()<<'\n'<<std::flush;
        return true;
    } catch(const std::exception& exception) {
        std::cerr<<"Project diagnostic failed: "<<exception.what()<<'\n';
        if(argc>1) {
            std::error_code ignored;
            std::filesystem::create_directories(argv[1],ignored);
            std::ofstream(std::filesystem::path(argv[1])/"failure.txt")<<exception.what()<<'\n';
        }
        return false;
    }
}

} // namespace Stack::Validation
