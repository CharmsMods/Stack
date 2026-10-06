#pragma once
#include "Persistence/RawProjectModel.h"

namespace Raw::Bracketing::Panorama {
inline bool IsPanorama(const Stack::Project::MultiFrameSourceSet& set) {
    const auto it=set.settings.find("bracketing");
    return it!=set.settings.end()&&it->is_object()&&it->value("reconstruction",std::string())=="panorama";
}
inline bool Compatible(const Stack::Project::RawCaptureCompatibilitySummary& a,
    const Stack::Project::RawCaptureCompatibilitySummary& b,std::string* reason) {
    const auto fail=[&](const char* message){if(reason)*reason=message;return false;};
    if(!a.supported||!b.supported||a.inputDomain!=Stack::Project::MfdInputDomain::MosaicCfa||
        b.inputDomain!=Stack::Project::MfdInputDomain::MosaicCfa)
        return fail("Panorama requires supported RAW captures.");
    if(a.cameraMake!=b.cameraMake||a.cameraModel!=b.cameraModel||
        (!a.uniqueCameraModel.empty()&&!b.uniqueCameraModel.empty()&&a.uniqueCameraModel!=b.uniqueCameraModel))
        return fail("Panorama requires captures from the same camera.");
    if(!a.lensModel.empty()&&!b.lensModel.empty()&&a.lensModel!=b.lensModel)
        return fail("Panorama requires captures from the same rectilinear lens.");
    if(reason)reason->clear();return true;
}
}
