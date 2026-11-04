#pragma once
#include "Raw/Bracketing/NeutralGuide.h"
#include "Raw/Bracketing/ResultSerialization.h"
#include "Renderer/RawPreviewProxy.h"
#include <fstream>
#include <stdexcept>

namespace Stack::Validation {
namespace SceneColorChecks {
using namespace Raw::Bracketing;
inline void Require(bool value,const char* message) {
    if(!value)throw std::runtime_error(std::string("Source color guidance: ")+message);
}
inline BracketingRecipe Recipe(unsigned groups) {
    BracketingRecipe recipe;recipe.groups.resize(groups);
    for(unsigned g=0;g<groups;++g)recipe.groups[g].id=std::to_string(g);
    return recipe;
}
inline ColorObservations Samples(unsigned groups,const std::array<double,3>& rgb={.18,.24,.12},double variance=1e-6) {
    ColorObservations values;
    constexpr std::array<unsigned,4> channels{0,1,1,2};
    for(unsigned s=0;s<4;++s) {
        values[s].resize(groups);
        for(unsigned g=0;g<groups;++g) {
            auto& o=values[s][g];o.value=o.fallback=rgb[channels[s]];
            o.variance=o.measurementVariance=variance;o.headroom=o.support=1;o.finite=true;
            o.exposure=std::exp2(double(g)-double(groups)/2);
        }
    }
    return values;
}
inline SceneColorEvidence::Cell Estimate(const ColorObservations& values,const BracketingRecipe& recipe) {
    return SceneColorEvidence::Estimate(values,BlendColor(values,{},recipe),recipe,{0,1,1,2});
}
inline bool Same(const Raw::SceneColorGuide& a,const Raw::SceneColorGuide& b) {
    return a.width==b.width&&a.height==b.height&&a.sensorWidth==b.sensorWidth&&a.sensorHeight==b.sensorHeight&&
        a.identity==b.identity&&a.cameraRgb==b.cameraRgb&&a.variance==b.variance&&
        a.reliability==b.reliability&&a.coverage==b.coverage;
}
inline std::shared_ptr<const Raw::SceneColorGuide> Compact(const SceneColorEvidence::Cell& cell) {
    std::vector<SceneColorEvidence::Tile> tiles(1);tiles[0].Initialize(8,4,0,0,8,4);
    for(unsigned y=0;y<4;y+=2)for(unsigned x=0;x<8;x+=2)tiles[0].Add(x,y,cell);
    return SceneColorEvidence::Finish(8,4,"source-color-validation",tiles);
}
}

inline void ValidateBracketingSceneColor() {
    using namespace SceneColorChecks;
    const auto recipe=Recipe(2);const auto original=Samples(2);const auto clean=Estimate(original,recipe);
    Require(clean.supported&&clean.reliability>.99,"clean measured color is not trusted");
    for(const std::array<unsigned,4> channels:{std::array<unsigned,4>{0,1,1,2},{2,1,1,0},{1,0,2,1},{1,2,0,1}}) {
        auto reordered=original;const std::array<double,3> rgb{.18,.24,.12};
        for(unsigned s=0;s<4;++s)for(auto& o:reordered[s])o.value=o.fallback=rgb[channels[s]];
        const auto cell=SceneColorEvidence::Estimate(reordered,BlendColor(reordered,{},recipe),recipe,channels);
        Require(cell.rgb==clean.rgb&&cell.reliability==clean.reliability,"CFA phase changed source color");
    }
    for(const unsigned groups:{1u,2u,20u}) {
        const auto estimate=Estimate(Samples(groups),Recipe(groups));
        Require(estimate.supported&&estimate.reliability>.98,"group count introduced false uncertainty");
        for(unsigned c=0;c<3;++c)Require(std::abs(estimate.rgb[c]-clean.rgb[c])<1e-12,"exposure metadata changed calibrated color");
    }
    for(double scale:{1./64,64.}) {
        auto values=original;
        for(auto& site:values)for(auto& o:site) {
            o.value*=scale;o.fallback*=scale;o.variance*=scale*scale;o.measurementVariance*=scale*scale;
        }
        Require(std::abs(Estimate(values,recipe).reliability-clean.reliability)<1e-10,"radiometric gauge changed confidence");
    }
    auto values=original;
    for(unsigned s=0;s<4;++s) {auto& o=values[s][1];o.support=0;o.localRejected=true;o.headroom=0;}
    const auto surviving=Estimate(values,recipe),single=Estimate(Samples(1),Recipe(1));
    Require(surviving.supported&&std::abs(surviving.reliability-single.reliability)<1e-12,
        "rejected alternate invalidated accepted source");
    for(auto& site:values) {site[1].clipped=true;site[1].fallbackClipped=true;}
    Require(Estimate(values,recipe).reliability==surviving.reliability,"clipped unused input contaminated evidence");
    values=original;
    for(auto& o:values[0]) {o.support=0;o.clipped=o.fallbackClipped=true;o.headroom=0;}
    const auto missing=Estimate(values,recipe);
    Require(!missing.supported&&missing.reliability==0,"inferred color acquired measurement support");
    values=Samples(1);values[0][0].clipped=true;
    Require(!Estimate(values,Recipe(1)).supported,"positive support overrode explicit source clipping");
    values=original;
    for(unsigned s=0;s<4;++s) {
        auto& o=values[s][s==0?1:0];o.support=0;o.headroom=0;o.clipped=o.fallbackClipped=true;
    }
    Require(Estimate(values,recipe).supported,"complementary unclipped channel measurements were discarded");
    values=original;values[0][1].value=.34;values[3][1].value=.02;
    Require(Estimate(values,recipe).reliability<.35,"admitted chromatic disagreement remained a strong edge guide");
    auto noisy=Samples(2,{.002,.003,.001},4e-5);
    Require(Estimate(noisy,recipe).reliability<.01,"dark noise became trusted chroma");
    for(auto& site:noisy)for(auto& o:site)o.value=-.001;
    Require(Estimate(noisy,recipe).supported&&Estimate(noisy,recipe).reliability==0,"signed black handling is invalid");
    values=original;
    for(auto& site:values)for(auto& o:site) {o.uncertaintyVariance=.02;o.variance+=.02;}
    Require(Estimate(values,recipe).reliability<.03,"registration/model risk was omitted");
    values=original;for(auto& site:values)for(auto& o:site)o.headroom=.05;
    Require(Estimate(values,recipe).reliability<.051,"near-clipping color retained full confidence");
    // Exercise the production neutral-guide entry point with genuinely
    // different authored contributions. Neither source color nor its evidence
    // is allowed to follow those choices.
    const auto fromNeutralPass=[&](BracketingRecipe authored) {
        std::vector<std::vector<Observation>> groups(2,std::vector<Observation>(16));
        for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x)for(unsigned g=0;g<2;++g)
            groups[g][y*4+x]=original[(y%2)*2+x%2][g];
        LuminanceTransform transform;transform.channels={0,1,1,2};transform.cameraToSrgb={1,0,0,0,1,0,0,0,1};
        std::vector<float> ev(4);std::array<float,256> counts{};std::vector<double> histogram;std::string error;
        std::vector<SceneColorEvidence::Tile> tiles(1);
        Require(BuildNeutralGuideTile(authored,transform,{0,0,4,4},2,groups,ev,counts,histogram,error,&tiles[0]),
            "neutral guide rejected source-color output");
        return SceneColorEvidence::Finish(4,4,"same-inputs",tiles);
    };
    auto manual=recipe;manual.automatic=false;
    manual.knots={{-12,-12,-8,{1,0},{1,0},{1,0}},{8,4,8,{1,0},{1,0},{1,0}}};
    Require(Same(*fromNeutralPass(recipe),*fromNeutralPass(manual)),"authored contributions altered source evidence");

