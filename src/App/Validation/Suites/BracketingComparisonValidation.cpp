#include "Raw/Bracketing/Processor.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include "Persistence/RawProjectModel.h"
#include "ThirdParty/stb_image_write.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

namespace Stack::Validation {
bool ValidateBracketingComparison(const Project::RawProjectSnapshot& snapshot,
    const std::vector<std::filesystem::path>& files,const std::filesystem::path& directory) {
    using namespace Raw::Bracketing;
    ProcessingRequest request;std::string error;
    const auto& set=snapshot.sourceSets.front();
    if(!Deserialize(set.settings.at("bracketing"),request.recipe,error))throw std::runtime_error(error);
    for(const auto& frame:set.frames) {
        const auto* asset=Project::FindEmbeddedAsset(snapshot,frame.assetId);
        if(!asset)throw std::runtime_error("Comparison asset missing");
        const auto path=std::find_if(files.begin(),files.end(),[&](const auto& f){return f.filename().string()==asset->originalFilename;});
        if(path==files.end())throw std::runtime_error("Comparison original missing");
        request.sources.push_back({frame.frameId,asset->sha256,asset->byteLength,*path});
    }
    request.cacheDirectory=directory/"comparison-cache";request.removeCacheOnRelease=true;
    const auto memory=Raw::Mfd::ResolveMfdProcessingMemoryBudget(
        0,Raw::Mfd::QueryPhysicalMemorySnapshot());
    if(!memory.valid)throw std::runtime_error(memory.message);
    request.memoryBudgetBytes=memory.budgetBytes;
    request.workerCount=ResolveBracketingWorkerCount(request.memoryBudgetBytes);
    auto result=Process(request);
    if(result.status!=BracketingResult::Status::Completed)throw std::runtime_error(result.message);
    request.analysis=result.analysis;
    auto detail=RenderNativeDetail(request,result.raw->metadata.visibleWidth/2,result.raw->metadata.visibleHeight/2,512);
    if(detail.originals.size()!=request.sources.size())throw std::runtime_error("Native comparison lost originals");
    nlohmann::json report;report["diagnostics"]=result.analysis->diagnostics;
    report["estimatedPeakResidentBytes"]=result.analysis->estimatedPeakResidentBytes;
    report["workerCount"]=request.workerCount;
    for(const auto& [frameId,alignment]:result.analysis->alignments) {
        report["alignment"].push_back({{"frameId",frameId},
            {"translationX",alignment.translationX},{"translationY",alignment.translationY},
            {"rotationDegrees",alignment.rotationDegrees},{"localApplied",alignment.localApplied},
            {"acceptedCoverage",alignment.acceptedCoverage},{"uncertainCoverage",alignment.uncertainCoverage},
            {"rejectedCoverage",alignment.rejectedCoverage},
            {"backend",alignment.backend},{"status",alignment.message}});
    }
    report["unrecoverableSamples"]=result.unrecoverableSamples;report["fallbackSamples"]=result.fallbackSamples;
    report["nativeWidth"]=detail.width;report["nativeHeight"]=detail.height;
    const auto& p=result.preview;
    for(std::size_t g=0;g<request.recipe.groups.size();++g) {
        double weight=0;for(std::size_t i=g;i<p.contributions.size();i+=request.recipe.groups.size())weight+=p.contributions[i];
        report["groups"].push_back({{"name",request.recipe.groups[g].name},{"meanActualContribution",weight/(p.width*p.height)}});
    }
    const auto save=[&](const std::string& name,const std::vector<float>& rgb,double exposure) {
        std::vector<unsigned char> pixels(detail.width*detail.height*3);
        for(std::size_t i=0;i<pixels.size()/3;++i)for(unsigned c=0;c<3;++c) {
            double v=0;for(unsigned k=0;k<3;++k)v+=detail.metadata.cameraToSrgb[c*3+k]*rgb[i*3+k]*exposure*
                detail.metadata.cameraWhiteBalance[k]/std::max(1e-6f,detail.metadata.cameraWhiteBalance[1]);
            v=std::max(0.,v);v/=1+v;v=v<=.0031308?v*12.92:1.055*std::pow(v,1/2.4)-.055;
            pixels[i*3+c]=static_cast<unsigned char>(std::clamp(v,0.,1.)*255+.5);
        }
        if(!stbi_write_png((directory/name).string().c_str(),detail.width,detail.height,3,pixels.data(),detail.width*3))throw std::runtime_error("Native comparison image write failed");
    };
    save("native-result.png",detail.resultRgb,1);
    for(std::size_t i=0;i<request.sources.size();++i) {
        const auto& source=request.sources[i];const auto& original=*detail.originals.at(source.frameId);
        report["originals"].push_back({{"filename",source.path.filename().string()},{"relativeEv",-std::log2(original.exposureScale)},
            {"noiseConfidence",result.analysis->noiseConfidence.at(source.frameId)}});
        save("native-original-"+std::to_string(i+1)+"-captured.png",original.rgb,1);
        save("native-original-"+std::to_string(i+1)+"-matched.png",original.rgb,original.exposureScale);
    }
    std::ofstream(directory/"comparison.json")<<report.dump(2);
    std::cout<<"Native original/result comparison and contribution report passed.\n"<<std::flush;
    return true;
}
} // namespace Stack::Validation
