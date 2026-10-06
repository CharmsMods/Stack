#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include "ProcessingEvidence.h"

namespace Raw::Bracketing {
enum class ProcessingStage { Assets, Preparing, Noise, Exposure, GlobalAlignment, LocalAlignment,
    Groups, Guide, Blend, Reconstruction, RawPreview, Completed, Canceled, Failed,
    PanoramaMatching, PanoramaLayout, PanoramaSeams, PanoramaComposite };
struct ProcessingThumbnail {
    std::string frameId;
    unsigned width=0,height=0,group=0;
    std::vector<std::uint8_t> rgba;
};
// Optional observational data. Never part of recipe/result/cache identities.
struct ProcessingProgress {
    ProcessingStage stage=ProcessingStage::Assets;
    std::uint64_t generation=0,completed=0,total=0;
    unsigned pass=0,width=0,height=0;
    std::string captureId,groupId,detail;
    double fraction=-1;
    // Estimated variance at evenly spaced normalized signal levels [0,1].
    // Empty unless supplied by the noise-model stage, never invented by UI.
    std::array<double,16> noiseVariance{};
    bool hasNoiseEstimate=false;
    std::shared_ptr<const ProcessingThumbnail> thumbnail;
    std::shared_ptr<const ProcessingEvidence> evidence;
    std::uint64_t sequence=0,occurrence=0;
    bool evidenceOnly=false;
};
inline const char* ProcessingStageTitle(ProcessingStage stage) {
    switch(stage) {
    case ProcessingStage::PanoramaMatching:return "Matching panorama captures";
    case ProcessingStage::PanoramaLayout:return "Solving panorama layout";
    case ProcessingStage::PanoramaSeams:return "Choosing panorama seams";
    case ProcessingStage::PanoramaComposite:return "Compositing panorama";
    case ProcessingStage::Assets:return "Preparing source files";
    case ProcessingStage::Preparing:return "Preparing captures";
    case ProcessingStage::Noise:return "Measuring burst noise";
    case ProcessingStage::Exposure:return "Calibrating exposure";
    case ProcessingStage::GlobalAlignment:return "Aligning captures";
    case ProcessingStage::LocalAlignment:return "Refining local alignment";
    case ProcessingStage::Groups:return "Combining exposure groups";
    case ProcessingStage::Guide:return "Building the luminance guide";
    case ProcessingStage::Blend:return "Blending the bracket";
    case ProcessingStage::Reconstruction:return "Reconstructing image detail";
    case ProcessingStage::RawPreview:return "Preparing RAW view";
    case ProcessingStage::Completed:return "Ready";
    case ProcessingStage::Canceled:return "Canceled";
    case ProcessingStage::Failed:return "Processing stopped";
    }return "Processing";
}
}
