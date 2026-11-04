#pragma once
#include "Raw/RawGpuImageLease.h"
struct RenderGraphSnapshot;
class RenderPipeline;
namespace Stack::Hdr {
Raw::RawGpuImageLease RenderCompanion(RenderPipeline& pipeline,const RenderGraphSnapshot& graph);
}
