#pragma once
#include "Raw/Bracketing/AlignmentInspectionGrid.h"
#include "BurstEngine/WireData.h"
#include <limits>
#include <stdexcept>

namespace Stack::Validation {
inline void ValidateBracketingAlignmentInspectionGrid() {
    using namespace Raw::Bracketing;
    const auto require=[](bool valid,const char* message) {
        if(!valid)throw std::runtime_error(message);
    };
    auto inspection=std::make_shared<AlignmentInspection>();
    inspection->rawWidth=4002;inspection->rawHeight=2000;
    inspection->width=1001;inspection->height=500;inspection->cellStrideBayerCells=2;
    const auto count=std::size_t(inspection->width)*inspection->height;
    inspection->confidence.assign(count,1);inspection->rejectionBits.assign(count,0);
    inspection->confidence[125*1001+500]=.25f;
    inspection->confidence[125*1001+501]=.75f;
    inspection->rejectionBits[125*1001+500]=8;
    const auto checkBoundary=[&](const AlignmentInspection& grid) {
        unsigned x=0,y=0;
        require(FindAlignmentInspectionCell(grid,2003,503,x,y)&&x==500&&y==125,
            "Alignment inspection shifted a fixed cell boundary through extent scaling.");
        require(grid.confidence[y*grid.width+x]==.25f&&grid.rejectionBits[y*grid.width+x]==8,
            "Alignment inspection probe selected the neighboring cell's evidence.");
        require(FindAlignmentInspectionCell(grid,2004,503,x,y)&&x==501&&y==125,
            "Alignment inspection did not advance at the exact fixed boundary.");
        require(FindAlignmentInspectionCell(grid,4001,1999,x,y)&&x==1000&&y==499,
            "Alignment inspection lost the shorter final fixed cell.");
        require(!FindAlignmentInspectionCell(grid,-.1,0,x,y)&&
            !FindAlignmentInspectionCell(grid,4002,0,x,y)&&
            !FindAlignmentInspectionCell(grid,0,2000,x,y)&&
            !FindAlignmentInspectionCell(grid,std::numeric_limits<double>::quiet_NaN(),0,x,y)&&
            !FindAlignmentInspectionCell(grid,0,std::numeric_limits<double>::infinity(),x,y),
            "Alignment inspection accepted an invalid raw coordinate.");
    };
    checkBoundary(*inspection);
    auto reference=std::make_shared<AlignmentInspection>();
    reference->rawWidth=8;reference->rawHeight=6;reference->width=4;reference->height=3;
    reference->cellStrideBayerCells=1;reference->confidence.assign(12,1);reference->rejectionBits.assign(12,0);
    unsigned x=0,y=0;
    require(FindAlignmentInspectionCell(*reference,3,5,x,y)&&x==1&&y==2,
        "Fixed-reference inspection no longer owns one Bayer cell per grid cell.");
    BracketingAnalysis analysis;analysis.alignmentInspection={{"capture",inspection},{"reference",reference}};
    const auto encoded=BurstEngine::Wire::Analysis(analysis);
    const auto roundTrip=[&](const BurstEngine::Json& document) {
        return BurstEngine::Wire::Analysis(BurstEngine::Json::from_cbor(BurstEngine::Json::to_cbor(document)));
    };
    const auto decoded=roundTrip(encoded);
    checkBoundary(*decoded->alignmentInspection.at("capture"));
    const auto& decodedReference=*decoded->alignmentInspection.at("reference");
    require(decodedReference.cellStrideBayerCells==1&&FindAlignmentInspectionCell(decodedReference,3,5,x,y)&&x==1&&y==2,
        "Wire roundtrip lost the fixed-reference inspection stride.");
    require(decoded->alignmentInspection.at("capture")->confidence==inspection->confidence&&
        decoded->alignmentInspection.at("capture")->rejectionBits==inspection->rejectionBits,
        "Wire roundtrip changed alignment inspection evidence.");
    auto legacy=encoded;legacy["alignmentInspection"]["capture"].erase("cellStrideBayerCells");
    const auto unknown=roundTrip(legacy)->alignmentInspection.at("capture");
    require(unknown->confidence==inspection->confidence&&unknown->rejectionBits==inspection->rejectionBits,
        "Legacy wire handling discarded alignment inspection evidence.");
    require(unknown->cellStrideBayerCells==0&&!HasAlignmentInspectionGrid(*unknown)&&
        !FindAlignmentInspectionCell(*unknown,2003,503,x,y),
        "Legacy wire inspection invented exact grid ownership without stride metadata.");
    auto malformed=encoded;malformed["alignmentInspection"]["capture"]["cellStrideBayerCells"]=3;
    bool rejected=false;
    try {roundTrip(malformed);}catch(const std::runtime_error&) {rejected=true;}
    require(rejected,"Wire accepted a nonzero inspection stride inconsistent with its grid dimensions.");
}
}
