#include "Renderer/Internal/RawZoneAreaRenderer.h"
#include "Renderer/RawDevelopmentStageCachePolicy.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/ScopedGLObjects.h"
#include "Raw/SceneColorGuide.h"
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Stack::Renderer {
namespace {
void Mix(std::size_t& key,std::size_t v) { key^=v+0x9e3779b9u+(key<<6u)+(key>>2u); }
struct ReadState {
    GLState::FramebufferState framebuffer;
    GLState::PixelPackState pack;
    GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST);
    unsigned int buffers[2]{};
    ReadState() {glGenFramebuffers(2,buffers); pack.ConfigureTightCpuReadback(); glDisable(GL_SCISSOR_TEST);}
    ~ReadState() {
        framebuffer.Restore(); pack.Restore(); glDeleteFramebuffers(2,buffers);
        if (scissor) glEnable(GL_SCISSOR_TEST);
    }
};

struct ColorSample { std::array<double,3> rgb{}; float confidence=0; };
struct ColorAverage {
    std::array<double,3> sum{};
    double weight=0;
    double count=0;
    void Add(const float* rgb,double confidence) {
        for (int c=0;c<3;++c) sum[c]+=double(rgb[c])*confidence;
        weight+=confidence;
    }
};
void AccumulateColor(std::vector<ColorAverage>& cells,int width,int height,
    int sourceWidth,int sourceHeight,int x,int y,const float* rgb,float confidence) {
    // Uniform guide footprints share the producer's fractional area convention.
    // Integer binning shifts boundaries when sensor/guide dimensions do not divide.
    const double x0=double(x)*width/sourceWidth,x1=double(x+1)*width/sourceWidth;
    const double y0=double(y)*height/sourceHeight,y1=double(y+1)*height/sourceHeight;
    const int firstX=std::clamp(int(std::floor(x0)),0,width-1);
    const int lastX=std::clamp(int(std::ceil(x1))-1,0,width-1);
    const int firstY=std::clamp(int(std::floor(y0)),0,height-1);
    const int lastY=std::clamp(int(std::ceil(y1))-1,0,height-1);
    for (int yy=firstY;yy<=lastY;++yy) for (int xx=firstX;xx<=lastX;++xx) {
        const double area=(std::min(x1,double(xx+1))-std::max(x0,double(xx)))*
            (std::min(y1,double(yy+1))-std::max(y0,double(yy)));
        auto& cell=cells[std::size_t(yy)*width+xx];
        cell.count+=area;
        if (confidence>0) cell.Add(rgb,area*confidence);
    }
}
struct ColorField {
    int width=0,height=0;
    std::vector<ColorSample> pixels;
    float ConfidenceAt(float u,float v) const {
        const float x=u*width-.5f,y=v*height-.5f;
        const int x0=int(std::floor(x)),y0=int(std::floor(y));
        const float fx=x-x0,fy=y-y0;
        float result=0;
        for (int dy=0;dy<2;++dy) for (int dx=0;dx<2;++dx) {
            const auto& p=pixels[std::size_t(std::clamp(y0+dy,0,height-1))*width+
                std::clamp(x0+dx,0,width-1)];
            result+=(dx ? fx : 1-fx)*(dy ? fy : 1-fy)*p.confidence;
        }
        return std::clamp(result,0.f,1.f);
    }
    void CheckAgreement(const std::vector<ColorAverage>& reference) {
        for (std::size_t i=0;i<pixels.size();++i) {
            auto& source=pixels[i];
            const auto& native=reference[i];
            const double sourceSum=std::abs(source.rgb[0])+std::abs(source.rgb[1])+std::abs(source.rgb[2]);
            const double nativeSum=std::abs(native.sum[0])+std::abs(native.sum[1])+std::abs(native.sum[2]);
            if (!(source.confidence>0) || !(native.weight>0) || !(sourceSum>0) || !(nativeSum>0)) {
                source.confidence=0; continue;
            }
            double distance=0;
            for (int c=0;c<3;++c) {
                const double delta=source.rgb[c]/sourceSum-native.sum[c]/nativeSum;
                distance+=delta*delta;
            }
            // A bounded, exposure-invariant suitability check, not a calibrated
            // probability. Lower source trust admits more chroma uncertainty.
            // Compare cell averages so a supported native thin edge does not
            // disagree merely because the compact guide contains its mixture.
            const double tolerance=.04+.12*(1-source.confidence);
            source.confidence*=float(1/(1+distance/(tolerance*tolerance)));
        }
    }
};

bool ReadColorField(const SceneColorGuidance& guidance,unsigned int framebuffer,
    int maximumWidth,int maximumHeight,const std::function<bool()>& cancelled,ColorField& field) {
    if (!guidance.IsAvailable()) return true;
    GLint binding=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
    struct RestoreBinding {GLint value;~RestoreBinding(){glBindTexture(GL_TEXTURE_2D,value);}} restore{binding};
    const auto dimensions=[&](unsigned int texture,int& width,int& height) {
        glBindTexture(GL_TEXTURE_2D,texture);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&height);
    };
    int sourceWidth=0,sourceHeight=0,confidenceWidth=0,confidenceHeight=0;
    dimensions(guidance.colorTexture,sourceWidth,sourceHeight);
    dimensions(guidance.confidenceTexture,confidenceWidth,confidenceHeight);
    if (sourceWidth<=0 || sourceHeight<=0 || sourceWidth!=confidenceWidth || sourceHeight!=confidenceHeight)
        throw std::runtime_error("Zones color guidance textures have incompatible dimensions");
    // Agreement is a bounded broad-footprint check even if a future caller
    // supplies a larger texture. Native coordinates still supply every edge.
    constexpr int maximumColorEdge=int(Raw::SceneColorGuide::MaximumDimension);
    field.width=std::min({sourceWidth,maximumWidth,maximumColorEdge});
    field.height=std::min({sourceHeight,maximumHeight,maximumColorEdge});
    if (std::size_t(field.width)>std::numeric_limits<std::size_t>::max()/std::size_t(field.height)/sizeof(ColorAverage))
        throw std::runtime_error("Zones color guidance dimensions exceed addressable storage");
    field.pixels.resize(std::size_t(field.width)*field.height);
    std::vector<ColorAverage> averages(field.pixels.size());
    constexpr int stripRows=32;
    const auto stripPixels=std::size_t(sourceWidth)*std::min(stripRows,sourceHeight);
    std::vector<float> rgba(stripPixels*4),confidence(stripPixels);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,framebuffer);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    const auto read=[&](unsigned int texture,int y,int rows,GLenum format,float* destination) {
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
        if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Zones color guidance readback framebuffer unavailable");
        glReadPixels(0,y,sourceWidth,rows,format,GL_FLOAT,destination);
        if (glGetError()!=GL_NO_ERROR) throw std::runtime_error("Zones color guidance readback failed");
    };
    for (int y=0;y<sourceHeight;y+=stripRows) {
        if (cancelled && cancelled()) return false;
        const int rows=std::min(stripRows,sourceHeight-y);
        read(guidance.colorTexture,y,rows,GL_RGBA,rgba.data());
        read(guidance.confidenceTexture,y,rows,GL_RED,confidence.data());
        for (int row=0;row<rows;++row) for (int x=0;x<sourceWidth;++x) {
            const auto i=std::size_t(row)*sourceWidth+x;
            const auto* rgb=rgba.data()+i*4;
            const bool valid=std::isfinite(confidence[i]) && confidence[i]>0 &&
                std::isfinite(rgb[0]) && std::isfinite(rgb[1]) && std::isfinite(rgb[2]) &&
                std::isfinite(rgb[3]) && rgb[3]>0 && rgb[3]<=1;
            AccumulateColor(averages,field.width,field.height,sourceWidth,sourceHeight,x,y+row,rgb,
                valid ? std::min(1.f,confidence[i]) : 0.f);
        }
    }
    for (std::size_t i=0;i<averages.size();++i) {
        const auto& average=averages[i];
        if (!(average.weight>0)) continue;
        for (int c=0;c<3;++c) field.pixels[i].rgb[c]=average.sum[c]/average.weight;
        field.pixels[i].confidence=std::clamp(float(average.weight/average.count),0.f,1.f);
    }
    return true;
}

