#pragma once
#include "Project/RawCapturedMask.h"
#include "Project/RawLayerStack.h"

namespace Stack::Project {
// Invoked on the caller's graph transaction. Empty destination publishes only.
std::string InsertMaskAdjustment(EditorNodeGraph::Graph&,const std::string&,std::string&,bool allowBranches=false);
struct AuthoredMaskEdit {
    std::string destination,existing,name;
    nlohmann::json recipe;
    std::shared_ptr<const Assets::FloatData> coverage;
};
std::string WriteAuthoredMask(EditorNodeGraph::Graph&, const AuthoredMaskEdit&);
}

