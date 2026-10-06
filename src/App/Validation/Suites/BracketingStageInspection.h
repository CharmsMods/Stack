#pragma once
#include "Raw/Bracketing/GroupTileStore.h"
#include "Raw/Bracketing/HdrColorFusion.h"
#include "BracketingInspectionOutput.h"

#include <fstream>

namespace Stack::Validation {
// Independently read the pre-publication group cache. Publication must retain
// these temporal measurements, with no hidden spatial filter on the path.
inline void WriteBracketStageInspection(const Raw::Bracketing::ProcessingRequest& request,
    const Raw::Bracketing::BracketingResult& result,const std::filesystem::path& output,
    nlohmann::json& report,const std::vector<std::array<unsigned,2>>& regions) {
    using namespace Raw::Bracketing;
    const auto& data=*result.analysis->prepared;
    const auto& frame=data.sources[data.origin].frame;
    std::vector<float> temporal(static_cast<std::size_t>(data.width)*data.height),variance(temporal.size());
    Raw::Mfd::DirectoryNormalizedTileCache cache(data.directory);std::string error;
    for(unsigned ty=0;ty<frame.tileRows;++ty) for(unsigned tx=0;tx<frame.tileColumns;++tx) {
        std::vector<std::vector<Observation>> groups;Raw::Mfd::PreparedRawTile origin;
        if(!ReadGroupTile(request,data,tx,ty,groups)||
           Raw::Mfd::ReadPreparedTile(frame,cache,tx,ty,origin,&error)!=Raw::Mfd::TileCacheReadStatus::Hit)
            throw std::runtime_error("Stage inspection could not read temporal measurements: "+error);
        ColorObservations samples;std::array<Pixel,4> color;
        for(unsigned y=0;y<origin.extent.height;++y) for(unsigned x=0;x<origin.extent.width;++x) {
            const auto p=y*origin.extent.width+x,px=origin.originX+x,py=origin.originY+y;
            const double ev=result.analysis->guideEv[(py/2)*(data.width/2)+px/2];
            const auto shares=Evaluate(request.recipe.automatic?result.analysis->suggestion:request.recipe.knots,ev);
            if((x&1u)==0) {
                GatherColorCell(groups,(y&~1u)*origin.extent.width+x,origin.extent.width,samples);
                color=BlendColor(samples,shares,request.recipe);
            }
            const auto& value=color[(y%2)*2+x%2];const double gain=origin.comparisonGain[p];
            temporal[py*data.width+px]=static_cast<float>(value.value/gain);
            variance[py*data.width+px]=static_cast<float>(value.measurementVariance/(gain*gain));
        }
    }
    const auto write=[&](const char* name,const std::vector<float>& values) {
        std::ofstream stream(output/name,std::ios::binary);
        if(!stream.write(reinterpret_cast<const char*>(values.data()),values.size()*sizeof(float)))
            throw std::runtime_error("Could not write bracket stage inspection.");
    };
    write("temporal-linear.f32",temporal);write("temporal-variance.f32",variance);
    const double maximumDifference=[&] {
        double maximum=0;const auto& published=*result.raw->normalizedMosaicBuffer;
        for(std::size_t p=0;p<temporal.size();++p) maximum=std::max(maximum,double(std::abs(temporal[p]-published[p])));
        return maximum;
    }();
    report["stageInspection"]={{"temporalMosaic","temporal-linear.f32"},
        {"temporalVariance","temporal-variance.f32"},{"fixedGuideAndContributions",true},
        {"publishedMaximumDifference",maximumDifference}};
    if(maximumDifference!=0) throw std::runtime_error("Published bracket differs from temporal fusion.");
    if(!WriteBracketNativeComparisons(result,output,report,regions,&temporal))
        throw std::runtime_error("Could not write color stage comparisons.");
}

}
