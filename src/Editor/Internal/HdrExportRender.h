#pragma once
#include "Editor/EditorRenderWorker.h"

class RenderPipeline;
namespace Stack::EditorRendering {
// Called only by the native-quality worker after loading the snapshot's source.
// The graph is copied for this export so display targets stay out of authoring.
bool ReadHdrExport(RenderPipeline& pipeline, const RenderGraphSnapshot& graph,
    Hdr::FileFormat format, EditorRenderWorker::Result& result);
}
