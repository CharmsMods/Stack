#pragma once

#include "NodeMath/ContractTypes.h"
#include "Raw/RawDevelopmentRecipe.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace Stack::Editor::RawLabInternal {

// Viewport and authored coverage use top-left normalized coordinates. Graph
// spatial descriptors use bottom-left coordinates and map to the shared source.
struct RawLabImageMapping {
    using Matrix = std::array<double,9>;
    Matrix canvasToLocal{1,0,0,0,1,0,0,0,1};
    Matrix localToCanvas = canvasToLocal;
    float sourceAspect = 1.f;

    static ImVec2 Transform(const Matrix& m, ImVec2 p) {
        return {float(m[0]*p.x+m[1]*p.y+m[2]),float(m[3]*p.x+m[4]*p.y+m[5])};
    }
    static Matrix Multiply(const Matrix& a, const Matrix& b) {
        Matrix result{};
        for (int r=0;r<3;++r) for (int c=0;c<3;++c)
            for (int k=0;k<3;++k) result[r*3+c] += a[r*3+k]*b[k*3+c];
        return result;
    }
    static std::optional<Matrix> Inverse(const Matrix& m) {
        for (double v : m) if (!std::isfinite(v)) return std::nullopt;
        const double d = m[0]*m[4]-m[1]*m[3];
        if (std::abs(d)<1e-12 || std::abs(m[6])+std::abs(m[7])+std::abs(m[8]-1)>1e-12) return std::nullopt;
        return Matrix{m[4]/d,-m[1]/d,(m[1]*m[5]-m[4]*m[2])/d,
            -m[3]/d,m[0]/d,(m[3]*m[2]-m[0]*m[5])/d,0,0,1};
    }
    ImVec2 Local(ImVec2 canvas) const { return Transform(canvasToLocal,canvas); }
    ImVec2 Canvas(ImVec2 local) const { return Transform(localToCanvas,local); }

    static std::optional<RawLabImageMapping> FromSpatial(
        const NodeMath::SpatialDescriptor& local, const NodeMath::SpatialDescriptor& displayed,
        const RawRecipe::RawCropRotationRecipe& crop) {
        const auto sourceToLocal = Inverse(local.sourceTransform);
        if (!sourceToLocal || !Inverse(displayed.sourceTransform)) return std::nullopt;
        const double width = local.fullWindow.width, height = local.fullWindow.height;
        if (width<=0 || height<=0 || local.pixelAspect<=0) return std::nullopt;
        const Matrix flip{1,0,0,0,-1,1,0,0,1};
        const Matrix window{crop.cropEnabled ? crop.cropWidth : 1,0,crop.cropEnabled ? crop.cropX : 0,
            0,crop.cropEnabled ? crop.cropHeight : 1,crop.cropEnabled ? crop.cropY : 0,0,0,1};
        RawLabImageMapping result;
        result.canvasToLocal = Multiply(flip,Multiply(*sourceToLocal,Multiply(displayed.sourceTransform,Multiply(flip,window))));
        const auto inverse = Inverse(result.canvasToLocal);
        if (!inverse) return std::nullopt;
        result.localToCanvas = *inverse;
        result.sourceAspect = float(width*local.pixelAspect/height);
        return result;
    }
};

} // namespace Stack::Editor::RawLabInternal