std::shared_ptr<RawRecipe::ImageGuide> ReadGuide(unsigned int texture,int width,int height,
    int maxEdge,Raw::RawWorkingSpace workingSpace,const std::function<bool()>& cancelled,
    const SceneColorGuidance& guidance) {
    if (!texture || width<=0 || height<=0) return {};
    auto guide=std::make_shared<RawRecipe::ImageGuide>();
    const float scale=maxEdge>0 ? std::min(1.0f,float(maxEdge)/std::max(width,height)) : 1;
    guide->width=std::max(1,int(std::lround(width*scale)));
    guide->height=std::max(1,int(std::lround(height*scale)));
    ReadState state;
    ColorField colors;
    if (!ReadColorField(guidance,state.buffers[0],guide->width,guide->height,cancelled,colors)) return {};
    std::vector<ColorAverage> nativeColors(colors.pixels.size());
    glBindFramebuffer(GL_READ_FRAMEBUFFER,state.buffers[0]);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    ScopedGLTexture reduced;
    if (guide->width!=width || guide->height!=height) {
        reduced.Reset(GLHelpers::CreateStorageTexture(guide->width,guide->height,GL_RGBA32F));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,state.buffers[1]);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,reduced.Get(),0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glBlitFramebuffer(0,0,width,height,0,0,guide->width,guide->height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,state.buffers[1]);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
    }
    if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Zones reference readback framebuffer unavailable");
    guide->pixels.resize(std::size_t(guide->width)*guide->height);
    if (guidance.IsAvailable()) guide->colorConfidence.resize(guide->pixels.size(),0);
    // Native resolution is never capped. Read a strip at a time to avoid an
    // additional full-resolution RGBA allocation, and cancel between strips.
    constexpr int stripRows=32;
    std::vector<float> rgba(std::size_t(guide->width)*std::min(stripRows,guide->height)*4);
    for (int y=0; y<guide->height; y+=stripRows) {
        if (cancelled && cancelled()) return {};
        const int rows=std::min(stripRows,guide->height-y);
        glReadPixels(0,y,guide->width,rows,GL_RGBA,GL_FLOAT,rgba.data());
        if (glGetError()!=GL_NO_ERROR) throw std::runtime_error("Zones reference image readback failed");
        for (int row=0; row<rows; ++row) for (int x=0; x<guide->width; ++x) {
            const auto i=(std::size_t(row)*guide->width+x)*4;
            const auto index=std::size_t(guide->height-1-y-row)*guide->width+x;
            auto& p=guide->pixels[index];
            if (!std::isfinite(rgba[i]) || !std::isfinite(rgba[i+1]) || !std::isfinite(rgba[i+2]) ||
                !std::isfinite(rgba[i+3]) || !(rgba[i+3]>0) || rgba[i+3]>1) {
                p.sceneEv=std::numeric_limits<float>::quiet_NaN(); continue;
            }
            const auto color=RawRecipe::WorkingRgbToColorWarpCoordinate({rgba[i],rgba[i+1],rgba[i+2]},workingSpace);
            if (!std::isfinite(color.a) || !std::isfinite(color.b) || !std::isfinite(color.sceneEv)) {
                p.sceneEv=std::numeric_limits<float>::quiet_NaN(); continue;
            }
            p={color.a,color.b,color.sceneEv};
            if (guidance.IsAvailable()) {
                // Native a/b preserves thin boundaries that the compact source
                // guide cannot resolve. Agreement is evaluated over its whole
                // cell footprint, independently of the authored EV adjustment.
                AccumulateColor(nativeColors,colors.width,colors.height,guide->width,guide->height,
                    x,y+row,rgba.data()+i,rgba[i+3]);
            }
        }
    }
    if (guidance.IsAvailable()) {
        colors.CheckAgreement(nativeColors);
        for (int y=0;y<guide->height;++y) {
            if (cancelled && cancelled()) return {};
            for (int x=0;x<guide->width;++x) {
                const auto index=std::size_t(guide->height-1-y)*guide->width+x;
                if (std::isfinite(guide->pixels[index].sceneEv))
                    guide->colorConfidence[index]=colors.ConfidenceAt((x+.5f)/guide->width,(y+.5f)/guide->height);
            }
        }
    }
    return guide;
}

