#pragma once
#include "Raw/DepthMask.h"
#include "ThirdParty/json.hpp"
#include <set>

namespace Raw {
inline nlohmann::json DepthMaskManifest(const std::vector<DepthMask>& masks) {
    nlohmann::json items=nlohmann::json::array();
    for(const auto& m:masks) {
        if(!m.Valid())throw std::runtime_error("Invalid bracket mask.");
        items.push_back({{"id",m.id},{"name",m.name},{"width",m.width},{"height",m.height},
            {"x",m.x},{"y",m.y},{"w",m.w},{"h",m.h}});
    }
    return items;
}
inline std::vector<DepthMask> ReadDepthMaskManifest(const nlohmann::json& items) {
    if(!items.is_array() || items.size()>36)throw std::runtime_error("Invalid bracket mask count.");
    std::vector<DepthMask> masks;std::set<std::string> seen;
    std::size_t bytes=0;
    for(const auto& j:items) {
        DepthMask m;m.id=j.at("id");m.name=j.at("name");m.width=j.at("width");m.height=j.at("height");
        m.x=j.at("x");m.y=j.at("y");m.w=j.at("w");m.h=j.at("h");
        if(!m.Valid() || !seen.insert(m.id).second)throw std::runtime_error("Invalid saved depth mask.");
        bytes+=std::size_t(m.w)*m.h;
        if(bytes>128u*1024*1024)throw std::runtime_error("Depth masks exceed the supported storage limit.");
        masks.push_back(std::move(m));
    }
    return masks;
}
}