    // Compact color mixtures must not fabricate a strong boundary color. A
    // luminance-only change with unchanged chromaticity remains trustworthy.
    SceneColorEvidence::Accumulator same,mixed;
    auto brighter=clean;for(auto& c:brighter.rgb)c*=16;
    auto other=Estimate(Samples(2,{.4,.1,.04}),recipe);
    same.Add(clean,.5);same.Add(brighter,.5);mixed.Add(clean,.5);mixed.Add(other,.5);
    const auto finish=[&](const SceneColorEvidence::Accumulator& a) {
        std::vector<SceneColorEvidence::Tile> tiles(1);tiles[0].Initialize(2,2,0,0,2,2);tiles[0].values[0]=a;
        return SceneColorEvidence::Finish(2,2,"mixture",tiles);
    };
    Require(finish(same)->reliability[0]>.98&&finish(mixed)->reliability[0]<.08,"compact footprint mixed luminance and color uncertainty");
    // Odd compact ratios exercise fractional footprints, final partial tiles,
    // and source support at a boundary; every cell must enter the guide.
    std::vector<SceneColorEvidence::Tile> tiles(3);
    for(unsigned t=0;t<3;++t) {
        const unsigned left=t*512,width=std::min(512u,1026-left);tiles[t].Initialize(1026,6,left,0,width,6);
        for(unsigned y=0;y<6;y+=2)for(unsigned x=left;x<left+width;x+=2)tiles[t].Add(x,y,clean);
    }
    const auto compact=SceneColorEvidence::Finish(1026,6,"complete-footprint",tiles);
    Require(compact->Valid()&&compact->width==512&&compact->height==2,"bounded reference dimensions changed");
    for(float c:compact->coverage)Require(c==1,"fractional compact bin has missing coverage");
    for(float c:compact->reliability)Require(c>.98,"uniform compact color lost confidence");
    auto partial=clean;partial.supported=false;
    SceneColorEvidence::Accumulator half;half.Add(clean,.5);half.Add(partial,.5);
    Require(finish(half)->coverage[0]==.5f&&finish(half)->reliability[0]>.98,"support fraction was confused with reliability");

