#pragma once
#include "Raw/Bracketing/NeutralGuide.h"
#include "Raw/Bracketing/GroupTileStore.h"
#include "Raw/Bracketing/ParallelProcessing.h"
#include <cstring>
#include <limits>
#include <stdexcept>

namespace Stack::Validation {
namespace NeutralGuideChecks {
using namespace Raw::Bracketing;
using Groups=std::vector<std::vector<Observation>>;
inline void Require(bool value,const char* reason) {
    if(!value)throw std::runtime_error(std::string("Bracketing neutral-guide invariant failed: ")+reason);
}
template<class T> inline bool Same(const std::vector<T>& a,const std::vector<T>& b) {
    return a.size()==b.size()&&(a.empty()||std::memcmp(a.data(),b.data(),a.size()*sizeof(T))==0);
}
inline LuminanceTransform Transform() {
    LuminanceTransform value;value.channels={0,1,1,2};
    value.cameraToSrgb={1.1f,-.1f,0,0,.9f,.1f,.05f,0,.95f};
    value.redGain=1.2f;value.blueGain=.85f;return value;
}
inline Groups Measurements(unsigned groups,const NeutralGuideTile& tile) {
    Groups result(groups,std::vector<Observation>(std::size_t(tile.width)*tile.height));
    for(unsigned g=0;g<groups;++g)for(unsigned y=0;y<tile.height;++y)for(unsigned x=0;x<tile.width;++x) {
        const auto i=std::size_t(y)*tile.width+x;
        const auto cell=(tile.top+y)/2*7+(tile.left+x)/2;
        auto& o=result[g][i];o.value=.0312345678901+.0145678901234*cell+.0134567890123*g+.0034567890123*(i%4);
        o.variance=.000123456789012*(g+1);o.measurementVariance=o.variance*.7;
        o.uncertaintyVariance=o.variance*.3;o.headroom=.9-.1*g;o.support=1+g;
        o.finite=true;o.fallback=.0123456789012*(g+1);o.exposure=std::exp2(double(g));
        o.fixedReference=g==0;o.localRejected=cell%5==0;
        if(cell%7==0&&g==0){o.support=0;o.clipped=true;o.fallbackClipped=true;}
        if(cell%11==0)o.support=0;
        if(cell%13==0)o.value=-.01;
    }
    return result;
}
// Frozen pre-extraction Processor loop. Keep its arithmetic and iteration order
// independent of BuildNeutralGuideTile so this checks extraction, not itself.
inline void PreviousLoop(const BracketingRecipe& recipe,const LuminanceTransform& luminance,
    const NeutralGuideTile& tile,unsigned cellWidth,const Groups& observations,
    std::vector<float>& guide,std::array<float,256>& counts,std::vector<double>& histogram) {
    const auto groups=recipe.groups.size();ColorObservations colorInput;
    std::vector<double> weights(groups);
    for(unsigned y=0;y<tile.height;y+=2)for(unsigned x=0;x<tile.width;x+=2) {
        std::array<double,4> cell{};std::fill(weights.begin(),weights.end(),0.0);
        GatherColorCell(observations,y*tile.width+x,tile.width,colorInput);
        const auto color=BlendColor(colorInput,{},recipe);
        for(unsigned c=0;c<4;++c) {
            const auto& b=color[c];cell[c]=b.value;
            for(std::size_t g=0;g<groups;++g)weights[g]+=b.actual[g]*.25;
        }
        const double ev=std::log2(std::max(1e-9,Luminance(luminance,cell))/.18);
        guide[((tile.top+y)/2)*cellWidth+(tile.left+x)/2]=static_cast<float>(ev);
        const auto bin=static_cast<std::size_t>(std::clamp((ev+12)/20*255,0.0,255.0));
        counts[bin]+=1;
        for(std::size_t g=0;g<groups;++g)histogram[bin*groups+g]+=weights[g];
    }
}
inline void Compare(const BracketingRecipe& recipe,const NeutralGuideTile& tile,const Groups& values) {
    std::vector<float> expected(35,-99),actual=expected;
    std::array<float,256> expectedCounts{},actualCounts{};actualCounts.fill(17);
    std::vector<double> expectedHistogram(256*recipe.groups.size()),actualHistogram(3,17);
    PreviousLoop(recipe,Transform(),tile,7,values,expected,expectedCounts,expectedHistogram);
    std::string error;
    Require(BuildNeutralGuideTile(recipe,Transform(),tile,7,values,actual,actualCounts,actualHistogram,error),"valid tile rejected");
    Require(Same(expected,actual)&&expectedCounts==actualCounts&&Same(expectedHistogram,actualHistogram),"old-loop exact parity");
}
}

inline void ValidateBracketingNeutralGuide() {
    using namespace NeutralGuideChecks;
    BracketingRecipe recipe;recipe.groups.resize(3);recipe.groups[2].enabled=false;
    const std::array<NeutralGuideTile,4> tiles={NeutralGuideTile{0,0,8,8},{8,0,6,8},{0,8,8,2},{8,8,6,2}};
    std::array<Groups,4> values;
    for(unsigned i=0;i<tiles.size();++i) {
        values[i]=Measurements(3,tiles[i]);CanonicalizeGroupTile(values[i]);Compare(recipe,tiles[i],values[i]);
    }
    // Tile tasks may finish in any order, but the reduction remains tile-index
    // ordered, matching Processor rather than accumulating in completion order.
    std::vector<float> expected(35,-99),actual=expected;
    std::array<std::array<float,256>,4> expectedCounts{},actualCounts{};
    std::array<std::vector<double>,4> expectedHistograms,actualHistograms;
    for(unsigned i=0;i<4;++i) {
        expectedHistograms[i].assign(768,0);
        PreviousLoop(recipe,Transform(),tiles[i],7,values[i],expected,expectedCounts[i],expectedHistograms[i]);
    }
    std::string error;
    for(unsigned i:{3u,1u,0u,2u})
        Require(BuildNeutralGuideTile(recipe,Transform(),tiles[i],7,values[i],actual,actualCounts[i],actualHistograms[i],error),"out-of-order tile");
    Require(Same(expected,actual),"out-of-order guide layout");
    std::fill(actual.begin(),actual.end(),-99);
    Require(ProcessParallelIndices(tiles.size(),4,{},[&](std::size_t i) {
        std::string localError;
        return BuildNeutralGuideTile(recipe,Transform(),tiles[i],7,values[i],actual,
            actualCounts[i],actualHistograms[i],localError);
    }),"parallel guide tiles");
    Require(Same(expected,actual),"parallel guide layout");
    std::array<float,256> oldCounts{},newCounts{};std::vector<double> oldHistogram(768),newHistogram(768);
    for(unsigned i=0;i<4;++i)for(unsigned bin=0;bin<256;++bin) {
        oldCounts[bin]+=expectedCounts[i][bin];newCounts[bin]+=actualCounts[i][bin];
        for(unsigned g=0;g<3;++g){oldHistogram[bin*3+g]+=expectedHistograms[i][bin*3+g];newHistogram[bin*3+g]+=actualHistograms[i][bin*3+g];}
    }
    Require(oldCounts==newCounts&&Same(oldHistogram,newHistogram),"deterministic histogram reduction");
    BracketingRecipe single;single.groups.resize(1);auto unrounded=Measurements(1,tiles[0]);Compare(single,tiles[0],unrounded);
    auto rounded=unrounded;CanonicalizeGroupTile(rounded);
    Require(unrounded[0][1].value!=rounded[0][1].value,"single-input fixture must contain unrounded doubles");
    const auto reject=[&](BracketingRecipe badRecipe,NeutralGuideTile tile,unsigned cellWidth,Groups input,std::size_t guideSize=35) {
        std::vector<float> guide(guideSize,-17);const auto priorGuide=guide;
        std::array<float,256> counts{};counts.fill(19);const auto priorCounts=counts;
        std::vector<double> histogram(5,23);const auto priorHistogram=histogram;
        error.clear();Require(!BuildNeutralGuideTile(badRecipe,Transform(),tile,cellWidth,input,guide,counts,histogram,error),"malformed shape accepted");
        Require(!error.empty()&&Same(guide,priorGuide)&&counts==priorCounts&&Same(histogram,priorHistogram),"invalid shape changed outputs");
    };
    reject(recipe,{0,0,0,8},7,values[0]);reject(recipe,{1,0,8,8},7,values[0]);
    reject(recipe,{0,0,7,8},7,values[0]);reject(recipe,tiles[0],0,values[0]);
    reject(recipe,{8,0,8,8},7,values[0]);reject(recipe,{0,8,8,8},7,values[0]);
    reject(recipe,tiles[0],7,values[0],1);
    auto missing=values[0];missing.pop_back();reject(recipe,tiles[0],7,missing);
    missing=values[0];missing[1].pop_back();reject(recipe,tiles[0],7,missing);
    BracketingRecipe empty;reject(empty,tiles[0],7,{});
    BracketingRecipe excess;excess.groups.resize(65);reject(excess,tiles[0],7,Measurements(65,tiles[0]));
    reject(recipe,{std::numeric_limits<unsigned>::max()-1,0,8,8},7,values[0]);
}

inline void ValidateBracketingNeutralGuideCache(const std::filesystem::path& directory) {
    using namespace NeutralGuideChecks;
    ProcessingRequest request;request.memoryBudgetBytes=16*1024*1024;request.recipe.groups.resize(3);
    request.recipe.originFrameId="neutral-guide-origin";
    PreparedDataset data;data.directory=directory;data.width=14;data.height=10;data.sources.resize(1);
    auto& frame=data.sources[0].frame;frame.tileRawPixels=8;frame.tileColumns=frame.tileRows=2;frame.cacheKey=std::string(64,'a');
    std::string error;
    for(unsigned ty=0;ty<2;++ty)for(unsigned tx=0;tx<2;++tx) {
        NeutralGuideTile tile{tx*8,ty*8,std::min(8u,14-tx*8),std::min(8u,10-ty*8)};
        auto canonical=Measurements(3,tile);CanonicalizeGroupTile(canonical);
        Require(WriteGroupTile(request,data,tx,ty,canonical,error),"cache fixture write");
        Groups loaded;Require(ReadGroupTile(request,data,tx,ty,loaded),"cache fixture read");
        Compare(request.recipe,tile,loaded);
        std::vector<float> a(35),b(35);std::array<float,256> ca{},cb{};std::vector<double> ha,hb;
        Require(BuildNeutralGuideTile(request.recipe,Transform(),tile,7,canonical,a,ca,ha,error)&&
            BuildNeutralGuideTile(request.recipe,Transform(),tile,7,loaded,b,cb,hb,error),"round-trip guide build");
        Require(Same(a,b)&&ca==cb&&Same(ha,hb),"resident versus actual cache guide parity");
    }
    // Actual cache publication cancellation must remain a failure. This does
    // not claim to run ProcessTiles or the full processor's cancellation path.
    request.shouldCancel=[] {return true;};data.motionIdentity="canceled-neutral-guide-write";
    const auto input=Measurements(3,{0,0,8,8});error.clear();
    Require(!WriteGroupTile(request,data,0,0,input,error)&&error=="Canceled","canceled cache publication");
    Groups missing;Require(!ReadGroupTile(request,data,0,0,missing),"canceled cache was published");
}
}
