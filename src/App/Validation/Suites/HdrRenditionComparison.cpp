#include "HdrRenditionComparison.h"
#include "Hdr/Color.h"
#include "ThirdParty/stb_image.h"
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace Stack::Validation {
namespace {
void Write(const std::filesystem::path& path,const std::vector<unsigned char>& bytes) {
    std::ofstream f(path,std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    if(!f) throw std::runtime_error("Cannot write HDR comparison artifact.");
}
// Preserve the primary image's compressed coefficients and ICC. Remove HDR
// metadata and the appended gain-map image without recompressing the base.
std::vector<unsigned char> StripGainMap(const std::vector<unsigned char>& bytes) {
    std::vector<unsigned char> result{0xff,0xd8};
    std::size_t i=2;
    while(i+3<bytes.size()) {
        const auto start=i;
        if(bytes[i++]!=0xff) break;
        while(i<bytes.size() && bytes[i]==0xff) ++i;
        if(i>=bytes.size()) break;
        const unsigned marker=bytes[i++];
        if(marker==0xd9) {result.insert(result.end(),{0xff,0xd9});return result;}
        const std::size_t length=(unsigned(bytes[i])<<8)|bytes[i+1];
        if(length<2 || i+length>bytes.size()) break;
        const bool icc=marker==0xe2 && length>=14 && std::memcmp(bytes.data()+i+2,"ICC_PROFILE\0",12)==0;
        const bool keep=marker<0xe0 || marker>0xef || marker==0xe0 || icc;
        if(keep) result.insert(result.end(),bytes.begin()+start,bytes.begin()+i+length);
        i+=length;
        if(marker==0xda) {
            const auto entropy=i;
            while(i+1<bytes.size()) {
                if(bytes[i]==0xff && bytes[i+1]==0xd9) {
                    result.insert(result.end(),bytes.begin()+entropy,bytes.begin()+i+2);
                    return result;
                }
                ++i;
            }
            break;
        }
    }
    throw std::runtime_error("Could not extract JPEG primary image.");
}
}
void WriteHdrRenditionComparison(const Hdr::Image& original,
    const std::vector<unsigned char>& hdrJpeg,const std::vector<unsigned char>& sdr,
    const std::filesystem::path& output) {
    const auto stripped=StripGainMap(hdrJpeg);
    if(Hdr::IsGainMapJpeg(stripped)) throw std::runtime_error("Comparison JPEG still contains HDR data.");
    Write(output/"HDR-stripped.jpg",stripped);
    int w=0,h=0,c=0;
    using Pixels=std::unique_ptr<unsigned char,decltype(&stbi_image_free)>;
    Pixels base(stbi_load_from_memory(stripped.data(),int(stripped.size()),&w,&h,&c,4),stbi_image_free);
    Pixels originalBase(stbi_load_from_memory(hdrJpeg.data(),int(hdrJpeg.size()),&w,&h,&c,4),stbi_image_free);
    if(!base || !originalBase || w!=original.width || h!=original.height ||
       std::memcmp(base.get(),originalBase.get(),std::size_t(w)*h*4)!=0)
        throw std::runtime_error("Gain-map stripping changed primary image pixels.");
    const int panel=std::min(960,w),height=std::max(1,int(double(h)*panel/w)),gap=16;
    Hdr::Image comparison;comparison.width=panel*3+gap*2;comparison.height=height;
    comparison.rgba.resize(std::size_t(comparison.width)*height*4,1.f);
    std::vector<unsigned char> preview(comparison.rgba.size(),255);
    for(int y=0;y<height;++y) for(int x=0;x<comparison.width;++x) {
        const auto dest=4*(std::size_t(y)*comparison.width+x);
        const int section=x/(panel+gap),local=x%(panel+gap);
        Hdr::Rgb master{.015f,.015f,.015f},fallback=master;
        if(section<3 && local<panel) {
            master={};fallback={};int count=0;
            const int x0=local*w/panel,x1=std::max(x0+1,(local+1)*w/panel);
            const int y0=y*h/height,y1=std::max(y0+1,(y+1)*h/height);
            for(int sy=y0;sy<y1;++sy) for(int sx=x0;sx<x1;++sx) {
                const auto src=4*(std::size_t(sy)*w+sx);++count;
                for(int k=0;k<3;++k) {
                    const float value=Hdr::Linear((section==1 ? base.get()[src+k] : sdr[src+k])/255.f);
                    fallback[k]+=value;
                    master[k]+=section==0 ? original.rgba[src+k] : value;
                }
            }
            for(int k=0;k<3;++k) {master[k]/=count;fallback[k]/=count;}
        }
        for(int k=0;k<3;++k) {
            comparison.rgba[dest+k]=master[k];
            preview[dest+k]=static_cast<unsigned char>(std::lround(255.f*Hdr::Srgb(fallback[k])));
        }
    }
    Write(output/"Comparison-HDR.jpg",Hdr::EncodeGainMapJpeg(comparison,preview));
    Write(output/"Comparison-HDR.png",Hdr::EncodePqPng(comparison));
    std::ofstream notes(output/"Comparison.txt");
    notes<<"Left: supplied edited HDR master. Middle: new HDR JPEG with gain map and HDR metadata removed. Right: new SDR PNG rendition.\n"
        "All panels use the same source/edit and box-resampled geometry. The middle JPEG has not been recompressed; decoded base pixels are verified identical.\n"
        "Comparison-HDR.jpg and Comparison-HDR.png contain real HDR in the left panel. Use an HDR-capable viewer with HDR enabled.\n"
        "An SDR viewer of the comparison JPEG shows the new SDR fallback on the left, so all three panels should look nearly alike.\n"
        "HDR-gain-map.jpg, HDR-stripped.jpg, SDR.png and SDR.jpg are full-size inspection files. JPEG compression can cause small differences.\n";
}
}
