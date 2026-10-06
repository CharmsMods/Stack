#include "ProcessingInternal.h"
#include "PresentationEvidence.h"
#include "GroupTileStore.h"
#include "LocalDetail.h"
#include <algorithm>

namespace Raw::Bracketing {
namespace {
bool MultipleInputs(const PreparedDataset& data) {
    return std::count_if(data.sources.begin(),data.sources.end(),[](const auto& source){return source.enabled;})>1;
}
bool TemporalGroups(const ProcessingRequest& request,const PreparedDataset& data,unsigned tx,unsigned ty,
    std::vector<std::vector<Observation>>& groups,std::string& error) {
    if(ReadGroupTile(request,data,tx,ty,groups)) return true;
    Mfd::PreparedRawTile unused;
    if(!ReadTemporalGroups(request,data,tx,ty,groups,unused,error)) return false;
    if(!CombineLocalDetail(request,data,tx,ty,groups,error))return false;
    CanonicalizeGroupTile(groups);
    return WriteGroupTile(request,data,tx,ty,groups,error);
}

}
bool PrepareGroupTile(const ProcessingRequest& request,const PreparedDataset& data,unsigned tx,unsigned ty,std::string& error) {
    if(!MultipleInputs(data)) return true;
    std::vector<std::vector<Observation>> groups;
    if(!TemporalGroups(request,data,tx,ty,groups,error))return false;
    if(request.evidenceStream&&!groups.empty()) {
        const auto side=data.sources[data.origin].frame.tileRawPixels;
        const unsigned left=tx*side,top=ty*side,w=std::min(side,data.width-left),h=std::min(side,data.height-top);
        Mfd::CfaLayout layout;Mfd::CfaLayout::TryCreate(data.sources[data.origin].frame.activeCfaPattern,layout);
        request.evidenceStream->Tile(request,ProcessingStage::Groups,left,top,w,h,[&](unsigned x,unsigned y) {
            PresentationSample sample;
            for(unsigned c=0;c<4;++c) {
                const auto& value=groups[PresentationGroup(request)][std::size_t(y-top+c/2)*w+x-left+c%2];
                const auto site=layout.SiteAt(x+c%2,y+c/2);
                const unsigned channel=site==Mfd::CfaSite::Red?0:site==Mfd::CfaSite::Blue?2:1;
                sample.rgb[channel]+=value.value*(channel==1?.5:1);sample.first+=float(value.support*.25);
                sample.known&=value.finite;
            }return sample;
        });
    }
    return true;
}
bool ReadGroups(const ProcessingRequest& request,const PreparedDataset& data,unsigned tx,unsigned ty,
    std::vector<std::vector<Observation>>& groups,Mfd::PreparedRawTile& origin,std::string& error) {
    if(!MultipleInputs(data)) return ReadTemporalGroups(request,data,tx,ty,groups,origin,error);
    Mfd::DirectoryNormalizedTileCache sourceCache(data.directory);
    const auto& frame=data.sources[data.origin].frame;
    if(Mfd::ReadPreparedTile(frame,sourceCache,tx,ty,origin,&error)!=Mfd::TileCacheReadStatus::Hit) return false;
    return TemporalGroups(request,data,tx,ty,groups,error);
}
}
