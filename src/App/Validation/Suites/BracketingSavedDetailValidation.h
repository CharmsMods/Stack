#pragma once
#include "Raw/Bracketing/Depth/Depth.h"
#include "Raw/Bracketing/InspectionImage.h"
#include "Raw/Bracketing/ProcessingInternal.h"
#include "Raw/Bracketing/ResultSerialization.h"
#include "Raw/RawTechnicalEvidence.h"
#include "NodeMath/ContractTypes.h"
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace Stack::Validation {
// Small prepared-source + persisted-result component check. No Process, decoder,
// group reconstruction or GPU is needed to inspect completed native pixels.
inline void ValidateBracketingSavedDetail(const std::filesystem::path& directory) {
    using namespace Raw::Bracketing;namespace Mfd=Raw::Mfd;
    const auto require=[](bool value,const std::string& message) {
        if(!value)throw std::runtime_error("Saved native detail: "+message);
    };
    constexpr unsigned width=32,height=24,count=width*height;
    const std::array<Raw::CfaPattern,4> patterns={Raw::CfaPattern::RGGB,Raw::CfaPattern::BGGR,
        Raw::CfaPattern::GRBG,Raw::CfaPattern::GBRG};
    for(unsigned variant=0;variant<patterns.size();++variant) {
        const auto path=directory/std::to_string(variant);std::filesystem::create_directories(path);
        Raw::RawImageData capture;auto& m=capture.metadata;
        m.rawWidth=m.visibleWidth=width;m.rawHeight=m.visibleHeight=height;
        m.cfaPattern=patterns[variant];m.pixelLayout=Raw::RawPixelLayout::MosaicBayer;
        m.mosaiced=true;m.bitDepth=12;m.whiteLevel=4095;m.orientation=1;
        m.cameraWhiteBalance={1,1,1,1};m.cameraToSrgb={1,0,0,0,1,0,0,0,1};
        capture.rawBuffer.resize(count);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
            capture.rawBuffer[y*width+x]=500+11*x+7*y+((y%2)*2+x%2)*97;
        const auto* bytes=reinterpret_cast<const std::uint8_t*>(capture.rawBuffer.data());
        const auto identity=Stack::RawEvidence::ComputeSourceIdentity(std::vector<std::uint8_t>(bytes,bytes+count*2));
        require(identity.valid,"source identity");m.sourceContentSha256=identity.sha256;m.sourceByteSize=identity.byteSize;
        Raw::DngGainMapOpcode gain;gain.bottom=height;gain.right=width;
        gain.mapPointsV=gain.mapPointsH=2;gain.mapSpacingV=gain.mapSpacingH=1;
        gain.gains={1.f,1.4f,1.7f,2.2f};m.dngGainMaps={gain};m.dngGainMapCount=1;
        m.dngOpcodeCount[1]=m.dngAppliedOpcodeCountByList[1]=1;
        Mfd::PreparationOptions options;options.tileRawPixels=8;
        Mfd::DirectoryNormalizedTileCache cache(path/"prepared");
        const auto prepared=Mfd::PrepareRawFrame(capture,options,cache);
        require(prepared.success,prepared.message);
        auto data=std::make_shared<PreparedDataset>();data->directory=path/"prepared";
        data->width=width;data->height=height;data->sources.resize(1);
        auto& source=data->sources.front();source.id="source";source.frame=prepared.frame;
        source.metadata=m;source.scale=1.75;
        auto analysis=std::make_shared<BracketingAnalysis>();analysis->prepared=data;
        ProcessingRequest request;request.analysis=analysis;request.memoryBudgetBytes=8*1024*1024;
        request.recipe.originFrameId="source";request.recipe.groups={{"group","Source",true,{{"source"}}}};
        request.recipe.knots=EqualCurves(1);
        // A known saved pre-gain mosaic deliberately differs from the fresh
        // source. The production serializer validates its persisted contract.
        BracketingResult result;result.status=BracketingResult::Status::Completed;
        result.identity=Stack::NodeMath::Sha256ContentIdentity("saved-detail-"+std::to_string(variant));
        result.raw=std::make_shared<Raw::RawImageData>();result.raw->metadata=m;
        result.raw->contentIdentity=result.identity;
        std::uint64_t hash=14695981039346656037ull;
        for(unsigned char c:result.identity){hash^=c;hash*=1099511628211ull;}
        result.raw->contentIdentityHash=result.raw->normalizedMosaicContentHash=hash;
        result.raw->normalizedMosaicInputContract=Raw::NormalizedMosaicInputContract::BracketingPreGain;
        auto mosaic=std::make_shared<std::vector<float>>(count);
        for(unsigned p=0;p<count;++p)(*mosaic)[p]=.03f+.00013f*p+.007f*(p%2);
        result.raw->normalizedMosaicBuffer=mosaic;
        auto& p=result.preview;p.width=p.height=1;p.metadata=m;
        p.resultRgb=p.sourceRgb={.1f,.1f,.1f};p.contributions=p.requested={1};p.guideEv={0};p.diagnostics={0};
        const auto roundTrip=[&](const BracketingResult& value,const std::string& name) {
            const auto file=path/(name+".bracket");require(Serialization::WriteResultFile(file,value),"write result");
            std::ifstream input(file,std::ios::binary);BracketingResult loaded;
            Serialization::ReadResult(input,std::filesystem::file_size(file),1,loaded,path/"inspection");
            require(!loaded.analysis,"saved Standard unexpectedly retained analysis");loaded.analysis=analysis;return loaded;
        };
        auto saved=roundTrip(result,"bayer");
        const auto full=RenderInspectionImage(request,saved,InspectionImageKind::Result,{},0,width);
        const auto original=RenderInspectionImage(request,saved,InspectionImageKind::Capture,"source",0,width);
        require(full&&original,"full inspection missing");
        const auto compare=[&](const Preview& detail,const CapturePreview& expected) {
            require(detail.width&&detail.height,"empty detail");
            for(unsigned y=0;y<detail.height;++y)for(unsigned x=0;x<detail.width;++x)for(unsigned c=0;c<3;++c) {
                const auto at=(std::size_t(unsigned(detail.sensorOriginY)+y)*width+unsigned(detail.sensorOriginX)+x)*3+c;
                require(std::abs(detail.resultRgb[(y*detail.width+x)*3+c]-expected.rgb[at])<1e-7,"gain/CFA/halo crop mismatch");
            }
        };
        for(const auto center:std::array<std::array<unsigned,2>,3>{{{{0,0}},{{17,13}},{{width-1,height-1}}}}) {
            const auto detail=Depth::ReadResultDetail(request,saved,center[0],center[1],10);compare(detail,*full);
            require(detail.sourceRgb.empty()&&detail.guideEv.empty()&&detail.contributions.empty()&&
                detail.requested.empty()&&detail.samples.empty()&&detail.diagnostics.empty(),"fabricated historical planes");
            const auto& sourceCrop=*detail.originals.at("source");
            Preview sourceDetail=detail;sourceDetail.resultRgb=sourceCrop.rgb;compare(sourceDetail,*original);
            require(sourceCrop.exposureScale==source.scale,"source match exposure changed");
        }
        const auto smallest=Depth::ReadResultDetail(request,saved,17,13,1);
        require(smallest.width==2&&smallest.height==2,"minimum even ROI changed");compare(smallest,*full);
        require(Depth::ReadResultDetail(request,saved,16,12,0).resultRgb.empty(),"zero-size detail was not empty");
        require(*saved.raw->normalizedMosaicBuffer==*mosaic,"inspection changed saved pixels");
        request.shouldCancel=[] {return true;};
        require(Depth::ReadResultDetail(request,saved,16,12,10).resultRgb.empty(),"pre-canceled detail published");
        unsigned polls=0;request.shouldCancel=[&] {return ++polls==4;};
        require(Depth::ReadResultDetail(request,saved,16,12,10).resultRgb.empty(),"mid-canceled detail published");
        polls=0;request.shouldCancel=[&] {return ++polls==32;};
        require(Depth::ReadResultDetail(request,saved,16,12,10).resultRgb.empty(),"source-crop cancellation published partial detail");
        request.shouldCancel={};
        // Stored diagnostics, when complete, remain tied to the saved pixels.
        auto evidence=std::make_shared<Raw::RawImageData::MultiFrameMeasurementSidecars>();
        evidence->validity=std::make_shared<const std::vector<std::uint8_t>>(count,1);
        evidence->clipping=std::make_shared<const std::vector<std::uint8_t>>(count,0);
        evidence->localRejection=std::make_shared<const std::vector<std::uint8_t>>(count,1);
        evidence->fallbackReason=std::make_shared<const std::vector<std::uint8_t>>(count,std::uint8_t(MeasurementFallbackReason::FixedReference));
        result.raw->multiFrameMeasurementSidecars=evidence;
        auto withEvidence=roundTrip(result,"evidence");
        const auto detail=Depth::ReadResultDetail(request,withEvidence,16,12,10);
        require(detail.diagnostics.size()==detail.width*detail.height,"stored diagnostics missing");
        for(auto flags:detail.diagnostics)require(flags==(Preview::Fallback|Preview::AlignmentRejected|Preview::FixedReference),"stored diagnostics changed");
        evidence->clipping.reset();result.raw->multiFrameMeasurementSidecars=evidence;
        auto partial=roundTrip(result,"partial");
        require(Depth::ReadResultDetail(request,partial,16,12,10).diagnostics.empty(),"partial diagnostics fabricated zeros");
        // Depth camera-RGB uses stored RGB, not Bayer reconstruction or gains.
        result.raw->reconstructedCameraRgb=true;result.raw->linearFloatBuffer.resize(count*3);
        for(unsigned i=0;i<count*3;++i)result.raw->linearFloatBuffer[i]=.001f*i;
        auto rgb=roundTrip(result,"rgb");
        const auto fullRgb=RenderInspectionImage(request,rgb,InspectionImageKind::Result,{},0,width);
        require(bool(fullRgb),"RGB full inspection missing");compare(Depth::ReadResultDetail(request,rgb,16,12,10),*fullRgb);
    }
}
}
