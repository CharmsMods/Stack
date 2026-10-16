#pragma once
#include "Raw/Curves/CurveTypes.h"
#include "Raw/Curves/TonalCurvePoints.h"
#include "ThirdParty/json.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace Stack::RawRecipe {
inline nlohmann::json CurveTonalPointJson(const CurveTonalPointState& tonal) {
    nlohmann::json value{{"region",tonal.region},{"baselineY",tonal.baselineY},
        {"replacesPoint",tonal.replacesPoint}};
    if (tonal.replacesPoint) {
        const auto handle=[](const RawBezierHandleState& h) {
            return nlohmann::json{{"strength",h.strength},{"manual",h.manual},
                {"offsetX",h.offsetX},{"offsetY",h.offsetY}};
        };
        value["originalX"]=tonal.originalX;
        value["in"]=handle(tonal.incoming);value["out"]=handle(tonal.outgoing);
    }
    return value;
}
inline bool IsValidCurveTonalPointJson(const nlohmann::json& value) {
    if (!value.is_object() || !value.contains("region") || !value["region"].is_number_integer() ||
        !value.contains("baselineY") || !value["baselineY"].is_number() ||
        !value.contains("replacesPoint") || !value["replacesPoint"].is_boolean()) return false;
    const double region=value["region"].get<double>();
    const double baseline=value["baselineY"].get<double>();
    if (region<0 || region>=5 || !std::isfinite(baseline) || baseline<0 || baseline>1) return false;
    if (!value["replacesPoint"].get<bool>()) return true;
    if (!value.contains("originalX") || !value["originalX"].is_number()) return false;
    const double x=value["originalX"].get<double>();
    if (!std::isfinite(x) || x<0 || x>1) return false;
    for (const char* key:{"in","out"}) {
        if (!value.contains(key) || !value[key].is_object()) return false;
        const auto& h=value[key];
        if (!h.contains("manual") || !h["manual"].is_boolean()) return false;
        for (const char* field:{"strength","offsetX","offsetY"}) {
            if (!h.contains(field) || !h[field].is_number()) return false;
            const double number=h[field].get<double>();
            const bool strength=std::string(field)=="strength";
            if (!std::isfinite(number) || number<(strength?0:-1) || number>(strength?1.5:1)) return false;
        }
    }
    return true;
}
inline CurveTonalPointState ReadCurveTonalPoint(const nlohmann::json& value) {
    CurveTonalPointState tonal;
    if (!IsValidCurveTonalPointJson(value)) return tonal;
    tonal.region=value["region"].get<int>();tonal.baselineY=value["baselineY"].get<float>();
    tonal.replacesPoint=value["replacesPoint"].get<bool>();
    if (tonal.replacesPoint) {
        tonal.originalX=value["originalX"].get<float>();
        const auto handle=[](const nlohmann::json& h) {
            return RawBezierHandleState{h["strength"].get<float>(),h["manual"].get<bool>(),
                h["offsetX"].get<float>(),h["offsetY"].get<float>()};
        };
        tonal.incoming=handle(value["in"]);tonal.outgoing=handle(value["out"]);
    }
    return tonal;
}

// These transformations act on the authored Bezier geometry. Processing and
// both editors therefore consume the same effective curve, including handles.
inline std::vector<RawBezierCurvePoint> AttenuateCurve(
    const std::vector<RawBezierCurvePoint>& reference, float amount, bool diagonal) {
    auto result = reference;
    const float a = std::clamp(amount, 0.f, 1.f);
    for (auto& p : result) {
        const float neutral = diagonal ? p.x : .5f;
        p.y = neutral + a * (p.y - neutral);
        for (auto* h : {&p.incoming, &p.outgoing})
            h->offsetY = a * h->offsetY + (diagonal ? (1-a)*h->offsetX : 0.f);
        if (IsTonalCurvePoint(p)) {
            p.tonal.baselineY=neutral+a*(p.tonal.baselineY-neutral);
            for (auto* h : {&p.tonal.incoming,&p.tonal.outgoing})
                h->offsetY=a*h->offsetY+(diagonal?(1-a)*h->offsetX:0.f);
        }
    }
    RemoveNeutralTonalCurvePoints(result);
    return result;
}
inline nlohmann::json CurvePointsJson(const std::vector<RawBezierCurvePoint>& points) {
    auto out = nlohmann::json::array();
    for (const auto& p : points) {
        const auto handle=[](const RawBezierHandleState& h) {
            return nlohmann::json{{"strength",h.strength},{"manual",h.manual},{"offsetX",h.offsetX},{"offsetY",h.offsetY}};
        };
        out.push_back({{"x",p.x},{"y",p.y},{"in",handle(p.incoming)},{"out",handle(p.outgoing)}});
        if (IsTonalCurvePoint(p)) out.back()["tonal"]=CurveTonalPointJson(p.tonal);
    }
    return out;
}
// Zero permits uncapped point collections. Existing RAW readers retain 64.
inline std::vector<RawBezierCurvePoint> ReadCurvePoints(const nlohmann::json& j, std::size_t maximumPoints = 64) {
    std::vector<RawBezierCurvePoint> out;
    if (!j.is_array()) return out;
    const auto number=[](const nlohmann::json& v,const char* key,float fallback) {
        auto it=v.find(key); const float f=it!=v.end() && it->is_number()?it->get<float>():fallback;
        return std::isfinite(f)?f:fallback;
    };
    for (const auto& v : j) {
        if (!v.is_object() || (maximumPoints && out.size()>=maximumPoints)) continue;
        RawBezierCurvePoint p;
        p.x=std::clamp(number(v,"x",0),0.f,1.f);p.y=std::clamp(number(v,"y",0),0.f,1.f);
        const auto read=[&](const char* key) {
            RawBezierHandleState h;h.strength=1;
            auto it=v.find(key); if(it==v.end() || !it->is_object()) return h;
            h.strength=std::clamp(number(*it,"strength",1),0.f,1.5f);
            h.manual=it->contains("manual") && (*it)["manual"].is_boolean() && (*it)["manual"].get<bool>();
            h.offsetX=std::clamp(number(*it,"offsetX",0),-1.f,1.f);h.offsetY=std::clamp(number(*it,"offsetY",0),-1.f,1.f);return h;
        };
        p.incoming=read("in");p.outgoing=read("out");
        if (const auto tonal=v.find("tonal");tonal!=v.end()) p.tonal=ReadCurveTonalPoint(*tonal);
        out.push_back(p);
    }
    std::stable_sort(out.begin(),out.end(),[](auto a,auto b){return a.x<b.x;});
    out.erase(std::unique(out.begin(),out.end(),[maximumPoints](auto a,auto b){
        return maximumPoints ? b.x-a.x<.0001f : b.x==a.x;
    }),out.end());
    return out;
}
inline nlohmann::json CurveAmountJson(const CurveAmountState& state) {
    return {{"locked",state.locked},{"amount",state.amount},{"reference",CurvePointsJson(state.reference)}};
}
inline CurveAmountState ReadCurveAmount(const nlohmann::json& j) {
    CurveAmountState s;
    if(!j.is_object()) return s;
    s.reference=ReadCurvePoints(j.value("reference",nlohmann::json::array()));
    if(s.reference.size()<2) return {};
    if(j.contains("locked") && j["locked"].is_boolean()) s.locked=j["locked"].get<bool>();
    if(j.contains("amount") && j["amount"].is_number()) s.amount=j["amount"].get<float>();
    s.amount=std::isfinite(s.amount)?std::clamp(s.amount,0.f,1.f):1.f;
    return s;
}
} // namespace Stack::RawRecipe
