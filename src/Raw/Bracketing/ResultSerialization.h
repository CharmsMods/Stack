#pragma once
#include "Processor.h"
#include <istream>

namespace Raw::Bracketing::Serialization {
// Reusable measurement format shared by project persistence and the process
// boundary. No project, editor, runtime-directory or active-window dependency.
bool WriteResultFile(const std::filesystem::path&,const BracketingResult&, bool auxiliaryOnly = false);
void ReadResult(std::istream&,std::uint64_t bytes,std::size_t groups,BracketingResult&,
    const std::filesystem::path& inspectionDirectory,
    const std::function<void(RawImageData&)>& loadMaster = {});
}
