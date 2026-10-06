#include "Raw/RawZoneArea.h"
#include "Raw/RawZoneAreaRasterizer.h"
#include <algorithm>
#include <cmath>

namespace Stack::RawRecipe {
bool ZoneAreaUsesGuidance(const RawZoneArea& area) {
    return std::any_of(area.strokes.begin(), area.strokes.end(), [](const auto& s) { return s.followEdges; });
}

std::vector<float> RasterizeGuidedZoneArea(const RawZoneArea& area, int width, int height,
    const RawCropRotationRecipe& transform, const ImageGuide& guide, const std::function<bool()>& cancelled) {
    ZoneAreaRasterizer rasterizer;
    const auto* mask=rasterizer.Evaluate(area,width,height,transform,&guide,cancelled);
    return mask ? *mask : std::vector<float>{};
}

std::shared_ptr<const RawZoneAreaMaskPreview> MakeZoneAreaMaskPreview(
    const std::vector<float>& pixels,int width,int height,std::size_t fingerprint,int maxEdge,const RawZoneArea* area) {
    if (width<=0 || height<=0 || maxEdge<=0 || pixels.size()!=std::size_t(width)*height) return {};
    auto result=std::make_shared<RawZoneAreaMaskPreview>();
    const float scale=std::min(1.0f,float(maxEdge)/std::max(width,height));
    result->width=std::max(1,int(std::lround(width*scale)));
    result->height=std::max(1,int(std::lround(height*scale)));
    result->maskFingerprint=fingerprint;
    if (area) {result->strokeCount=area->strokes.size();result->lastStrokePointCount=area->strokes.empty()?0:area->strokes.back().path.size();}
    result->coverage.resize(std::size_t(result->width)*result->height);
    // Area average preserves soft coverage in the display, including small
    // selected fragments, while native statistics use every original pixel.
    for (int y=0; y<result->height; ++y) for (int x=0; x<result->width; ++x) {
        const int x0=x*width/result->width, x1=std::max(x0+1,(x+1)*width/result->width);
        const int y0=y*height/result->height, y1=std::max(y0+1,(y+1)*height/result->height);
        double sum=0;
        for (int yy=y0; yy<y1; ++yy) for (int xx=x0; xx<x1; ++xx) sum+=pixels[std::size_t(yy)*width+xx];
        result->coverage[std::size_t(y)*result->width+x]=float(sum/((x1-x0)*(y1-y0)));
    }
    return result;
}
} // namespace Stack::RawRecipe
