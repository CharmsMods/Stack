#include "EditorNodeGraphDevelopSerialization.h"

#include <algorithm>
#include <string>
#include <utility>

namespace EditorNodeGraph {
namespace {

nlohmann::json SerializeDevelopSubjectImportanceRegion(const DevelopSubjectImportanceRegion& region) {
    return {
        { "id", region.id },
        { "mode", DevelopSubjectImportanceModeStableString(region.mode) },
        { "enabled", region.enabled },
        { "centerX", region.centerX },
        { "centerY", region.centerY },
        { "radiusX", region.radiusX },
        { "radiusY", region.radiusY },
        { "feather", region.feather },
        { "strength", region.strength }
    };
}

DevelopSubjectImportanceRegion DeserializeDevelopSubjectImportanceRegion(const nlohmann::json& value) {
    DevelopSubjectImportanceRegion region;
    if (!value.is_object()) {
        return region;
    }
    region.id = value.value("id", region.id);
    region.mode = DevelopSubjectImportanceModeFromStableString(
        value.value("mode", std::string("Important")));
    region.enabled = value.value("enabled", region.enabled);
    region.centerX = std::clamp(value.value("centerX", region.centerX), 0.0f, 1.0f);
    region.centerY = std::clamp(value.value("centerY", region.centerY), 0.0f, 1.0f);
    region.radiusX = std::clamp(value.value("radiusX", region.radiusX), 0.01f, 1.0f);
    region.radiusY = std::clamp(value.value("radiusY", region.radiusY), 0.01f, 1.0f);
    region.feather = std::clamp(value.value("feather", region.feather), 0.0f, 1.0f);
    region.strength = std::clamp(value.value("strength", region.strength), 0.0f, 1.0f);
    return region;
}

nlohmann::json SerializeDevelopSubjectImportanceStrokePoint(const DevelopSubjectImportanceStrokePoint& point) {
    return {
        { "x", point.x },
        { "y", point.y }
    };
}

DevelopSubjectImportanceStrokePoint DeserializeDevelopSubjectImportanceStrokePoint(const nlohmann::json& value) {
    DevelopSubjectImportanceStrokePoint point;
    if (!value.is_object()) {
        return point;
    }
    point.x = std::clamp(value.value("x", point.x), 0.0f, 1.0f);
    point.y = std::clamp(value.value("y", point.y), 0.0f, 1.0f);
    return point;
}

nlohmann::json SerializeDevelopSubjectImportanceStroke(const DevelopSubjectImportanceStroke& stroke) {
    nlohmann::json points = nlohmann::json::array();
    for (const DevelopSubjectImportanceStrokePoint& point : stroke.points) {
        points.push_back(SerializeDevelopSubjectImportanceStrokePoint(point));
    }
    return {
        { "id", stroke.id },
        { "mode", DevelopSubjectImportanceModeStableString(stroke.mode) },
        { "enabled", stroke.enabled },
        { "subtract", stroke.subtract },
        { "radius", stroke.radius },
        { "feather", stroke.feather },
        { "strength", stroke.strength },
        { "points", std::move(points) }
    };
}

DevelopSubjectImportanceStroke DeserializeDevelopSubjectImportanceStroke(const nlohmann::json& value) {
    DevelopSubjectImportanceStroke stroke;
    if (!value.is_object()) {
        return stroke;
    }
    stroke.id = value.value("id", stroke.id);
    stroke.mode = DevelopSubjectImportanceModeFromStableString(
        value.value("mode", std::string("Important")));
    stroke.enabled = value.value("enabled", stroke.enabled);
    stroke.subtract = value.value("subtract", stroke.subtract);
    stroke.radius = std::clamp(value.value("radius", stroke.radius), 0.005f, 0.25f);
    stroke.feather = std::clamp(value.value("feather", stroke.feather), 0.0f, 1.0f);
    stroke.strength = std::clamp(value.value("strength", stroke.strength), 0.0f, 1.0f);
    const nlohmann::json points = value.value("points", nlohmann::json::array());
    if (points.is_array()) {
        for (const nlohmann::json& pointJson : points) {
            stroke.points.push_back(DeserializeDevelopSubjectImportanceStrokePoint(pointJson));
            if (stroke.points.size() >= 192) {
                break;
            }
        }
    }
    return stroke;
}

} // namespace

nlohmann::json SerializeDevelopSubjectImportanceMap(const DevelopSubjectImportanceMap& map) {
    nlohmann::json regions = nlohmann::json::array();
    for (const DevelopSubjectImportanceRegion& region : map.regions) {
        regions.push_back(SerializeDevelopSubjectImportanceRegion(region));
    }
    nlohmann::json strokes = nlohmann::json::array();
    for (const DevelopSubjectImportanceStroke& stroke : map.strokes) {
        strokes.push_back(SerializeDevelopSubjectImportanceStroke(stroke));
    }
    return {
        { "schemaVersion", map.schemaVersion },
        { "enabled", map.enabled },
        { "showOverlay", map.showOverlay },
        { "overlayOpacity", map.overlayOpacity },
        { "showInterpretedMapOverlay", map.showInterpretedMapOverlay },
        { "interpretedMapOpacity", map.interpretedMapOpacity },
        { "showRefinedMapOverlay", map.showRefinedMapOverlay },
        { "refinedMapOpacity", map.refinedMapOpacity },
        { "brushEnabled", map.brushEnabled },
        { "brushSubtract", map.brushSubtract },
        { "brushMode", DevelopSubjectImportanceModeStableString(map.brushMode) },
        { "brushRadius", map.brushRadius },
        { "brushFeather", map.brushFeather },
        { "brushStrength", map.brushStrength },
        { "activeRegionId", map.activeRegionId },
        { "activeStrokeId", map.activeStrokeId },
        { "nextRegionId", map.nextRegionId },
        { "nextStrokeId", map.nextStrokeId },
        { "regions", std::move(regions) },
        { "strokes", std::move(strokes) }
    };
}

DevelopSubjectImportanceMap DeserializeDevelopSubjectImportanceMap(const nlohmann::json& value) {
    DevelopSubjectImportanceMap map;
    if (!value.is_object()) {
        return map;
    }
    map.schemaVersion = std::max(1, value.value("schemaVersion", map.schemaVersion));
    map.enabled = value.value("enabled", map.enabled);
    map.showOverlay = value.value("showOverlay", map.showOverlay);
    map.overlayOpacity = std::clamp(value.value("overlayOpacity", map.overlayOpacity), 0.05f, 1.0f);
    map.showInterpretedMapOverlay =
        value.value("showInterpretedMapOverlay", map.showInterpretedMapOverlay);
    map.interpretedMapOpacity =
        std::clamp(value.value("interpretedMapOpacity", map.interpretedMapOpacity), 0.05f, 1.0f);
    map.showRefinedMapOverlay =
        value.value("showRefinedMapOverlay", map.showRefinedMapOverlay);
    map.refinedMapOpacity =
        std::clamp(value.value("refinedMapOpacity", map.refinedMapOpacity), 0.05f, 1.0f);
    map.brushEnabled = value.value("brushEnabled", map.brushEnabled);
    map.brushSubtract = value.value("brushSubtract", map.brushSubtract);
    map.brushMode = DevelopSubjectImportanceModeFromStableString(
        value.value("brushMode", std::string("Important")));
    map.brushRadius = std::clamp(value.value("brushRadius", map.brushRadius), 0.005f, 0.25f);
    map.brushFeather = std::clamp(value.value("brushFeather", map.brushFeather), 0.0f, 1.0f);
    map.brushStrength = std::clamp(value.value("brushStrength", map.brushStrength), 0.0f, 1.0f);
    map.activeRegionId = std::max(0, value.value("activeRegionId", map.activeRegionId));
    map.activeStrokeId = std::max(0, value.value("activeStrokeId", map.activeStrokeId));
    map.nextRegionId = std::max(1, value.value("nextRegionId", map.nextRegionId));
    map.nextStrokeId = std::max(1, value.value("nextStrokeId", map.nextStrokeId));
    const nlohmann::json regions = value.value("regions", nlohmann::json::array());
    if (regions.is_array()) {
        for (const nlohmann::json& regionJson : regions) {
            map.regions.push_back(DeserializeDevelopSubjectImportanceRegion(regionJson));
            if (map.regions.size() >= 32) {
                break;
            }
        }
    }
    for (const DevelopSubjectImportanceRegion& region : map.regions) {
        map.nextRegionId = std::max(map.nextRegionId, region.id + 1);
    }
    const nlohmann::json strokes = value.value("strokes", nlohmann::json::array());
    if (strokes.is_array()) {
        for (const nlohmann::json& strokeJson : strokes) {
            map.strokes.push_back(DeserializeDevelopSubjectImportanceStroke(strokeJson));
            if (map.strokes.size() >= 128) {
                break;
            }
        }
    }
    for (const DevelopSubjectImportanceStroke& stroke : map.strokes) {
        map.nextStrokeId = std::max(map.nextStrokeId, stroke.id + 1);
    }
    if (map.regions.empty()) {
        map.activeRegionId = 0;
    } else {
        const auto activeIt = std::find_if(
            map.regions.begin(),
            map.regions.end(),
            [&](const DevelopSubjectImportanceRegion& region) {
                return region.id == map.activeRegionId;
            });
        if (activeIt == map.regions.end()) {
            map.activeRegionId = map.regions.front().id;
        }
    }
    if (map.strokes.empty()) {
        map.activeStrokeId = 0;
    } else {
        const auto activeIt = std::find_if(
            map.strokes.begin(),
            map.strokes.end(),
            [&](const DevelopSubjectImportanceStroke& stroke) {
                return stroke.id == map.activeStrokeId;
            });
        if (activeIt == map.strokes.end()) {
            map.activeStrokeId = map.strokes.back().id;
        }
    }
    return map;
}

} // namespace EditorNodeGraph
