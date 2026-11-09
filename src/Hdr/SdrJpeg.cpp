#include "ImageFile.h"
#include "ThirdParty/stb_image_write.h"
#include <lcms2.h>
#include <stdexcept>

namespace Stack::Hdr {
std::vector<unsigned char> EncodeSdrJpeg(int width,int height,const std::vector<unsigned char>& rgba) {
    if(width<=0 || height<=0 || width>65535 || height>65535 || rgba.size()!=size_t(width)*height*4)
        throw std::runtime_error("Invalid SDR JPEG dimensions.");
    for(size_t i=3;i<rgba.size();i+=4)
        if(rgba[i]!=255) throw std::runtime_error("JPEG requires an opaque image. Choose SDR PNG to preserve transparency.");
    std::vector<unsigned char> jpeg;
    const auto write=[](void* context,void* data,int count) {
        auto& bytes=*static_cast<std::vector<unsigned char>*>(context);
        const auto* begin=static_cast<unsigned char*>(data);
        bytes.insert(bytes.end(),begin,begin+count);
    };
    if(!stbi_write_jpg_to_func(write,&jpeg,width,height,4,rgba.data(),95) || jpeg.size()<2)
        throw std::runtime_error("Could not encode SDR JPEG.");
    // These bytes are sRGB encoded. Tag them explicitly for managed readers.
    const auto profile=cmsCreate_sRGBProfile();
    if(!profile) throw std::runtime_error("Could not create SDR JPEG color profile.");
    struct ProfileScope { cmsHPROFILE value; ~ProfileScope(){cmsCloseProfile(value);} } scope{profile};
    cmsUInt32Number size=0;
    const bool measured=cmsSaveProfileToMem(profile,nullptr,&size)!=0;
    std::vector<unsigned char> icc(measured ? size : 0);
    const bool saved=measured && size>0 && size<=65519 && cmsSaveProfileToMem(profile,icc.data(),&size);
    if(!saved) throw std::runtime_error("Could not encode SDR JPEG color profile.");
    const unsigned length=size+16;
    std::vector<unsigned char> marker{255,226,static_cast<unsigned char>(length>>8),static_cast<unsigned char>(length),
        'I','C','C','_','P','R','O','F','I','L','E',0,1,1};
    marker.insert(marker.end(),icc.begin(),icc.end());
    jpeg.insert(jpeg.begin()+2,marker.begin(),marker.end());
    return jpeg;
}
}
