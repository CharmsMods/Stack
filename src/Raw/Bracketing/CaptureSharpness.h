#pragma once
#include "ProcessingInternal.h"

namespace Raw::Bracketing {
// Bounded native patch evidence. A lack of measurable structure leaves equal
// detail preferences; noise by itself cannot nominate a sharper capture.
bool MeasureCaptureSharpness(const ProcessingRequest&,PreparedDataset&,
    std::vector<std::string>&,std::string&);
}
