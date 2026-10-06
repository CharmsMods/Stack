#pragma once
#include "Raw/Bracketing/ExposureRefinement.h"
#include "Raw/Bracketing/ExposureEvidence.h"
#include <stdexcept>

namespace Stack::Validation {
inline void ValidateBracketingLocalExposure() {
    using namespace Raw::Bracketing;
    namespace Mfd=Raw::Mfd;
    ProcessingRequest request;
    request.recipe.groups={{"a","A",true,{{"reference"}}},{"b","B",true,{{"alternate"}}}};
    PreparedSource reference,alternate;reference.id="reference";alternate.id="alternate";
    reference.frame.activeCfaPattern=Raw::CfaPattern::RGGB;reference.frame.activeExtent={256,256};
    alternate.frame=reference.frame;
    Mfd::CfaPlanePyramid a,b;
    for(unsigned site=0;site<4;++site) {
        Mfd::CfaPyramidLevel level;level.site=static_cast<Mfd::CfaSite>(site);level.extent={128,128};
        level.signal.resize(128*128);level.variance.assign(128*128,1e-8);level.validMask.assign(128*128,1);
        for(unsigned y=0;y<128;++y) for(unsigned x=0;x<128;++x)
            level.signal[y*128+x]=.1+.03*site+.0001*x+.0002*y;
        a.planes[site].push_back(level);
        for(auto& sample:level.signal) sample*=.5;
        b.planes[site].push_back(level);
    }
    Mfd::LocalMotionGrid grid;grid.valid=true;grid.width=grid.height=2;
    grid.referenceRawExtent=grid.sourceRawExtent={256,256};grid.spacingRawX=grid.spacingRawY=255;
    grid.nodes.resize(4);grid.structuredCount=4;
    for(auto& node:grid.nodes) {node.state=Mfd::MotionNodeState::Structured;
        node.confidence=1;node.covarianceRaw={.0001,0,.0001};}
    Mfd::LocalMotionOptions options;std::vector<std::string> diagnostics;
    const auto refine=[&] {RefineLocallyRegisteredExposure(request,reference,alternate,a,b,grid,options,diagnostics);};
    alternate.scale=2.1;refine();
    if(std::abs(alternate.scale-2)>1e-6||alternate.scaleVariance<=0)
        throw std::runtime_error("Local HDR exposure refinement did not recover the known scale");
    for(auto& value:b.planes[0][0].signal) value*=1.2;
    alternate.scale=2.1;refine();
    if(alternate.scale!=2.1) throw std::runtime_error("Local exposure calibration accepted color-inconsistent edges");
    for(auto& value:b.planes[0][0].signal) value/=1.2;
    request.recipe.groups[1].frames[0].manualExposure=true;alternate.scale=2.1;refine();
    if(alternate.scale!=2.1) throw std::runtime_error("Local exposure refinement changed a manual correction");
    request.recipe.groups[1].frames[0].manualExposure=false;
    for(auto& plane:a.planes) plane[0].validMask.assign(128*128,0);
    alternate.scale=2.1;refine();
    if(alternate.scale!=2.1) throw std::runtime_error("Missing HDR overlap changed the retained exposure scale");
    PreparedDataset proxies;proxies.width=proxies.height=128;
    for(unsigned i=0;i<4;++i) {
        PreparedSource source;source.proxyStride=2;
        source.original=std::make_shared<CapturePreview>();auto& proxy=*source.original;
        proxy.width=proxy.height=64;proxy.rgb.resize(64*64*3);proxy.clipped.resize(64*64);
        for(unsigned p=0;p<64*64;++p) {
            proxy.clipped[p]=p%127==i;
            for(unsigned c=0;c<3;++c) proxy.rgb[p*3+c]=float(.02+.003*i+.001*c+.00001*p);
        }
        if(i%2) source.localMotion=std::make_shared<Mfd::LocalMotionGrid>(grid);
        proxies.sources.push_back(std::move(source));
    }
    ProcessingRequest roomy,tight;roomy.memoryBudgetBytes=1024ull*1024*1024;tight.memoryBudgetBytes=16ull*1024*1024;
    ExposureEvidenceCache resident(roomy,proxies),evicting(tight,proxies);
    for(unsigned pass=0;pass<2;++pass) for(unsigned source=0;source<4;++source) {
        const auto& expected=resident.Get(source);const auto& actual=evicting.Get(source);
        if(expected.size()!=actual.size()) throw std::runtime_error("Exposure evidence changed with memory budget");
        for(std::size_t p=0;p<expected.size();++p)
            if(expected[p].valid!=actual[p].valid||expected[p].values!=actual[p].values)
                throw std::runtime_error("Evicting exposure evidence changed local correspondence or clipping");
    }
}
}
