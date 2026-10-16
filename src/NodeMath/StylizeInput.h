#pragma once
#include "NodeMath/ImageOperationInput.h"
#include "ThirdParty/json.hpp"

namespace Stack::NodeMath {
// Parameters choose algorithms explicitly. Rewiring never substitutes another
// authored mode just because the new input lacks its required components.
inline bool ValidateStylizeInput(const std::string& type,const nlohmann::json& parameters,
    const ImageOperationInput& input,std::string& reason) {
    if (type=="PaletteReconstructor" && !input.fullRgb) {
        reason="Palette Rebuild needs all three real RGB components.";return false;
    }
    if (type=="CellShading" && parameters.value("quantMode",0)!=1 && !input.colorRelationships) {
        reason=input.colorUnavailableReason;return false;
    }
    if (type=="Halftoning") {
        const int mode=parameters.value("colorMode",0);
        if (mode==0 && !input.scalar && !input.colorRelationships) {
            reason=input.colorUnavailableReason;return false;
        }
        if ((mode==2 || mode==3 || parameters.value("gray",false)) && !input.fullRgb) {
            reason="CMY, CMYK and grayscale color processing need all three real RGB components.";return false;
        }
    }
    return true;
}
}
