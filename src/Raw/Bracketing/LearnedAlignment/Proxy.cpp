#include "Internal.h"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace Raw::Bracketing::LearnedAlignment {
bool WriteProxy(const ProcessingRequest& request,const PreparedDataset& data,const PreparedSource& source,
    const ProxyGeometry& geometry,const std::filesystem::path& path,std::string& error) {
    const auto count=std::size_t(geometry.width)*geometry.height;
    std::vector<std::array<double,3>> sums(count);
    std::vector<std::array<unsigned,3>> weights(count);
    Mfd::CfaLayout layout;
    if(!Mfd::CfaLayout::TryCreate(source.frame.activeCfaPattern,layout)){error="AI alignment requires a supported Bayer layout.";return false;}
    Mfd::DirectoryNormalizedTileCache cache(data.directory);
    for(unsigned ty=0;ty<source.frame.tileRows;++ty)for(unsigned tx=0;tx<source.frame.tileColumns;++tx) {
        if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
        Mfd::PreparedRawTile tile;
        if(Mfd::ReadPreparedTile(source.frame,cache,tx,ty,tile,&error)!=Mfd::TileCacheReadStatus::Hit)return false;
        for(unsigned y=0;y<tile.extent.height;++y)for(unsigned x=0;x<tile.extent.width;++x) {
            const auto rx=tile.originX+x,ry=tile.originY+y;
            const auto px=rx/geometry.stride,py=ry/geometry.stride;
            if(px>=geometry.width||py>=geometry.height)continue;
            const auto site=layout.SiteAt(rx,ry);
            // Match the trial's three-plane input; the other green plane is
            // available exclusively to the native verifier. Output is RAW.
            if(site==Mfd::CfaSite::Green1)continue;
            const unsigned c=site==Mfd::CfaSite::Red?0:site==Mfd::CfaSite::Blue?2:1;
            const auto p=std::size_t(y)*tile.extent.width+x,q=std::size_t(py)*geometry.width+px;
            const double v=double(tile.normalizedMosaic[p])*tile.comparisonGain[p]*source.scale;
            if(!std::isfinite(v)||Mfd::HasSampleFlag(tile.sampleFlags[p],Mfd::PreparedSampleFlag::Defective)||
                Mfd::HasSampleFlag(tile.sampleFlags[p],Mfd::PreparedSampleFlag::DecoderRepaired))continue;
            sums[q][c]+=v;++weights[q][c];
        }
    }
    std::ofstream stream(path,std::ios::binary);
    const auto& display=data.sources[data.origin].metadata;
    for(std::size_t p=0;p<count;++p) {
        if(p%geometry.width==0&&request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
        double camera[3];float rgb[3]{};
        for(unsigned c=0;c<3;++c)camera[c]=(weights[p][c]?sums[p][c]/weights[p][c]:0)*
            display.cameraWhiteBalance[c]/std::max(1e-6f,display.cameraWhiteBalance[1]);
        for(unsigned c=0;c<3;++c)for(unsigned k=0;k<3;++k)rgb[c]+=float(display.cameraToSrgb[c*3+k]*camera[k]);
        stream.write(reinterpret_cast<const char*>(rgb),sizeof(rgb));
    }
    stream.flush();const bool good=bool(stream);stream.close();
    if(!good||!stream){error="Could not write an AI alignment proxy.";return false;}
    return true;
}
}
