#pragma once
#include "Processor.h"

namespace Raw::Bracketing {
enum class InspectionImageKind { Capture, Result, Group };
// One selected image at a time. Preparation thumbnails remain small; native
// inspection reads the immutable tiles without retaining every decoded capture.
std::shared_ptr<const CapturePreview> RenderInspectionImage(const ProcessingRequest&,
    const BracketingResult&,InspectionImageKind,const std::string& frame,unsigned group,
    unsigned maximumDimension);
}
