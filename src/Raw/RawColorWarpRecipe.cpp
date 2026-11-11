#include "Raw/ColorWarpMath.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
namespace Stack::RawRecipe {
namespace {
float ClampFinite(float value,float fallback,float low,float high) {
    return std::clamp(std::isfinite(value)?value:fallback,low,high);
}
float JsonFloat(const nlohmann::json& value,const char* key,float fallback) {
    const auto it=value.find(key);
    return it!=value.end() && it->is_number()?it->get<float>():fallback;
}
nlohmann::json RangeJson(const RawColorWarpRange& r) {
    return {{"low",r.low},{"high",r.high},{"featherLow",r.featherLow},{"featherHigh",r.featherHigh},{"enabled",r.enabled}};
}
RawColorWarpRange ReadRange(const nlohmann::json& value,RawColorWarpRange r) {
    if(!value.is_object())return r;
    r.low=JsonFloat(value,"low",r.low);r.high=JsonFloat(value,"high",r.high);
    r.featherLow=JsonFloat(value,"featherLow",r.featherLow);r.featherHigh=JsonFloat(value,"featherHigh",r.featherHigh);
    r.enabled=value.value("enabled",r.enabled);return r;
}
}
RawColorWarpRecipe::RawColorWarpRecipe() {
    static const auto families=[] {
        std::vector<RawColorWarpPin> result;
        for(int i=0;i<8;++i)result.push_back(MakeColorWarpFamily(i));
        return result;
    }();
    pins=families;
}
RawColorWarpRecipe SanitizeColorWarpRecipe(RawColorWarpRecipe colorWarp) {
    colorWarp.version = 4;
    colorWarp.strength = ClampFinite(colorWarp.strength, 1.0f, 0.0f, 2.0f);
    if (colorWarp.pins.size() > kMaxRawColorWarpPins) {
        colorWarp.pins.resize(kMaxRawColorWarpPins);
    }
    if (colorWarp.regions.size() > kMaxRawColorWarpRegions) {
        colorWarp.regions.resize(kMaxRawColorWarpRegions);
    }
    std::unordered_set<std::string> usedRegionIds;
    for (std::size_t regionIndex = 0; regionIndex < colorWarp.regions.size(); ++regionIndex) {
        RawColorWarpRegion& region = colorWarp.regions[regionIndex];
        if (region.id.empty() || usedRegionIds.find(region.id) != usedRegionIds.end()) {
            region.id = "region-" + std::to_string(regionIndex + 1u);
            while (usedRegionIds.find(region.id) != usedRegionIds.end()) {
                region.id += "-copy";
            }
        }
        usedRegionIds.insert(region.id);
        if (region.circles.size() > kMaxRawColorWarpSampleCircles) {
            region.circles.resize(kMaxRawColorWarpSampleCircles);
        }
        std::unordered_set<std::string> usedCircleIds;
        for (std::size_t circleIndex = 0; circleIndex < region.circles.size(); ++circleIndex) {
            RawColorWarpSampleCircle& circle = region.circles[circleIndex];
            if (circle.id.empty() || usedCircleIds.find(circle.id) != usedCircleIds.end()) {
                circle.id = "sample-" + std::to_string(circleIndex + 1u);
                while (usedCircleIds.find(circle.id) != usedCircleIds.end()) {
                    circle.id += "-copy";
                }
            }
            usedCircleIds.insert(circle.id);
            circle.centerU = ClampFinite(circle.centerU, 0.5f, 0.0f, 1.0f);
            circle.centerV = ClampFinite(circle.centerV, 0.5f, 0.0f, 1.0f);
            circle.radiusU = ClampFinite(circle.radiusU, 0.02f, 0.000001f, 1.0f);
            circle.radiusV = ClampFinite(circle.radiusV, 0.02f, 0.000001f, 1.0f);
        }
        region.reachPixels = ClampFinite(region.reachPixels, 24.0f, 0.0f, 4096.0f);
        region.spatialSupport = ClampFinite(region.spatialSupport, 1.0f, 0.0f, 1.0f);
        region.edgeStop = ClampFinite(region.edgeStop, 0.65f, 0.0f, 1.0f);
        region.featherPixels = ClampFinite(region.featherPixels, 0.0f, 0.0f, 4096.0f);
    }

    std::unordered_set<std::string> usedIds;
    for (std::size_t index = 0; index < colorWarp.pins.size(); ++index) {
        RawColorWarpPin& pin = colorWarp.pins[index];
        if (pin.id.empty() || usedIds.find(pin.id) != usedIds.end()) {
            pin.id = "color-" + std::to_string(index + 1u);
            while (usedIds.find(pin.id) != usedIds.end()) {
                pin.id += "-copy";
            }
        }
        usedIds.insert(pin.id);
        if (pin.name.empty()) {
            pin.name = pin.protectColor
                ? "Protected Color " + std::to_string(index + 1u)
                : "Color " + std::to_string(index + 1u);
        }
        SanitizeColorWarpPin(pin);
        if (usedRegionIds.find(pin.regionId) == usedRegionIds.end()) pin.regionId.clear();
    }
    std::array<bool,8> families{};
    std::size_t samples=0;
    colorWarp.pins.erase(std::remove_if(colorWarp.pins.begin(),colorWarp.pins.end(),[&](const auto& pin) {
        if(pin.family<0)return ++samples>kMaxRawColorWarpSamples;
        if(families[pin.family])return true;
        families[pin.family]=true;return false;
    }),colorWarp.pins.end());
    for(int family=0;family<8;++family)
        if(!families[family])colorWarp.pins.push_back(MakeColorWarpFamily(family));
    std::stable_sort(colorWarp.pins.begin(),colorWarp.pins.end(),[](const auto& a,const auto& b) {
        return (a.family<0?8:a.family)<(b.family<0?8:b.family);
    });

    return colorWarp;
}

nlohmann::json SerializeColorWarpRecipe(const RawColorWarpRecipe& input) {
    const RawColorWarpRecipe colorWarp = SanitizeColorWarpRecipe(input);
    nlohmann::json result = nlohmann::json::object();
    result["version"] = colorWarp.version;
    result["enabled"] = colorWarp.enabled;
    result["strength"] = colorWarp.strength;
    result["coordinateModel"] = "oklab-joint-luminance-d65-v4";
    result["combine"] = colorWarp.combine==RawColorWarpCombine::Blend ? "blend" : "accumulate";
    result["regions"] = nlohmann::json::array();
    for (const RawColorWarpRegion& region : colorWarp.regions) {
        nlohmann::json item = nlohmann::json::object();
        item["id"] = region.id;
        item["spatialMode"] = RawColorWarpSpatialModeStableString(region.spatialMode);
        item["reachPixels"] = region.reachPixels;
        item["spatialSupport"] = region.spatialSupport;
        item["edgeStop"] = region.edgeStop;
        item["featherPixels"] = region.featherPixels;
        item["featherDirection"] =
            RawColorWarpFeatherDirectionStableString(region.featherDirection);
        item["circles"] = nlohmann::json::array();
        for (const RawColorWarpSampleCircle& circle : region.circles) {
            nlohmann::json circleItem = nlohmann::json::object();
            circleItem["id"] = circle.id;
            circleItem["centerU"] = circle.centerU;
            circleItem["centerV"] = circle.centerV;
            circleItem["radiusU"] = circle.radiusU;
            circleItem["radiusV"] = circle.radiusV;
            circleItem["interpretationMode"] =
                RawColorWarpInterpretationModeStableString(circle.interpretation);
            circleItem["polarity"] = circle.polarity == RawColorWarpSamplePolarity::Exclude
                ? "exclude"
                : "include";
            item["circles"].push_back(std::move(circleItem));
        }
        result["regions"].push_back(std::move(item));
    }
    result["pins"] = nlohmann::json::array();
    for (const RawColorWarpPin& pin : colorWarp.pins) {
        nlohmann::json item = nlohmann::json::object();
        item["id"] = pin.id;
        item["name"] = pin.name;
        item["enabled"] = pin.enabled;
        item["protectColor"] = pin.protectColor;
        item["sourceA"] = pin.sourceA;
        item["sourceB"] = pin.sourceB;
        item["family"] = pin.family;
        item["hueRange"] = RangeJson(pin.hueRange);
        item["saturationRange"] = RangeJson(pin.saturationRange);
        item["exposureRange"] = RangeJson(pin.exposureRange);
        item["moveColors"] = pin.moveColors;
        item["hueShift"] = pin.hueShift;
        item["saturation"] = pin.saturation;
        item["smoothSelection"] = pin.smoothSelection;
        item["sampleRadius"] = pin.sampleRadius;
        item["strength"] = pin.strength;
        item["regionId"] = pin.regionId;
        item["brightness"] = pin.brightness;
        item["brightnessSoftness"]=pin.brightnessSoftness;
        item["tintA"]=pin.tintA;item["tintB"]=pin.tintB;
        item["population"]=nlohmann::json::array();
        for(const auto& p:pin.population)item["population"].push_back({p.a,p.b,p.sceneEv,p.mass});
        result["pins"].push_back(std::move(item));
    }
    return result;
}

RawColorWarpRecipe DeserializeColorWarpRecipe(const nlohmann::json& value) {
    RawColorWarpRecipe result;
    if (!value.is_object()) {
        return result;
    }
    if (value.value("version", 0) != 4 ||
        value.value("coordinateModel", std::string()) !=
            "oklab-joint-luminance-d65-v4") {
        if(value.empty()) return result;
        throw std::runtime_error("Unsupported Color Warp format.");
    }
    result.version = 4;
    result.enabled = value.value("enabled", true);
    result.strength = JsonFloat(value, "strength", 1.0f);
    result.combine=value.value("combine",std::string("accumulate"))=="blend" ? RawColorWarpCombine::Blend : RawColorWarpCombine::Accumulate;
    const nlohmann::json regions = value.value("regions", nlohmann::json::array());
    if (regions.is_array()) {
        for (const nlohmann::json& item : regions) {
            if (!item.is_object()) continue;
            RawColorWarpRegion region;
            region.id = item.value("id", std::string());
            region.spatialMode = RawColorWarpSpatialModeFromStableString(
                item.value("spatialMode", std::string("all-matches")));
            region.reachPixels = JsonFloat(item, "reachPixels", 24.0f);
            region.spatialSupport = JsonFloat(item, "spatialSupport", 1.0f);
            region.edgeStop = JsonFloat(item, "edgeStop", 0.65f);
            region.featherPixels = JsonFloat(item, "featherPixels", 0.0f);
            region.featherDirection = RawColorWarpFeatherDirectionFromStableString(
                item.value("featherDirection", std::string("centered")));
            const nlohmann::json circles = item.value("circles", nlohmann::json::array());
            if (circles.is_array()) {
                for (const nlohmann::json& circleItem : circles) {
                    if (!circleItem.is_object()) continue;
                    RawColorWarpSampleCircle circle;
                    circle.id = circleItem.value("id", std::string());
                    circle.centerU = JsonFloat(circleItem, "centerU", 0.5f);
                    circle.centerV = JsonFloat(circleItem, "centerV", 0.5f);
                    circle.radiusU = JsonFloat(circleItem, "radiusU", 0.02f);
                    circle.radiusV = JsonFloat(circleItem, "radiusV", 0.02f);
                    circle.interpretation = RawColorWarpInterpretationModeFromStableString(
                        circleItem.value(
                            "interpretationMode",
                            std::string("guided-family")));
                    circle.polarity = circleItem.value(
                        "polarity", std::string("include")) == "exclude"
                        ? RawColorWarpSamplePolarity::Exclude
                        : RawColorWarpSamplePolarity::Include;
                    region.circles.push_back(std::move(circle));
                }
            }
            result.regions.push_back(std::move(region));
        }
    }
    result.pins.clear();
    const nlohmann::json pins = value.value("pins", nlohmann::json::array());
    if (pins.is_array()) {
        for (const nlohmann::json& item : pins) {
            if (!item.is_object()) {
                continue;
            }
            RawColorWarpPin pin;
            pin.id = item.value("id", std::string());
            pin.name = item.value("name", std::string());
            pin.enabled = item.value("enabled", true);
            pin.protectColor = item.value("protectColor", false);
            pin.sourceA = JsonFloat(item, "sourceA", 0.0f);
            pin.sourceB = JsonFloat(item, "sourceB", 0.0f);
            pin.family=item.value("family",-1);
            pin.hueRange=ReadRange(item.value("hueRange",nlohmann::json::object()),pin.hueRange);
            pin.saturationRange=ReadRange(item.value("saturationRange",nlohmann::json::object()),pin.saturationRange);
            pin.exposureRange=ReadRange(item.value("exposureRange",nlohmann::json::object()),pin.exposureRange);
            pin.moveColors=item.value("moveColors",false);
            pin.hueShift=JsonFloat(item,"hueShift",0);
            pin.saturation=JsonFloat(item,"saturation",0);
            pin.smoothSelection=JsonFloat(item,"smoothSelection",0);
            pin.sampleRadius=JsonFloat(item,"sampleRadius",0);
            pin.strength = JsonFloat(item, "strength", 1.0f);
            pin.regionId = item.value("regionId", std::string());
            pin.brightness = JsonFloat(item, "brightness", 0.0f);
            pin.brightnessSoftness=JsonFloat(item,"brightnessSoftness",.75f);
            pin.tintA=JsonFloat(item,"tintA",0);pin.tintB=JsonFloat(item,"tintB",0);
            if(auto it=item.find("population");it!=item.end() && it->is_array())
                for(const auto& p:*it) {
                    if(pin.population.size()>=kMaxRawColorWarpPopulation)break;
                    if(p.is_array() && p.size()==4 && std::all_of(p.begin(),p.end(),[](const auto& x){return x.is_number();}))
                        pin.population.push_back({p[0].get<float>(),p[1].get<float>(),p[2].get<float>(),p[3].get<float>()});
                }
            result.pins.push_back(std::move(pin));
        }
    }
    return SanitizeColorWarpRecipe(std::move(result));
}


}
