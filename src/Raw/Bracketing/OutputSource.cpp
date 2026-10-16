#include "OutputSource.h"
#include "ProcessingInternal.h"
#include <cmath>
#include <stdexcept>

namespace Raw::Bracketing {
std::vector<float> BuildOutputMosaic(const BracketingResult& result,
    const std::function<bool()>& cancel,const std::function<void(double)>& progress) {
    if(!result.raw || !result.raw->normalizedMosaicBuffer ||
       result.raw->normalizedMosaicInputContract != NormalizedMosaicInputContract::BracketingPreGain ||
       !result.analysis || !result.analysis->prepared)
        throw std::runtime_error("RAW output requires the completed Bayer result and its prepared source analysis.");
    const auto& data=*result.analysis->prepared;
    if(data.origin>=data.sources.size() || result.raw->metadata.rawWidth!=data.width ||
       result.raw->metadata.rawHeight!=data.height ||
       result.raw->normalizedMosaicBuffer->size()!=std::size_t(data.width)*data.height)
        throw std::runtime_error("RAW output dimensions do not match the prepared reference.");
    const auto& source=data.sources[data.origin];
    auto mosaic=*result.raw->normalizedMosaicBuffer;
    Mfd::DirectoryNormalizedTileCache cache(data.directory);
    const auto total=std::uint64_t(source.frame.tileRows)*source.frame.tileColumns;
    std::uint64_t completed=0;
    for(unsigned ty=0;ty<source.frame.tileRows;++ty)for(unsigned tx=0;tx<source.frame.tileColumns;++tx) {
        if(cancel&&cancel())throw std::runtime_error("Output canceled.");
        Mfd::PreparedRawTile tile;std::string error;
        if(Mfd::ReadPreparedTile(source.frame,cache,tx,ty,tile,&error)!=Mfd::TileCacheReadStatus::Hit)
            throw std::runtime_error(error.empty()?"Prepared reference calibration is unavailable.":error);
        const auto count=std::size_t(tile.extent.width)*tile.extent.height;
        if(tile.comparisonGain.size()!=count || tile.originX+tile.extent.width>data.width ||
           tile.originY+tile.extent.height>data.height)
            throw std::runtime_error("Prepared reference calibration is invalid.");
        for(unsigned y=0;y<tile.extent.height;++y)for(unsigned x=0;x<tile.extent.width;++x) {
            const auto q=std::size_t(y)*tile.extent.width+x;
            auto& value=mosaic[std::size_t(tile.originY+y)*data.width+tile.originX+x];
            if(!std::isfinite(tile.comparisonGain[q])||tile.comparisonGain[q]<=0)
                throw std::runtime_error("Prepared reference calibration contains an invalid gain.");
            value*=tile.comparisonGain[q];
            if(!std::isfinite(value))throw std::runtime_error("Burst result contains a non-finite RAW measurement.");
        }
        if(progress)progress(double(++completed)/total);
    }
    return mosaic;
}
}
