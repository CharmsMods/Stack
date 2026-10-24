#pragma once
#include "Raw/Bracketing/AlignmentAnchorEvidence.h"
#include <random>
#include <stdexcept>

namespace Stack::Validation {
inline void ValidateBracketingAnchorEvidence(const std::filesystem::path& directory) {
    using namespace Raw::Bracketing;using namespace Raw::Mfd;
    const char* phase="scalar";
    const auto require=[&](bool ok){if(!ok)throw std::runtime_error(std::string("Alignment anchor evidence: ")+phase);};
    require(SelectMeasuredAlignmentAnchor({1,2,3},{3,1,2},2)==1);
    require(SelectMeasuredAlignmentAnchor({1,2,3},{3,1,3},2)==2);
    require(SelectMeasuredAlignmentAnchor({1,2,3},{-1,-2,-3},2)==2);
    require(SelectMeasuredAlignmentAnchor({1,2,3},{1,std::numeric_limits<double>::quiet_NaN(),2},2)==2);
    // With no scene information, observed gradient covariance equals noise,
    // including the shared center's positive off-diagonal term.
    const AlignmentAnchorEvidence::Matrix n{2,1,2};
    require(AlignmentAnchorEvidence::DirectionalScore(n,n,{1,0,0,1})==0);
    require(AlignmentAnchorEvidence::DirectionalScore({1,0,1},n,{1,0,0,1})<0);
    PreparedDataset data;data.directory=directory;data.width=data.height=128;data.origin=0;data.sources.resize(4);
    ProcessingRequest request;request.recipe.alignmentMode=AlignmentMode::AutomaticLocal;
    std::string error;std::vector<std::string> diagnostics;
    DirectoryNormalizedTileCache cache(directory);
    for(unsigned i=0;i<4;++i){auto& s=data.sources[i];s.id="anchor-test-"+std::to_string(i);s.enabled=true;s.exposureCalibrated=true;s.scale=i?.25:1;
        s.alignmentDiagnostic.accepted=true;auto& f=s.frame;f.cacheKey=f.sourceContentSha256=std::string(64,char('a'+i));
        f.sensorActiveArea={0,0,128,128};f.activeExtent={128,128};f.activeCfaPattern=Raw::CfaPattern::RGGB;
        f.tileRawPixels=128;f.tileColumns=f.tileRows=1;f.calibration.maximumBlackByCfaSite.fill(64);
        f.calibration.whiteLevelByCfaSite.fill(4159);f.calibration.usableSpanByCfaSite.fill(4095);
        s.noise.preparedFrameCacheKey=f.cacheKey;s.noise.quality=NoiseModelQuality::TrustedMetadata;
        for(auto& site:s.noise.sites){site.offsetVariance=1e-6;site.quantizationVariance=1./(12.*4095*4095);site.quantizationIncluded=true;}
    }
    // Native synthetic samples, no decoder or external capture. Blurred
    // versions attenuate the same analytic spatial frequencies.
    const auto write=[&](unsigned index,double amplitude,double sigma=0.,unsigned flags=0,bool foreground=false,bool parallax=false,double blurSigma=0.) {
        auto& source=data.sources[index];AffineModel inverse;require(InvertAffineModel(source.alignment,inverse));
        PreparedRawTile tile;tile.extent={128,128};tile.normalizedMosaic.resize(128*128);tile.comparisonGain.assign(128*128,1.f);tile.sampleFlags.assign(128*128,0);
        std::mt19937 random(7301+index);std::normal_distribution<double> normal(0,sigma>0?sigma:1.);
        for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x){const auto p=y*128+x;
            auto point=inverse.Map({double(x),double(y)});if(parallax&&point.x>64)point.x+=.7;
            const double blurA=std::exp(-.5*blurSigma*blurSigma*(.21*.21+.19*.19));
            const double blurB=std::exp(-.5*blurSigma*blurSigma*(.13*.13+.17*.17));
            double value=.3+amplitude*(blurA*std::cos(point.x*.21)*std::cos(point.y*.19)+.4*blurB*std::sin(point.x*.13+point.y*.17));
            if(foreground&&x<32&&y<32)value=.3+.2*std::cos(x*.7)*std::cos(y*.6);
            if(sigma>0)value+=normal(random);tile.normalizedMosaic[p]=float(value);
            if(flags==1||(flags==2&&!(x<48&&y<48))||(flags==3&&x<24&&y<24))tile.sampleFlags[p]=1;
        }
        require(cache.Write(source.frame.cacheKey,tile,&error));
    };
    const auto plan=[&]{std::vector<AlignmentExposureCohortInput> inputs;for(const auto& s:data.sources)inputs.push_back({s.enabled,s.exposureCalibrated,s.scale});return BuildAlignmentExposureCohorts(inputs,data.origin);};
    const auto rank=[&]{auto before=plan(),after=before;diagnostics.clear();error.clear();require(RankAlignmentAnchors(request,data,after,diagnostics,error));
        require(after.memberCount==before.memberCount);auto a=after.order,b=before.order;std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());require(a==b);return after;};
    const auto reset=[&]{for(auto& s:data.sources){s.alignment={};s.enabled=true;s.exposureCalibrated=true;s.alignmentDiagnostic.accepted=true;s.localMotion.reset();s.noise.quality=NoiseModelQuality::TrustedMetadata;for(auto& n:s.noise.sites)n.offsetVariance=1e-6;}
        data.sources[0].scale=1;for(unsigned i=1;i<4;++i)data.sources[i].scale=.25;
        write(0,.04);write(1,.08);write(2,.08,0,0,false,false,6);write(3,.08,0,0,false,false,5);};
    phase="sharp versus blur";reset();require(rank().anchorForSource[1]==1);
    phase="integer translation preserves equal-texture score";for(unsigned i=1;i<4;++i)write(i,.04);
    data.sources[1].alignment.translationRaw={2,2};write(1,.04);require(rank().anchorForSource[1]==2);data.sources[1].alignment={};
    phase="two-member blurred median";reset();data.sources[3].enabled=false;require(rank().anchorForSource[1]==1);reset();
    phase="equal image tie";write(1,.04);write(2,.04);write(3,.04);require(rank().anchorForSource[1]==2);
    phase="translated/subpixel/rotated texture";
    for(unsigned variant=0;variant<3;++variant){reset();auto& warp=data.sources[1].alignment;
        warp.translationRaw={variant==0?2.:.65,variant==0?-2.:.35};
        if(variant==2){const double angle=.025;warp.centerRaw={64,64};warp.linear={std::cos(angle),-std::sin(angle),std::sin(angle),std::cos(angle)};}
        write(1,.08);require(rank().anchorForSource[1]==1);
    }
    phase="modest parallax";reset();write(1,.08,0,0,false,true);require(rank().anchorForSource[1]==1);
    phase="localized moving foreground";reset();write(2,.02,0,0,true);require(rank().anchorForSource[1]==1);
    phase="clipped patch does not veto distributed support";reset();write(1,.08,0,3);require(rank().anchorForSource[1]==1);
    phase="concentrated common support";reset();write(1,.08,0,2);require(rank().anchorForSource[1]==2);
    phase="fully clipped";reset();write(1,.08,0,1);require(rank().anchorForSource[1]==2);
    phase="noise-only signed evidence";reset();for(unsigned i=1;i<4;++i){const double sigma=.001*i;for(auto& n:data.sources[i].noise.sites)n.offsetVariance=sigma*sigma;write(i,0,sigma);}require(rank().anchorForSource[1]==2);
    phase="noisy texture not rewarded";reset();for(unsigned i=1;i<4;++i){const double sigma=i==2?.02:.001;for(auto& n:data.sources[i].noise.sites)n.offsetVariance=sigma*sigma;write(i,.04,sigma);}require(rank().anchorForSource[1]!=2);
    phase="disabled source membership";reset();data.sources[1].enabled=false;auto disabled=rank();require(disabled.memberCount[1]==0&&std::find(disabled.order.begin(),disabled.order.end(),1)==disabled.order.end());
    phase="disabled origin retained";reset();data.sources[0].enabled=false;require(rank().anchorForSource[1]==1&&!data.sources[0].enabled);
    phase="origin cohort unchanged";reset();for(auto& s:data.sources)s.scale=1;require(rank().anchorForSource[1]==0);
    phase="generic profile fallback";reset();data.sources[1].noise.quality=NoiseModelQuality::GenericLowConfidence;require(rank().anchorForSource[1]==2);
    phase="unaccepted geometry fallback";reset();data.sources[1].alignmentDiagnostic.accepted=false;require(rank().anchorForSource[1]==2);
    phase="existing fields preserved";reset();data.sources[3].localMotion=std::make_shared<LocalMotionGrid>();require(rank().anchorForSource[1]==2);
    phase="AI path unchanged";reset();request.recipe.aiAlignment=true;require(rank().anchorForSource[1]==2);request.recipe.aiAlignment=false;
    phase="external proposal path unchanged";reset();request.alignmentProposals.resize(1);require(rank().anchorForSource[1]==2);request.alignmentProposals.clear();
    phase="cancel is failure, plan unchanged";reset();auto before=plan(),after=before;unsigned cancelChecks=0;request.shouldCancel=[&] {return ++cancelChecks==4;};error.clear();
    require(!RankAlignmentAnchors(request,data,after,diagnostics,error)&&error=="Canceled"&&after.order==before.order&&after.anchorForSource==before.anchorForSource);request.shouldCancel={};
    phase="cache read failure is not missing evidence";data.sources[3].frame.cacheKey=std::string(64,'f');data.sources[3].noise.preparedFrameCacheKey=data.sources[3].frame.cacheKey;after=before;error.clear();
    require(!RankAlignmentAnchors(request,data,after,diagnostics,error)&&!error.empty()&&after.order==before.order&&after.anchorForSource==before.anchorForSource);
}
} // namespace Stack::Validation
