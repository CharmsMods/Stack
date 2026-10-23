#include "AlignmentProposalInput.h"
#include "NodeMath/ContractTypes.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

namespace Raw::Bracketing {
namespace {
bool Digest(const std::string& value) {
    return value.size()==64&&std::all_of(value.begin(),value.end(),
        [](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
bool Fail(std::string& error,const std::string& detail) {
    error="Alignment proposal: "+detail;return false;
}
struct Field {
    AlignmentProposal descriptor;
    std::vector<float> flow;
    bool Map(Mfd::RawCoordinate raw,Mfd::RawCoordinate& mapped) const {
        const auto& p=descriptor;
        const double x=(raw.x-p.originRaw)/p.stepRaw,y=(raw.y-p.originRaw)/p.stepRaw;
        if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>=p.width-1||y>=p.height-1)return false;
        const auto ix=static_cast<unsigned>(x),iy=static_cast<unsigned>(y);
        const double fx=x-ix,fy=y-iy;
        double dx=0,dy=0;
        for(unsigned j=0;j<2;++j)for(unsigned i=0;i<2;++i) {
            const auto offset=(std::size_t(iy+j)*p.width+ix+i)*2;
            const double w=(i?fx:1-fx)*(j?fy:1-fy);
            dx+=w*flow[offset];dy+=w*flow[offset+1];
        }
        mapped={raw.x+p.stepRaw*dx,raw.y+p.stepRaw*dy};
        return std::isfinite(mapped.x)&&std::isfinite(mapped.y)&&mapped.x>=0&&mapped.y>=0&&
            mapped.x<p.rawWidth&&mapped.y<p.rawHeight;
    }
};
bool Load(const AlignmentProposal& p,bool reverse,const ProcessingRequest& request,
    std::shared_ptr<const Field>& result,std::string& error) {
    if(request.shouldCancel&&request.shouldCancel())return Fail(error,"Canceled");
    auto field=std::make_shared<Field>();field->descriptor=p;
    const auto bytes=std::uint64_t(p.width)*p.height*2*sizeof(float);
    const auto& path=reverse?p.reversePath:p.forwardPath;
    const auto& digest=reverse?p.reverseSha256:p.forwardSha256;
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input||input.tellg()!=static_cast<std::streamoff>(bytes))return Fail(error,"field size differs from its descriptor.");
    field->flow.resize(static_cast<std::size_t>(bytes/sizeof(float)));input.seekg(0);
    if(!input.read(reinterpret_cast<char*>(field->flow.data()),static_cast<std::streamsize>(bytes)))
        return Fail(error,"field could not be read completely.");
    const std::string_view content(reinterpret_cast<const char*>(field->flow.data()),static_cast<std::size_t>(bytes));
    if(Stack::NodeMath::Sha256ContentIdentity(std::vector<std::string_view>{content})!="sha256:"+digest)
        return Fail(error,"field checksum differs from its descriptor.");
    if(!std::all_of(field->flow.begin(),field->flow.end(),[](float v){return std::isfinite(v);}))
        return Fail(error,"field contains nonfinite motion.");
    if(request.shouldCancel&&request.shouldCancel())return Fail(error,"Canceled");
    result=std::move(field);return true;
}
}
bool ValidateAlignmentProposals(const ProcessingRequest& request,std::string& error) {
    if(request.alignmentProposals.empty())return true;
    if(request.recipe.alignmentMode!=AlignmentMode::AutomaticLocal||
       request.recipe.reconstruction==ReconstructionMode::Panorama)
        return Fail(error,"requires automatic local, non-panorama alignment.");
    if(request.alignmentProposals.size()>request.sources.size())return Fail(error,"too many pairs.");
    std::set<std::string> ids;
    for(const auto& p:request.alignmentProposals) {
        const auto reference=std::find_if(request.sources.begin(),request.sources.end(),
            [&](const Source& s){return s.frameId==p.referenceFrameId;});
        const auto source=std::find_if(request.sources.begin(),request.sources.end(),
            [&](const Source& s){return s.frameId==p.sourceFrameId;});
        if(p.referenceFrameId!=request.recipe.originFrameId||p.sourceFrameId==p.referenceFrameId||
           !ids.insert(p.sourceFrameId).second||reference==request.sources.end()||source==request.sources.end()||
           !Digest(p.referenceSha256)||!Digest(p.sourceSha256)||
           reference->sha256!=p.referenceSha256||source->sha256!=p.sourceSha256)
            return Fail(error,"source identities do not match the selected captures.");
        if(!Digest(p.forwardSha256)||!Digest(p.reverseSha256)||p.modelIdentity.empty()||
           p.modelIdentity.size()>4096||p.forwardPath.empty()||p.reversePath.empty())
            return Fail(error,"missing model or field identity.");
        const auto pixels=std::uint64_t(p.width)*p.height;
        // At most 64 MiB for the two fields; reserve no more than 1/16 of the
        // explicit job budget. Load only one pair, outside rendering/merging.
        if(p.width<2||p.height<2||pixels>4*1024*1024||pixels*16>request.memoryBudgetBytes/16||
           p.rawWidth<2||p.rawHeight<2||!std::isfinite(p.originRaw)||p.originRaw<0||
           !std::isfinite(p.stepRaw)||p.stepRaw<1||
           p.originRaw+(p.width-1)*p.stepRaw>=p.rawWidth||
           p.originRaw+(p.height-1)*p.stepRaw>=p.rawHeight)
            return Fail(error,"invalid geometry or pair exceeds the bounded memory allowance.");
    }
    return true;
}
bool AttachAlignmentProposal(const ProcessingRequest& request,const std::string& sourceId,
    Mfd::BidirectionalLocalMotionRequest& motion,std::string& error) {
    const auto found=std::find_if(request.alignmentProposals.begin(),request.alignmentProposals.end(),
        [&](const AlignmentProposal& p){return p.sourceFrameId==sourceId;});
    if(found==request.alignmentProposals.end())return true;
    const auto& p=*found;
    if(!motion.reference||!motion.alternate||p.rawWidth!=motion.reference->rawExtent.width||
       p.rawHeight!=motion.reference->rawExtent.height||p.rawWidth!=motion.alternate->rawExtent.width||
       p.rawHeight!=motion.alternate->rawExtent.height)return Fail(error,"decoded RAW geometry differs.");
    std::shared_ptr<const Field> forward,reverse;
    if(!Load(p,false,request,forward,error)||!Load(p,true,request,reverse,error))return false;
    motion.forwardProposal=[forward](Mfd::RawCoordinate point,Mfd::RawCoordinate& source){return forward->Map(point,source);};
    motion.reverseProposal=[reverse](Mfd::RawCoordinate point,Mfd::RawCoordinate& source){return reverse->Map(point,source);};
    return true;
}
} // namespace Raw::Bracketing
