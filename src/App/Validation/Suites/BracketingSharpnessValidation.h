#pragma once
#include "Raw/Bracketing/ProcessingInternal.h"
#include <random>
#include <iostream>
#include <stdexcept>

namespace Stack::Validation {
template<class MakeCapture,class MakeRequest>
void ValidateBracketingCaptureSharpness(const std::filesystem::path& directory,
    MakeCapture capture,MakeRequest request) {
    using namespace Raw::Bracketing;
    const auto check=[](bool okay,const std::string& error) {if(!okay) throw std::runtime_error(error);};
    constexpr unsigned width=1280,height=128;
    const auto scene=[](unsigned x,unsigned y,double sigma) {
        return .16+.055*std::exp(-.5*sigma*sigma*.11*.11)*std::sin(x*.11)+
            .03*std::exp(-.5*sigma*sigma*.6*.6)*std::sin(x*.6)+
            .024*std::exp(-.5*sigma*sigma*.53*.53)*std::cos(y*.53);
    };
    const auto make=[&](bool flat,bool equalBlur) {
        std::vector<Raw::RawImageData> sources;
        for(unsigned f=0;f<5;++f) {
            const double deviation=f==4?.003:.001;
            auto raw=capture(static_cast<char>('a'+f),1,deviation,100,width);
            std::mt19937 random(34987+f*1381);std::normal_distribution<double> noise(0,deviation);
            for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
                const double value=flat?.16:scene(x,y,(equalBlur||f==2)?0:2.);
                raw.rawBuffer[y*width+x]=static_cast<std::uint16_t>(std::llround(1024+63976*(value+noise(random))));
            }
            sources.push_back(std::move(raw));
        }
        return sources;
    };
    auto r=request(make(false,false),directory/"blurred-origin",true);
    r.memoryBudgetBytes=2ull*1024*1024*1024;
    PreparedDataset prepared;std::vector<std::string> diagnostics;std::string error;
    check(Prepare(r,prepared,diagnostics,error),error);
    for(const auto& source:prepared.sources)
        std::cout<<"Sharpness fixture "<<source.id<<": "<<source.detailPreference<<'\n';
    check(prepared.sources[2].detailPreference>.99&&prepared.sources[0].detailPreference<.6,
        "Capture sharpness did not distinguish a sharp alternate from the softer origin");
    double ordinaryError=0,selectiveError=0;
    const auto& frame=prepared.sources[prepared.origin].frame;
    for(unsigned tx=0;tx<frame.tileColumns;++tx) {
        Raw::Mfd::PreparedRawTile tile;std::vector<std::vector<Observation>> selective,ordinary;
        check(ReadTemporalGroups(r,prepared,tx,0,selective,tile,error),error);
        std::vector<double> saved;
        for(auto& source:prepared.sources) {saved.push_back(source.detailPreference);source.detailPreference=1;}
        check(ReadTemporalGroups(r,prepared,tx,0,ordinary,tile,error),error);
        for(std::size_t f=0;f<saved.size();++f) prepared.sources[f].detailPreference=saved[f];
        for(unsigned y=8;y+8<height;++y) for(unsigned x=0;x<tile.extent.width;++x) {
            const auto sx=tile.originX+x;if(sx<8||sx+8>=width) continue;
            const auto p=y*tile.extent.width+x;const double expected=scene(static_cast<unsigned>(sx),y,0);
            ordinaryError+=std::pow(ordinary[0][p].value-expected,2);
            selectiveError+=std::pow(selective[0][p].value-expected,2);
        }
    }
    check(selectiveError<ordinaryError*.8,"Measured sharpness preferences did not improve recovered fine detail");
    const auto large=Process(r);check(large.status==BracketingResult::Status::Completed,large.message);
    r.memoryBudgetBytes=192ull*1024*1024;
    const auto small=Process(r);check(small.status==BracketingResult::Status::Completed,small.message);
    check(large.analysis->prepared->sources[0].frame.tileRawPixels!=small.analysis->prepared->sources[0].frame.tileRawPixels,
        "Sharpness fixture did not exercise different tile geometries");
    check(*large.raw->normalizedMosaicBuffer==*small.raw->normalizedMosaicBuffer,
        "Detail fusion or multiscale filtering changed with tile geometry");
    for(bool flat:{false,true}) {
        auto equal=request(make(flat,true),directory/(flat?"flat-noise":"equal-sharpness"),true);
        PreparedDataset data;diagnostics.clear();error.clear();
        check(Prepare(equal,data,diagnostics,error),error);
        for(const auto& source:data.sources) check(source.detailPreference>.9,
            "Capture sharpness confused independent noise with real detail");
    }
    std::cout<<"Measured capture sharpness: fine-detail MSE ratio "<<selectiveError/ordinaryError
             <<", blurred-origin preference "<<prepared.sources[0].detailPreference
             <<"; tile-layout, noise and flat-scene checks passed.\n";
}
}
