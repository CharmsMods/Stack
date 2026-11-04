#include "Hdr/RasterSource.h"
#include "Hdr/Color.h"
#include "NodeMath/SourceColorMetadata.h"
#include "ThirdParty/stb_image.h"
#include <lcms2.h>
#include <zlib.h>
#include <climits>
#include <memory>
#include <stdexcept>

namespace Stack::Hdr {
namespace {
using NodeMath::EmbeddedColorPayloadKind;
std::vector<unsigned char> ProfileBytes(const NodeMath::SourceColorMetadata& metadata) {
    const auto& bytes=metadata.retainedPayload;
    if(metadata.payloadKind==EmbeddedColorPayloadKind::JpegIccProfile) return bytes;
    if(metadata.payloadKind!=EmbeddedColorPayloadKind::PngIccpChunk) return {};
    auto separator=std::find(bytes.begin(),bytes.end(),0);
    if(separator==bytes.end() || bytes.end()-separator<3 || separator[1]!=0) return {};
    const auto* compressed=&separator[2];
    const size_t size=bytes.size()-size_t(compressed-bytes.data());
    // The ICC header carries the decompressed size. Read it before allocating.
    std::array<unsigned char,128> header{};
    z_stream stream{};
    if(inflateInit(&stream)!=Z_OK) return {};
    stream.next_in=const_cast<unsigned char*>(compressed); stream.avail_in=static_cast<uInt>(size);
    stream.next_out=header.data(); stream.avail_out=static_cast<uInt>(header.size());
    const int status=inflate(&stream,Z_NO_FLUSH);
    const auto read=stream.total_out;
    inflateEnd(&stream);
    if((status!=Z_OK && status!=Z_STREAM_END) || read!=header.size()) return {};
    uLongf length=(uint32_t(header[0])<<24)|(uint32_t(header[1])<<16)|(uint32_t(header[2])<<8)|header[3];
    if(length<128 || length>64u*1024*1024) return {};
    std::vector<unsigned char> profile(length);
    if(uncompress(profile.data(),&length,compressed,static_cast<uLong>(size))!=Z_OK || length!=profile.size()) return {};
    return profile;
}
struct Profile {
    cmsHPROFILE handle;
    ~Profile() { if(handle) cmsCloseProfile(handle); }
};
struct Transform {
    cmsHTRANSFORM handle;
    ~Transform() { if(handle) cmsDeleteTransform(handle); }
};
}

unsigned char SdrPreviewByte(float v,FileFormat source) {
    constexpr float grey=.1845f;
    if(source==FileFormat::PqPng && v>grey) {
        const float slope=1.f/(1.f-grey)-1.f/(MasterHeadroom-grey);
        v=grey+(v-grey)/(1.f+(v-grey)*slope);
    }
    return static_cast<unsigned char>(std::lround(255.f*std::clamp(Srgb(v),0.f,1.f)));
}
bool DecodeDisplaySource(const std::vector<unsigned char>& bytes, Image& result, FileFormat* format) {
    if(bytes.empty() || bytes.size()>INT_MAX) return false;
    if(format) *format=FileFormat::SdrPng;
    if(IsPqPng(bytes)) { result=DecodePqPng(bytes); if(format) *format=FileFormat::PqPng; return true; }
    if(IsGainMapJpeg(bytes)) { result=DecodeGainMapJpeg(bytes); if(format) *format=FileFormat::GainMapJpeg; return true; }
    return DecodeProfiledSdr(bytes,result);
}
bool DecodeProfiledSdr(const std::vector<unsigned char>& bytes,Image& result) {
    if(bytes.empty() || bytes.size()>INT_MAX) return false;
    int width=0,height=0,channels=0;
    if(!stbi_info_from_memory(bytes.data(),int(bytes.size()),&width,&height,&channels)) return false;
    auto metadata=NodeMath::InspectSourceColorMetadata(bytes,width,height,channels,NodeMath::LogicalPrecision::UInt16,{});
    const bool p3=metadata.payloadKind==EmbeddedColorPayloadKind::PngColorSignal &&
        metadata.descriptor.color.value.identity=="display-p3-d65";
    auto profile=ProfileBytes(metadata);
    if(!p3 && profile.empty()) return false;
    stbi_set_flip_vertically_on_load_thread(0);
    std::unique_ptr<stbi_us,decltype(&stbi_image_free)> pixels(stbi_load_16_from_memory(bytes.data(),int(bytes.size()),
        &width,&height,&channels,4),stbi_image_free);
    if(!pixels) return false;
    Image image; image.width=width; image.height=height;
    image.rgba.resize(size_t(width)*height*4);
    if(p3) {
        for(size_t i=0;i<image.rgba.size();i+=4) {
            const float r=Linear(pixels.get()[i]/65535.f),g=Linear(pixels.get()[i+1]/65535.f),b=Linear(pixels.get()[i+2]/65535.f);
            image.rgba[i]=1.22494f*r-.22494f*g;
            image.rgba[i+1]=-.042057f*r+1.042057f*g;
            image.rgba[i+2]=-.019638f*r-.078636f*g+1.098274f*b;
        }
    } else {
        Profile src{cmsOpenProfileFromMem(profile.data(),static_cast<cmsUInt32Number>(profile.size()))};
        if(!src.handle) return false;
        const bool gray=cmsGetColorSpace(src.handle)==cmsSigGrayData;
        if(!gray && cmsGetColorSpace(src.handle)!=cmsSigRgbData) return false;
        cmsCIExyY wp{.3127,.3290,1};
        cmsCIExyYTRIPLE primaries{{.64,.33,1},{.30,.60,1},{.15,.06,1}};
        auto* gamma=cmsBuildGamma(nullptr,1.);
        if(!gamma) throw std::bad_alloc();
        cmsToneCurve* curves[]{gamma,gamma,gamma};
        Profile dst{cmsCreateRGBProfile(&wp,&primaries,curves)};
        cmsFreeToneCurve(gamma);
        if(!dst.handle) throw std::bad_alloc();
        Transform transform{cmsCreateTransform(src.handle,gray?TYPE_GRAY_16:TYPE_RGBA_16,dst.handle,TYPE_RGBA_FLT,
            INTENT_RELATIVE_COLORIMETRIC,cmsFLAGS_NOOPTIMIZE)};
        if(!transform.handle) return false;
        std::vector<stbi_us> grayRow(gray?width:0);
        for(int y=0;y<height;++y) {
            const auto offset=size_t(y)*width*4;
            const stbi_us* row=&pixels.get()[offset];
            if(gray) {
                for(int x=0;x<width;++x) grayRow[x]=row[4*x];
                row=grayRow.data();
            }
            cmsDoTransform(transform.handle,row,&image.rgba[offset],width);
        }
    }
    for(size_t i=3;i<image.rgba.size();i+=4) image.rgba[i]=pixels.get()[i]/65535.f;
    result=std::move(image);
    return true;
}
}
