#pragma once
#include "Raw/Bracketing/Processor.h"
#include "Raw/Bracketing/InspectionImage.h"

namespace Stack::BurstEngine {
// Check only when scheduling work/deciding whether source extraction may be
// skipped. It is not a per-frame availability probe.
bool HasLiveSession(const std::shared_ptr<const Raw::Bracketing::BracketingAnalysis>&);
// Blocking process calls. Invoke on an operation worker, never while drawing.
// Results retain an opaque worker-session lease through their analysis. Closing
// the final lease kills the private worker without waiting on the caller thread.
Raw::Bracketing::BracketingResult Process(const Raw::Bracketing::ProcessingRequest&);
// Reblends the retained result overview. `source` is that result's original
// overview, not a native-detail crop. Invariant measurement samples are reused
// from the local source instead of crossing the process boundary on each edit.
Raw::Bracketing::Preview ReblendPreview(const Raw::Bracketing::Preview&,
    const Raw::Bracketing::BracketingRecipe&,const Raw::Bracketing::BracketingAnalysis&,
    std::string* error=nullptr,const std::function<bool()>& shouldCancel={});
Raw::Bracketing::Preview RenderNativeDetail(const Raw::Bracketing::ProcessingRequest&,
    unsigned x,unsigned y,unsigned size=192);
// Preserve the core's prepared-source requirements. A restored standard Bayer
// result needs a preparationOnly Process call and its live analysis lease before
// native detail or capture/group/result inspection. Importing saved pixels alone
// does not provide the prepared source comparison gains. The adapter transfers
// saved pixels without replacing them or inventing another development path.
Raw::Bracketing::Preview RenderResultDetail(const Raw::Bracketing::ProcessingRequest&,
    const Raw::Bracketing::BracketingResult&,unsigned x,unsigned y,unsigned size=512);
std::shared_ptr<const Raw::Bracketing::CapturePreview> RenderInspectionImage(
    const Raw::Bracketing::ProcessingRequest&,const Raw::Bracketing::BracketingResult&,
    Raw::Bracketing::InspectionImageKind,const std::string& frame,unsigned group,unsigned maximumDimension);
}
