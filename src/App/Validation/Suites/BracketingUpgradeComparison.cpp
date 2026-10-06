#include "BracketingUpgradeComparison.h"
#include "Raw/Bracketing/ProcessingInternal.h"
#include "Raw/Bracketing/CaptureSharpness.h"
#include "Raw/Bracketing/GroupTileStore.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

namespace Stack::Validation {
namespace {
void Write(const std::filesystem::path& path,const std::vector<float>& values) {
    std::ofstream stream(path,std::ios::binary);
    if(!stream.write(reinterpret_cast<const char*>(values.data()),values.size()*sizeof(float)))
        throw std::runtime_error("Could not write upgrade comparison measurements.");
}
nlohmann::json Difference(const std::vector<float>& a,const std::vector<float>& b) {
    double sum=0,squares=0,maximum=0;std::size_t changed=0;
    for(std::size_t p=0;p<a.size();++p) {
        const double d=double(b[p])-a[p];sum+=d;squares+=d*d;
        maximum=std::max(maximum,std::abs(d));changed+=std::abs(d)>1e-7;
    }
    return {{"mean",sum/a.size()},{"rms",std::sqrt(squares/a.size())},
        {"maximumAbsolute",maximum},{"fractionAbove1e_7",double(changed)/a.size()}};
}
}

void WriteBracketUpgradeComparison(const Raw::Bracketing::ProcessingRequest& request,
    const Raw::Bracketing::BracketingResult& result,const std::filesystem::path& directory,
    const std::vector<std::array<unsigned,2>>& requested) {
    using namespace Raw::Bracketing;
    const auto& data=*result.analysis->prepared;
    if(request.recipe.groups.size()!=1||!result.raw->normalizedMosaicBuffer)
        throw std::runtime_error("Upgrade comparison requires a single exposure group with Bayer output.");
    const unsigned size=std::min({448u,data.width,data.height})&~3u;
    if(size<64) throw std::runtime_error("Upgrade comparison requires at least 64 sensor pixels.");
    auto centers=requested;
    if(centers.empty()) centers={{data.width/2,data.height/2}};
    if(centers.size()>16) throw std::runtime_error("Upgrade comparison is limited to 16 regions.");

    // Share immutable prepared captures, original motion and rejection maps.
    // The non-owning control must never remove the production cache on release.
    PreparedDataset control;control.directory=data.directory;control.sources=data.sources;
    control.origin=data.origin;control.width=data.width;control.height=data.height;
    bool refined=false;
    nlohmann::json manifest={{"version",1},{"origin",data.sources[data.origin].id},
        {"cameraWhiteBalance",data.sources[data.origin].metadata.cameraWhiteBalance},
        {"cameraToSrgb",data.sources[data.origin].metadata.cameraToSrgb},
        {"domain","calibrated scene-linear Bayer, common origin gain"},
        {"baselineNote","Re-evaluated established temporal merger with original motion and whole-capture preferences. Not an archived binary comparison."},
        {"variants",{"reference","previous-standard","refined-temporal","updated"}}};
    Raw::Mfd::CfaLayout layout;
    if(!Raw::Mfd::CfaLayout::TryCreate(data.sources[data.origin].frame.activeCfaPattern,layout))
        throw std::runtime_error("Upgrade comparison requires a Bayer grid.");
    for(unsigned s=0;s<4;++s) {
        const auto offset=layout.OffsetFor(static_cast<Raw::Mfd::CfaSite>(s));
        manifest["cfaOffsets"].push_back({offset.x,offset.y});
    }
    for(auto& source:control.sources) {
        const auto* field=source.refinedMotion.get();
        const auto accepted=field?std::count_if(field->cells.begin(),field->cells.end(),
            [](const auto& c){return c.confidence>0;}):0;
        refined|=accepted>0;
        manifest["sources"].push_back({{"id",source.id},{"enabled",source.enabled},
            {"refinedCells",accepted},{"gridCells",field?field->cells.size():0},
            {"updatedDetailPreference",source.detailPreference}});
        source.refinedMotion.reset();source.detailPreference=1;
    }
    std::string error;std::vector<std::string> diagnostics;
    if(!MeasureCaptureSharpness(request,control,diagnostics,error)) throw std::runtime_error(error);
    for(std::size_t s=0;s<control.sources.size();++s)
        manifest["sources"][s]["previousDetailPreference"]=control.sources[s].detailPreference;
    manifest["controlDiagnostics"]=diagnostics;
    std::filesystem::create_directories(directory);
    const auto& frame=data.sources[data.origin].frame;
    const unsigned side=frame.tileRawPixels;
    for(std::size_t r=0;r<centers.size();++r) {
        const unsigned left=std::min(centers[r][0]>size/2?centers[r][0]-size/2:0u,data.width-size)&~3u;
        const unsigned top=std::min(centers[r][1]>size/2?centers[r][1]-size/2:0u,data.height-size)&~3u;
        const auto path=directory/("region-"+std::to_string(r));std::filesystem::create_directories(path);
        std::vector<float> reference(size*size),previous(reference.size()),temporal(reference.size()),updated(reference.size());
        std::vector<float> previousVariance(reference.size()),updatedVariance(reference.size()),support(reference.size());
        for(unsigned ty=top/side;ty<=(top+size-1)/side;++ty)
        for(unsigned tx=left/side;tx<=(left+size-1)/side;++tx) {
            if(request.shouldCancel&&request.shouldCancel()) throw std::runtime_error("Canceled");
            std::vector<std::vector<Observation>> before,after;Raw::Mfd::PreparedRawTile tile;
            if(!ReadTemporalGroups(request,control,tx,ty,before,tile,error)) throw std::runtime_error(error);
            CanonicalizeGroupTile(before);
            if(refined) {
                if(!ReadTemporalGroups(request,data,tx,ty,after,tile,error)) throw std::runtime_error(error);
                CanonicalizeGroupTile(after);
            }
            const auto& aligned=refined?after:before;
            const unsigned x0=std::max(left,tx*side)-left,y0=std::max(top,ty*side)-top;
            const unsigned x1=std::min(left+size,static_cast<unsigned>(tile.originX+tile.extent.width))-left;
            const unsigned y1=std::min(top+size,static_cast<unsigned>(tile.originY+tile.extent.height))-top;
            for(unsigned y=y0;y<y1;++y) for(unsigned x=x0;x<x1;++x) {
                const auto p=std::size_t(y)*size+x,q=std::size_t(top+y-tile.originY)*tile.extent.width+left+x-tile.originX;
                const auto published=std::size_t(top+y)*data.width+left+x;
                const double gain=tile.comparisonGain[q];
                const auto oldPixel=Blend({before[0][q]},{1.},request.recipe);
                const auto alignedPixel=Blend({aligned[0][q]},{1.},request.recipe);
                reference[p]=static_cast<float>(tile.normalizedMosaic[q]*gain);
                // Round in the same normalized mosaic domain as publication.
                previous[p]=static_cast<float>(static_cast<float>(oldPixel.value/gain)*gain);
                temporal[p]=static_cast<float>(static_cast<float>(alignedPixel.value/gain)*gain);
                updated[p]=static_cast<float>(result.raw->normalizedMosaicBuffer->at(published)*gain);
                previousVariance[p]=static_cast<float>(oldPixel.measurementVariance);
                updatedVariance[p]=static_cast<float>(result.raw->multiFrameMeasurementSidecars->variance->at(published)*gain*gain);
                support[p]=result.raw->multiFrameMeasurementSidecars->effectiveSupport->at(published);
            }
        }
        Write(path/"reference.f32",reference);Write(path/"previous-standard.f32",previous);
        Write(path/"refined-temporal.f32",temporal);Write(path/"updated.f32",updated);
        Write(path/"previous-variance.f32",previousVariance);Write(path/"updated-variance.f32",updatedVariance);
        Write(path/"effective-support.f32",support);
        manifest["regions"].push_back({{"directory",path.filename().string()},{"left",left},{"top",top},{"size",size},
            {"alignmentDifference",Difference(previous,temporal)},{"detailDifference",Difference(temporal,updated)},
            {"totalDifference",Difference(previous,updated)}});
        std::cout<<"Upgrade comparison region "<<r+1<<"/"<<centers.size()<<" exported.\n"<<std::flush;
    }
    std::ofstream out(directory/"manifest.json");out<<manifest.dump(2);
    if(!out) throw std::runtime_error("Could not write upgrade comparison manifest.");
}
}
