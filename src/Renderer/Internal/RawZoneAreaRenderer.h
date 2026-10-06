#pragma once
#include "Raw/RawZoneArea.h"
#include "Raw/RawZoneAreaRasterizer.h"
#include <unordered_map>

namespace Stack::Renderer {
class RawZoneAreaRenderer {
public:
    ~RawZoneAreaRenderer();
    void Clear();
    // Runs on the owning render worker, including full-resolution statistics.
    unsigned int Render(unsigned int input, unsigned int reference, int width, int height,
        const RawRecipe::RawDevelopmentRecipe& recipe, bool measure, bool fullResolution,
        std::vector<RawRecipe::RawZoneAreaStatistics>& statistics,
        const std::function<bool()>& cancelled = {}, std::size_t referenceRevision = 0,
        const std::function<unsigned int(const std::string&)>& coverage = {});
    // Borrowed mask texture, owned by this worker's mask cache.
    unsigned int Coverage(const RawRecipe::RawZoneArea& area, unsigned int reference, int width, int height,
        Raw::RawWorkingSpace space, std::size_t referenceRevision, const std::function<bool()>& cancelled);
    std::size_t MaskBuildCount() const { return m_MaskBuildCount; }
    std::size_t GuideBuildCount() const { return m_GuideBuildCount; }
    std::size_t StrokeEvaluationCount() const { return m_Rasterizer.StrokeEvaluations(); }
    std::shared_ptr<const RawRecipe::ImageGuide> PreviewGuide() const { return m_PreviewGuide; }
private:
    struct Mask {
        unsigned int texture = 0; std::size_t bytes = 0; std::uint64_t used = 0;
        std::shared_ptr<const RawRecipe::RawZoneAreaMaskPreview> preview;
    };
    unsigned int MaskTexture(const RawRecipe::RawZoneArea& area, int width, int height,
        const RawRecipe::RawCropRotationRecipe& transform, const std::function<bool()>& cancelled,
        const RawRecipe::ImageGuide* guide, std::shared_ptr<const RawRecipe::RawZoneAreaMaskPreview>& preview);
    bool PrepareGuide(unsigned int reference, int width, int height,
        const RawRecipe::RawDevelopmentRecipe& recipe, std::size_t referenceRevision, bool native,
        const std::function<bool()>& cancelled);
    std::shared_ptr<const RawRecipe::ImageGuide> m_Guide, m_PreviewGuide;
    std::size_t m_GuideKey = 0, m_PreviewGuideKey = 0, m_GuideBuildCount = 0;
    unsigned int m_GainProgram = 0;
    unsigned int m_ApplyProgram = 0;
    std::unordered_map<std::size_t, Mask> m_Masks;
    std::size_t m_MaskBytes = 0;
    std::size_t m_MaskBuildCount = 0;
    std::uint64_t m_Use = 0;
    RawRecipe::ZoneAreaRasterizer m_Rasterizer;
};
}