    const auto guide=Compact(clean);Require(guide->Valid(),"valid immutable guide rejected");
    auto invalid=*guide;invalid.variance[0]=-1;Require(!invalid.Valid(),"negative variance accepted");
    invalid=*guide;invalid.reliability[0]=std::numeric_limits<float>::quiet_NaN();Require(!invalid.Valid(),"nonfinite confidence accepted");
    invalid=*guide;invalid.cameraRgb.pop_back();Require(!invalid.Valid(),"truncated color accepted");
}

inline void ValidateBracketingSceneColorStorage(const std::filesystem::path& directory) {
    using namespace SceneColorChecks;std::filesystem::create_directories(directory);
    BracketingResult result;result.raw=std::make_shared<Raw::RawImageData>();result.identity="scene-color-storage";
    auto& raw=*result.raw;raw.metadata.rawWidth=raw.metadata.visibleWidth=8;raw.metadata.rawHeight=raw.metadata.visibleHeight=4;
    raw.metadata.pixelLayout=Raw::RawPixelLayout::MosaicBayer;raw.metadata.cfaPattern=Raw::CfaPattern::RGGB;
    raw.metadata.mosaiced=true;raw.metadata.orientation=6;
    raw.normalizedMosaicInputContract=Raw::NormalizedMosaicInputContract::BracketingPreGain;
    raw.normalizedMosaicBuffer=std::make_shared<const std::vector<float>>(32,.2f);
    raw.sceneColorGuide=Compact(Estimate(Samples(2),Recipe(2)));
    auto depth=std::make_shared<Raw::ReconstructionDepth>();depth->sensorWidth=8;depth->sensorHeight=4;
    depth->width=2;depth->height=1;depth->acceptedPairs=1;depth->measuredFraction=1;
    depth->inverseDepth={.2f,.8f};depth->confidence={.5f,.9f};
    Raw::DepthMask mask;mask.id=std::string(64,'a');mask.name="Near";mask.width=mask.w=2;mask.height=mask.h=1;
    mask.pixels=std::make_shared<const Raw::DepthMask::Bytes>(Raw::DepthMask::Bytes{64,192});depth->masks.push_back(mask);
    raw.reconstructionDepth=depth;
    auto& p=result.preview;p.width=p.height=1;p.metadata=raw.metadata;
    p.resultRgb=p.sourceRgb={.1f,.2f,.3f};p.contributions=p.requested={1};p.guideEv={0};p.diagnostics={0};
    const auto read=[&](const std::filesystem::path& path,bool external) {
        Require(Serialization::WriteResultFile(path,result,external),"result write failed");
        std::ifstream stream(path,std::ios::binary);BracketingResult restored;
        Serialization::ReadResult(stream,std::filesystem::file_size(path),1,restored,directory/"inspection",
            [&](Raw::RawImageData& target){target.normalizedMosaicBuffer=raw.normalizedMosaicBuffer;target.reconstructionDepth=depth;});
        return restored;
    };
    for(bool external:{false,true}) {
        const auto restored=read(directory/(external?"aux.bracket":"worker.bracket"),external);
        Require(restored.raw->sceneColorGuide&&Same(*restored.raw->sceneColorGuide,*raw.sceneColorGuide),"worker/project sidecar changed guidance");
        Require(restored.raw->metadata.orientation==6&&restored.raw->sceneColorGuide->sensorWidth==8,
            "serialization rotated native evidence");
        Require(restored.raw->reconstructionDepth&&restored.raw->reconstructionDepth->Available()&&
            restored.raw->reconstructionDepth->masks.size()==1&&*restored.raw->reconstructionDepth->masks[0].Read()==*mask.pixels,
            "version6 changed compressed or external depth masks");
    }
    const auto guide=raw.sceneColorGuide;raw.sceneColorGuide.reset();
    Require(!read(directory/"without-guide.bracket",false).raw->sceneColorGuide,"old result invented source evidence");
    raw.sceneColorGuide=guide;raw.metadata.rawWidth=raw.metadata.visibleWidth=16;
    raw.metadata.rawHeight=raw.metadata.visibleHeight=8;raw.metadata.pixelLayout=Raw::RawPixelLayout::LinearRgb;
    raw.metadata.linearChannels=3;raw.metadata.linearSampleFormat=Raw::RawSampleFormat::Float32;
    raw.metadata.mosaiced=false;raw.metadata.cfaPattern=Raw::CfaPattern::Unknown;raw.reconstructedCameraRgb=true;
    raw.normalizedMosaicInputContract=Raw::NormalizedMosaicInputContract::None;raw.normalizedMosaicBuffer.reset();
    raw.linearFloatBuffer.assign(16*8*3,.2f);
    const auto scaled=read(directory/"reconstructed.bracket",false);
    Require(Same(*scaled.raw->sceneColorGuide,*guide),"reconstruction persistence rescaled native guide");
    Raw::RawImageData proxy;
    Require(Renderer::RawPreviewProxy::BuildPreviewRawData(*scaled.raw,4,proxy)&&proxy.sceneColorGuide==scaled.raw->sceneColorGuide,
        "RAW proxy lost or copied source guide");
    const auto oldKey=Renderer::RawPreviewProxy::BuildCacheKey("result",proxy,4);
    auto revised=std::make_shared<Raw::SceneColorGuide>(*guide);revised->identity+="-revised";proxy.sceneColorGuide=revised;
    Require(oldKey!=Renderer::RawPreviewProxy::BuildCacheKey("result",proxy,4),"proxy cache omitted source guide identity");
}
}