std::shared_ptr<const RawRecipe::ImageGuide> Preview(const std::shared_ptr<const RawRecipe::ImageGuide>& source) {
    if (std::max(source->width,source->height)<=768) return source;
    auto preview=std::make_shared<RawRecipe::ImageGuide>();
    preview->revision=source->revision; preview->recipeFingerprint=source->recipeFingerprint;
    const float scale=768.0f/std::max(source->width,source->height);
    preview->width=std::max(1,int(std::lround(source->width*scale)));
    preview->height=std::max(1,int(std::lround(source->height*scale)));
    preview->pixels.resize(std::size_t(preview->width)*preview->height);
    if (!source->colorConfidence.empty()) preview->colorConfidence.resize(preview->pixels.size());
    for (int y=0; y<preview->height; ++y) for (int x=0; x<preview->width; ++x) {
        const int xx=std::min(source->width-1,int((x+.5f)*source->width/preview->width));
        const int yy=std::min(source->height-1,int((y+.5f)*source->height/preview->height));
        preview->pixels[std::size_t(y)*preview->width+x]=source->pixels[std::size_t(yy)*source->width+xx];
        if (!preview->colorConfidence.empty())
            preview->colorConfidence[std::size_t(y)*preview->width+x]=source->colorConfidence[std::size_t(yy)*source->width+xx];
    }
    return preview;
}
}

