#include "PhotometricReliability.h"
#include <array>
#include <atomic>
#include <thread>

namespace Raw::Bracketing {
namespace {
struct Evidence { std::array<float,4> z{};std::uint8_t valid=0; };
}

bool BuildColorReliability(const Mfd::ReliabilityBuildRequest& request,
    Mfd::ReliabilityMap& result,std::string& error) {
    result={};
    const auto stride=request.cellStrideBayerCells;
    if(!stride||!request.motionGrid||!request.motionGrid->valid||!request.residualEvaluator||
        request.rawExtent.width<2||request.rawExtent.height<2) {error="Invalid bracket reliability request.";return false;}
    const auto rawCellWidth=request.rawExtent.width/2,rawCellHeight=request.rawExtent.height/2;
    const unsigned width=static_cast<unsigned>((rawCellWidth+stride-1)/stride);
    const unsigned height=static_cast<unsigned>((rawCellHeight+stride-1)/stride);
    const std::size_t count=static_cast<std::size_t>(width)*height;
    result.rawExtent=request.rawExtent;result.cellExtent={width,height};
    result.noiseQuality=request.noiseQuality;result.noiseConfidence=1;
    result.cells.resize(count);std::vector<Evidence> evidence(count);
    const auto canceled=[&](){return request.shouldCancel&&request.shouldCancel();};
    const auto rows=[&](const auto& evaluate) {
        std::atomic<unsigned> next{0};
        const auto run=[&](){while(!canceled()) {const auto y=next.fetch_add(1);if(y>=height) break;evaluate(y);}};
        std::vector<std::thread> workers;
        const auto workersToUse=std::clamp(request.workerCount,1u,std::max(1u,height));
        for(unsigned i=1;i<workersToUse;++i) workers.emplace_back(run);
        run();for(auto& worker:workers) worker.join();
        return !canceled();
    };
    if(!rows([&](unsigned y) {
        for(unsigned x=0;x<width;++x) {
            const auto index=static_cast<std::size_t>(y)*width+x;
            auto& cell=result.cells[index];cell.cellX=x;cell.cellY=y;
            const auto cx=std::min<std::uint64_t>(rawCellWidth-1,static_cast<std::uint64_t>(x)*stride+stride/2);
            const auto cy=std::min<std::uint64_t>(rawCellHeight-1,static_cast<std::uint64_t>(y)*stride+stride/2);
            Mfd::LocalMotionFieldSample center;
            if(!Mfd::EvaluateLocalMotionField(*request.motionGrid,{2.*cx+.5,2.*cy+.5},
                request.motionOptions,center,nullptr)) {
                cell.rejectionBits=Mfd::ReliabilityRejectMask(Mfd::ReliabilityRejectBit::AlignmentInvalid);continue;
            }
            cell.alignmentConfidence=std::clamp(center.alignmentConfidence,0.,1.);
            for(unsigned s=0;s<4;++s) {
                const auto site=static_cast<Mfd::CfaSite>(s);const auto offset=request.layout.OffsetFor(site);
                const Mfd::RawCoordinate raw={2.*cx+offset.x,2.*cy+offset.y};
                Mfd::LocalMotionFieldSample motion;Mfd::ReliabilityResidualSample sample;
                if(!Mfd::EvaluateLocalMotionField(*request.motionGrid,raw,request.motionOptions,motion,nullptr)||
                   !request.residualEvaluator(raw,site,motion,sample)||!sample.valid||!sample.hardValid) continue;
                const double variance=sample.referenceGateVariance+sample.alternateGateVariance+
                    request.parameters.fusion.numericalVarianceFloor;
                const double z=(sample.alternateValue-sample.referenceValue)/std::sqrt(variance);
                if(!(variance>0)||!std::isfinite(z)) continue;
                evidence[index].z[s]=static_cast<float>(std::clamp(z,-1e6,1e6));
                evidence[index].valid|=static_cast<std::uint8_t>(1u<<s);
            }
        }
    })) {error="Canceled";return false;}
    if(request.reportProgress) request.reportProgress(.55);
    if(request.reportObservation)try{request.reportObservation(result,true);}catch(...){}
    // One decision across the four color sites avoids accepting a displaced
    // red/blue boundary merely because the two green sites agree. Two patch
    // sizes distinguish incoherent sample noise from a coherent displaced edge.
    if(!rows([&](unsigned y) {
        for(unsigned x=0;x<width;++x) {
            auto& cell=result.cells[static_cast<std::size_t>(y)*width+x];
            if(cell.rejectionBits) continue;
            double mismatch=0;unsigned maximumSupport=0;
            for(const int radius:{1,3}) {
                std::array<double,4> sum{},squared{};std::array<unsigned,4> support{};
                for(int dy=-radius;dy<=radius;++dy) for(int dx=-radius;dx<=radius;++dx) {
                    const auto xx=static_cast<int>(x)+dx,yy=static_cast<int>(y)+dy;
                    if(xx<0||yy<0||xx>=static_cast<int>(width)||yy>=static_cast<int>(height)) continue;
                    const auto& e=evidence[static_cast<std::size_t>(yy)*width+xx];
                    for(unsigned s=0;s<4;++s) if(e.valid&(1u<<s)) {
                        sum[s]+=e.z[s];squared[s]+=double(e.z[s])*e.z[s];++support[s];
                    }
                }
                for(unsigned s=0;s<4;++s) {
                    mismatch=std::max(mismatch,NoiseCorrectedMismatch(sum[s],squared[s],support[s]));
                    maximumSupport=std::max(maximumSupport,support[s]);
                }
            }
            cell.validResidualCount=maximumSupport;cell.patchScale=mismatch;
            cell.patchGate=Mfd::FlatTopQuinticGate(mismatch,3.,7.);
            if(maximumSupport<4) {
                // Missing reference color evidence, including clipped HDR
                // highlights, is uncertainty rather than proof of mismatch.
                // The merge still validates the actual alternate sample.
                cell.rejectionBits|=Mfd::ReliabilityRejectMask(Mfd::ReliabilityRejectBit::InsufficientPatchSupport);
                cell.patchGate=.2;
            } else if(cell.patchGate<=0) {
                cell.rejectionBits|=Mfd::ReliabilityRejectMask(Mfd::ReliabilityRejectBit::PatchRejected);
            }
            cell.rawConfidence=cell.erodedConfidence=cell.reliability=cell.alignmentConfidence*cell.patchGate;
        }
    })) {error="Canceled";return false;}
    for(const auto& cell:result.cells) if(!cell.rejectionBits&&cell.reliability>.05) ++result.usableCellCount;
    result.minimumUsableCellCount=Mfd::MinimumUsableReliabilityCells(count,request.parameters.reliability);
    result.frameUsable=result.usableCellCount>=result.minimumUsableCellCount;
    result.valid=true;result.message="Color-coordinated, noise-corrected bracket reliability.";
    if(request.reportObservation)try{request.reportObservation(result,false);}catch(...){}
    if(request.reportProgress) request.reportProgress(1);
    return true;
}
}
