#pragma once

#include "Raw/RawAutoStartPoint.h"
#include "Raw/RawImageAnalysis.h"

class RenderPipeline;
struct RenderGraphSnapshot;
struct RenderTextureStats;

namespace Stack::Renderer {

RawAnalysis::CurrentFrameInputStats BuildCurrentFrameInputStats(const RenderTextureStats& stats);

// Render explicit candidate recipes on the caller's GPU pipeline. No editor,
// active tab, worker thread or validation environment is required.
std::vector<RawAutoStartPoint::RawAutoStartPointCandidateRenderResult> RenderRawCandidates(
    RenderPipeline& pipeline,
    const RenderGraphSnapshot& graph,
    const std::string& sourceKey,
    const std::vector<RawAutoStartPoint::RawAutoStartPointCandidateRenderRequest>& requests);

} // namespace Stack::Renderer
