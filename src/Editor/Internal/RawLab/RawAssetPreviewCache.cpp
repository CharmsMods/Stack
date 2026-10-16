#include "RawAssetPreviewCache.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Renderer/GLStateGuards.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Stack::Editor {
void RawAssetPreviewCache::Synchronize(const std::string& document, const Assets::Library& assets) {
    if (m_Document != document) { Clear(); m_Document = document; }
    for (auto it=m_Entries.begin();it!=m_Entries.end();) {
        const auto found=std::find_if(assets.begin(),assets.end(),[&](const auto& a){return a.id==it->first;});
        if (found==assets.end() || it->second.data.lock()!=found->data) it=m_Entries.erase(it);
        else ++it;
    }
}

unsigned int RawAssetPreviewCache::Texture(const Assets::Record& asset) {
    const int space=asset.provenance.value("workingSpace",int(Raw::RawWorkingSpace::LinearSrgbD65));
    auto found=m_Entries.find(asset.id);
    if (found!=m_Entries.end() && found->second.data.lock()==asset.data &&
        found->second.kind==asset.kind && found->second.workingSpace==space) {
        found->second.lastUse=++m_Use; return found->second.texture.Get();
    }
    if (!asset.data || asset.data->width<=0 || asset.data->height<=0 || asset.data->channels<=0) return 0;
    const auto& data=*asset.data;
    std::array<unsigned char,64*40*4> pixels{};
    for(int y=0;y<40;++y)for(int x=0;x<64;++x) {
        const int sx=std::min(data.width-1,int((x+.5f)*data.width/64));
        const int sy=std::min(data.height-1,int((y+.5f)*data.height/40));
        const std::size_t at=(std::size_t(sy)*data.width+sx)*data.channels;
        if (at+data.channels>data.values.size()) return 0;
        std::array<float,3> rgb{data.values[at],data.values[at],data.values[at]};
        if(data.channels>=3) {
            rgb={data.values[at],data.values[at+1],data.values[at+2]};
            if(space==int(Raw::RawWorkingSpace::LinearRec2020D65))
                rgb=RawRecipe::ColorWarpCoordinateToWorkingRgb(RawRecipe::WorkingRgbToColorWarpCoordinate(rgb,Raw::RawWorkingSpace::LinearRec2020D65),Raw::RawWorkingSpace::LinearSrgbD65);
        } else if(data.channels==2) {
            RawRecipe::RawColorWarpCoordinate color; color.a=data.values[at];color.b=data.values[at+1];color.lightness=.7f;
            rgb=RawRecipe::ColorWarpCoordinateToWorkingRgb(color,Raw::RawWorkingSpace::LinearSrgbD65);
        }
        for(int c=0;c<3;++c) {
            float value=std::clamp(rgb[c],0.f,1.f);
            if(asset.kind!=Assets::Kind::Mask)value=value<=.0031308f?12.92f*value:1.055f*std::pow(value,1.f/2.4f)-.055f;
            pixels[(y*64+x)*4+c]=static_cast<unsigned char>(value*255);
        }
        pixels[(y*64+x)*4+3]=255;
    }
    Renderer::GLState::TextureBinding binding(GL_TEXTURE_2D,GL_TEXTURE_BINDING_2D);
    Renderer::GLState::PixelUnpackState unpack; unpack.ConfigureTightCpuUpload();
    unsigned int texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,40,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    unpack.Restore();binding.Restore();
    if(m_Entries.size()>=128 && found==m_Entries.end()) {
        const auto oldest=std::min_element(m_Entries.begin(),m_Entries.end(),[](const auto& a,const auto& b){return a.second.lastUse<b.second.lastUse;});
        m_Entries.erase(oldest);
    }
    auto& entry=m_Entries[asset.id];entry.texture.Reset(texture);entry.data=asset.data;
    entry.kind=asset.kind;entry.workingSpace=space;entry.lastUse=++m_Use;
    return texture;
}
}
