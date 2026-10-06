#include "Raw/RawZoneArea.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Stack::RawRecipe {
namespace {
float Finite(float value, float fallback, float lo, float hi) {
    return std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
}
float Number(const nlohmann::json& j, const char* key, float fallback) {
    const auto p = j.find(key);
    return p != j.end() && p->is_number() ? p->get<float>() : fallback;
}
bool Boolean(const nlohmann::json& j, const char* key, bool fallback) {
    const auto p = j.find(key);
    return p != j.end() && p->is_boolean() ? p->get<bool>() : fallback;
}
std::string Text(const nlohmann::json& j, const char* key) {
    const auto p = j.find(key);
    return p != j.end() && p->is_string() ? p->get<std::string>() : std::string();
}
nlohmann::json Handle(const RawBezierHandleState& h) {
    return {{"strength", h.strength}, {"manual", h.manual}, {"offsetX", h.offsetX}, {"offsetY", h.offsetY}};
}
RawBezierHandleState ReadHandle(const nlohmann::json& j, const char* key) {
    RawBezierHandleState h;
    auto it = j.find(key);
    if (it == j.end() || !it->is_object()) return h;
    h.strength = Number(*it, "strength", 0.0f);
    h.manual = Boolean(*it, "manual", false);
    h.offsetX = Number(*it, "offsetX", 0.0f);
    h.offsetY = Number(*it, "offsetY", 0.0f);
    return h;
}
template<class T> void Hash(std::size_t& h, const T& v) {
    h ^= std::hash<T>{}(v) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
}
}

void SanitizeZoneAreas(std::vector<RawZoneArea>& areas) {
    if (areas.size() > kMaxZoneAreas) areas.resize(kMaxZoneAreas);
    std::unordered_set<std::string> ids;
    for (std::size_t i = 0; i < areas.size(); ++i) {
        auto& a = areas[i];
        if (a.id.empty() || ids.count(a.id)) {
            a.id = "area-" + std::to_string(i + 1);
            while (ids.count(a.id)) a.id += "-copy";
        }
        ids.insert(a.id);
        if (a.name.size() > 128) a.name.resize(128);
        a.offsetEv = Finite(a.offsetEv, 0, -16, 16);
        a.sourceAspect = Finite(a.sourceAspect, 1, 0.01f, 100);
        for (auto& p : a.points) {
            p.ev = Finite(p.ev, 0, -32, 32);
            p.deltaEv = Finite(p.deltaEv, 0, -16, 16);
            for (auto* h : {&p.incoming, &p.outgoing}) {
                h->strength = Finite(h->strength, 0, 0, 1.5f);
                h->offsetX = Finite(h->offsetX, 0, -64, 64);
                h->offsetY = Finite(h->offsetY, 0, -32, 32);
            }
        }
        std::stable_sort(a.points.begin(), a.points.end(), [](const auto& x, const auto& y) { return x.ev < y.ev; });
        a.points.erase(std::unique(a.points.begin(), a.points.end(), [](const auto& x, const auto& y) {
            return std::abs(x.ev - y.ev) < 0.00001f;
        }), a.points.end());
        if (a.points.empty()) a.points = {{-8, 0}, {6, 0}};
        if (a.points.size() == 1) {
            auto p = a.points.front();
            p.ev = p.ev < 31.99f ? p.ev + 0.01f : p.ev - 0.01f;
            a.points.push_back(p);
            std::sort(a.points.begin(), a.points.end(), [](const auto& x, const auto& y) { return x.ev < y.ev; });
        }
        if (a.points.size() > kMaxRawPointCurvePoints) a.points.resize(kMaxRawPointCurvePoints);
        if (a.strokes.size() > kMaxZoneStrokes) a.strokes.resize(kMaxZoneStrokes);
        for (auto& s : a.strokes) {
            s.radius = Finite(s.radius, 0.06f, 0.00001f, 2);
            s.softness = Finite(s.softness, 1, 0, 1);
            s.opacity = Finite(s.opacity, 1, 0, 1);
            s.edgeSensitivity = Finite(s.edgeSensitivity, .65f, 0, 1);
            if (s.path.size() > kMaxZoneStrokePoints) s.path.resize(kMaxZoneStrokePoints);
            for (auto& p : s.path) {
                p.u = Finite(p.u, 0.5f, 0, 1);
                p.v = Finite(p.v, 0.5f, 0, 1);
            }
        }
    }
}

nlohmann::json SerializeZoneAreas(const std::vector<RawZoneArea>& areas) {
    auto result = nlohmann::json::array();
    for (const auto& a : areas) {
        auto points = nlohmann::json::array(), strokes = nlohmann::json::array();
        for (const auto& p : a.points) points.push_back({{"ev", p.ev}, {"deltaEv", p.deltaEv},
            {"incoming", Handle(p.incoming)}, {"outgoing", Handle(p.outgoing)}});
        for (const auto& s : a.strokes) {
            auto path = nlohmann::json::array();
            for (const auto& p : s.path) path.push_back({p.u, p.v});
            strokes.push_back({{"erase", s.erase}, {"radius", s.radius}, {"softness", s.softness},
                {"opacity", s.opacity}, {"path", std::move(path)}});
            if (s.followEdges || s.edgeSensitivity != .65f) {
                strokes.back()["followEdges"] = s.followEdges;
                strokes.back()["edgeSensitivity"] = s.edgeSensitivity;
            }
        }
        result.push_back({{"id", a.id}, {"name", a.name}, {"enabled", a.enabled},
            {"offsetEv", a.offsetEv}, {"sourceAspect", a.sourceAspect},
            {"points", std::move(points)}, {"strokes", std::move(strokes)}});
    }
    return result;
}

