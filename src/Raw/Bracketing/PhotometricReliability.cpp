#include "PhotometricReliability.h"
#include "ParallelProcessing.h"
#include <array>

namespace Raw::Bracketing {
namespace {
struct Evidence { std::array<float,4> z{};std::uint8_t valid=0; };

struct PatchMoments {
    std::array<double,4> sum{},squared{};
    std::array<unsigned,4> support{};

    void Add(const Evidence& evidence) {
        for(unsigned s=0;s<4;++s)if(evidence.valid&(1u<<s)) {
            const double z=evidence.z[s];
            sum[s]+=z;squared[s]+=z*z;++support[s];
        }
    }
};

struct CompensatedSum {
    double sum=0,correction=0;

    void Add(double value) {
        const double next=sum+value;
        correction+=std::abs(sum)>=std::abs(value)?(sum-next)+value:(value-next)+sum;
        sum=next;
    }

    double Value()const {return sum+correction;}
};

struct PatchWindow {
    std::array<CompensatedSum,4> sum{},squared{};
    std::array<unsigned,4> support{};

    void Accumulate(const PatchMoments& column,bool remove=false) {
        const double sign=remove?-1.:1.;
        for(unsigned s=0;s<4;++s) {
            sum[s].Add(sign*column.sum[s]);squared[s].Add(sign*column.squared[s]);
            if(remove)support[s]-=column.support[s];
            else support[s]+=column.support[s];
            if(!support[s]) {sum[s]={};squared[s]={};}
        }
    }
};

// Each entering column is read once for both patch sizes. Seven columns and
// two windows are sufficient regardless of image width or worker count.
// Compensated updates keep a large rejected residual from contaminating the
// small residuals that remain after its column leaves the window.
template<typename Visitor>
void VisitPatchStatisticsRow(const std::vector<Evidence>& evidence,unsigned width,
    unsigned height,unsigned y,const Visitor& visit) {
    struct Column {PatchMoments small,large;};
    std::array<Column,7> columns;
    PatchWindow small,large;
    const auto load=[&](unsigned x) {
        Column column;
        const auto first=y>3?y-3:0u;
        const auto last=std::min(height-1,y+3);
        for(unsigned yy=first;yy<=last;++yy) {
            const auto& sample=evidence[static_cast<std::size_t>(yy)*width+x];
            column.large.Add(sample);
            if(yy+1>=y&&yy<=y+1)column.small.Add(sample);
        }
        return column;
    };
    for(unsigned x=0;x<std::min(width,4u);++x) {
        columns[x]=load(x);large.Accumulate(columns[x].large);
        if(x<=1)small.Accumulate(columns[x].small);
    }
    for(unsigned x=0;x<width;++x) {
        double mismatch=0;unsigned maximumSupport=0;
        for(const auto* patch:{&small,&large})for(unsigned s=0;s<4;++s) {
            mismatch=std::max(mismatch,NoiseCorrectedMismatch(
                patch->sum[s].Value(),patch->squared[s].Value(),patch->support[s]));
            maximumSupport=std::max(maximumSupport,patch->support[s]);
        }
        visit(x,mismatch,maximumSupport);
        if(x+1==width)break;
        // Advance the small window before replacing the oldest large-window
        // slot. Its entering column is already resident in the seven slots.
        if(x>=1)small.Accumulate(columns[(x-1)%7].small,true);
        if(x+2<width)small.Accumulate(columns[(x+2)%7].small);
        if(x>=3)large.Accumulate(columns[(x-3)%7].large,true);
        if(x+4<width) {
            auto& incoming=columns[(x+4)%7];incoming=load(x+4);
            large.Accumulate(incoming.large);
        }
    }
}
}

bool BuildColorReliability(const Mfd::ReliabilityBuildRequest& request,
    Mfd::ReliabilityMap& result,std::string& error,const CaptureMotionEvaluator& evaluateMotion) {
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
    const auto motionAt=[&](Mfd::RawCoordinate point,Mfd::LocalMotionFieldSample& sample) {
        return evaluateMotion?evaluateMotion(point,sample):
            Mfd::EvaluateLocalMotionField(*request.motionGrid,point,request.motionOptions,sample,nullptr);
    };
    const auto rows=[&](const auto& evaluate) {
        return ProcessParallelIndices(height,request.workerCount,request.shouldCancel,
            [&](std::size_t y) {evaluate(static_cast<unsigned>(y));return true;});
    };
    if(!rows([&](unsigned y) {
        for(unsigned x=0;x<width;++x) {
            const auto index=static_cast<std::size_t>(y)*width+x;
            auto& cell=result.cells[index];cell.cellX=x;cell.cellY=y;
            const auto cx=std::min<std::uint64_t>(rawCellWidth-1,static_cast<std::uint64_t>(x)*stride+stride/2);
            const auto cy=std::min<std::uint64_t>(rawCellHeight-1,static_cast<std::uint64_t>(y)*stride+stride/2);
            Mfd::LocalMotionFieldSample center;
            if(!motionAt({2.*cx+.5,2.*cy+.5},center)) {
                cell.rejectionBits=Mfd::ReliabilityRejectMask(Mfd::ReliabilityRejectBit::AlignmentInvalid);continue;
            }
            cell.alignmentConfidence=std::clamp(center.alignmentConfidence,0.,1.);
            for(unsigned s=0;s<4;++s) {
                const auto site=static_cast<Mfd::CfaSite>(s);const auto offset=request.layout.OffsetFor(site);
                const Mfd::RawCoordinate raw={2.*cx+offset.x,2.*cy+offset.y};
                Mfd::LocalMotionFieldSample motion;Mfd::ReliabilityResidualSample sample;
                if(!motionAt(raw,motion)||
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
        const auto first=result.cells.begin()+static_cast<std::size_t>(y)*width;
        if(std::all_of(first,first+width,[](const auto& cell) {return cell.rejectionBits!=0;}))return;
        VisitPatchStatisticsRow(evidence,width,height,y,[&](unsigned x,double mismatch,unsigned maximumSupport) {
            auto& cell=result.cells[static_cast<std::size_t>(y)*width+x];
            if(cell.rejectionBits)return;
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
        });
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
