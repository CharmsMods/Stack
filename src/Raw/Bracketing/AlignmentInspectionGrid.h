#pragma once
#include "Processor.h"
#include <cmath>
#include <cstdint>

namespace Raw::Bracketing {
// Inspection cells own fixed Bayer-cell blocks, including a shorter final
// block. Extent-normalized scaling would move their interior boundaries.
// This observational contract is shared by wire validation and both UI probes;
// it does not call or link the private alignment/reliability implementation.
inline bool HasAlignmentInspectionGrid(const AlignmentInspection& inspection) {
    const auto stride=inspection.cellStrideBayerCells;
    const std::uint64_t width=inspection.rawWidth/2,height=inspection.rawHeight/2;
    return stride>0&&stride<=16&&width>0&&height>0&&
        inspection.width==(width+stride-1)/stride&&
        inspection.height==(height+stride-1)/stride;
}
inline bool FindAlignmentInspectionCell(const AlignmentInspection& inspection,
    double rawX,double rawY,unsigned& cellX,unsigned& cellY) {
    cellX=cellY=0;
    if(!HasAlignmentInspectionGrid(inspection)||!std::isfinite(rawX)||!std::isfinite(rawY)||
       rawX<0||rawY<0||rawX>=double(inspection.rawWidth/2)*2||
       rawY>=double(inspection.rawHeight/2)*2)return false;
    const double stepRawPixels=2.0*inspection.cellStrideBayerCells;
    cellX=static_cast<unsigned>(rawX/stepRawPixels);
    cellY=static_cast<unsigned>(rawY/stepRawPixels);
    return true;
}
}
