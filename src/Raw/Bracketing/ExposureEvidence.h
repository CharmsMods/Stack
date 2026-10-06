#pragma once
#include "Alignment.h"
#include <array>
#include <list>
#include <stdexcept>

namespace Raw::Bracketing {
struct ExposurePatch {std::array<double,25> values{};std::uint32_t valid=0;};
struct ExposurePoint {Mfd::RawCoordinate coordinate;unsigned spatialBin=0;};

// Correspondence depends on a source and output coordinate, not the other
// member of an exposure pair. Reuse it across pairs. Memory controls cache
// residency only, so low-memory and Queue jobs evaluate the same evidence.
class ExposureEvidenceCache {
public:
    ExposureEvidenceCache(const ProcessingRequest& request,const PreparedDataset& data):request_(request),data_(data) {
        const auto& origin=data.sources[data.origin];const auto& proxy=*origin.original;
        const unsigned step=std::max(1u,static_cast<unsigned>(std::ceil(std::sqrt(
            static_cast<double>(proxy.width)*proxy.height/4096))));
        for(unsigned y=1;y+1<proxy.height;y+=step) for(unsigned x=1;x+1<proxy.width;x+=step)
            points.push_back({{x*double(origin.proxyStride)+.5,y*double(origin.proxyStride)+.5},
                std::min(7u,y*8/proxy.height)*8+std::min(7u,x*8/proxy.width)});
        for(const auto& source:data.sources) stride_=std::max(stride_,source.proxyStride);
        const auto perSource=std::max<std::size_t>(1,points.size()*sizeof(ExposurePatch));
        capacity_=std::min<std::size_t>(data.sources.size(),
            std::max<std::uint64_t>(2,request.memoryBudgetBytes/16/perSource));
    }
    const std::vector<ExposurePatch>& Get(std::size_t sourceIndex) {
        const auto found=std::find_if(entries_.begin(),entries_.end(),
            [&](const auto& entry){return entry.source==sourceIndex;});
        if(found!=entries_.end()) {entries_.splice(entries_.begin(),entries_,found);return entries_.front().patches;}
        if(entries_.size()>=capacity_) entries_.pop_back();
        Entry entry;entry.source=sourceIndex;entry.patches.resize(points.size());
        for(std::size_t p=0;p<points.size();++p) {
            if(request_.shouldCancel&&request_.shouldCancel()) throw std::runtime_error("Canceled");
            auto& patch=entry.patches[p];unsigned sample=0;
            for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx,++sample) {
                const Mfd::RawCoordinate raw={points[p].coordinate.x+dx*double(stride_),
                    points[p].coordinate.y+dy*double(stride_)};
                double value=0;
                if(SampleAlignmentProxy(data_.sources[sourceIndex],raw,value)&&value<.8) {
                    patch.values[sample]=value;patch.valid|=1u<<sample;
                }
            }
        }
        entries_.push_front(std::move(entry));return entries_.front().patches;
    }
    std::vector<ExposurePoint> points;
private:
    struct Entry {std::size_t source=0;std::vector<ExposurePatch> patches;};
    const ProcessingRequest& request_;const PreparedDataset& data_;
    std::size_t capacity_=2;unsigned stride_=1;std::list<Entry> entries_;
};
}
