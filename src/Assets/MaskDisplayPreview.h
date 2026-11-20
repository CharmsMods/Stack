#pragma once
#include "Assets/CreatedAsset.h"
#include "Assets/MaskRaster.h"
#include <algorithm>
#include <cmath>
namespace Stack::Assets {
// Display-only. Native coverage stays shared and untouched.
inline std::shared_ptr<const FloatData> MaskDisplayPreview(const MaskRaster& raster,int width,int height,int maxEdge) {
    if(width<=0 || height<=0 || raster.size()!=size_t(width)*height)return {};
    auto data=std::make_shared<FloatData>();
    const double scale=std::min(1.,double(maxEdge)/std::max(width,height));
    data->width=std::max(1,int(std::round(width*scale)));data->height=std::max(1,int(std::round(height*scale)));
    data->channels=1;data->values.resize(size_t(data->width)*data->height);
    for(int y=0;y<data->height;++y)for(int x=0;x<data->width;++x) {
        const int sx=std::min(width-1,int((x+.5)*width/data->width)),sy=std::min(height-1,int((y+.5)*height/data->height));
        data->values[size_t(y)*data->width+x]=raster[size_t(sy)*width+sx];
    }
    return data;
}
}

