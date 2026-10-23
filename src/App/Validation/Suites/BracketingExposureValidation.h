#pragma once
#include "Raw/Bracketing/ExposureRefinement.h"
#include "Raw/Bracketing/ExposureEvidence.h"
#include <stdexcept>

namespace Stack::Validation {
inline void ValidateBracketingCalibrationPublication() {
    using namespace Raw::Bracketing;
    ProcessingRequest request;request.recipe.alignmentMode=AlignmentMode::FixedCoordinates;
    request.recipe.groups={{"a","A",true,{{"reference"},{"alternate"}}}};
    const auto initialize=[](PreparedDataset& data) {
        data.width=data.height=16;
        for(unsigned i=0;i<2;++i) {
            PreparedSource source;source.id=i?"alternate":"reference";source.scale=i?3:1;
            source.original=std::make_shared<CapturePreview>();auto& proxy=*source.original;
            proxy.width=proxy.height=8;proxy.rgb.assign(8*8*3,.2f);
            // Deliberately no usable overlap: only an explicit anchor or prior
            // can establish the alternate's exposure in these admission checks.
            proxy.clipped.assign(8*8,1);data.sources.push_back(std::move(source));
        }
    };
    std::vector<std::string> diagnostics;std::string error;
    {
        PreparedDataset data;initialize(data);
        if(!Calibrate(request,data,diagnostics,error,true,true)||
            !data.sources[1].enabled||data.sources[1].exposureCalibrated||
            std::abs(data.sources[1].scale-3)>1e-12||!(data.sources[1].scaleVariance>0))
            throw std::runtime_error("Unresolved exposure did not retain an uncertain enabled search seed");
        if(!Calibrate(request,data,diagnostics,error,true,false)||
            !data.sources[0].enabled||!data.sources[0].exposureCalibrated||
            data.sources[1].enabled||data.sources[1].exposureCalibrated)
            throw std::runtime_error("Final exposure admission did not preserve the calibrated reference alone");
    }
    auto& manual=request.recipe.groups[0].frames[1];manual.manualExposure=true;manual.relativeEv=-2;
    {
        PreparedDataset data;initialize(data);data.sources[0].enabled=false;
        if(!Calibrate(request,data,diagnostics,error,true,false)||data.sources[0].enabled||
            !data.sources[1].enabled||!data.sources[1].exposureCalibrated||data.sources[1].scale!=4)
            throw std::runtime_error("Disconnected manual exposure anchor was not retained with disabled origin");
    }
    {
        PreparedDataset data;initialize(data);
        data.sources[0].scale=1.5;data.sources[0].scaleVariance=.01;
        data.sources[1].scaleVariance=.02;
        // Defensive internal-API check, outside the public recipe +/-32 EV
        // range: finite EV can overflow the derived squared gain variance.
        manual.relativeEv=-600;
        if(Calibrate(request,data,diagnostics,error,true,false)||
            error.find("representable")==std::string::npos||
            data.sources[0].scale!=1.5||data.sources[1].scale!=3||
            data.sources[0].scaleVariance!=.01||data.sources[1].scaleVariance!=.02)
            throw std::runtime_error("Invalid derived exposure gain partially published calibration coefficients");
    }
}

inline void ValidateBracketingLocalExposure() {
    using namespace Raw::Bracketing;
    namespace Mfd=Raw::Mfd;
    ProcessingRequest request;
    request.recipe.groups={{"a","A",true,{{"reference"}}},{"b","B",true,{{"alternate"}}}};
    PreparedSource reference,alternate;reference.id="reference";alternate.id="alternate";
    // This fixture supplies known identity correspondence, not a motion search.
    alternate.localMotionHasRegisteredGeometry=true;
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
        // Cache equivalence assumes the supplied synthetic geometry is known.
        source.alignmentDiagnostic.accepted=true;
        source.localMotionHasRegisteredGeometry=true;
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
            if(expected[p].valid!=actual[p].valid||expected[p].color!=actual[p].color)
                throw std::runtime_error("Evicting exposure evidence changed local correspondence or clipping");
    }
    // A current local field supersedes the global diagnostic. Synthetic
    // Structured counts alone must not fabricate registered provenance.
    roomy.recipe.alignmentMode=AlignmentMode::AutomaticLocal;
    auto& provenance=proxies.sources[1];provenance.localMotion.reset();
    provenance.alignmentDiagnostic.accepted=false;
    ExposureEvidenceCache guarded(roomy,proxies);
    if(!guarded.HasRegisteredGeometry(0)||guarded.HasRegisteredGeometry(1))
        throw std::runtime_error("Exposure evidence lost the origin or accepted failed global geometry");
    provenance.alignmentDiagnostic.accepted=true;
    if(!guarded.HasRegisteredGeometry(1))
        throw std::runtime_error("Accepted global geometry was not usable before local alignment");
    provenance.localMotion=std::make_shared<Mfd::LocalMotionGrid>(grid);
    provenance.localMotionHasRegisteredGeometry=false;
    if(guarded.HasRegisteredGeometry(1))
        throw std::runtime_error("Synthetic Structured nodes fabricated exposure geometry provenance");
    provenance.localMotionHasRegisteredGeometry=true;
    if(!guarded.HasRegisteredGeometry(1))
        throw std::runtime_error("Registered current local geometry was rejected");
    provenance.localMotionHasRegisteredGeometry=false;
    roomy.recipe.alignmentMode=AlignmentMode::FixedCoordinates;
    if(!guarded.HasRegisteredGeometry(1))
        throw std::runtime_error("Explicit fixed-coordinate geometry was rejected");
    ValidateBracketingCalibrationPublication();
}
}
