#include "ImageFile.h"
#include "Color.h"
#include <ultrahdr_api.h>
#include <climits>
#include <memory>
#include <stdexcept>

namespace Stack::Hdr {
namespace {
void Check(uhdr_error_info_t error) {
    if(error.error_code!=UHDR_CODEC_OK) throw std::runtime_error(error.has_detail ? error.detail : "HDR JPEG codec error.");
}
}
bool IsGainMapJpeg(const std::vector<unsigned char>& b) {
    return b.size()>4 && b[0]==255 && b[1]==216 && b.size()<=INT_MAX &&
        is_uhdr_image(const_cast<unsigned char*>(b.data()),int(b.size()))==1;
}
std::vector<unsigned char> EncodeGainMapJpeg(const Image& im,const std::vector<unsigned char>& sdr) {
    if(im.width<=0 || im.height<=0 || im.rgba.size()!=size_t(im.width)*im.height*4 || sdr.size()!=im.rgba.size())
        throw std::runtime_error("Invalid HDR JPEG renditions.");
    std::vector<uint32_t> pq(im.rgba.size()/4);
    for(size_t i=0;i<pq.size();++i) {
        const auto* p=&im.rgba[4*i];
        if(!std::all_of(p,p+4,[](float v){ return std::isfinite(v); })) throw std::runtime_error("Non-finite HDR JPEG pixel.");
        if(p[3]!=1.f) throw std::runtime_error("Choose HDR PNG to preserve transparency.");
        const auto c=To2020({p[0],p[1],p[2]}); uint32_t pixel=3u<<30;
        for(int k=0;k<3;++k) pixel|=uint32_t(std::lround(Pq(c[k]*ReferenceWhite)*1023.f))<<(k*10);
        pq[i]=pixel;
    }
    const auto w=unsigned(im.width),h=unsigned(im.height);
    uhdr_raw_image_t hdr{UHDR_IMG_FMT_32bppRGBA1010102,UHDR_CG_BT_2100,UHDR_CT_PQ,UHDR_CR_FULL_RANGE,w,h,{pq.data()},{w}};
    uhdr_raw_image_t base{UHDR_IMG_FMT_32bppRGBA8888,UHDR_CG_BT_709,UHDR_CT_SRGB,UHDR_CR_FULL_RANGE,w,h,{const_cast<unsigned char*>(sdr.data())},{w}};
    std::unique_ptr<uhdr_codec_private_t,decltype(&uhdr_release_encoder)> enc(uhdr_create_encoder(),uhdr_release_encoder);
    if(!enc) throw std::bad_alloc();
    Check(uhdr_enc_set_raw_image(enc.get(),&hdr,UHDR_HDR_IMG));
    Check(uhdr_enc_set_raw_image(enc.get(),&base,UHDR_SDR_IMG));
    Check(uhdr_enc_set_using_multi_channel_gainmap(enc.get(),1));
    Check(uhdr_enc_set_quality(enc.get(),95,UHDR_BASE_IMG));
    Check(uhdr_enc_set_quality(enc.get(),95,UHDR_GAIN_MAP_IMG));
    Check(uhdr_enc_set_gainmap_scale_factor(enc.get(),2));
    Check(uhdr_enc_set_target_display_peak_brightness(enc.get(),PeakNits));
    Check(uhdr_encode(enc.get()));
    auto result=uhdr_get_encoded_stream(enc.get());
    if(!result || !result->data || result->data_sz==0) throw std::runtime_error("HDR JPEG encoder returned no image.");
    auto bytes=static_cast<unsigned char*>(result->data);
    return {bytes,bytes+result->data_sz};
}
Image DecodeGainMapJpeg(const std::vector<unsigned char>& b,float displayHeadroom) {
    std::unique_ptr<uhdr_codec_private_t,decltype(&uhdr_release_decoder)> dec(uhdr_create_decoder(),uhdr_release_decoder);
    if(!dec) throw std::bad_alloc();
    uhdr_compressed_image_t input{const_cast<unsigned char*>(b.data()),b.size(),b.size(),UHDR_CG_UNSPECIFIED,UHDR_CT_UNSPECIFIED,UHDR_CR_UNSPECIFIED};
    Check(uhdr_dec_set_image(dec.get(),&input));
    if(displayHeadroom>0.f) Check(uhdr_dec_set_out_max_display_boost(dec.get(),displayHeadroom));
    Check(uhdr_dec_set_out_color_transfer(dec.get(),UHDR_CT_PQ));
    Check(uhdr_dec_set_out_img_format(dec.get(),UHDR_IMG_FMT_32bppRGBA1010102));
    Check(uhdr_decode(dec.get()));
    auto raw=uhdr_get_decoded_image(dec.get());
    Image im; im.width=int(raw->w); im.height=int(raw->h); im.rgba.resize(size_t(im.width)*im.height*4);
    for(int y=0;y<im.height;++y) for(int x=0;x<im.width;++x) {
        auto word=static_cast<uint32_t*>(raw->planes[0])[size_t(y)*raw->stride[0]+x];
        Rgb c{}; for(int k=0;k<3;++k) c[k]=Nits(float((word>>(k*10))&1023)/1023.f)/ReferenceWhite;
        if(raw->cg==UHDR_CG_BT_2100) c=To709(c);
        else if(raw->cg==UHDR_CG_DISPLAY_P3) c={1.22494f*c[0]-.22494f*c[1],-.042057f*c[0]+1.042057f*c[1],-.019638f*c[0]-.078636f*c[1]+1.098274f*c[2]};
        auto* dst=&im.rgba[(size_t(y)*im.width+x)*4];
        std::copy(c.begin(),c.end(),dst); dst[3]=1.f;
    }
    return im;
}
}
