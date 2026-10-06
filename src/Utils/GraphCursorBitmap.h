#pragma once
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace ImGuiExtras {
struct GraphCursorBitmap {
    int size=0,hotspot=0;
    std::vector<unsigned char> pixels;
};

// Straight-alpha RGBA for a native cursor. The solid dot has a one-pixel AA
// boundary; hover feedback is a separate, restrained accent around it.
inline GraphCursorBitmap BuildGraphCursorBitmap(float scale,ImVec4 dot,ImVec4 accent,float hover) {
    scale=std::clamp(scale,0.5f,4.0f);
    const int half=static_cast<int>(std::ceil(7*scale));
    GraphCursorBitmap bitmap{half*2+1,half,{}};
    bitmap.pixels.resize(bitmap.size*bitmap.size*4);
    for (int y=0;y<bitmap.size;++y) for (int x=0;x<bitmap.size;++x) {
        const float dx=float(x-half),dy=float(y-half),distance=std::sqrt(dx*dx+dy*dy);
        const float body=std::clamp(4*scale+0.5f-distance,0.0f,1.0f)*dot.w;
        const float halo=std::clamp(6*scale+0.5f-distance,0.0f,1.0f)*0.12f*hover*accent.w;
        const float alpha=body+halo*(1-body);
        const auto index=(y*bitmap.size+x)*4;
        const float foreground[3]{dot.x,dot.y,dot.z},background[3]{accent.x,accent.y,accent.z};
        for (int c=0;c<3;++c) {
            const float channel=alpha>0 ? (foreground[c]*body+background[c]*halo*(1-body))/alpha : 0;
            bitmap.pixels[index+c]=static_cast<unsigned char>(std::clamp(channel,0.0f,1.0f)*255+0.5f);
        }
        bitmap.pixels[index+3]=static_cast<unsigned char>(std::clamp(alpha,0.0f,1.0f)*255+0.5f);
    }
    return bitmap;
}
}
