// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Engine/style.hpp"

namespace Stack::Renderer::Glass {
inline std::optional<stack::glass::gl::MaterialParameters> ClearControlMaterial(float halfHeightPx) {
    auto material=stack::glass::engine::clearWater(halfHeightPx);
    if(material) {
        // Stack's selected controls transmit the body without a broad gray
        // reflection veil. The directional bevel/meniscus still catches light.
        // This is fixed art direction, never a source-brightness switch.
        material->bodyReflection=0.f;
        // Shared optical art direction: stronger displacement, extending through
        // 90% of the radius while retaining a smooth, undistorted center join.
        material->edge.reachPx*=1.30f;
        material->edge.widthPx=.90f*halfHeightPx;
    }
    return material;
}
}
