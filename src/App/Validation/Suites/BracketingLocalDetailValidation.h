#pragma once
#include "Raw/Bracketing/LocalDetailEvidence.h"
#include "Raw/Bracketing/LocalDetailTransform.h"
#include "Raw/Bracketing/LocalDetailPatch.h"
#include "Raw/RawProcessingMath.h"
#include <random>
#include <iostream>
#include <stdexcept>

namespace Stack::Validation {
inline void ValidateBracketingLocalDetailEvidence() {
    using namespace Raw::Bracketing;
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    for(unsigned n:{16u,32u}) {
        LocalDetailTransform transform(n);std::vector<double> patch(n*n);
        for(unsigned p=0;p<n*n;++p)patch[p]=-.02+.001*std::sin(p*.37);
        const auto restored=transform.Inverse(transform.Forward(patch));
        for(unsigned p=0;p<n*n;++p)check(std::abs(patch[p]-restored[p])<1e-12,"Local DCT changed signed samples");
        std::vector<LocalDetailCapture> captures(3);
        const double responses[3]={.4,1,.6};
        for(unsigned f=0;f<3;++f)for(unsigned c=0;c<4;++c) {
            auto& capture=captures[f];capture.coefficients[c].resize(n*n);
            capture.measurementBound[c]=1e-9;capture.precision[c]=1e9;
            capture.coefficients[c][0]=(c==3?1.2:-.002)*n;
            for(unsigned k=1;k<n*n;++k)capture.coefficients[c][k]=responses[f]*.002*std::sin(k*.77+c*.6);
        }
        const auto evidence=AnalyzeLocalDetail(captures,n);
        check(evidence.selectedCount>n*n/3,"Repeatable local sharpness differences supplied no detail decisions");
        check(!evidence.selected[0],"Local detail changed average brightness");
        check(evidence.minimumCaptureSupport>=1&&evidence.minimumCaptureSupport<=3,"Local detail invented capture support");
        double before=0,after=0;
        for(unsigned k=1;k<n*n;++k)if(evidence.selected[k]) {
            double value=0,weight=0,baseline=0;
            for(unsigned f=0;f<3;++f) {
                const auto w=evidence.weights[f][k];check(w>=0&&w<=1,"Local detail used inverse-blur amplification");
                weight+=w;value+=w*captures[f].coefficients[0][k];baseline+=captures[f].coefficients[0][k]/3;
            }
            check(std::abs(weight-1)<1e-12,"Local detail weights are not normalized");
            before+=baseline*baseline;after+=value*value;
        }
        check(after>before*1.2,"Local evidence did not retain more measured texture");
        auto two=captures;two.pop_back();check(AnalyzeLocalDetail(two,n).selectedCount==0,"Two captures made new detail selections");
        unsigned cancelChecks=0;
        check(AnalyzeLocalDetail(captures,n,[&]{return ++cancelChecks>4;}).canceled,
            "Local detail ignored cancellation during evidence fitting");
        auto conflict=captures;
        for(unsigned k=1;k<n*n;++k)conflict[1].coefficients[3][k]*=-1;
        check(AnalyzeLocalDetail(conflict,n).selectedCount==0,"Single-color contradiction escaped local evidence checks");
        auto equal=captures;
        for(auto& capture:equal)capture.coefficients=captures[1].coefficients;
        check(AnalyzeLocalDetail(equal,n).selectedCount==0,"Equal sharpness was needlessly changed");
        std::mt19937 random(39612+n);std::normal_distribution<double> noise(0,std::sqrt(1e-9));
        for(auto& capture:equal)for(auto& c:capture.coefficients)for(unsigned k=1;k<n*n;++k)c[k]=noise(random);
        check(AnalyzeLocalDetail(equal,n).selectedCount==0,"Independent flat-region noise selected its own detail");
    }
    double weightSum=0;
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
        weightSum=LocalDetailWindow(x,y)+LocalDetailWindow(x+8,y)+LocalDetailWindow(x,y+8)+LocalDetailWindow(x+8,y+8);
        check(std::abs(weightSum-1)<1e-12,"Overlapping detail windows changed amplitude");
    }
    std::cout<<"Local detail: 16/32-pixel transforms, weak texture, normalized weights, signed/overrange DC, color conflict and flat fallback passed.\n";
}

