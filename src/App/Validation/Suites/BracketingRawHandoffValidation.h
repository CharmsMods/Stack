#pragma once
#include "Raw/HdrDisplayMapping.h"
#include "Renderer/RenderPipeline.h"
#include <cmath>
#include <stdexcept>

namespace Stack::Validation {
// Exercise the actual RAW graph, its stage cache and native-region path with
// the published float Bayer input. A proxy or stale demosaic cache must fail.
inline void InspectBracketRawHandoff(const std::shared_ptr<const Raw::RawImageData>& raw,nlohmann::json& report) {
    RenderPipeline pipeline;pipeline.Initialize();
    pipeline.SetRawDevelopmentAnalysisEnabled(false);pipeline.SetRawRgbDenoiseAsyncEnabled(false);
    pipeline.SetRawDevelopmentViewportValidationEnabled(false);pipeline.SetPreviewMaxDimension(0);
    RenderGraphSnapshot graph;RenderGraphNode node;node.nodeId=1;node.kind=RenderGraphNodeKind::RawDevelopment;
    node.rawDevelopment.embeddedRawData=raw;
    auto recipe=RawRecipe::MakeDefaultRecipe(raw->metadata.sourcePath,"Bracket native inspection");
    recipe.source.fingerprint=raw->metadata.sourceContentSha256;
    recipe.rgbDenoise.enabled=false;recipe.technical.mosaicDenoise.enabled=false;
    recipe.finishTone.layerJson["localBaselineEnabled"]=false;
    recipe.finishTone.layerJson["foundationAdaptiveAssist"]=false;
    node.rawDevelopment.recipe=recipe;graph.nodes.push_back(node);
    RenderGraphNode target;target.nodeId=2;target.kind=RenderGraphNodeKind::Output;graph.nodes.push_back(target);
    graph.links.push_back({1,"imageOut",2,"imageIn"});graph.outputNodeId=2;graph.outputSocketId="imageOut";
    std::vector<float> baseline;
    const auto read=[&]() {
        pipeline.ExecuteGraph(graph);
        const int width=pipeline.GetCanvasWidth(),height=pipeline.GetCanvasHeight();
        if(!pipeline.GetOutputTexture()||width<=0||height<=0) throw std::runtime_error("Bracket RAW render failed.");
        std::vector<float> values(static_cast<std::size_t>(width)*height*4);
        glBindTexture(GL_TEXTURE_2D,pipeline.GetOutputTexture());glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,values.data());
        return values;
    };
    baseline=read();const int width=pipeline.GetCanvasWidth(),height=pipeline.GetCanvasHeight();
    const auto& m=raw->metadata;
    if(static_cast<std::size_t>(width)*height!=static_cast<std::size_t>(m.visibleWidth)*m.visibleHeight)
        throw std::runtime_error("Bracket RAW native render changed the sensor pixel count.");
    report["rawHandoff"]["width"]=width;report["rawHandoff"]["height"]=height;
    graph.nodes[0].rawDevelopment.recipe.viewTransform.layerJson["displayCurve"]=Raw::HdrDisplay::Photographic;
    auto photographic=read();double photographicDifference=0;
    if(photographic.size()!=baseline.size())throw std::runtime_error("HDR display changed native dimensions.");
    for(std::size_t i=0;i<photographic.size();++i) {
        if(!std::isfinite(photographic[i]))throw std::runtime_error("HDR display produced a nonfinite RAW result.");
        photographicDifference=std::max(photographicDifference,double(std::abs(photographic[i]-baseline[i])));
    }
    if(photographicDifference<=1e-7)throw std::runtime_error("Photographic curve did not reach the RAW output or reused a stale display cache.");
    graph.nodes[0].rawDevelopment.recipe=recipe;
    auto restoredDisplay=read();
    if(restoredDisplay!=baseline)throw std::runtime_error("Restoring Standard did not reproduce its cached RAW output.");
    report["rawHandoff"]["photographicDisplayMaximumDifference"]=photographicDifference;
    report["rawHandoff"]["displayCacheRestoredExactly"]=true;
    photographic.clear();photographic.shrink_to_fit();restoredDisplay.clear();restoredDisplay.shrink_to_fit();
    const auto baselineMethod=recipe.technical.demosaicMethod;
    for(const auto method:{Raw::DemosaicMethod::NearestNeighbor,Raw::DemosaicMethod::Bilinear,
        Raw::DemosaicMethod::MalvarHeCutler,Raw::DemosaicMethod::HamiltonAdams}) {
        graph.nodes[0].rawDevelopment.recipe.technical.demosaicMethod=method;
        auto pixels=read();double difference=0,maxDifference=0;
        if(pixels.size()!=baseline.size()) throw std::runtime_error("Demosaic changed native output dimensions.");
        for(std::size_t i=0;i<pixels.size();++i) {
            if(!std::isfinite(pixels[i])) throw std::runtime_error("Nonfinite bracket RAW pixel.");
            const double d=std::abs(pixels[i]-baseline[i]);difference+=d;maxDifference=std::max(maxDifference,d);
        }
        if(method!=baselineMethod&&maxDifference<=1e-7) throw std::runtime_error("Demosaic change did not reach RAW pixels.");
        report["rawHandoff"]["demosaic"].push_back({{"method",int(method)},
            {"meanAbsoluteDifference",difference/pixels.size()},{"maximumDifference",maxDifference}});
    }
    graph.nodes[0].rawDevelopment.recipe.technical.demosaicMethod=baselineMethod;
    Raw::ViewportRegion region{width,height,width/3,height/3,std::min(384,width/2),std::min(384,height/2)};
    pipeline.SetRawViewportRequest({region,1.,1});auto cropped=read();const auto actual=pipeline.GetRawViewportRegion();
    if(!actual.Valid()||pipeline.GetCanvasWidth()!=actual.width||pipeline.GetCanvasHeight()!=actual.height)
        throw std::runtime_error("Bracket native-region render failed.");
    double maxDifference=0,totalDifference=0;std::size_t differingSamples=0;
    nlohmann::json worstSample;
    for(int y=0;y<actual.height;++y) for(int x=0;x<actual.width;++x) for(int c=0;c<3;++c) {
        const auto full=((height-actual.y-actual.height+y)*width+actual.x+x)*4+c;
        const float a=baseline[full],b=cropped[(y*actual.width+x)*4+c];
        if(!std::isfinite(a)||!std::isfinite(b)) throw std::runtime_error("Nonfinite bracket native-region pixel.");
        const double difference=std::abs(double(a)-b);totalDifference+=difference;
        if(difference>.004) ++differingSamples;
        if(difference>maxDifference) {
            maxDifference=difference;
            worstSample={{"x",actual.x+x},{"y",actual.y+actual.height-1-y},
                {"channel",c},{"full",a},{"region",b}};
        }
    }
    report["rawHandoff"]["nativeRegionMaximumDifference"]=maxDifference;
    report["rawHandoff"]["nativeRegionMeanDifference"]=totalDifference/(actual.width*actual.height*3);
    report["rawHandoff"]["nativeRegionSamplesOutsideTolerance"]=differingSamples;
    report["rawHandoff"]["nativeRegionWorstSample"]=worstSample;
    // The RAW graph uses half-float render targets. Match the tolerance of
    // the existing full-frame/ROI parity suite, rather than requiring float32.
    for(unsigned control=0;control<2;++control) {
        auto& edited=graph.nodes[0].rawDevelopment.recipe;edited=recipe;
        if(control==0) {
            edited.technical.mosaicDenoise.enabled=true;
            edited.technical.mosaicDenoise.lumaStrength=.5f;
            edited.technical.mosaicDenoise.chromaStrength=.5f;
        } else {
            edited.rgbDenoise.enabled=true;edited.rgbDenoise.lumaMap.baseMultiplier=.4f;
            edited.rgbDenoise.chromaMap.baseMultiplier=.6f;
        }
        auto changed=read();double delta=0;
        if(changed.size()!=cropped.size()) throw std::runtime_error("RAW denoise changed native ROI dimensions.");
        for(std::size_t p=0;p<changed.size();++p) {
            if(!std::isfinite(changed[p])) throw std::runtime_error("RAW denoise produced a nonfinite value.");
            delta=std::max(delta,double(std::abs(changed[p]-cropped[p])));
        }
        if(delta<=1e-7) throw std::runtime_error("RAW denoise control did not affect the bracket.");
        report["rawHandoff"][control?"rgbDenoiseMaximumDifference":"cfaDenoiseMaximumDifference"]=delta;
        edited=recipe;auto restored=read();double restoredDifference=0;
        for(std::size_t p=0;p<restored.size();++p)
            restoredDifference=std::max(restoredDifference,double(std::abs(restored[p]-cropped[p])));
        if(restoredDifference>.004) throw std::runtime_error("Disabling RAW denoise did not restore the bracket.");
    }
    if(maxDifference>.004) {
        auto& linear=graph.nodes[0].rawDevelopment.recipe;linear=recipe;
        linear.viewTransform.layerJson["enabled"]=false;
        linear.finishTone.layerJson["enabled"]=false;
        pipeline.SetRawViewportRequest({});auto full=read();
        pipeline.SetRawViewportRequest({region,1.,2});auto part=read();
        const auto linearRegion=pipeline.GetRawViewportRegion();
        double difference=0;
        for(int y=0;y<linearRegion.height;++y) for(int x=0;x<linearRegion.width;++x) for(int c=0;c<3;++c) {
            const auto p=((height-linearRegion.y-linearRegion.height+y)*width+linearRegion.x+x)*4+c;
            difference=std::max(difference,double(std::abs(part[(y*linearRegion.width+x)*4+c]-full[p])));
        }
        report["rawHandoff"]["ungradedNativeRegionMaximumDifference"]=difference;
    }
    pipeline.Shutdown();
    if(maxDifference>.004) throw std::runtime_error("Native bracket ROI does not match full RAW output: "+std::to_string(maxDifference));
}
}
