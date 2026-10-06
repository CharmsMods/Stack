#pragma once
#include "Raw/Bracketing/ProcessingInternal.h"
#include "Raw/Bracketing/PreparedTileSampler.h"
#include "Raw/RawProcessingMath.h"
#include "ThirdParty/stb_image_write.h"
#include "BracketingRgbInspection.h"
#include <fstream>

namespace Stack::Validation {
inline bool WriteBracketNativeComparisons(const Raw::Bracketing::BracketingResult& result,
    const std::filesystem::path& output,nlohmann::json& report,
    const std::vector<std::array<unsigned,2>>& requestedRegions={},
    const std::vector<float>* temporal=nullptr) {
    using namespace Raw::Bracketing;
    if(result.raw->reconstructedCameraRgb)
        return WriteBracketRgbComparisons(result,output,report,requestedRegions);
    const auto& data=*result.analysis->prepared;const auto& origin=data.sources[data.origin];
    const auto& mosaic=*result.raw->normalizedMosaicBuffer;
    const auto& metadata=origin.metadata;
    report["nativeComparison"]={{"exposureEv",4},{"display","matched camera color, Reinhard, sRGB"},
        {"width",data.width},{"height",data.height},{"origin",origin.id},
        {"cameraWhiteBalance",metadata.cameraWhiteBalance},{"cameraToSrgb",metadata.cameraToSrgb}};
    std::ofstream binary(output/"merged-linear.f32",std::ios::binary);
    binary.write(reinterpret_cast<const char*>(mosaic.data()),mosaic.size()*sizeof(float));
    if(!binary) return false;
    PreparedTileSampler sampler(data.directory,8);
    std::string error;
    const unsigned size=std::min({384u,data.width-8,data.height-8})&~1u;
    const unsigned extent=size+8;
    std::vector<std::array<unsigned,2>> centers={{data.width/2,data.height/2},{extent/2,extent/2},
        {data.width-extent/2,extent/2},{extent/2,data.height-extent/2},{data.width-extent/2,data.height-extent/2}};
    centers.insert(centers.end(),requestedRegions.begin(),requestedRegions.end());
    for(unsigned region=0;region<centers.size();++region) {
        const auto left=std::min(centers[region][0]>extent/2?centers[region][0]-extent/2:0u,data.width-extent)&~1u;
        const auto top=std::min(centers[region][1]>extent/2?centers[region][1]-extent/2:0u,data.height-extent)&~1u;
        std::vector<float> reference(static_cast<std::size_t>(extent)*extent),merged(reference.size()),
            beforeFilter(temporal?reference.size():0);
        double support=0,noise=0;
        for(unsigned y=0;y<extent;++y) for(unsigned x=0;x<extent;++x) {
            Raw::Mfd::SameCfaTapInput sample;
            if(!sampler.ReadSample(origin.frame,left+x,top+y,sample,&error)) return false;
            // Both inputs use the same pointwise gain before display.
            reference[y*extent+x]=static_cast<float>(sample.normalizedSample*sample.comparisonGain);
            merged[y*extent+x]=static_cast<float>(mosaic[(top+y)*data.width+left+x]*sample.comparisonGain);
            if(temporal) beforeFilter[y*extent+x]=static_cast<float>((*temporal)[(top+y)*data.width+left+x]*sample.comparisonGain);
            support+=result.raw->multiFrameMeasurementSidecars->effectiveSupport->at((top+y)*data.width+left+x);
            noise+=result.raw->multiFrameMeasurementSidecars->variance->at((top+y)*data.width+left+x);
        }
        const unsigned columns=temporal?3:2;
        std::vector<unsigned char> pixels(static_cast<std::size_t>(size)*size*columns*3);
        for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) for(unsigned image=0;image<columns;++image) {
            const auto& samples=image==0?reference:(temporal&&image==1?beforeFilter:merged);
            auto rgb=Raw::Processing::DemosaicMalvarHeCutlerAt(samples,extent,extent,
                origin.frame.activeCfaPattern,x+4,y+4);
            for(unsigned c=0;c<3;++c) rgb[c]*=metadata.cameraWhiteBalance[c]/std::max(1e-6f,metadata.cameraWhiteBalance[1]);
            for(unsigned c=0;c<3;++c) {
                double linear=0;for(unsigned k=0;k<3;++k) linear+=metadata.cameraToSrgb[c*3+k]*rgb[k];
                linear=std::max(0.,linear*16);linear/=1+linear;
                const double encoded=linear<=.0031308?12.92*linear:1.055*std::pow(linear,1/2.4)-.055;
                pixels[(static_cast<std::size_t>(y)*size*columns+x+image*size)*3+c]=
                    static_cast<unsigned char>(std::clamp(std::lround(encoded*255),0l,255l));
            }
        }
        const auto path=output/((temporal?"native-stages-":"native-pair-")+std::to_string(region)+".png");
        if(!stbi_write_png(path.string().c_str(),size*columns,size,3,pixels.data(),size*columns*3)) return false;
        std::ofstream referenceFile(output/("native-reference-"+std::to_string(region)+".f32"),std::ios::binary);
        referenceFile.write(reinterpret_cast<const char*>(reference.data()),reference.size()*sizeof(float));
        if(!referenceFile) return false;
        report["nativeComparison"]["regions"].push_back({{"left",left+4},{"top",top+4},{"size",size},
            {"comparison",path.filename().string()},
            {"columns",temporal?"origin, temporal merge, published result":"origin, result"},
            {"referenceFloatExtent",extent},{"referenceFloatHalo",4},
            {"meanEffectiveSupport",support/reference.size()},{"meanMeasurementVariance",noise/reference.size()}});
    }
    for(const auto& source:data.sources) report["noiseProfiles"][source.id]=Raw::Mfd::SerializeNoiseModelDiagnostics(source.noise);
    return true;
}
}
