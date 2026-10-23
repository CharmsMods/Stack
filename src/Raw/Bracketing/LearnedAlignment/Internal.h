#pragma once
#include "../ProcessingInternal.h"
#include "../LearnedAlignment.h"

namespace Raw::Bracketing::LearnedAlignment {
struct ProxyGeometry {unsigned width=0,height=0,stride=0;};
bool WriteProxy(const ProcessingRequest&,const PreparedDataset&,const PreparedSource&,const ProxyGeometry&,
    const std::filesystem::path&,std::string&);
bool RunModel(const ProcessingRequest&,const std::filesystem::path& job,std::string&);
}
