#include "Renderer/Internal/RawZoneAreaRenderer.h"
#include "Renderer/RawDevelopmentStageCachePolicy.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/ScopedGLObjects.h"
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

std::shared_ptr<RawRecipe::ImageGuide> ReadGuide(unsigned int texture,int width,int height,
    int maxEdge,Raw::RawWorkingSpace workingSpace,const std::function<bool()>& cancelled) {
    auto guide=std::make_shared<RawRecipe::ImageGuide>();
    const float scale=maxEdge>0 ? std::min(1.0f,float(maxEdge)/std::max(width,height)) : 1;
    guide->width=std::max(1,int(std::lround(width*scale)));
    guide->height=std::max(1,int(std::lround(height*scale)));
    ReadState state;
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
    // Native resolution is never capped. Read a strip at a time to avoid an
    // additional full-resolution RGB allocation, and cancel between strips.
    constexpr int stripRows=32;
    std::vector<float> rgb(std::size_t(guide->width)*std::min(stripRows,guide->height)*3);
    for (int y=0; y<guide->height; y+=stripRows) {
        if (cancelled && cancelled()) return {};
        const int rows=std::min(stripRows,guide->height-y);
        glReadPixels(0,y,guide->width,rows,GL_RGB,GL_FLOAT,rgb.data());
        for (int row=0; row<rows; ++row) for (int x=0; x<guide->width; ++x) {
            const auto i=(std::size_t(row)*guide->width+x)*3;
            auto& p=guide->pixels[std::size_t(guide->height-1-y-row)*guide->width+x];
            if (!std::isfinite(rgb[i]) || !std::isfinite(rgb[i+1]) || !std::isfinite(rgb[i+2])) {
                p.sceneEv=std::numeric_limits<float>::quiet_NaN(); continue;
            }
            const auto color=RawRecipe::WorkingRgbToColorWarpCoordinate({rgb[i],rgb[i+1],rgb[i+2]},workingSpace);
            p={color.a,color.b,color.sceneEv};
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
    for (int y=0; y<preview->height; ++y) for (int x=0; x<preview->width; ++x) {
        const int xx=std::min(source->width-1,int((x+.5f)*source->width/preview->width));
        const int yy=std::min(source->height-1,int((y+.5f)*source->height/preview->height));
        preview->pixels[std::size_t(y)*preview->width+x]=source->pixels[std::size_t(yy)*source->width+xx];
    }
    return preview;
}
}

bool RawZoneAreaRenderer::PrepareGuide(unsigned int reference,int width,int height,
    const RawRecipe::RawDevelopmentRecipe& recipe,std::size_t referenceRevision,bool native,
    const std::function<bool()>& cancelled) {
    std::size_t key=referenceRevision;
    Mix(key,width); Mix(key,height); Mix(key,int(recipe.technical.workingSpace));
    if (!native) {m_Guide.reset(); m_GuideKey=0;}
    if (referenceRevision && (native ? m_GuideKey==key && m_Guide : m_PreviewGuideKey==key && m_PreviewGuide)) return true;
    auto guide=ReadGuide(reference,width,height,native ? 0 : 768,recipe.technical.workingSpace,cancelled);
    if (!guide) return false;
    // Callers without a source revision must identify actual content. A GL
    // texture name alone can be recycled or overwritten between images.
    if (!referenceRevision) for (const auto& p:guide->pixels) {
        Mix(key,std::hash<float>{}(p.a)); Mix(key,std::hash<float>{}(p.b)); Mix(key,std::hash<float>{}(p.sceneEv));
    }
    guide->revision=key;
    guide->recipeFingerprint=RawDevelopmentCache::BuildStageFingerprints(recipe,0).calibratedNeutral;
    if (native) {m_Guide=guide; m_GuideKey=key;}
    m_PreviewGuide=Preview(guide); m_PreviewGuideKey=key;
    ++m_GuideBuildCount;
    return true;
}
} // namespace Stack::Renderer
