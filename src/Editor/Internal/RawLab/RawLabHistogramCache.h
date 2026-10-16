#pragma once

#include "Editor/Internal/RawLab/RawLabCurveEditor.h"
#include <cstdint>
#include <tuple>

namespace Stack::Editor::RawLabInternal {

// One small display cache per editor. Accepted readback revisions change when
// pixels or measurement metadata are published, including area refinements.
// No image samples or authored values are retained here.
class RawLabHistogramCache {
public:
    const RawLabGraphHistogram& Zones(const RawDevelopmentGraphScopeReadback& scope,
        std::uint64_t revision, float minimumEv, float maximumEv, float middleGrey) {
        const Key next{revision, RawDevelopmentGraphScopeStage::LocalRangeInput,
            Raw::RawWorkingSpace{}, -1, minimumEv, maximumEv, middleGrey};
        return Get(next, [&] { return BuildRawLabZonesHistogram(scope, minimumEv, maximumEv, middleGrey); });
    }

    const RawLabGraphHistogram& FinishTone(const RawDevelopmentGraphScopeReadback& scope,
        std::uint64_t revision, RawDevelopmentGraphScopeStage stage,
        Raw::RawWorkingSpace workingSpace, int domain,
        float minimumEv, float maximumEv, float middleGrey) {
        const Key next{revision, stage, workingSpace, domain, minimumEv, maximumEv, middleGrey};
        return Get(next, [&] { return BuildRawLabFinishToneHistogram(scope, stage, workingSpace,
            domain, minimumEv, maximumEv, middleGrey); });
    }
private:
    using Key = std::tuple<std::uint64_t, RawDevelopmentGraphScopeStage,
        Raw::RawWorkingSpace, int, float, float, float>;
    Key key{};
    RawLabGraphHistogram histogram;

    template<class Build>
    const RawLabGraphHistogram& Get(const Key& next, Build build) {
        // Zero is an unpublished/cleared readback. It must never reuse a
        // previous result solely because the plotted domain is unchanged.
        if (std::get<0>(next) == 0 || next != key) {
            histogram = build();
            key = next;
        }
        return histogram;
    }
};

} // namespace Stack::Editor::RawLabInternal
