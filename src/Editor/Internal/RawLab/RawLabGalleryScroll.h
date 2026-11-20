#pragma once
#include <algorithm>
#include <cmath>
#include "Animation/FilmstripMotion.h"

namespace Stack::Editor::RawLabInternal {
struct GalleryScrollPosition {
    int frame = -2;
    float offset = 0.f;

    bool HasPosition() const { return frame >= 0; }
    bool NeedsRestore(int currentFrame) const { return frame < currentFrame - 1; }

    void Remember(int currentFrame, float displayedOffset) {
        frame = currentFrame;
        offset = displayedOffset;
    }
};

struct GalleryScrollMotion {
    Stack::Animation::FilmstripMotion glide;
    std::vector<float> uniformStops;
    float uniformMaximum=-1.f,uniformStride=-1.f;
    void PrepareUniform(float maximum,float stride) {
        maximum=std::max(0.f,maximum);stride=std::max(1.f,stride);
        if(maximum==uniformMaximum && stride==uniformStride)return;
        uniformMaximum=maximum;uniformStride=stride;uniformStops.clear();
        const auto count=static_cast<std::size_t>(std::floor(maximum/stride));
        uniformStops.reserve(count+2);
        for(std::size_t i=0;i<=count;++i)uniformStops.push_back(float(i)*stride);
        if(uniformStops.back()<maximum)uniformStops.push_back(maximum);
    }
    float current = -1.f, lastApplied = -1.f, overshoot = 0.f, rebound = 0.f;
    int frame = -2;
    void Observe(float native, float& target, int nextFrame) {
        if(frame==nextFrame)return;
        if (frame != nextFrame-1 || current < 0.f || std::abs(native-lastApplied)>1.1f) {
            current = target = native; overshoot = rebound = 0.f;
            glide.Reset(native);
        }
        frame = nextFrame;
    }
};
}
