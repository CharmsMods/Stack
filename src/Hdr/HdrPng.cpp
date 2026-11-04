#include "ImageFile.h"
#include "Color.h"
#include "ThirdParty/stb_image.h"
#include <zlib.h>
#include <cstring>
#include <climits>
#include <limits>
#include <memory>
#include <stdexcept>

namespace Stack::Hdr {
namespace {
constexpr unsigned char Signature[]{137,80,78,71,13,10,26,10};
constexpr unsigned char Cicp[]{9,16,0,1};
void U32(std::vector<unsigned char>& b,uint32_t n) {
    for(int shift=24;shift>=0;shift-=8) b.push_back(static_cast<unsigned char>(n>>shift));
}
uint32_t Read(const unsigned char* p) {
    return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
}
void Chunk(std::vector<unsigned char>& bytes,const char* tag,const unsigned char* data,size_t size) {
    U32(bytes,static_cast<uint32_t>(size));
    const auto offset=bytes.size();
    bytes.insert(bytes.end(),tag,tag+4);
    if(size) bytes.insert(bytes.end(),data,data+size);
    U32(bytes,crc32(0,bytes.data()+offset,static_cast<uInt>(4+size)));
}
}
bool IsPqPng(const std::vector<unsigned char>& b) {
    if(b.size()<33 || std::memcmp(b.data(),Signature,8)!=0) return false;
    for(size_t i=8;i<=b.size()-12;) {
        const auto n=Read(&b[i]);
        if(n>b.size()-i-12 || !std::memcmp(&b[i+4],"IDAT",4)) return false;
        if(!std::memcmp(&b[i+4],"cICP",4)) return n==4 && !std::memcmp(&b[i+8],Cicp,4)
            && Read(&b[i+12])==crc32(0,&b[i+4],8);
        i+=12+size_t(n);
    }
    return false;
}
std::vector<unsigned char> EncodePqPng(const Image& im) {
    if(im.width<=0 || im.height<=0 || size_t(im.width)*im.height*4!=im.rgba.size() || size_t(im.width)>(UINT_MAX-1u)/8)
        throw std::runtime_error("Invalid HDR PNG image dimensions.");
    std::vector<unsigned char> out(std::begin(Signature),std::end(Signature)),header;
    U32(header,im.width); U32(header,im.height);
    header.insert(header.end(),{16,6,0,0,0});
    Chunk(out,"IHDR",header.data(),header.size()); Chunk(out,"cICP",Cicp,4);
    z_stream z{};
    if(deflateInit(&z,4)!=Z_OK) throw std::runtime_error("Cannot initialize HDR PNG encoder.");
    struct End {z_stream& z; ~End(){deflateEnd(&z);} } end{z};
    std::vector<unsigned char> row(1+size_t(im.width)*8), block(65536);
    for(int y=0;y<im.height;++y) {
        for(int x=0;x<im.width;++x) {
            const auto* p=&im.rgba[4*(size_t(y)*im.width+x)];
            if(!std::all_of(p,p+4,[](float v){return std::isfinite(v);})) throw std::runtime_error("Non-finite HDR PNG pixel.");
            auto c=To2020({p[0],p[1],p[2]});
            for(int k=0;k<4;++k) {
                float sample=k==3 ? std::clamp(p[3],0.f,1.f) : Pq(ReferenceWhite*c[k]);
                if(!std::isfinite(sample)) throw std::runtime_error("Non-finite HDR PNG pixel.");
                auto n=static_cast<unsigned>(std::lround(sample*65535.f));
                const size_t offset=1+size_t(x)*8+size_t(k)*2;
                row[offset]=static_cast<unsigned char>(n>>8);
                row[offset+1]=static_cast<unsigned char>(n);
            }
        }
        row[0]=1;
        for(size_t i=row.size()-1;i>8;--i) row[i]-=row[i-8];
        z.next_in=row.data(); z.avail_in=static_cast<uInt>(row.size());
        const bool last=y==im.height-1;
        int status;
        do {
            z.next_out=block.data(); z.avail_out=static_cast<uInt>(block.size());
            status=deflate(&z,last?Z_FINISH:Z_NO_FLUSH);
            if(status!=Z_OK && status!=Z_STREAM_END) throw std::runtime_error("HDR PNG compression failed.");
            if(z.avail_out<block.size()) Chunk(out,"IDAT",block.data(),block.size()-z.avail_out);
        } while(z.avail_in || (last && status!=Z_STREAM_END));
    }
    Chunk(out,"IEND",nullptr,0);
    return out;
}
Image DecodePqPng(const std::vector<unsigned char>& b) {
    if(!IsPqPng(b) || b.size()>INT_MAX) throw std::runtime_error("Unsupported HDR PNG.");
    stbi_set_flip_vertically_on_load_thread(0);
    Image image; int channels=0;
    std::unique_ptr<stbi_us,decltype(&stbi_image_free)> p(stbi_load_16_from_memory(b.data(),int(b.size()),&image.width,&image.height,&channels,4),stbi_image_free);
    if(!p) throw std::runtime_error("Could not decode HDR PNG pixels.");
    image.rgba.resize(size_t(image.width)*image.height*4);
    for(size_t i=0;i<image.rgba.size();i+=4) {
        Rgb rgb{};
        for(int k=0;k<3;++k) rgb[k]=Nits(p.get()[i+k]/65535.f)/ReferenceWhite;
        rgb=To709(rgb); std::copy(rgb.begin(),rgb.end(),&image.rgba[i]);
        image.rgba[i+3]=p.get()[i+3]/65535.f;
    }
    return image;
}
}
