#pragma once
#include "Protocol.h"
#include "Raw/Bracketing/Processor.h"

namespace Stack::BurstEngine::Wire {
Json Request(const Raw::Bracketing::ProcessingRequest&);
Raw::Bracketing::ProcessingRequest Request(const Json&);
Json Preview(const Raw::Bracketing::Preview&);
Raw::Bracketing::Preview Preview(const Json&);
Json Capture(const Raw::Bracketing::CapturePreview&);
std::shared_ptr<const Raw::Bracketing::CapturePreview> Capture(const Json&);
Json Analysis(const Raw::Bracketing::BracketingAnalysis&);
std::shared_ptr<Raw::Bracketing::BracketingAnalysis> Analysis(const Json&);
Json Progress(const Raw::Bracketing::ProcessingProgress&);
Raw::Bracketing::ProcessingProgress Progress(const Json&);
Json ResultSummary(const Raw::Bracketing::BracketingResult&);
void ApplyResultSummary(const Json&,Raw::Bracketing::BracketingResult&);
}
