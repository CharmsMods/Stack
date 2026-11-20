#include "Raw/MaskAuthoring.h"
#include <cmath>
#include <stdexcept>

namespace Stack::MaskAuthoring {
nlohmann::json Serialize(const Recipe& recipe) {
    auto marks=nlohmann::json::array();
    for(const auto& m:recipe.marks) marks.push_back({{"tool",int(m.tool)},{"role",int(m.role)},
        {"points",m.points},{"samples",m.samples},{"radius",m.radius},{"feather",m.feather}});
    return {{"version",1},{"wholeImage",recipe.wholeImage},{"marks",std::move(marks)}};
}
Recipe Deserialize(const nlohmann::json& json) {
    if(json.at("version")!=1) throw std::runtime_error("Unsupported mask authoring version.");
    Recipe recipe;
    recipe.wholeImage=json.at("wholeImage").get<bool>();
    const auto& marks=json.at("marks");
    if(!marks.is_array() || marks.size()>4096) throw std::runtime_error("Invalid mask marks.");
    for(const auto& entry:marks) {
        Mark m;
        const int tool=entry.at("tool"), role=entry.at("role");
        const auto& points=entry.at("points");
        if(tool<0 || tool>int(Tool::Edge) || role<0 || role>int(Role::Transition) ||
            !points.is_array() || points.empty() || points.size()>65536)
            throw std::runtime_error("Invalid mask mark.");
        m.tool=Tool(tool);m.role=Role(role);m.radius=entry.at("radius");m.feather=entry.at("feather");
        if(!(m.radius>0 && m.radius<=1 && m.feather>=0 && m.feather<=1)) throw std::runtime_error("Invalid brush settings.");
        for(const auto& point:points) {
            Point uv=point.get<Point>();
            if(!(uv[0]>=0 && uv[0]<=1 && uv[1]>=0 && uv[1]<=1)) throw std::runtime_error("Invalid mask coordinate.");
            m.points.push_back(uv);
        }
        if(auto it=entry.find("samples");it!=entry.end()) {
            if(!it->is_array() || it->size()>4096)throw std::runtime_error("Invalid mask color samples.");
            for(const auto& sample:*it){auto value=sample.get<std::array<float,3>>();
                if(!std::all_of(value.begin(),value.end(),[](float v){return std::isfinite(v);}))throw std::runtime_error("Invalid mask color.");
                m.samples.push_back(value);}
        }
        recipe.marks.push_back(std::move(m));
    }
    return recipe;
}
}

