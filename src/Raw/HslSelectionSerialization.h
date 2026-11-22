#pragma once
#include "Raw/HslSelectionTypes.h"
#include "ThirdParty/json.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace Stack::RawRecipe {
inline void SanitizeHslSelections(std::vector<HslSelection>& selections) {
    if(selections.size()>kMaxHslSelections)throw std::runtime_error("Too many HSL selections.");
    const auto finite=[](float v,float fallback,float low,float high){return std::clamp(std::isfinite(v)?v:fallback,low,high);};
    std::unordered_set<std::string> ids;
    for(auto& s:selections) {
        if(s.id.empty() || ids.count(s.id)) {
            s.id="hsl-"+std::to_string(ids.size()+1);while(ids.count(s.id))s.id+="-copy";
        }
        ids.insert(s.id);if(s.name.empty())s.name="Selection";
        s.hue=finite(s.hue,0,-180,180);s.hueWidth=finite(s.hueWidth,25,0,180);
        s.sourceChroma=finite(s.sourceChroma,-1,-1,4);s.chromaReach=finite(s.chromaReach,.09f,.002f,4);
        s.hueSoftness=finite(s.hueSoftness,15,0,180);s.influence=finite(s.influence,1,0,2);
        for(auto& value:s.grade)value=finite(value,0,-1,1);
        s.areaSoftness=finite(s.areaSoftness,.01f,0,.25f);
        for(auto* bands:{&s.bands,&s.excludedBands}) {
            if(bands->size()>kMaxHslBands)throw std::runtime_error("Too many HSL brightness ranges.");
            for(auto& b:*bands){b.low=finite(b.low,-2,-32,32);b.high=finite(b.high,2,b.low,32);b.softness=finite(b.softness,.5f,0,8);}
        }
        if(s.outline.size()>kMaxHslOutlinePoints)throw std::runtime_error("HSL outline exceeds the supported point count.");
        for(auto& p:s.outline)for(auto& v:p)v=finite(v,.5f,0,1);
        for(auto& v:s.values){v[0]=finite(v[0],0,-360,360);v[1]=finite(v[1],0,-1,10);v[2]=finite(v[2],0,-2,2);}
    }
}
inline nlohmann::json SerializeHslSelections(const std::vector<HslSelection>& selections) {
    auto out=nlohmann::json::array();
    for(const auto& s:selections) {
        nlohmann::json j={{"id",s.id},{"name",s.name},{"enabled",s.enabled},{"hue",s.hue},
            {"hueWidth",s.hueWidth},{"hueSoftness",s.hueSoftness},{"allColors",s.allColors},
            {"areaEnabled",s.areaEnabled},{"areaSoftness",s.areaSoftness},{"outline",s.outline},
            {"influence",s.influence},{"own",s.own},{"values",s.values}};
        const auto bands=[](const auto& list){auto a=nlohmann::json::array();for(const auto& b:list)a.push_back({b.low,b.high,b.softness});return a;};
        if(s.sourceChroma>=0){j["sourceChroma"]=s.sourceChroma;j["chromaReach"]=s.chromaReach;}
        if(s.warpMask)j["warpMask"]=true;
        if(s.independentChannels)j["independentChannels"]=true;
        if(s.neutralOnly)j["neutralOnly"]=true;
        if(s.grade!=std::array<float,3>{})j["grade"]=s.grade;
        j["bands"]=bands(s.bands);j["excludedBands"]=bands(s.excludedBands);out.push_back(std::move(j));
    }
    return out;
}
inline std::vector<HslSelection> DeserializeHslSelections(const nlohmann::json& list) {
    if(!list.is_array() || list.size()>kMaxHslSelections)throw std::runtime_error("Invalid HSL selections.");
    std::vector<HslSelection> result;
    for(const auto& j:list) {
        HslSelection s;s.id=j.value("id",s.id);s.name=j.value("name",s.name);s.enabled=j.value("enabled",true);
        s.hue=j.value("hue",s.hue);s.hueWidth=j.value("hueWidth",s.hueWidth);s.hueSoftness=j.value("hueSoftness",s.hueSoftness);
        s.sourceChroma=j.value("sourceChroma",-1.f);s.chromaReach=j.value("chromaReach",.09f);s.warpMask=j.value("warpMask",false);
        s.independentChannels=j.value("independentChannels",false);
        s.neutralOnly=j.value("neutralOnly",false);s.grade=j.value("grade",s.grade);
        s.allColors=j.value("allColors",false);s.areaEnabled=j.value("areaEnabled",false);s.areaSoftness=j.value("areaSoftness",s.areaSoftness);
        s.outline=j.value("outline",s.outline);s.influence=j.value("influence",1.f);s.own=j.value("own",s.own);s.values=j.value("values",s.values);
        const auto bands=[](const nlohmann::json& a){
            if(!a.is_array() || a.size()>kMaxHslBands)throw std::runtime_error("Invalid HSL brightness ranges.");
            std::vector<HslBrightnessBand> result;
            for(const auto& b:a){if(!b.is_array() || b.size()!=3)throw std::runtime_error("Invalid HSL brightness range.");result.push_back({b.at(0).get<float>(),b.at(1).get<float>(),b.at(2).get<float>()});}return result;
        };
        s.bands=bands(j.value("bands",nlohmann::json::array()));s.excludedBands=bands(j.value("excludedBands",nlohmann::json::array()));
        result.push_back(std::move(s));
    }
    SanitizeHslSelections(result);return result;
}
}