template<class MakeCapture,class MakeRequest>
void ValidateBracketingLocalDetailPipeline(const std::filesystem::path& directory,MakeCapture makeCapture,MakeRequest makeRequest) {
    using namespace Raw::Bracketing;
    const auto check=[](bool value,const std::string& message){if(!value)throw std::runtime_error(message);};
    constexpr unsigned width=1280,height=128;
    const auto scene=[](unsigned x,unsigned y,double sigma) {
        return .16+.055*std::exp(-.5*sigma*sigma*.11*.11)*std::sin(x*.11)+
            .03*std::exp(-.5*sigma*sigma*.6*.6)*std::sin(x*.6)+
            .024*std::exp(-.5*sigma*sigma*.53*.53)*std::cos(y*.53);
    };
    std::vector<Raw::RawImageData> captures;
    for(unsigned f=0;f<5;++f) {
        auto raw=makeCapture(char('a'+f),1,.001,100,width);
        std::mt19937 random(7911+f*1741);std::normal_distribution<double> noise(0,.001);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
            const bool sharp=(x<width/2&&f==1)||(x>=width/2&&f==2);
            raw.rawBuffer[y*width+x]=std::uint16_t(std::llround(1024+63976*(scene(x,y,sharp?0:2)+noise(random))));
        }
        captures.push_back(std::move(raw));
    }
    auto request=makeRequest(captures,directory,true);request.memoryBudgetBytes=2ull*1024*1024*1024;
    const auto result=Process(request);check(result.status==BracketingResult::Status::Completed,result.message);
    const auto& data=*result.analysis->prepared;const auto& frame=data.sources[data.origin].frame;
    double before=0,after=0;std::array<double,4> bias{};std::array<unsigned,2> changed{};std::string error;
    for(unsigned tx=0;tx<frame.tileColumns;++tx) {
        std::vector<std::vector<Observation>> baseline;Raw::Mfd::PreparedRawTile tile;
        check(ReadTemporalGroups(request,data,tx,0,baseline,tile,error),error);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<tile.extent.width;++x) {
            const unsigned sx=unsigned(tile.originX)+x;const auto p=std::size_t(y)*tile.extent.width+x,q=std::size_t(y)*width+sx;
            const double value=result.raw->normalizedMosaicBuffer->at(q),original=baseline[0][p].value;
            bias[(y%2)*2+sx%2]+=value-original;
            if(y<32||y+32>=height||sx<48||sx+48>=width||std::abs(int(sx)-int(width/2))<48)continue;
            const double expected=scene(sx,y,0);before+=std::pow(original-expected,2);after+=std::pow(value-expected,2);
            if(std::abs(value-original)>1e-7)++changed[sx>=width/2];
            check(result.raw->multiFrameMeasurementSidecars->effectiveSupport->at(q)<=5.00001,
                "Detail patch neighbors inflated capture support");
            check(result.raw->multiFrameMeasurementSidecars->variance->at(q)>0,"Local detail lost sensor variance");
        }
    }
    std::cout<<"Local varying-sharpness fixture: MSE ratio "<<after/before<<", changed left/right "<<changed[0]<<"/"<<changed[1]<<'\n';
    check(changed[0]>100&&changed[1]>100,"Local fusion failed to select different captures on opposite sides");
    check(after<before*.99,"Local fusion did not improve detail over the established temporal baseline");
    for(const double b:bias)check(std::abs(b/(width*height/4))<2e-8,"Overlapping local corrections changed average brightness");
    auto detailRequest=request;detailRequest.analysis=result.analysis;
    const auto detail=RenderNativeDetail(detailRequest,512,64,120);check(detail.width>0,"Local detail native view missing");
    for(unsigned y=8;y+8<detail.height;y+=11)for(unsigned x=8;x+8<detail.width;x+=13) {
        const auto full=Raw::Processing::DemosaicMalvarHeCutlerAt(*result.raw->normalizedMosaicBuffer,width,height,
            Raw::CfaPattern::RGGB,unsigned(detail.sensorOriginX)+x,unsigned(detail.sensorOriginY)+y);
        for(unsigned c=0;c<3;++c)check(std::abs(full[c]-detail.resultRgb[(y*detail.width+x)*3+c])<1e-6,
            "Local detail native view disagrees with full output at a tile seam");
    }
    const auto repeat=Process(detailRequest);check(repeat.status==BracketingResult::Status::Completed,repeat.message);
    check(*repeat.raw->normalizedMosaicBuffer==*result.raw->normalizedMosaicBuffer,"Local detail cache reuse changed pixels");
    check(*repeat.raw->multiFrameMeasurementSidecars->variance==*result.raw->multiFrameMeasurementSidecars->variance,
        "Local detail cache reuse changed sensor variance");
}
}
