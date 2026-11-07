#include "Processor.h"
#include <system_error>

namespace Raw::Bracketing {
BracketingResult::ReconstructionInspection::~ReconstructionInspection() {
    if(storageOwner)return; // The session retires its whole tree away from drawing.
    if(!directory.empty()){std::error_code e;std::filesystem::remove_all(directory,e);}
}
}
