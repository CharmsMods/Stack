#pragma once
#include "Raw/Bracketing/Processor.h"
#include "ThirdParty/stb_image_write.h"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace Stack::Validation {
inline bool WriteBracketRgbComparisons(const Raw::Bracketing::BracketingResult& result,
    const std::filesystem::path& output,nlohmann::json& report,
    const std::vector<std::array<unsigned,2>>& regions) {
    const auto& raw=*result.raw;const auto& m=raw.metadata;
    const unsigned width=m.visibleWidth,height=m.visibleHeight;
    const unsigned scale=result.reconstructionInspection?result.reconstructionInspection->scale:1;
    const auto& rgb=raw.linearFloatBuffer;
    if(rgb.size()!=std::size_t(width)*height*3)return false;
    std::ofstream binary(output/"merged-camera-rgb.f32",std::ios::binary);
    binary.write(reinterpret_cast<const char*>(rgb.data()),rgb.size()*sizeof(float));
    if(!binary)return false;
    report["nativeComparison"]={{"exposureEv",2},{"display","camera color, Reinhard, sRGB"},
        {"width",width},{"height",height},{"orientation",m.orientation},{"scale",scale},
        {"cameraWhiteBalance",m.cameraWhiteBalance},{"cameraToSrgb",m.cameraToSrgb}};
    auto centers=regions;if(centers.empty())centers.push_back({width/(2*scale),height/(2*scale)});
    const unsigned size=std::min({640u,width,height});
    for(std::size_t r=0;r<centers.size();++r) {
        const unsigned cx=centers[r][0]*scale,cy=centers[r][1]*scale;
        const unsigned left=std::min(cx>size/2?cx-size/2:0u,width-size);
        const unsigned top=std::min(cy>size/2?cy-size/2:0u,height-size);
        std::vector<unsigned char> bytes(std::size_t(size)*size*3);
        for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x)for(unsigned c=0;c<3;++c) {
            double v=0;for(unsigned k=0;k<3;++k)
                v+=m.cameraToSrgb[c*3+k]*rgb[(std::size_t(top+y)*width+left+x)*3+k]*
                    m.cameraWhiteBalance[k]/std::max(1e-6f,m.cameraWhiteBalance[1]);
            v=std::max(0.,v*4);v/=1+v;v=v<=.0031308?12.92*v:1.055*std::pow(v,1/2.4)-.055;
            bytes[(std::size_t(y)*size+x)*3+c]=static_cast<unsigned char>(std::clamp(std::lround(v*255),0l,255l));
        }
        const auto path=output/("native-rgb-"+std::to_string(r)+".png");
        if(!stbi_write_png(path.string().c_str(),size,size,3,bytes.data(),size*3))return false;
        report["nativeComparison"]["regions"].push_back({{"left",left},{"top",top},{"size",size},
            {"comparison",path.filename().string()}});
    }
    return true;
}
}
