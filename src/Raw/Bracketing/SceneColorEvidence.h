#pragma once
#include "HdrColorFusion.h"
#include "SceneColorContract.h"

namespace Raw::Bracketing {

namespace SceneColorEvidence {
// Research basis: calibrated radiance/uncertainty in Granados et al. (2010),
// https://vcai.mpi-inf.mpg.de/projects/opthdr/granados10_opthdr.pdf ; and
// noise-corrected color comparisons in Wronski et al. (2019), section 5.2.2,
// https://storage.googleapis.com/gweb-research2023-media/pubtools/5211.pdf .
// This guidance-only score reuses Stack's existing neutral fusion estimate;
// it does not claim to reproduce either paper's full reconstruction algorithm.
// A conservative engineering tolerance in normalized camera chromaticity.
// It controls how strongly uncertain color may guide another operation, not
// which source measurement is fused or whether an output pixel is visible.
inline constexpr double colorToleranceSquared=.04*.04;
struct Cell {
    std::array<double,3> rgb{},variance{},chroma{};
    double chromaVariance=0,reliability=0;
    bool supported=false;
};

inline bool Chromaticity(const std::array<double,3>& rgb,const std::array<double,3>& variance,
    std::array<double,3>& chroma,double& chromaVariance) {
    const double sum=rgb[0]+rgb[1]+rgb[2];
    if(!(sum>1e-12)||!std::isfinite(sum))return false;
    double noise=0;
    for(unsigned c=0;c<3;++c) {
        chroma[c]=rgb[c]/sum;
        for(unsigned k=0;k<3;++k) {
            const double derivative=((c==k?1.:0.)-chroma[c])/sum;
            noise+=derivative*derivative*variance[k];
        }
    }
    if(!std::isfinite(noise))return false;
    chromaVariance=noise;return true;
}

inline Cell Estimate(const ColorObservations& observations,const std::array<Pixel,4>& neutral,
    const BracketingRecipe& recipe,const std::array<unsigned,4>& channels) {
    Cell out;std::array<double,3> modelDeviation{};
    std::array<unsigned,2> green{};unsigned greens=0;
    for(unsigned s=0;s<4;++s) {
        const auto& p=neutral[s];
        if(!p.valid||p.clipped||!(p.support>0)||!std::isfinite(p.value)||
            !std::isfinite(p.measurementVariance)||p.measurementVariance<0||
            !std::isfinite(p.uncertaintyVariance)||p.uncertaintyVariance<0)return out;
        const unsigned c=channels[s];if(c>2)return out;
        const double w=c==1?.5:1;
        out.rgb[c]+=w*p.value;
        out.variance[c]+=w*w*p.measurementVariance;
        // Registration/gain uncertainty may be shared by the two green sites.
        // Do not count that common risk as two independent noise measurements.
        modelDeviation[c]+=w*std::sqrt(p.uncertaintyVariance);
        if(c==1&&greens<2)green[greens++]=s;
    }
    for(unsigned c=0;c<3;++c)out.variance[c]+=modelDeviation[c]*modelDeviation[c];
    out.supported=true;
    if(!Chromaticity(out.rgb,out.variance,out.chroma,out.chromaVariance))return out;
    const double signal=out.rgb[0]+out.rgb[1]+out.rgb[2];
    const double totalVariance=out.variance[0]+out.variance[1]+out.variance[2];
    // A chromaticity ratio in darkness has little meaning even when a noisy
    // numerator happens to look colorful. Preserve signed RGB; only guidance
    // confidence is reduced. Bright, genuinely saturated colors remain usable.
    const double signalReliability=signal*signal/(signal*signal+16*totalVariance+1e-24);
    double mosaicRisk=0;
    if(greens==2) {
        const auto& a=neutral[green[0]];const auto& b=neutral[green[1]];
        const double difference=a.value-b.value;
        mosaicRisk=.25*std::max(0.,difference*difference-a.variance-b.variance)/(signal*signal);
    }
    double headroom=0,disagreement=0,comparisonWeight=0;
    for(std::size_t g=0;g<recipe.groups.size();++g) {
        if(!recipe.groups[g].enabled)continue;
        std::array<double,3> rgb{},variance{},risk{};
        double weight=0,safety=1;bool complete=true;
        for(unsigned s=0;s<4;++s) {
            if(g>=observations[s].size())return Cell{};
            const auto& o=observations[s][g];weight+=neutral[s].actual[g]*.25;
            if(!o.finite||o.clipped||!(o.support>0)||!std::isfinite(o.value)||
                !std::isfinite(o.variance)||o.variance<0||!std::isfinite(o.headroom)) {
                // Explicit validity/clipping wins even if an upstream payload
                // contradicts its flags with a positive support/weight value.
                if(neutral[s].actual[g]>0)return Cell{};
                complete=false;continue;
            }
            safety=std::min(safety,std::clamp(o.headroom,0.,1.));
            const unsigned c=channels[s];const double w=c==1?.5:1;
            const double model=std::max(0.,o.uncertaintyVariance);
            const double measurement=o.measurementVariance>=0?o.measurementVariance:std::max(0.,o.variance-model);
            rgb[c]+=w*o.value;variance[c]+=w*w*measurement;risk[c]+=w*std::sqrt(model);
        }
        if(!(weight>0))continue;
        // Rejected/clipped groups with no actual contribution never reduce
        // the trust in a different, well-measured surviving exposure.
        headroom+=weight*safety;
        if(!complete)continue;
        for(unsigned c=0;c<3;++c)variance[c]+=risk[c]*risk[c];
        std::array<double,3> chroma{};double noise=0;
        if(!Chromaticity(rgb,variance,chroma,noise))continue;
        double difference=0;
        for(unsigned c=0;c<3;++c)difference+=(chroma[c]-out.chroma[c])*(chroma[c]-out.chroma[c]);
        // Compare in the common calibrated domain and subtract expected
        // measurement/model noise. Raw exposure changes alone are not edges.
        // The neutral estimate shares these observations, so summing both
        // noise terms is deliberately conservative rather than a chi-square test.
        disagreement+=weight*std::max(0.,difference-noise-out.chromaVariance);
        comparisonWeight+=weight;
    }
    if(comparisonWeight>0)disagreement/=comparisonWeight;
    const double uncertainty=out.chromaVariance+mosaicRisk+disagreement;
    out.reliability=std::clamp(headroom,0.,1.)*signalReliability/
        (1+uncertainty/colorToleranceSquared);
    if(!std::isfinite(out.reliability))out.reliability=0;
    return out;
}

struct Accumulator {
    std::array<double,3> rgb{},variance{},chroma{};
    double area=0,support=0,weight=0,chromaSquare=0,chromaNoise=0;
    void Add(const Cell& cell,double fraction) {
        area+=fraction;
        if(!cell.supported)return;
        support+=fraction;
        const double w=fraction*cell.reliability;
        if(!(w>0))return;
        weight+=w;
        for(unsigned c=0;c<3;++c) {
            rgb[c]+=w*cell.rgb[c];variance[c]+=w*cell.variance[c];chroma[c]+=w*cell.chroma[c];
            chromaSquare+=w*cell.chroma[c]*cell.chroma[c];
        }
        chromaNoise+=w*cell.chromaVariance;
    }
    void Add(const Accumulator& other) {
        area+=other.area;support+=other.support;weight+=other.weight;
        chromaSquare+=other.chromaSquare;chromaNoise+=other.chromaNoise;
        for(unsigned c=0;c<3;++c) {rgb[c]+=other.rgb[c];variance[c]+=other.variance[c];chroma[c]+=other.chroma[c];}
    }
};
static_assert(sizeof(Accumulator)<=kAccumulatorBytes,"Update scene-color working-memory planning.");
// Each processing tile owns only its compact footprint. Workers never mutate
// shared bins; final reduction follows tile index, independently of completion.
struct Tile {
    unsigned left=0,top=0,width=0,height=0,guideWidth=0,guideHeight=0,cellWidth=0,cellHeight=0;
    std::vector<Accumulator> values;
    void Initialize(unsigned sensorWidth,unsigned sensorHeight,unsigned x,unsigned y,unsigned w,unsigned h) {
        *this=Tile{};
        const auto size=Dimensions(sensorWidth,sensorHeight);guideWidth=size[0];guideHeight=size[1];
        cellWidth=sensorWidth/2;cellHeight=sensorHeight/2;
        if(!guideWidth||!guideHeight||!w||!h||std::uint64_t(x)+w>sensorWidth||std::uint64_t(y)+h>sensorHeight)return;
        left=unsigned(std::uint64_t(x)*guideWidth/sensorWidth);
        top=unsigned(std::uint64_t(y)*guideHeight/sensorHeight);
        const auto right=unsigned((std::uint64_t(x+w)*guideWidth+sensorWidth-1)/sensorWidth);
        const auto bottom=unsigned((std::uint64_t(y+h)*guideHeight+sensorHeight-1)/sensorHeight);
        width=right-left;height=bottom-top;values.resize(std::size_t(width)*height);
    }
    void Add(unsigned sensorX,unsigned sensorY,const Cell& cell) {
        const unsigned x=sensorX/2,y=sensorY/2;
        // Integrate the entire Bayer-cell footprint, including fractional
        // bucket boundaries, instead of sparsely sampling a downscaled image.
        const double x0=double(x)*guideWidth/cellWidth,x1=double(x+1)*guideWidth/cellWidth;
        const double y0=double(y)*guideHeight/cellHeight,y1=double(y+1)*guideHeight/cellHeight;
        const unsigned bx0=unsigned(std::uint64_t(x)*guideWidth/cellWidth);
        const unsigned by0=unsigned(std::uint64_t(y)*guideHeight/cellHeight);
        const unsigned bx1=unsigned((std::uint64_t(x+1)*guideWidth+cellWidth-1)/cellWidth);
        const unsigned by1=unsigned((std::uint64_t(y+1)*guideHeight+cellHeight-1)/cellHeight);
        for(unsigned by=by0;by<by1;++by)for(unsigned bx=bx0;bx<bx1;++bx) {
            const double area=(std::min(x1,double(bx+1))-std::max(x0,double(bx)))*
                (std::min(y1,double(by+1))-std::max(y0,double(by)));
            values[std::size_t(by-top)*width+bx-left].Add(cell,area);
        }
    }
};
static_assert(sizeof(Tile)<=kTileDescriptorBytes,"Update scene-color tile-memory planning.");
inline std::shared_ptr<const SceneColorGuide> Finish(unsigned sensorWidth,unsigned sensorHeight,
    const std::string& identity,std::vector<Tile>& tiles) {
    auto guide=std::make_shared<SceneColorGuide>();const auto size=Dimensions(sensorWidth,sensorHeight);
    guide->sensorWidth=sensorWidth;guide->sensorHeight=sensorHeight;guide->width=size[0];guide->height=size[1];guide->identity=identity;
    const auto count=std::size_t(guide->width)*guide->height;
    std::vector<Accumulator> total(count);
    for(auto& tile:tiles) {
        for(unsigned y=0;y<tile.height;++y)for(unsigned x=0;x<tile.width;++x)
            total[std::size_t(tile.top+y)*guide->width+tile.left+x].Add(tile.values[std::size_t(y)*tile.width+x]);
        std::vector<Accumulator>().swap(tile.values);
    }
    guide->cameraRgb.resize(count*3);guide->variance.resize(count*3);
    guide->reliability.resize(count);guide->coverage.resize(count);
    for(std::size_t p=0;p<count;++p) {
        const auto& a=total[p];
        if(!(a.area>0))continue;
        guide->coverage[p]=float(std::clamp(a.support/a.area,0.,1.));
        if(!(a.weight>0&&a.support>0))continue;
        double chromaSquare=0;
        for(unsigned c=0;c<3;++c) {
            guide->cameraRgb[p*3+c]=float(a.rgb[c]/a.weight);
            // Neighboring groups/registration estimates can be correlated.
            // Do not invent N-fold certainty by averaging a compact footprint.
            guide->variance[p*3+c]=float(a.variance[c]/a.weight);
            chromaSquare+=(a.chroma[c]/a.weight)*(a.chroma[c]/a.weight);
        }
        const double mixture=std::max(0.,a.chromaSquare/a.weight-chromaSquare-a.chromaNoise/a.weight);
        guide->reliability[p]=float(std::clamp((a.weight/a.support)/(1+mixture/colorToleranceSquared),0.,1.));
    }
    return guide;
}
}
}
