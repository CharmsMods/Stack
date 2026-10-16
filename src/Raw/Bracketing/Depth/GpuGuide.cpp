#include "GpuRuntime.h"
#include "GuideShaders.h"
#include "../ProcessingInternal.h"
#include <algorithm>

namespace Raw::Bracketing::Depth {
bool BuildGuide(GpuRuntime& gpu,const PreparedDataset& data,std::size_t index,GpuGuide& guide,std::string& error) {
    const auto& source=data.sources[index];
    while(std::max(data.width,data.height)/guide.stride>1536)guide.stride*=2;
    guide.width=data.width/guide.stride;guide.height=data.height/guide.stride;
    if(guide.width<64||guide.height<64){error="Depth tracking needs an image at least 256 pixels on each side.";return false;}
    std::size_t count=0;for(unsigned level=0;level<5;++level){guide.offsets[level]=unsigned(count);count+=(guide.width>>level)*(guide.height>>level);}
    GLuint program=0,pyramid=0,tileBuffer=0;
    if(!gpu.Run([&](std::string& e){
        program=gpu.Program(Shaders::Guide,e);pyramid=gpu.Program(Shaders::Pyramid,e);
        if(!program||!pyramid)return false;
        guide.buffer=gpu.Buffer(count*sizeof(float));
        tileBuffer=gpu.Buffer(std::size_t(source.frame.tileRawPixels)*source.frame.tileRawPixels*3*sizeof(float));return true;
    },error))return false;
    Mfd::DirectoryNormalizedTileCache cache(data.directory);
    for(unsigned ty=0;ty<source.frame.tileRows;++ty)for(unsigned tx=0;tx<source.frame.tileColumns;++tx) {
        if(gpu.Request().shouldCancel&&gpu.Request().shouldCancel()){error="Canceled";return false;}
        Mfd::PreparedRawTile tile;
        if(Mfd::ReadPreparedTile(source.frame,cache,tx,ty,tile,&error)!=Mfd::TileCacheReadStatus::Hit)return false;
        if(tile.originX%guide.stride||tile.originY%guide.stride){error="Depth guide tile origins are not Bayer-cell aligned.";return false;}
        const auto bytes=tile.normalizedMosaic.size()*sizeof(float);
        // Format packing only. Signal transformation and averaging run on GPU.
        const std::vector<float> flags(tile.sampleFlags.begin(),tile.sampleFlags.end());
        if(!gpu.Run([&](std::string&){
            gpu.Upload(tileBuffer,0,bytes,tile.normalizedMosaic.data());
            gpu.Upload(tileBuffer,bytes,bytes,tile.comparisonGain.data());gpu.Upload(tileBuffer,bytes*2,bytes,flags.data());
            gpu.Use(program);gpu.Bind(0,tileBuffer);gpu.Bind(1,guide.buffer);
            gpu.I2("tileSize",tile.extent.width,tile.extent.height);gpu.I2("tileOrigin",int(tile.originX),int(tile.originY));
            gpu.I2("guideSize",guide.width,guide.height);gpu.I("stride",guide.stride);
            gpu.Dispatch((tile.extent.width/guide.stride+15)/16,(tile.extent.height/guide.stride+15)/16);return true;
        },error))return false;
    }
    for(unsigned level=1;level<5;++level)if(!gpu.Run([&](std::string&){
        gpu.Use(pyramid);gpu.Bind(0,guide.buffer);gpu.Bind(1,guide.buffer);
        gpu.I2("size",guide.width>>(level-1),guide.height>>(level-1));
        gpu.I("srcOffset",guide.offsets[level-1]);gpu.I("dstOffset",guide.offsets[level]);
        gpu.Dispatch(((guide.width>>level)+15)/16,((guide.height>>level)+15)/16);return true;
    },error))return false;
    return true;
}
}
