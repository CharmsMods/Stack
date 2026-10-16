#pragma once
#include "Project/NodeEditTarget.h"
#include <functional>

namespace Stack::Editor {
inline std::size_t NodeInputAnalysisIdentity(const Project::NodeEditTarget& target,
    std::uint64_t revision,std::size_t source,int frame) {
    const auto identity=std::hash<std::string>{}(target.Key()+"/"+std::to_string(revision)+"/"+
        std::to_string(source)+"/"+std::to_string(frame));
    return identity ? identity : 1;
}
}
