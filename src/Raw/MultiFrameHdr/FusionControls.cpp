#include "Raw/MultiFrameHdr/FusionControls.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace Raw::Hdr {
namespace {
bool InRange(float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; }
}

bool ValidateFusionControls(const FusionControls& p, std::string* error) {
    const auto fail = [&](const char* s) { if (error) *error = s; return false; };
    if (!InRange(p.targetEv, -12, 12) || !InRange(p.preferredEv, -12, 12) ||
        !InRange(p.targetFollow, 0, 1) || !InRange(p.blendWidthEv, 0.25f, 16))
        return fail("Fusion exposure, follow strength, or blend width is outside its range.");
    for (float v : p.targetCurve) if (!InRange(v, -12, 12)) return fail("Invalid target exposure curve.");
    for (float v : p.preferenceCurve) if (!InRange(v, -12, 12)) return fail("Invalid source preference curve.");
    if (p.sources.size() > kFusionMaxSources || p.masks.size() > 32)
        return fail("Fusion supports up to 20 sources and 32 local masks.");
    std::unordered_set<std::string> ids;
    for (const auto& s : p.sources)
        if (s.sourceId.empty() || !ids.insert(s.sourceId).second || !InRange(s.biasStops, -12, 12))
            return fail("Fusion source IDs must be unique and bias must be within 12 stops.");
    for (const auto& m : p.masks)
        if (!InRange(m.centerX, 0, 1) || !InRange(m.centerY, 0, 1) ||
            !InRange(m.radiusX, 0.005f, 2) || !InRange(m.radiusY, 0.005f, 2) ||
            !InRange(m.feather, 0.01f, 1) || !InRange(m.targetEv, -12, 12) ||
            !InRange(m.preferredEv, -12, 12)) return fail("Invalid local Fusion mask.");
    if (error) error->clear();
    return true;
}

nlohmann::json SerializeFusionControls(const FusionControls& p) {
    nlohmann::json j = {{"version", 1}, {"targetEv", p.targetEv}, {"preferredEv", p.preferredEv},
        {"targetFollow", p.targetFollow}, {"blendWidthEv", p.blendWidthEv},
        {"preferSources", p.preferSources}, {"targetCurve", p.targetCurve},
        {"preferenceCurve", p.preferenceCurve}, {"sources", nlohmann::json::array()},
        {"masks", nlohmann::json::array()}};
    for (const auto& s : p.sources) j["sources"].push_back({{"id", s.sourceId}, {"enabled", s.enabled}, {"biasStops", s.biasStops}});
    for (const auto& m : p.masks) j["masks"].push_back({{"name", m.name}, {"enabled", m.enabled},
        {"centerX", m.centerX}, {"centerY", m.centerY}, {"radiusX", m.radiusX}, {"radiusY", m.radiusY},
        {"feather", m.feather}, {"targetEv", m.targetEv}, {"preferredEv", m.preferredEv}});
    return j;
}

bool DeserializeFusionControls(const nlohmann::json& j, FusionControls& p, std::string* error) {
    try {
        if (!j.is_object() || j.value("version", 1) != 1) throw std::runtime_error("Unsupported Fusion controls.");
        FusionControls d;
        d.targetEv = j.value("targetEv", d.targetEv); d.preferredEv = j.value("preferredEv", d.preferredEv);
        d.targetFollow = j.value("targetFollow", d.targetFollow); d.blendWidthEv = j.value("blendWidthEv", d.blendWidthEv);
        d.preferSources = j.value("preferSources", d.preferSources);
        for (const char* key : {"targetCurve", "preferenceCurve"}) {
            if (!j.contains(key)) continue;
            if (!j[key].is_array() || j[key].size() != 7) throw std::runtime_error("Fusion curves need seven knots.");
            auto& curve = std::string(key) == "targetCurve" ? d.targetCurve : d.preferenceCurve;
            for (std::size_t i = 0; i < 7; ++i) curve[i] = j[key][i].get<float>();
        }
        if (j.contains("sources")) {
            if (!j["sources"].is_array()) throw std::runtime_error("Fusion sources must be an array.");
            for (const auto& s : j["sources"]) d.sources.push_back({s.at("id").get<std::string>(), s.value("enabled", true), s.value("biasStops", 0.0f)});
        }
        if (j.contains("masks")) {
            if (!j["masks"].is_array()) throw std::runtime_error("Fusion masks must be an array.");
            for (const auto& m : j["masks"]) {
                FusionLocalMask a;
                a.name = m.value("name", a.name); a.enabled = m.value("enabled", a.enabled);
                a.centerX = m.value("centerX", a.centerX); a.centerY = m.value("centerY", a.centerY);
                a.radiusX = m.value("radiusX", a.radiusX); a.radiusY = m.value("radiusY", a.radiusY);
                a.feather = m.value("feather", a.feather); a.targetEv = m.value("targetEv", a.targetEv);
                a.preferredEv = m.value("preferredEv", a.preferredEv); d.masks.push_back(a);
            }
        }
        if (!ValidateFusionControls(d, error)) return false;
        p = std::move(d); return true;
    } catch (const std::exception& e) { if (error) *error = e.what(); return false; }
}