std::vector<RawZoneArea> DeserializeZoneAreas(const nlohmann::json& json) {
    std::vector<RawZoneArea> areas;
    if (!json.is_array()) return areas;
    for (const auto& j : json) {
        if (!j.is_object()) continue;
        if (areas.size() == kMaxZoneAreas) break;
        RawZoneArea a;
        a.id = Text(j, "id"); a.name = Text(j, "name");
        a.enabled = Boolean(j, "enabled", true);
        a.offsetEv = Number(j, "offsetEv", 0); a.sourceAspect = Number(j, "sourceAspect", 1);
        if (j.contains("points") && j["points"].is_array()) {
            a.points.clear();
            for (const auto& p : j["points"]) {
                if (!p.is_object()) continue;
                a.points.push_back({Number(p, "ev", 0), Number(p, "deltaEv", 0), ReadHandle(p, "incoming"), ReadHandle(p, "outgoing")});
                if (a.points.size() == kMaxRawPointCurvePoints) break;
            }
        }
        if (j.contains("strokes") && j["strokes"].is_array()) for (const auto& s : j["strokes"]) {
            if (!s.is_object()) continue;
            RawZoneBrushStroke stroke;
            stroke.erase = Boolean(s, "erase", false); stroke.radius = Number(s, "radius", 0.06f);
            stroke.softness = Number(s, "softness", 1); stroke.opacity = Number(s, "opacity", 1);
            stroke.followEdges = Boolean(s, "followEdges", false);
            stroke.edgeSensitivity = Number(s, "edgeSensitivity", .65f);
            if (s.contains("path") && s["path"].is_array()) for (const auto& p : s["path"]) {
                if (p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number())
                    stroke.path.push_back({p[0].get<float>(), p[1].get<float>()});
                if (stroke.path.size() == kMaxZoneStrokePoints) break;
            }
            a.strokes.push_back(std::move(stroke));
            if (a.strokes.size() == kMaxZoneStrokes) break;
        }
        areas.push_back(std::move(a));
    }
    SanitizeZoneAreas(areas);
    return areas;
}

bool EqualZoneAreas(const std::vector<RawZoneArea>& a,const std::vector<RawZoneArea>& b) {
    const auto handle=[](const auto& x,const auto& y) {
        return x.manual==y.manual && x.strength==y.strength && x.offsetX==y.offsetX && x.offsetY==y.offsetY;
    };
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[&](const auto& x,const auto& y) {
        if (x.id!=y.id || x.name!=y.name || x.enabled!=y.enabled || x.offsetEv!=y.offsetEv ||
            x.sourceAspect!=y.sourceAspect || x.points.size()!=y.points.size() || x.strokes.size()!=y.strokes.size()) return false;
        if (!std::equal(x.points.begin(),x.points.end(),y.points.begin(),[&](const auto& p,const auto& q) {
            return p.ev==q.ev && p.deltaEv==q.deltaEv && handle(p.incoming,q.incoming) && handle(p.outgoing,q.outgoing);
        })) return false;
        return std::equal(x.strokes.begin(),x.strokes.end(),y.strokes.begin(),[](const auto& p,const auto& q) {
            return p.erase==q.erase && p.radius==q.radius && p.softness==q.softness && p.opacity==q.opacity &&
                p.followEdges==q.followEdges && p.edgeSensitivity==q.edgeSensitivity && p.path.size()==q.path.size() &&
                std::equal(p.path.begin(),p.path.end(),q.path.begin(),[](const auto& u,const auto& v) {return u.u==v.u && u.v==v.v;});
        });
    });
}
std::size_t ZoneStrokeFingerprint(const RawZoneBrushStroke& s) {
    std::size_t h=1;
    Hash(h, s.erase); Hash(h, s.radius); Hash(h, s.softness); Hash(h, s.opacity); Hash(h, s.path.size());
    if (s.followEdges) { Hash(h, 0x45444745); Hash(h, s.edgeSensitivity); }
    for (const auto& p : s.path) { Hash(h, p.u); Hash(h, p.v); }
    return h;
}
std::size_t ZoneAreaMaskFingerprint(const RawZoneArea& a) {
    std::size_t h = 1;
    Hash(h, a.sourceAspect); Hash(h, a.strokes.size());
    for (const auto& s : a.strokes) {
        Hash(h, s.erase); Hash(h, s.radius); Hash(h, s.softness); Hash(h, s.opacity); Hash(h, s.path.size());
        if (s.followEdges) { Hash(h, 0x45444745); Hash(h, s.edgeSensitivity); }
        for (const auto& p : s.path) { Hash(h, p.u); Hash(h, p.v); }
    }
    return h;
}
std::size_t ZoneAreaGainFingerprint(const RawZoneArea& a) {
    std::size_t h = ZoneAreaMaskFingerprint(a);
    Hash(h, a.id); Hash(h, a.enabled); Hash(h, a.offsetEv);
    for (const auto& p : a.points) {
        Hash(h, p.ev); Hash(h, p.deltaEv);
        for (const auto* s : {&p.incoming, &p.outgoing}) {
            Hash(h, s->manual); Hash(h, s->strength); Hash(h, s->offsetX); Hash(h, s->offsetY);
        }
    }
    return h;
}
} // namespace Stack::RawRecipe
