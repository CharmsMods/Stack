#include "Renderer/SceneColorGuideCache.h"
#include "Renderer/GLHelpers.h"
#include "Raw/RawColorTransform.h"
#include "Raw/RawOrientation.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Stack::Renderer {
namespace {
void HashBytes(std::uint64_t& hash,const void* value,std::size_t size) {
    const auto* bytes=static_cast<const unsigned char*>(value);
    for(std::size_t i=0;i<size;++i) { hash^=bytes[i]; hash*=1099511628211ull; }
}
std::array<float,9> Multiply(const std::array<float,9>& a,const std::array<float,9>& b) {
    std::array<float,9> result{};
    for(int r=0;r<3;++r) for(int c=0;c<3;++c)
        for(int k=0;k<3;++k) result[3*r+c]+=a[3*r+k]*b[3*k+c];
    return result;
}
// Same output-to-sensor transform as RawGpuPipeline::orientVisibleUv. Both
// coordinates here are top-down; only the upload row is inverted for OpenGL.
std::array<unsigned,2> SensorPixel(unsigned x,unsigned y,unsigned w,unsigned h,
    const SceneColorGuideTransform& t) {
    if(t.flipHorizontally)x=w-1-x;
    if(t.flipVertically)y=h-1-y;
    switch(t.orientation) {
        case 2:return {w-1-x,y};
        case 3:return {w-1-x,h-1-y};
        case 4:return {x,h-1-y};
        case 5:return {y,x};
        case 6:return {y,w-1-x};
        case 7:return {h-1-y,w-1-x};
        case 8:return {h-1-y,x};
        default:return {x,y};
    }
}
}

SceneColorGuideTransform BuildSceneColorGuideTransform(const Raw::RawMetadata& metadata,
    const Raw::RawDevelopSettings& settings,const RawRecipe::RawColorCalibrationTransform& calibration) {
    SceneColorGuideTransform result;
    auto matrix=Raw::BuildRawCameraToWorkingTransform(metadata,settings,true);
    if(!settings.cameraTransformEnabled || settings.debugBypassCameraTransform)
        matrix={1,0,0,0,1,0,0,0,1};
    if(settings.debugTransposeCameraMatrix)
        for(int r=0;r<3;++r) for(int c=r+1;c<3;++c)std::swap(matrix[3*r+c],matrix[3*c+r]);
    const auto wb=Raw::ResolveRawWhiteBalance(metadata,settings);
    for(int r=0;r<3;++r) for(int c=0;c<3;++c)matrix[3*r+c]*=wb[c];
    result.matrix=calibration.active ? Multiply(calibration.matrix,matrix) : matrix;
    result.orientation=Raw::RotateExifOrientationClockwise(metadata.orientation,settings.rotationDegrees);
    result.flipHorizontally=settings.flipHorizontally;
    result.flipVertically=settings.flipVertically;
    return result;
}

SceneColorGuideCache::Lease SceneColorGuideCache::Prepare(
    const std::shared_ptr<const Raw::SceneColorGuide>& source,const SceneColorGuideTransform& transform,
    const std::function<bool()>& shouldCancel) {
    if(!source || (shouldCancel && shouldCancel()))return {};
    for(const auto& entry:m_Entries)
        if(entry->source==source && entry->transform==transform)
            return {{entry->color.Get(),entry->confidence.Get(),entry->revision},entry};
    if(!source->Valid() || transform.orientation<1 || transform.orientation>8 ||
        !std::all_of(transform.matrix.begin(),transform.matrix.end(),[](float v){return std::isfinite(v);}))
        throw std::runtime_error("The HDR source color guide or its color transform is invalid.");
    if(m_Entries.size()==2) {
        const auto disposable=std::find_if(m_Entries.begin(),m_Entries.end(),[](const auto& entry){return entry.use_count()==1;});
        if(disposable==m_Entries.end())
            throw std::runtime_error("The active graph exceeds the two-source color-guidance memory budget.");
        // Evict before allocating, and never destroy an active consumer's map.
        m_Entries.erase(disposable);
    }
    const bool swap=transform.orientation>=5;
    const unsigned width=swap ? source->height : source->width;
    const unsigned height=swap ? source->width : source->height;
    const auto count=std::size_t(width)*height;
    std::vector<float> color(count*4,0),confidence(count,0);
    for(unsigned y=0;y<height;++y) {
        if(shouldCancel && shouldCancel())return {};
        for(unsigned x=0;x<width;++x) {
            const auto pixel=SensorPixel(x,y,width,height,transform);
            const auto from=std::size_t(pixel[1])*source->width+pixel[0];
            const auto to=std::size_t(height-1-y)*width+x;
            double energy=0,uncertainty=0;
            bool finite=true;
            for(int c=0;c<3;++c) {
                double value=0,sigma=0;
                for(int k=0;k<3;++k) {
                    const double m=transform.matrix[3*c+k];
                    value+=m*source->cameraRgb[3*from+k];
                    // Unknown cross-channel covariance: the triangle bound on
                    // standard deviation avoids assuming independent channels.
                    sigma+=std::abs(m)*std::sqrt(double(source->variance[3*from+k]));
                }
                finite &= std::isfinite(value) && std::abs(value)<=std::numeric_limits<float>::max();
                color[4*to+c]=finite ? float(value) : 0.f;
                energy+=value*value; uncertainty+=sigma*sigma;
            }
            // Keep signed scene RGB. Confidence alone describes source support;
            // alpha remains opaque and never carries uncertainty or clipping.
            color[4*to+3]=1;
            if(finite && energy>0 && std::isfinite(energy+uncertainty)) {
                const double transformTrust=energy/(energy+64*uncertainty);
                confidence[to]=float(source->reliability[from]*source->coverage[from]*transformTrust);
            }
        }
    }
    Entry entry;
    entry.source=source; entry.transform=transform;
    entry.revision=1469598103934665603ull;
    constexpr char contract[]="scene-color-working-v1";
    HashBytes(entry.revision,contract,sizeof(contract));
    HashBytes(entry.revision,source->identity.data(),source->identity.size());
    HashBytes(entry.revision,transform.matrix.data(),sizeof(float)*9);
    HashBytes(entry.revision,&transform.orientation,sizeof(transform.orientation));
    HashBytes(entry.revision,&transform.flipHorizontally,sizeof(bool));
    HashBytes(entry.revision,&transform.flipVertically,sizeof(bool));
    if(!entry.revision)entry.revision=1;
    // Consumers do confidence-aware interpolation explicitly, so unknown cells
    // cannot leak invalid RGB through ordinary linear texture filtering.
    entry.color.Reset(GLHelpers::CreateTextureFromData(color.data(),width,height,GL_RGBA32F,GL_RGBA,GL_FLOAT));
    entry.confidence.Reset(GLHelpers::CreateTextureFromData(confidence.data(),width,height,GL_R32F,GL_RED,GL_FLOAT));
    if(shouldCancel && shouldCancel())return {};
    if(!entry.color || !entry.confidence)
        throw std::runtime_error("The renderer could not allocate or upload the HDR source color guide.");
    m_Entries.push_back(std::make_shared<Entry>(std::move(entry)));
    const auto& ready=m_Entries.back();
    return {{ready->color.Get(),ready->confidence.Get(),ready->revision},ready};
}
}
