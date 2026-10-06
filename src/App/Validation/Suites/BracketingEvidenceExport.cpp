#include "Raw/Bracketing/HdrColorFusion.h"
#include "BracketingEvidenceExport.h"
#include "Raw/Bracketing/PreparedRegion.h"
#include "Raw/Bracketing/SceneGradient.h"
#include "Raw/Bracketing/GroupTileStore.h"
#include <fstream>
#include <cmath>
#include <iostream>

namespace Stack::Validation {
namespace Mfd=Raw::Mfd;
namespace {
void Write(const std::filesystem::path& path,const std::vector<float>& values) {
    std::ofstream out(path,std::ios::binary);
    if(!out.write(reinterpret_cast<const char*>(values.data()),values.size()*sizeof(float)))
        throw std::runtime_error("Could not write native reconstruction evidence.");
}
}

void WriteBracketEvidence(const Raw::Bracketing::ProcessingRequest& request,
    const Raw::Bracketing::BracketingResult& result,const std::filesystem::path& directory,
    const std::vector<std::array<unsigned,2>>& requested) {
    using namespace Raw::Bracketing;
    const auto& data=*result.analysis->prepared;
    const auto& origin=data.sources[data.origin];
    const bool nativeMosaic=bool(result.raw->normalizedMosaicBuffer);
    // Native, bounded crops with a 32-pixel comparison guard. Never retain a
    // full-resolution copy of every capture or modify a user project.
    const unsigned size=std::min({448u,data.width,data.height})&~3u;
    if(size<64) throw std::runtime_error("Evidence export needs at least 64 sensor pixels.");
    auto centers=requested;
    if(centers.empty()) centers={{data.width/2,data.height/2},{size/2,size/2}};
    if(centers.size()>16) throw std::runtime_error("Evidence export is limited to 16 native regions.");
    Mfd::CfaLayout layout;
    if(!Mfd::CfaLayout::TryCreate(origin.frame.activeCfaPattern,layout))
        throw std::runtime_error("Evidence export needs a Bayer grid.");
    nlohmann::json manifest={{"version",1},{"analysisIdentity",result.analysis->identity},
        {"baselineDomain",nativeMosaic?"published-mosaic":"native-temporal-mosaic-before-super-resolution"},
        {"recipe",Serialize(request.recipe)},{"origin",data.origin},
        {"sensorWidth",data.width},{"sensorHeight",data.height},
        {"cameraWhiteBalance",origin.metadata.cameraWhiteBalance},
        {"cameraToSrgb",origin.metadata.cameraToSrgb},
        {"fields",{"value","measurementVariance","uncertaintyVariance","usable","headroom",
            "covarianceXX","covarianceXY","covarianceYY"}},
        {"registrationNote","Conditioned on existing alignment. These exports are not independent held-out alignment evidence."}};
    for(unsigned s=0;s<4;++s) {
        const auto offset=layout.OffsetFor(static_cast<Mfd::CfaSite>(s));
        manifest["cfaOffsets"].push_back({offset.x,offset.y});
    }
    for(std::size_t s=0;s<data.sources.size();++s) {
        const auto& source=data.sources[s];
        const bool aligned=source.alignmentDiagnostic.model!=CaptureAlignment::Model::Identity||
            (s!=data.origin&&source.alignmentDiagnostic.localApplied);
        manifest["sources"].push_back({{"id",source.id},{"group",source.group},
            {"enabled",source.enabled},{"exposureScale",source.scale},
            {"detailPreference",source.detailPreference},
            {"interpolationCorrelationAllowance",aligned?2.5:1.}});
    }
    std::filesystem::create_directories(directory);
    for(std::size_t r=0;r<centers.size();++r) {
        const unsigned left=std::min(centers[r][0]>size/2?centers[r][0]-size/2:0u,data.width-size)&~3u;
        const unsigned top=std::min(centers[r][1]>size/2?centers[r][1]-size/2:0u,data.height-size)&~3u;
        const auto path=directory/("region-"+std::to_string(r));std::filesystem::create_directories(path);
        PreparedTileSampler sampler(data.directory,16);Mfd::PreparedRawTile tile;std::string error;
        if(!ReadPreparedRegion(origin.frame,sampler,left,top,size,size,tile,error)) throw std::runtime_error(error);
        std::vector<Mfd::RawSignalGradient> gradients;
        if(!BuildSceneGradients(request,data,tile,gradients,error)) throw std::runtime_error(error);
        const std::size_t count=std::size_t(size)*size;
        std::vector<double> pilot(count);
        std::vector<float> baseline(count),reference(count);
        for(unsigned y=0;y<size;++y) for(unsigned x=0;x<size;++x) {
            const auto p=std::size_t(y)*size+x;double sum=0;unsigned n=0;
            for(unsigned dy=0;dy<4;dy+=2) for(unsigned dx=0;dx<4;dx+=2) {
                const auto q=((y/4)*4+y%2+dy)*size+(x/4)*4+x%2+dx;
                const auto v=tile.normalizedMosaic[q]*tile.comparisonGain[q];
                const auto flags=tile.sampleFlags[q];
                if(!Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::Saturated)&&
                   !Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::ExplicitDecoderClip)&&
                   !Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::Defective)&&std::isfinite(v)) {sum+=v;++n;}
            }
            reference[p]=tile.normalizedMosaic[p]*tile.comparisonGain[p];
            pilot[p]=std::max(0.,n?sum/n:double(reference[p]));
            if(nativeMosaic)baseline[p]=result.raw->normalizedMosaicBuffer->at(std::size_t(top+y)*data.width+left+x)*tile.comparisonGain[p];
        }
        Write(path/"reference.f32",reference);
        // Current HDR shares and group measurements remain authoritative. The
        // experiment may only replace temporal detail within an exposure group.
        std::vector<float> groupValues(count*request.recipe.groups.size()),shares(groupValues.size());
        std::vector<std::vector<Observation>> groups;Mfd::PreparedRawTile groupTile;
        const unsigned tileSize=origin.frame.tileRawPixels;
        for(unsigned ty=top/tileSize;ty<=(top+size-1)/tileSize;++ty)
        for(unsigned tx=left/tileSize;tx<=(left+size-1)/tileSize;++tx) {
            if(!ReadGroups(request,data,tx,ty,groups,groupTile,error)) throw std::runtime_error(error);
            const unsigned x0=std::max(left,tx*tileSize)-left,y0=std::max(top,ty*tileSize)-top;
            const unsigned x1=std::min(left+size,(tx+1)*tileSize)-left,y1=std::min(top+size,(ty+1)*tileSize)-top;
            for(unsigned y=y0;y<y1;++y) for(unsigned x=x0;x<x1;++x) {
            const unsigned px=left+x,py=top+y;
            const auto q=(py-groupTile.originY)*groupTile.extent.width+px-groupTile.originX;
            ColorObservations samples;
            GatherColorCell(groups,((py&~1u)-groupTile.originY)*groupTile.extent.width+(px&~1u)-groupTile.originX,groupTile.extent.width,samples);
            const auto ev=result.analysis->guideEv[(py/2)*(data.width/2)+px/2];
            const auto blend=BlendColor(samples,Evaluate(request.recipe.automatic?result.analysis->suggestion:request.recipe.knots,ev),request.recipe)[(py%2)*2+px%2];
            if(!nativeMosaic)baseline[std::size_t(y)*size+x]=static_cast<float>(blend.value);
            for(std::size_t g=0;g<groups.size();++g) {
                groupValues[g*count+std::size_t(y)*size+x]=static_cast<float>(groups[g][q].value);
                shares[g*count+std::size_t(y)*size+x]=static_cast<float>(blend.actual[g]);
            }
            }
        }
        Write(path/"baseline.f32",baseline);Write(path/"groups.f32",groupValues);Write(path/"shares.f32",shares);
        for(std::size_t s=0;s<data.sources.size();++s) {
            const auto& source=data.sources[s];std::vector<float> values(count*8);
            if(!source.enabled) {Write(path/("source-"+std::to_string(s)+".f32"),values);continue;}
            const bool aligned=source.alignmentDiagnostic.model!=CaptureAlignment::Model::Identity||
                (s!=data.origin&&source.alignmentDiagnostic.localApplied);
            Mfd::PreparedRawTile direct;
            if(!aligned&&!ReadPreparedRegion(source.frame,sampler,left,top,size,size,direct,error))
                throw std::runtime_error(error);
            for(unsigned y=0;y<size;++y) {
                if(request.shouldCancel&&request.shouldCancel()) throw std::runtime_error("Canceled");
                for(unsigned x=0;x<size;++x) {
                    const auto p=std::size_t(y)*size+x;Observation v;Mfd::SymmetricRawCovariance covariance{};
                    if(!aligned) {
                        const auto site=static_cast<std::size_t>(layout.SiteAt(left+x,top+y));
                        const auto& profile=source.noise.sites[site];Mfd::GainPropagatedVariance noise;
                        const double gain=direct.comparisonGain[p]*source.scale;
                        if(!Mfd::PropagateKnownGainVariance(profile,gain,pilot[p],
                            source.frame.calibration.usableSpanByCfaSite[site],noise,&error)) throw std::runtime_error(error);
                        const auto flags=direct.sampleFlags[p];
                        v.value=direct.normalizedMosaic[p]*gain;
                        v.finite=std::isfinite(v.value)&&!Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::Defective);
                        v.clipped=Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::Saturated)||
                            Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::ExplicitDecoderClip);
                        v.support=v.finite&&!v.clipped?1:0;
                        v.measurementVariance=std::max(1e-12,noise.variance);
                        v.uncertaintyVariance=Mfd::ResidualModelVariance(profile,pilot[p])+
                            pilot[p]*pilot[p]*source.scaleVariance/(source.scale*source.scale);
                        v.headroom=std::clamp((.995-direct.normalizedMosaic[p])/.045,0.,1.);
                        v.headroom=v.headroom*v.headroom*(3-2*v.headroom);
                    } else {
                        v=SampleAlignedCapture(source,layout,sampler,left+x,top+y,pilot[p],gradients[p],error);
                        if(!error.empty()) throw std::runtime_error(error);
                        covariance={.0025,0,.0025};
                        if(source.localMotion) {
                            Mfd::LocalMotionFieldSample motion;Mfd::LocalMotionOptions options;
                            if(EvaluateCaptureMotion(source,{double(left+x),double(top+y)},options,motion))
                                covariance=motion.covarianceRaw;
                        }
                    }
                    values[p*8]=static_cast<float>(v.value);values[p*8+1]=static_cast<float>(v.measurementVariance);
                    values[p*8+2]=static_cast<float>(v.uncertaintyVariance);values[p*8+3]=v.finite&&v.support>0&&!v.clipped?1.f:0.f;
                    values[p*8+4]=static_cast<float>(v.headroom);
                    values[p*8+5]=static_cast<float>(covariance.xxRawPixelsSquared);
                    values[p*8+6]=static_cast<float>(covariance.xyRawPixelsSquared);
                    values[p*8+7]=static_cast<float>(covariance.yyRawPixelsSquared);
                }
            }
            Write(path/("source-"+std::to_string(s)+".f32"),values);
        }
        manifest["regions"].push_back({{"directory",path.filename().string()},{"left",left},{"top",top},{"size",size}});
        std::cout<<"Native evidence region "<<r+1<<"/"<<centers.size()<<" exported.\n"<<std::flush;
    }
    std::ofstream out(directory/"manifest.json");out<<manifest.dump(2);
    if(!out) throw std::runtime_error("Could not write evidence manifest.");
}
}