bool RawZoneAreaRenderer::PrepareGuide(unsigned int reference,int width,int height,
    const RawRecipe::RawDevelopmentRecipe& recipe,std::size_t referenceRevision,bool native,
    const std::function<bool()>& cancelled,const SceneColorGuidance& guidance) {
    if ((guidance.colorTexture || guidance.confidenceTexture || guidance.revision) &&
        (!guidance.IsAvailable() || !guidance.revision))
        throw std::runtime_error("Zones color guidance requires both textures and a content revision");
    std::size_t key=referenceRevision;
    Mix(key,width); Mix(key,height); Mix(key,int(recipe.technical.workingSpace));
    if (guidance.IsAvailable()) Mix(key,std::hash<std::uint64_t>{}(guidance.revision));
    if (!native) {m_Guide.reset(); m_GuideKey=0;}
    if (referenceRevision && (native ? m_GuideKey==key && m_Guide : m_PreviewGuideKey==key && m_PreviewGuide)) return true;
    auto guide=ReadGuide(reference,width,height,native ? 0 : 768,recipe.technical.workingSpace,cancelled,guidance);
    if (!guide) return false;
    // Callers without a source revision must identify actual content. A GL
    // texture name alone can be recycled or overwritten between images.
    if (!referenceRevision) for (const auto& p:guide->pixels) {
        Mix(key,std::hash<float>{}(p.a)); Mix(key,std::hash<float>{}(p.b)); Mix(key,std::hash<float>{}(p.sceneEv));
    }
    if (!referenceRevision) for (const auto confidence:guide->colorConfidence) Mix(key,std::hash<float>{}(confidence));
    guide->revision=key;
    guide->recipeFingerprint=RawDevelopmentCache::BuildStageFingerprints(recipe,0).calibratedNeutral;
    if (native) {m_Guide=guide; m_GuideKey=key;}
    m_PreviewGuide=Preview(guide); m_PreviewGuideKey=key;
    ++m_GuideBuildCount;
    return true;
}
} // namespace Stack::Renderer
