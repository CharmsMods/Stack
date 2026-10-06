#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Raw::Bracketing {
enum class EvidenceKind { Image, Confidence, Rejection, Guide, Contribution, Support };
struct EvidenceRaster {
    std::string label,units;
    EvidenceKind kind=EvidenceKind::Image;
    unsigned width=0,height=0;
    double rawWidth=0,rawHeight=0;
    float minimum=0,maximum=1;
    // Alpha is validity, not confidence. Zero-valued measurements remain visible.
    std::vector<std::uint8_t> rgba;
};
struct EvidenceVector {
    float x=0,y=0,dx=0,dy=0,confidence=0;
    bool rejected=false;
};
struct EvidencePoint {float x=0,y=0;};
struct EvidenceMetric {std::string label,units;double value=0;};
struct EvidenceKernel {float x=0,y=0,xx=0,xy=0,yy=0;};
struct ProcessingEvidence {
    std::vector<EvidenceRaster> rasters;
    std::vector<EvidenceVector> vectors;
    std::vector<EvidencePoint> curve,observations;
    std::vector<EvidenceMetric> metrics;
    std::vector<EvidenceKernel> kernels;
    std::string equation,caption,curveLabel,xUnits,yUnits;
    // Maps reference UV to source UV, including translation about the sensor center.
    std::array<float,6> affine{1,0,0,0,1,0};
    bool hasAffine=false,provisional=false;
    float focusX=.5f,focusY=.5f;
    std::size_t Bytes() const {
        std::size_t bytes=sizeof(*this)+vectors.size()*sizeof(EvidenceVector)+
            (curve.size()+observations.size())*sizeof(EvidencePoint)+kernels.size()*sizeof(EvidenceKernel);
        for(const auto& raster:rasters)bytes+=raster.rgba.size();
        return bytes;
    }
};
}
