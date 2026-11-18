#include "Raw/Bracketing/AlignmentProposalGeometry.h"
#include "Raw/Bracketing/AlignmentProposalInput.h"
#include "Raw/Bracketing/ProcessingInternal.h"
#include "NodeMath/ContractTypes.h"
#ifdef _WIN32
#include "App/Validation/Suites/BracketingAnchorEvidenceValidation.h"
#endif
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace Raw::Bracketing;
using namespace Raw::Mfd;
void Check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
bool Near(RawCoordinate a,RawCoordinate b,double tolerance=1e-6) {
    return std::hypot(a.x-b.x,a.y-b.y)<=tolerance;
}

void InverseChecks() {
    const PixelExtent extent{1024,1024};AffineModel identity;
    const auto smooth=[](RawCoordinate p,RawCoordinate& mapped,RawMappingJacobian* j) {
        mapped={p.x+7+.0005*(p.x-50)*(p.x-50),1.01*p.y+std::sin(.02*p.x)};
        if(j){j->linear={1+.001*(p.x-50),0,.02*std::cos(.02*p.x),1.01};j->valid=true;}
        return true;
    };
    RawCoordinate target,inverted{-1,-1};smooth({45,50},target,nullptr);
    Check(ProposalGeometry::InvertReference(smooth,extent,extent,identity,target,inverted)&&Near(inverted,{45,50},.001),
        "The accepted nonlinear anchor map was not inverted");
    unsigned calls=0;
    const auto unsupported=[&](RawCoordinate,RawCoordinate&,RawMappingJacobian*){++calls;return false;};
    inverted={-1,-1};
    Check(!ProposalGeometry::InvertReference(unsupported,extent,extent,identity,target,inverted)&&
        Near(inverted,{-1,-1})&&calls==1,"Missing geometry invented an inverse or changed the output");
    for(unsigned kind=0;kind<4;++kind) {
        const auto unusable=[kind](RawCoordinate p,RawCoordinate& mapped,RawMappingJacobian* j) {
            mapped=p;if(j){j->valid=kind!=0;
                if(kind==1)j->linear={1,0,0,0};
                if(kind==2)j->linear={-1,0,0,1};
                if(kind==3)j->linear={100,0,0,.001};}
            return true;
        };
        Check(!ProposalGeometry::InvertReference(unusable,extent,extent,identity,{50,50},inverted),
            "A missing, singular, folded or ill-conditioned derivative supplied an inverse");
    }
    calls=0;
    const auto tooFar=[&](RawCoordinate p,RawCoordinate& mapped,RawMappingJacobian* j) {
        ++calls;mapped={p.x+200,p.y};if(j)j->valid=true;return true;
    };
    Check(!ProposalGeometry::InvertReference(tooFar,extent,extent,identity,{500,500},inverted)&&calls<=45,
        "Optional inversion exceeded its spatial/work bounds");
    Check(!ProposalGeometry::InvertReference(smooth,extent,extent,identity,{-1,50},inverted)&&
        !ProposalGeometry::InvertReference(smooth,extent,extent,identity,{1024,50},inverted),
        "An out-of-domain target supplied a proposal");
}

struct TemporaryDirectory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("stack-proposal-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory(){std::filesystem::create_directories(path);}
    ~TemporaryDirectory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};

std::string WriteField(const std::filesystem::path& path,float dx,float dy) {
    std::vector<float> flow(8*8*2);
    for(std::size_t p=0;p<flow.size();p+=2){flow[p]=dx;flow[p+1]=dy;}
    std::ofstream stream(path,std::ios::binary);stream.write(reinterpret_cast<const char*>(flow.data()),
        static_cast<std::streamsize>(flow.size()*sizeof(float)));stream.close();Check(bool(stream),"Could not write the bounded proposal check");
    return Stack::NodeMath::Sha256ContentIdentity(std::vector<std::string_view>{
        {reinterpret_cast<const char*>(flow.data()),flow.size()*sizeof(float)}}).substr(7);
}

