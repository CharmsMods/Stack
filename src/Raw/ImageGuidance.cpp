#include "Raw/ImageGuidance.h"
#include <array>
#include <cstdint>

namespace Stack::RawRecipe {
namespace {
bool Finite(const ImageGuidePixel& p) {
    return std::isfinite(p.a) && std::isfinite(p.b) && std::isfinite(p.sceneEv);
}
float Fade(float cost) {
    const float t = std::clamp((cost - .3f) / .7f, 0.0f, 1.0f);
    return 1 - t * t * (3 - 2 * t);
}
}

bool FollowImageEdges(const ImageGuide& guide, std::vector<float>& coverage,
    int seedX, int seedY, float radiusPixels, float sensitivity,
    const std::function<bool()>& cancelled) {
    if (!guide.Valid() || coverage.size() != guide.pixels.size()) return false;
    if (cancelled && cancelled()) return false;
    const int w = guide.width, h = guide.height;
    seedX = std::clamp(seedX, 0, w - 1); seedY = std::clamp(seedY, 0, h - 1);
    const std::size_t seed = std::size_t(seedY) * w + seedX;
    if (coverage[seed] <= 0 || !Finite(guide.pixels[seed])) {
        std::fill(coverage.begin(), coverage.end(), 0.0f);
        return true;
    }
    // Median of a small core resists isolated noise without sampling the outer
    // brush fringe. The reference stays anchored to the beginning of the stroke.
    std::array<std::vector<float>, 3> samples;
    const int core = std::clamp(int(std::floor(radiusPixels * .1f)), 0, 4);
    for (int y = std::max(0, seedY-core); y <= std::min(h-1, seedY+core); ++y)
        for (int x = std::max(0, seedX-core); x <= std::min(w-1, seedX+core); ++x) {
            const auto i = std::size_t(y)*w+x;
            const auto& p = guide.pixels[i];
            if (coverage[i] <= 0 || !Finite(p)) continue;
            samples[0].push_back(p.a); samples[1].push_back(p.b); samples[2].push_back(p.sceneEv);
        }
    ImageGuidePixel reference;
    float* channels[] = {&reference.a, &reference.b, &reference.sceneEv};
    for (int c=0; c<3; ++c) {
        auto& values = samples[c];
        std::nth_element(values.begin(), values.begin()+values.size()/2, values.end());
        *channels[c] = values[values.size()/2];
    }
    sensitivity = std::clamp(sensitivity, 0.0f, 1.0f);
    const float colorScale = .012f + (1-sensitivity)*.10f;
    const float evScale = .35f + (1-sensitivity)*2;
    // Broad appearance tolerance allows gradual illumination changes within a
    // material. Abrupt local changes are evaluated separately on each path.
    const auto affinity = [&](const ImageGuidePixel& p) {
        return ImageGuideEdgeDistance(p, reference, .10f+(1-sensitivity)*.35f, 6+(1-sensitivity)*12);
    };
    const auto coarse = [&](int x, int y) {
        ImageGuidePixel p;
        const int step = std::max(1, int(std::lround(std::min(w,h)*.001f)));
        float count = 0;
        for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
            const auto& q=guide.pixels[std::size_t(std::clamp(y+dy*step,0,h-1))*w+std::clamp(x+dx*step,0,w-1)];
            if (!Finite(q)) continue;
            p.a+=q.a; p.b+=q.b; p.sceneEv+=q.sceneEv; ++count;
        }
        if (count) {p.a/=count; p.b/=count; p.sceneEv/=count;}
        return p;
    };
    // Quantized minimax flood: crossing a strong edge is costly even when the
    // pixel on its far side matches the seed. Fine texture does not accumulate
    // distance penalties along a long stroke. Four neighbors prevent corner leaks.
    std::vector<std::uint8_t> cost(coverage.size(),255);
    std::array<std::vector<std::size_t>,255> buckets;
    cost[seed]=0; buckets[0].push_back(seed);
    std::size_t visited=0;
    for (int level=0; level<255; ++level) {
        auto& bucket=buckets[level];
        while (!bucket.empty()) {
            const auto index=bucket.back(); bucket.pop_back();
            if (cost[index]!=level) continue;
            if ((++visited & 1023u)==0 && cancelled && cancelled()) return false;
            const int x=int(index%w), y=int(index/w);
            const auto& source=guide.pixels[index];
            const auto sourceCoarse=coarse(x,y);
            for (const auto delta : {std::pair<int,int>{-1,0},{1,0},{0,-1},{0,1}}) {
                const int xx=x+delta.first, yy=y+delta.second;
                if (xx<0 || yy<0 || xx>=w || yy>=h) continue;
                const auto next=std::size_t(yy)*w+xx;
                if (coverage[next]<=0 || cost[next]<=level || !Finite(guide.pixels[next])) continue;
                const auto& target=guide.pixels[next];
                // Native fine edges remain barriers even when averaging hides
                // a thin gap. Coarse evidence suppresses internal texture noise.
                const float edge=std::max(
                    .5f*ImageGuideEdgeDistance(source,target,colorScale,evScale),
                    3.0f*ImageGuideEdgeDistance(sourceCoarse,coarse(xx,yy),colorScale,evScale));
                const float penalty=std::max(edge,affinity(target));
                if (penalty>=1 || !std::isfinite(penalty)) continue;
                const int nextCost=std::max(level,int(std::lround(penalty*254)));
                if (nextCost>=cost[next]) continue;
                cost[next]=static_cast<std::uint8_t>(nextCost);
                buckets[nextCost].push_back(next);
            }
        }
    }
    for (std::size_t i=0; i<coverage.size(); ++i) {
        if ((i & 4095u)==0 && cancelled && cancelled()) return false;
        coverage[i] *= cost[i]==255 ? 0.0f : Fade(float(cost[i])/254);
    }
    return true;
}
} // namespace Stack::RawRecipe
