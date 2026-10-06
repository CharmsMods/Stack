#pragma once
#include "Raw/RawZoneArea.h"

namespace Stack::RawRecipe {
// Worker-owned cache. Reuses completed strokes while the last stroke grows.
// Recipe edits, transforms and reference changes invalidate the affected prefix.
class ZoneAreaRasterizer {
public:
    const std::vector<float>* Evaluate(const RawZoneArea& area, int width, int height,
        const RawCropRotationRecipe& transform, const ImageGuide* guide,
        const std::function<bool()>& cancelled = {});
    std::size_t StrokeEvaluations() const { return m_StrokeEvaluations; }
    std::size_t Bytes() const;
    void Clear();
    std::size_t GeometrySegments() const { return m_GeometrySegments; }
private:
    struct State {
        std::vector<float> mask, lower, upper;
        bool assisted = false;
    };
    bool Apply(State& state, const RawZoneArea& area, const RawZoneBrushStroke& stroke,
        int width, int height, const RawCropRotationRecipe& transform,
        const ImageGuide* guide, const std::function<bool()>& cancelled, bool retainFootprint = false);
    State m_Prefix, m_Result;
    RawZoneBrushStroke m_FootprintStroke;
    std::vector<float> m_Footprint;
    std::size_t m_GeometrySegments = 0;
    std::vector<std::size_t> m_Strokes;
    std::size_t m_Domain = 0, m_StrokeEvaluations = 0;
    std::string m_AreaId;
};
}
