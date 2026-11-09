#pragma once
#include "ThirdParty/json.hpp"
#include <cmath>
#include <string>

namespace Stack::RawWorkspace {
// Browsing/sequence evidence only. Processing still verifies original contents
// and calibrates exposure from pixels; these tags never become processing gain.
struct GalleryCaptureMetadata {
    bool checked = false;
    std::string make, model, lens;
    int width = 0, height = 0;
    double shutter = 0, aperture = 0, iso = 0, focalLength = 0;
    bool CameraKnown() const { return checked && !model.empty() && width > 0 && height > 0; }
    bool ExposureKnown() const {
        return std::isfinite(shutter) && shutter > 0 && std::isfinite(aperture) && aperture > 0 &&
            std::isfinite(iso) && iso > 0;
    }
    double ExposureStops() const { return std::log2(shutter)+std::log2(iso)-2*std::log2(aperture); }
};
inline void to_json(nlohmann::json& j, const GalleryCaptureMetadata& m) {
    j={{"checked",m.checked},{"make",m.make},{"model",m.model},{"lens",m.lens},
        {"width",m.width},{"height",m.height},{"shutter",m.shutter},{"aperture",m.aperture},
        {"iso",m.iso},{"focalLength",m.focalLength}};
}
inline void from_json(const nlohmann::json& j, GalleryCaptureMetadata& m) {
    m={}; if(!j.is_object()) return;
    try {
        m.checked=j.value("checked",false);m.make=j.value("make",std::string{});
        m.model=j.value("model",std::string{});m.lens=j.value("lens",std::string{});
        m.width=j.value("width",0);m.height=j.value("height",0);
        m.shutter=j.value("shutter",0.);m.aperture=j.value("aperture",0.);
        m.iso=j.value("iso",0.);m.focalLength=j.value("focalLength",0.);
    } catch(...) {m={};}
}
}
