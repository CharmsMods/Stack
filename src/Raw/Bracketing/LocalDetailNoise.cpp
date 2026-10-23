#include "LocalDetailNoise.h"
#include "BorderSupport.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
bool LocalDetailNoise::Add(const PreparedDataset& data,const PreparedSource& source,
    PreparedTileSampler& sampler,unsigned x,unsigned y,double pilot,const Observation& observation,std::string& error) {
    Row row;
    if(!PrepareRow(data,source,sampler,x,y,pilot,observation,row,error))return false;
    Add(observation,row);return true;
}

void LocalDetailNoise::Add(const Observation& observation,const Row& row) {
    m_UncertaintyTrace+=std::max(0.,observation.uncertaintyVariance);
    for(std::size_t i=0;i<row.count;++i)m_Columns[row.columns[i].index]+=row.columns[i].amplitude;
    m_MaximumRow=std::max(m_MaximumRow,row.sum);
}

bool LocalDetailNoise::PrepareRow(const PreparedDataset& data,const PreparedSource& source,
    PreparedTileSampler& sampler,unsigned x,unsigned y,double pilot,const Observation& observation,
    Row& row,std::string& error) {
    row={};
    Mfd::CfaLayout layout;if(!Mfd::CfaLayout::TryCreate(source.frame.activeCfaPattern,layout))return false;
    const auto site=layout.SiteAt(x,y);const auto si=std::size_t(site);
    const bool aligned=source.alignmentDiagnostic.model!=CaptureAlignment::Model::Identity||
        (&source!=&data.sources[data.origin]&&source.alignmentDiagnostic.localApplied);
    if(!aligned) {
        const double sd=std::sqrt(std::max(1e-12,observation.measurementVariance));
        row.columns[row.count++]={std::uint64_t(y)*source.frame.activeExtent.width+x,sd};
        row.sum=sd;return true;
    }
    auto mapped=source.alignment.Map({double(x),double(y)});
    if(source.localMotion) {
        Mfd::LocalMotionFieldSample motion;Mfd::LocalMotionOptions options;
        if(!EvaluateCaptureMotion(source,{double(x),double(y)},options,motion))return false;
        mapped=motion.sourceRaw;
    }
    Mfd::KeysBicubicFootprint footprint;
    if(!BuildSupportedFootprint(layout.RawToPlane(mapped,site),layout.PlaneExtent(site,source.frame.activeExtent),footprint))return false;
    for(std::size_t i=0;i<footprint.taps.size();++i) {
        if(footprint.coefficients[i]==0)continue;
        const auto raw=layout.PlanePixelToRaw(footprint.taps[i]);Mfd::SameCfaTapInput tap;
        if(!sampler.ReadSample(source.frame,std::uint64_t(raw.x),std::uint64_t(raw.y),tap,&error))return false;
        Mfd::GainPropagatedVariance variance;
        if(!Mfd::PropagateKnownGainVariance(source.noise.sites[si],tap.comparisonGain*source.scale,
            std::max(0.,pilot),source.frame.calibration.usableSpanByCfaSite[si],variance,&error))return false;
        const double a=std::abs(footprint.coefficients[i])*std::sqrt(std::max(1e-12,variance.variance));
        row.columns[row.count++]={std::uint64_t(raw.y)*source.frame.activeExtent.width+std::uint64_t(raw.x),a};row.sum+=a;
    }
    return true;
}
double LocalDetailNoise::MeasurementBound() const {
    double column=0;for(const auto& entry:m_Columns)column=std::max(column,entry.second);
    return std::max(1e-12,column*m_MaximumRow);
}
}