bool IsNeutralFusion(const FusionControls& p) {
    if (p.targetEv != 0 || p.preferSources || p.preferredEv != 0 || p.targetFollow != 0) return false;
    for (float v : p.targetCurve) if (v != 0) return false;
    for (float v : p.preferenceCurve) if (v != 0) return false;
    for (const auto& s : p.sources) if (!s.enabled || s.biasStops != 0) return false;
    for (const auto& m : p.masks) if (m.enabled && (m.targetEv != 0 || m.preferredEv != 0)) return false;
    return true;
}

float EvaluateFusionCurve(const std::array<float, 7>& curve, float x) {
    if (x <= kFusionCurveEv.front()) return curve.front();
    for (std::size_t i = 1; i < curve.size(); ++i) if (x <= kFusionCurveEv[i]) {
        float t = (x - kFusionCurveEv[i - 1]) / (kFusionCurveEv[i] - kFusionCurveEv[i - 1]);
        t = t * t * (3 - 2 * t);
        return curve[i - 1] + t * (curve[i] - curve[i - 1]);
    }
    return curve.back();
}

float FusionMaskCoverage(const FusionLocalMask& m, float x, float y) {
    if (!m.enabled) return 0;
    const float dx = (x - m.centerX) / m.radiusX, dy = (y - m.centerY) / m.radiusY;
    float t = std::clamp((1 - std::sqrt(dx * dx + dy * dy)) / m.feather, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

FusionControlField EvaluateFusionField(const FusionControls& p, float luma, float x, float y) {
    FusionControlField f;
    f.targetEv = p.targetEv + EvaluateFusionCurve(p.targetCurve, luma);
    f.preferredEv = p.preferredEv + EvaluateFusionCurve(p.preferenceCurve, luma);
    for (const auto& m : p.masks) {
        const float a = FusionMaskCoverage(m, x, y);
        f.targetEv += a * m.targetEv; f.preferredEv += a * m.preferredEv;
    }
    f.targetEv = std::clamp(f.targetEv, -16.0f, 16.0f);
    f.preferredEv = std::clamp(f.preferredEv + p.targetFollow * f.targetEv, -24.0f, 24.0f);
    return f;
}

FusionPixel EvaluateFusionPixel(const FusionControls& p, const FusionSourceInfo* sources,
    const FusionObservation* values, std::size_t count, std::size_t anchor,
    float luma, float x, float y) {
    FusionPixel r;
    count = std::min(count, kFusionMaxSources);
    if (count == 0) { r.fallback = true; return r; }
    anchor = std::min(anchor, count - 1);
    const auto field = EvaluateFusionField(p, luma, x, y);
    r.targetEv = field.targetEv;
    float totalAuto = 0, autoEv = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const float w = std::isfinite(values[i].automaticWeight) ? std::max(0.0f, values[i].automaticWeight) : 0;
        totalAuto += w; autoEv += w * sources[i].captureEv;
    }
    r.preferredEv = (totalAuto > 0 ? autoEv / totalAuto : sources[anchor].captureEv) + field.preferredEv;
    float maximumLog = -std::numeric_limits<float>::infinity();
    std::array<float, kFusionMaxSources> logs;
    logs.fill(-std::numeric_limits<float>::infinity());
    for (std::size_t i = 0; i < count; ++i) {
        if (!(values[i].automaticWeight > 0) || !std::isfinite(values[i].scene) ||
            !std::isfinite(values[i].variance) || values[i].variance < 0) continue;
        bool enabled = true; float bias = 0;
        for (const auto& s : p.sources) if (s.sourceId == sources[i].sourceId) { enabled = s.enabled; bias = s.biasStops; break; }
        if (!enabled) continue;
        float logWeight = std::log(values[i].automaticWeight) + bias * std::log(2.0f);
        if (p.preferSources) {
            const float delta = (sources[i].captureEv - r.preferredEv) / p.blendWidthEv;
            logWeight -= 0.5f * delta * delta;
        }
        logs[i] = logWeight; maximumLog = std::max(maximumLog, logWeight);
    }
    float sum = 0;
    if (std::isfinite(maximumLog)) for (std::size_t i = 0; i < count; ++i) {
        r.contribution[i] = std::exp(logs[i] - maximumLog); sum += r.contribution[i];
    }
    if (!(sum > 0)) {
        // Display fallback is explicit and never invents measurement validity.
        r.fallback = true; r.noValidMeasurement = true; r.contribution[anchor] = 1; sum = 1;
    }
    float supportDenominator = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const float a = r.contribution[i] /= sum;
        if (a <= 0) continue;
        r.fallback |= values[i].anchorFallback;
        r.scene += a * (std::isfinite(values[i].scene) ? values[i].scene : 0);
        r.variance += a * a * (std::isfinite(values[i].variance) ? std::max(0.0f, values[i].variance) : 0);
        supportDenominator += a * a / std::max(1.0f, values[i].effectiveSamples);
    }
    const float gain = std::exp2(r.targetEv);
    r.scene *= gain; r.variance *= gain * gain;
    r.effectiveSamples = std::max(1.0f, 1 / std::max(1.0e-20f, supportDenominator));
    return r;
}
} // namespace Raw::Hdr