void AttachmentChecks() {
    TemporaryDirectory directory;
    ProcessingRequest request;request.recipe.originFrameId="origin";
    request.recipe.alignmentMode=AlignmentMode::AutomaticLocal;request.memoryBudgetBytes=64ull*1024*1024;
    request.sources={{"origin",std::string(64,'a')},{"source",std::string(64,'b')},{"anchor",std::string(64,'c')}};
    AlignmentProposal proposal;proposal.referenceFrameId="origin";proposal.sourceFrameId="source";
    proposal.referenceSha256=request.sources[0].sha256;proposal.sourceSha256=request.sources[1].sha256;
    proposal.modelIdentity="constant-motion-contract-check";proposal.width=proposal.height=8;
    proposal.rawWidth=proposal.rawHeight=128;proposal.stepRaw=16;
    proposal.forwardPath=directory.path/"forward.flow";proposal.reversePath=directory.path/"reverse.flow";
    proposal.forwardSha256=WriteField(proposal.forwardPath,.75f,-.375f);
    proposal.reverseSha256=WriteField(proposal.reversePath,-.75f,.375f);
    request.alignmentProposals={proposal};std::string error;
    Check(ValidateAlignmentProposals(request,error),error.c_str());
    PreparedSource origin,source,anchor;origin.id="origin";source.id="source";anchor.id="anchor";
    origin.frame.activeExtent=source.frame.activeExtent=anchor.frame.activeExtent={128,128};
    CfaPlanePyramid reference,alternate;reference.rawExtent=alternate.rawExtent={128,128};
    BidirectionalLocalMotionRequest direct;direct.reference=&reference;direct.alternate=&alternate;
    Check(AttachAlignmentProposal(request,origin,source,direct,error),error.c_str());
    RawCoordinate mapped;
    Check(direct.forwardProposal&&direct.reverseProposal&&direct.forwardProposal({45,45},mapped)&&Near(mapped,{57,39}),
        "Direct-origin forward proposal changed");
    Check(direct.reverseProposal({57,39},mapped)&&Near(mapped,{45,45}),"Direct-origin reverse proposal changed");

    anchor.alignment.translationRaw={4,3};anchor.localMotionHasRegisteredGeometry=true;
    anchor.localMotion=std::make_shared<LocalMotionGrid>();auto& grid=*anchor.localMotion;
    grid.valid=true;grid.width=grid.height=2;grid.spacingRawX=grid.spacingRawY=127;
    grid.referenceRawExtent=grid.sourceRawExtent={128,128};grid.globalWarp=anchor.alignment;grid.structuredCount=4;
    grid.nodes.resize(4);
    for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x) {
        auto& node=grid.nodes[y*2+x];node.gridX=x;node.gridY=y;node.centerRaw={x*127.,y*127.};
        node.residualRaw={6,-2};node.state=MotionNodeState::Structured;node.confidence=1;node.covarianceRaw={.0025,0,.0025};
    }
    BidirectionalLocalMotionRequest cohort;cohort.reference=&reference;cohort.alternate=&alternate;
    Check(AttachAlignmentProposal(request,anchor,source,cohort,error),error.c_str());
    Check(cohort.forwardProposal&&cohort.reverseProposal&&cohort.forwardProposal({55,46},mapped)&&Near(mapped,{57,39}),
        "Origin flow was used directly in cohort coordinates");
    Check(cohort.reverseProposal({57,39},mapped)&&Near(mapped,{55,46}),
        "Reverse proposal did not return cohort coordinates");
    anchor.localMotion.reset();
    Check(cohort.forwardProposal({55,46},mapped)&&Near(mapped,{57,39}),"Proposal callbacks lost their anchor lease");
    Check(!cohort.forwardProposal({0,0},mapped),"An unsupported anchor inverse supplied a hint");

    auto missing=direct;
    anchor.localMotionHasRegisteredGeometry=false;
    Check(AttachAlignmentProposal(request,anchor,source,missing,error)&&!missing.forwardProposal&&!missing.reverseProposal,
        "A cohort with no established geometry supplied learned hints");
    missing=direct;source.id="unproposed";
    Check(AttachAlignmentProposal(request,origin,source,missing,error)&&!missing.forwardProposal&&!missing.reverseProposal,
        "A reused request retained proposals for a different source");
    source.id="source";
    request.alignmentProposals[0].referenceFrameId="unrelated";
    missing=direct;
    Check(!AttachAlignmentProposal(request,origin,source,missing,error)&&!missing.forwardProposal&&!missing.reverseProposal,
        "Wrong proposal reference identity was accepted or retained stale callbacks");
    request.alignmentProposals[0]=proposal;source.frame.activeExtent.width=126;
    Check(!AttachAlignmentProposal(request,origin,source,missing,error),"Wrong decoded source geometry was accepted");
    source.frame.activeExtent.width=128;request.alignmentProposals[0].forwardSha256=std::string(64,'0');
    Check(!AttachAlignmentProposal(request,origin,source,missing,error),"A changed flow payload bypassed its content identity");
}
}

int main() {
    try {
        InverseChecks();AttachmentChecks();
#ifdef _WIN32
        TemporaryDirectory anchors;
        Stack::Validation::ValidateBracketingAnchorEvidence(anchors.path/"anchor-evidence");
        std::cout<<"Native anchor ranking, including AI and external proposals, passed\n";
#endif
        std::cout<<"Alignment proposal coordinates, inverse bounds, ownership and identity checks passed\n";return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
