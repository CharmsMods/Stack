#pragma once
#include "Processor.h"

namespace Raw::Bracketing::DetailInternal {
// The even-origin halo preserves the active Bayer phase. Keep the existing
// native-view framing while sharing it with authoritative saved-result reads.
// Nonzero requests return an even ROI (at least two pixels when the source
// permits); zero remains empty. Sources smaller than the halo use their extent.
struct Region {
    unsigned left=0,top=0,width=0,height=0,borderX=0,borderY=0;
    unsigned OutputWidth() const {return width-2*borderX;}
    unsigned OutputHeight() const {return height-2*borderY;}
    unsigned OutputLeft() const {return left+borderX;}
    unsigned OutputTop() const {return top+borderY;}
};
Region ChooseRegion(unsigned width,unsigned height,unsigned x,unsigned y,unsigned size);
bool ReadOriginals(const ProcessingRequest&,const PreparedDataset&,const Region&,
    std::map<std::string,std::shared_ptr<const CapturePreview>>&,std::string& error);
}
