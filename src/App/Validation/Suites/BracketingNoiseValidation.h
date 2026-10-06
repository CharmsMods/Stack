#pragma once
#include "Raw/Bracketing/BurstNoise.h"
#include "Raw/Bracketing/PhotometricReliability.h"
#include "Raw/Bracketing/ExposureGraph.h"
#include <random>
#include <stdexcept>

namespace Stack::Validation {
inline void ValidateBracketingNoiseEvidence() {
    using namespace Raw::Bracketing;
    std::mt19937 random(4157);std::normal_distribution<double> normal;
    std::vector<BurstNoiseBlock> blocks;
    for(unsigned b=0;b<512;++b) {
        const double signal=.005+.5*b/511;
        const double variance=.00008*signal+.000012;
        std::vector<double> details;
        for(unsigned i=0;i<64;++i) details.push_back(normal(random)*std::sqrt(variance));
        BurstNoiseBlock block;
        if(!MeasureBurstNoiseBlock(details,signal,block)) throw std::runtime_error("Burst noise block failed");
        if(b%19==0) block.variance*=20; // minority moving/structured patches
        blocks.push_back(block);
    }
    Raw::Mfd::SiteNoiseProfile previous,profile;previous.quantizationIncluded=true;
    previous.shotScale=.0001;previous.offsetVariance=1e-7;
    if(!FitBurstNoise(blocks,previous,profile)||std::abs(profile.offsetVariance/.000012-1)>.2||
        std::abs(profile.shotScale/.00008-1)>.25)
        throw std::runtime_error("Temporal noise fit did not recover the Poisson-Gaussian profile");
    std::vector<BurstNoiseBlock> darkBlocks(128,{.003,2e-7});
    previous.shotScale=.0004;previous.offsetVariance=4e-6;
    if(!FitBurstNoise(darkBlocks,previous,profile)||
        std::abs((profile.shotScale*.003+profile.offsetVariance)/2e-7-1)>.05)
        throw std::runtime_error("Dark-burst calibration retained an excessive generic noise level");
    // Noise energy at the expected level does not count as scene structure.
    if(NoiseCorrectedMismatch(0,49,49)>1e-12||NoiseCorrectedMismatch(245,1225,49)<20)
        throw std::runtime_error("Noise-corrected mismatch confused noise and coherent displacement");
    Raw::Mfd::LocalMotionGrid motion;motion.valid=true;motion.width=motion.height=2;
    motion.referenceRawExtent=motion.sourceRawExtent={64,64};
    motion.spacingRawX=motion.spacingRawY=63;motion.nodes.resize(4);motion.structuredCount=4;
    for(auto& node:motion.nodes) {node.state=Raw::Mfd::MotionNodeState::Structured;
        node.confidence=1;node.covarianceRaw={.0025,0,.0025};}
    Raw::Mfd::ReliabilityBuildRequest request;request.rawExtent={64,64};request.motionGrid=&motion;
    Raw::Mfd::CfaLayout::TryCreate(Raw::CfaPattern::RGGB,request.layout);
    request.noiseQuality=Raw::Mfd::NoiseModelQuality::GenericLowConfidence;
    std::vector<double> samples(64*64);
    for(auto& sample:samples) sample=normal(random)*.005;
    bool displaced=false,missingReference=false;
    request.residualEvaluator=[&](Raw::Mfd::RawCoordinate raw,Raw::Mfd::CfaSite site,
        const Raw::Mfd::LocalMotionFieldSample&,Raw::Mfd::ReliabilityResidualSample& sample) {
        sample.valid=true;sample.hardValid=!missingReference;
        sample.referenceValue=.02;sample.alternateValue=.02+samples[static_cast<unsigned>(raw.y)*64+static_cast<unsigned>(raw.x)];
        sample.referenceGateVariance=sample.alternateGateVariance=.0000125;
        if(displaced&&site==Raw::Mfd::CfaSite::Red&&raw.x>=16&&raw.x<48&&raw.y>=16&&raw.y<48)
            sample.alternateValue+=.05;
        return true;
    };
    Raw::Mfd::ReliabilityMap map;std::string error;
    const auto patch=Raw::Mfd::ReliabilityRejectMask(Raw::Mfd::ReliabilityRejectBit::PatchRejected);
    if(!BuildColorReliability(request,map,error)) throw std::runtime_error(error);
    unsigned rejected=0;for(const auto& cell:map.cells) rejected+=(cell.rejectionBits&patch)!=0;
    if(rejected>map.cells.size()/100) throw std::runtime_error("Noise-only patches lost their temporal support");
    displaced=true;
    if(!BuildColorReliability(request,map,error)||!(map.cells[16*32+16].rejectionBits&patch))
        throw std::runtime_error("Generic noise confidence bypassed a single-color displaced boundary");
    displaced=false;missingReference=true;
    if(!BuildColorReliability(request,map,error)) throw std::runtime_error(error);
    for(const auto& cell:map.cells) if(cell.rejectionBits&patch)
        throw std::runtime_error("Missing reference evidence was mistaken for a photometric contradiction");
    std::vector<double> ev={0,0,0},uncertainty;
    const std::vector<ExposureEdge> connected={{0,1,1,.0001},{1,2,1.02,.0001},{0,2,2.01,.0001}};
    if(!SolveExposureGraph(connected,{true,false,false},ev,uncertainty)||
        std::abs(ev[1]-.9966666667)>1e-6||std::abs(ev[2]-2.0133333333)>1e-6||uncertainty[1]<=0)
        throw std::runtime_error("Exposure graph did not balance independent overlap constraints");
    ev={0,0,0};
    if(SolveExposureGraph({{0,1,1,.0001}},{true,false,false},ev,uncertainty))
        throw std::runtime_error("Exposure graph accepted a disconnected HDR input");
}
}
