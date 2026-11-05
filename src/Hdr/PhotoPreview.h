#pragma once
#include "Raw/RawGpuImageLease.h"
#include <string>
struct RenderGraphSnapshot;
class RenderPipeline;
namespace Stack::Hdr {
// Empty without an error means this output has no HDR rendition (for example,
// a numeric channel). A failed supported rendition must not be published as SDR.
Raw::RawGpuImageLease RenderCompanion(RenderPipeline& pipeline,const RenderGraphSnapshot& graph,
    std::string& error);
}
