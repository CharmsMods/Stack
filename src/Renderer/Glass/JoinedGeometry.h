#pragma once
#include <array>
#include <cmath>

namespace Stack::Renderer::Glass {
// The graph editor admits at most 64 authored points. The compact smooth-union
// field is shared with the research material; each point retains its own strain.
struct JoinedGeometry {
    static constexpr int Capacity=64;
    int count=0;
    std::array<std::array<float,4>,Capacity> shapes{}; // local center, half extents
    std::array<std::array<float,4>,Capacity> metrics{}; // radius, inverse xx/xy/yy
    bool Valid() const {
        if(count<2 || count>Capacity)return false;
        for(int i=0;i<count;++i) {
            const auto& s=shapes[i];const auto& m=metrics[i];
            for(float x:s)if(!std::isfinite(x))return false;
            for(float x:m)if(!std::isfinite(x))return false;
            if(std::abs(s[0])>1.e6f || std::abs(s[1])>1.e6f || s[2]<.01f || s[3]<.01f ||
                s[2]>1.e5f || s[3]>1.e5f || m[0]<0 || m[0]>s[2] || m[0]>s[3] ||
                m[1]<=0 || m[3]<=0 || m[1]+m[3]>2.091f ||
                std::abs(m[1]*m[3]-m[2]*m[2]-1.f)>.001f)return false;
        }
        return true;
    }
};
}
