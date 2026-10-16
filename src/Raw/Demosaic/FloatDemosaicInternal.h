#pragma once
#include "FloatDemosaic.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Raw::Demosaic::Internal {
struct Canceled {};
inline void CheckCancel(const std::function<bool()>& cancel, int row = 0) {
    if ((row & 15) == 0 && cancel && cancel()) throw Canceled{};
}
inline int Mirror(int p, int size) {
    if (size <= 1) return 0;
    const int period = 2 * (size - 1);
    p %= period;
    if (p < 0) p += period;
    return p < size ? p : period - p;
}
inline int Color(CfaPattern pattern, int x, int y) {
    const int p = ((y & 1) << 1) | (x & 1);
    switch (pattern) {
        case CfaPattern::RGGB: return std::array<int,4>{0,1,1,2}[p];
        case CfaPattern::BGGR: return std::array<int,4>{2,1,1,0}[p];
        case CfaPattern::GBRG: return std::array<int,4>{1,2,0,1}[p];
        case CfaPattern::GRBG: return std::array<int,4>{1,0,2,1}[p];
        default: return 1;
    }
}
struct Input {
    const std::vector<float>& samples;
    int width, height;
    CfaPattern pattern;
    const std::function<bool()>& cancel;
    std::array<float,9> cameraToSrgb{1,0,0,0,1,0,0,0,1};
    std::size_t Index(int x, int y) const {
        return static_cast<std::size_t>(Mirror(y,height))*width + Mirror(x,width);
    }
    float At(int x, int y) const { return samples[Index(x,y)]; }
    int Cfa(int x, int y) const { return Color(pattern,x,y); }
    void Check(int row = 0) const { CheckCancel(cancel,row); }
};
void Directional(const Input& input, DemosaicMethod method, std::vector<float>& rgb);
void Lmmse(const Input& input, std::vector<float>& rgb);
void Dcb(const Input& input, bool enhanced, std::vector<float>& rgb);
void RefineChroma(const Input& input, std::vector<float>& rgb, int iterations);
void Ppg(const Input& input,std::vector<float>& rgb);
void Vng(const Input& input,bool fourColors,std::vector<float>& rgb);
void Ahd(const Input& input,std::vector<float>& rgb);
} // namespace Raw::Demosaic::Internal
