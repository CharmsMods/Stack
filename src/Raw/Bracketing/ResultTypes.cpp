#include "Processor.h"
#include <system_error>

namespace Raw::Bracketing {
BracketingResult::ReconstructionInspection::~ReconstructionInspection() {
    if(!directory.empty()){std::error_code e;std::filesystem::remove_all(directory,e);}
}
}
