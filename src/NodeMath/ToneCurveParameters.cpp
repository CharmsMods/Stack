#include "NodeMath/ToneCurveParameters.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/Curves/CurveAuthoring.h"
#include <cmath>

namespace Stack::NodeMath {
namespace {
bool Finite(const nlohmann::json& value) {
    if (value.is_number_float()) return std::isfinite(value.get<double>());
    if (value.is_structured()) for (const auto& child:value) if (!Finite(child)) return false;
    return true;
}
}
void ClearPendingToneCurveAnalysis(nlohmann::json& parameters) {
    if (!parameters.contains("advanced") || !parameters["advanced"].is_object()) return;
    auto& advanced=parameters["advanced"];
    if (advanced.contains("autoCalibratePending")) advanced["autoCalibratePending"]=false;
    if (advanced.contains("autoCalibrateForceReanalysis")) advanced["autoCalibrateForceReanalysis"]=false;
}
bool IsToneCurveParameterActive(const nlohmann::json& parameters,const std::string& id) {
    const auto mode=parameters.value("curveMode",std::string("components"));
    if (mode=="components") return id.rfind("amount-",0)==0;
    if (mode=="luminance") return id=="luminance-amount";
    if (mode=="contrast") return id=="contrast" || id=="pivot" || id=="anchor" ||
        id=="width" || id=="transition" || id=="protection";
    return false;
}
nlohmann::json DefaultToneCurveParameters() {
    RawRecipe::RawPointCurveSet curves;
    for (auto& curve:curves.curves) curve.points={{0,0,1},{1,1,1}};
    auto parameters=nlohmann::json{{"schemaVersion",1},{"curveMode","components"},
        {"domain",0},{"logMinEv",-10.f},{"logMaxEv",6.f},{"middleGrey",.18f},
        {"luminanceTone",RawRecipe::SerializeSceneTone(RawRecipe::SceneTone{})},
        {"advanced",nlohmann::json::object()}};
    RawRecipe::StorePointCurveSetInFinishToneJson(parameters,curves);
    for (int channel=0;channel<4;++channel)
        parameters["curveAmounts"][RawRecipe::RawPointCurveChannelKey(static_cast<RawRecipe::RawPointCurveChannel>(channel))]=
            RawRecipe::CurveAmountJson({});
    return parameters;
}
nlohmann::json CaptureToneCurveParameters(const nlohmann::json& value) {
    auto parameters=DefaultToneCurveParameters();
    for (const char* key:{"curveMode","domain","logMinEv","logMaxEv","middleGrey", "pointCurves","pointCurveSetVersion","curveAmounts","advanced"})
        if (value.contains(key)) parameters[key]=value.at(key);
    if (value.contains("luminanceTone")) {
        const auto& luminance=value.at("luminanceTone");
        for (const char* key:{"representation","enabled","curve","curveAmount","anchorOffsetEv","contrast","pivotEv","coreHalfWidthEv","transitionEv","outerProtection","points"})
            if (luminance.contains(key)) parameters["luminanceTone"][key]=luminance.at(key);
    }
    return parameters;
}
bool ValidateToneCurveParameters(const nlohmann::json& value,std::string& error) {
    const auto fail=[&](const char* reason){error=reason;return false;};
    try {
        if (!value.is_object() || value.value("schemaVersion",0)!=1 || !Finite(value))
            return fail("Unsupported or non-finite Tone Curve parameters.");
        const auto mode=value.value("curveMode",std::string{});
        if (mode!="components" && mode!="luminance" && mode!="contrast" && mode!="advanced")
            return fail("Unknown Tone Curve processing mode.");
        const int domain=value.value("domain",-1);
        if (domain<0 || domain>1 || value.value("middleGrey",0.f)<=0 ||
            value.value("logMaxEv",0.f)<=value.value("logMinEv",0.f))
            return fail("Invalid Tone Curve plotting domain.");
        const auto pointsValid=[](const nlohmann::json& points,bool emptyIdentity) {
            if (!points.is_array()) return false;
            if (points.empty()) return emptyIdentity;
            if (points.size()<2 || points.size()>64) return false;
            double previous=-1;
            bool tonalRegions[5]{};
            for (const auto& point:points) {
                if (!point.is_object() || !point.contains("x") || !point.at("x").is_number() ||
                    !point.contains("y") || !point.at("y").is_number()) return false;
                const double x=point.at("x").get<double>(),y=point.at("y").get<double>();
                if (x<0 || x>1 || y<0 || y>1 || x<=previous) return false;
                previous=x;
                if (point.contains("tonal")) {
                    const auto& tonal=point.at("tonal");
                    if (!RawRecipe::IsValidCurveTonalPointJson(tonal)) return false;
                    const int region=tonal.at("region").get<int>();
                    if (tonalRegions[region]) return false;
                    tonalRegions[region]=true;
                    if (tonal.at("replacesPoint").get<bool>() &&
                        tonal.at("originalX").get<double>()!=x) return false;
                }
                for (const char* key:{"in","out"}) if (point.contains(key)) {
                    const auto& handle=point.at(key);
                    if (!handle.is_object()) return false;
                    for (const char* field:{"strength","offsetX","offsetY"})
                        if (handle.contains(field) && !handle.at(field).is_number()) return false;
                    if (handle.contains("manual") && !handle.at("manual").is_boolean()) return false;
                }
            }
            return true;
        };
        const auto amountValid=[&](const nlohmann::json& amount) {
            if (!amount.is_object()) return false;
            if (amount.empty()) return true;
            if (!amount.contains("locked") || !amount.at("locked").is_boolean() ||
                !amount.contains("amount") || !amount.at("amount").is_number()) return false;
            const float strength=amount.at("amount").get<float>();
            if (strength<0 || strength>1) return false;
            return amount.contains("reference") &&
                pointsValid(amount.at("reference"),!amount.at("locked").get<bool>());
        };
        const auto& luminance=value.at("luminanceTone");
        if (!luminance.is_object() || !pointsValid(luminance.at("curve"),false))
            return fail("Tone Curve needs its current authored luminance points.");
        for (const char* key:{"anchorOffsetEv","contrast","pivotEv","coreHalfWidthEv","transitionEv","outerProtection"})
            if (!luminance.contains(key) || !luminance.at(key).is_number())
                return fail("Invalid Tone Curve contrast parameters.");
        if (!luminance.at("enabled").is_boolean() || !amountValid(luminance.at("curveAmount")))
            return fail("Invalid Tone Curve luminance amount.");
        const auto& slopes=luminance.at("points");
        if (!slopes.is_array() || slopes.size()<2 || slopes.size()>32)
            return fail("Invalid Tone Curve contrast points.");
        double lastEv=-1e30;
        for (const auto& point:slopes) {
            if (!point.is_object() || !point.at("ev").is_number() || !point.at("contrast").is_number() ||
                point.at("ev").get<double>()<=lastEv || point.at("contrast").get<double>()<=0)
                return fail("Invalid Tone Curve contrast point.");
            lastEv=point.at("ev").get<double>();
        }
        if (value.value("pointCurveSetVersion",0)!=2 || !value.at("pointCurves").is_object())
            return fail("Tone Curve needs its current authored component curves.");
        for (int i=0;i<4;++i) {
            const auto key=RawRecipe::RawPointCurveChannelKey(static_cast<RawRecipe::RawPointCurveChannel>(i));
            const auto& curve=value.at("pointCurves").at(key);
            if (!curve.is_object() || curve.value("interpolation",std::string{})!="bezier-segments-v1" ||
                !pointsValid(curve.at("points"),true) ||
                (curve.contains("basePoints") && !pointsValid(curve.at("basePoints"),false)))
                return fail("Invalid Tone Curve component points.");
            if (value.contains("curveAmounts") && value.at("curveAmounts").contains(key) &&
                !amountValid(value.at("curveAmounts").at(key)))
                return fail("Invalid Tone Curve component amount.");
        }
        if (!value.at("advanced").is_object()) return fail("Invalid advanced Tone Curve parameters.");
        for (const char* key:{"points","basePoints"}) if (value.at("advanced").contains(key) &&
            !pointsValid(value.at("advanced").at(key),true)) return fail("Invalid advanced Tone Curve points.");
        return true;
    } catch (const nlohmann::json::exception&) {
        return fail("Malformed current Tone Curve parameters.");
    }
}
}
