#include "EditorNodeGraphCustomMaskSerialization.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace EditorNodeGraph {
namespace {

std::string CustomMaskReferenceModeToString(CustomMaskReferenceMode mode) {
    return mode == CustomMaskReferenceMode::GraphNode ? "GraphNode" : "CustomSize";
}

CustomMaskReferenceMode CustomMaskReferenceModeFromString(const std::string& value) {
    return value == "GraphNode" ? CustomMaskReferenceMode::GraphNode : CustomMaskReferenceMode::CustomSize;
}

std::string CustomMaskObjectTypeToString(CustomMaskObjectType type) {
    switch (type) {
        case CustomMaskObjectType::Rectangle: return "Rectangle";
        case CustomMaskObjectType::Ellipse: return "Ellipse";
        case CustomMaskObjectType::Polygon: return "Polygon";
        case CustomMaskObjectType::FreeformPath: return "FreeformPath";
    }
    return "Rectangle";
}

CustomMaskObjectType CustomMaskObjectTypeFromString(const std::string& value) {
    if (value == "Ellipse") return CustomMaskObjectType::Ellipse;
    if (value == "Polygon") return CustomMaskObjectType::Polygon;
    if (value == "FreeformPath") return CustomMaskObjectType::FreeformPath;
    return CustomMaskObjectType::Rectangle;
}

std::string CustomMaskOperationToString(CustomMaskOperation operation) {
    switch (operation) {
        case CustomMaskOperation::Add: return "Add";
        case CustomMaskOperation::Subtract: return "Subtract";
        case CustomMaskOperation::Intersect: return "Intersect";
        case CustomMaskOperation::Exclude: return "Exclude";
    }
    return "Add";
}

CustomMaskOperation CustomMaskOperationFromString(const std::string& value) {
    if (value == "Subtract") return CustomMaskOperation::Subtract;
    if (value == "Intersect") return CustomMaskOperation::Intersect;
    if (value == "Exclude") return CustomMaskOperation::Exclude;
    return CustomMaskOperation::Add;
}

std::string CustomMaskToolToString(CustomMaskTool tool) {
    switch (tool) {
        case CustomMaskTool::Brush: return "Brush";
        case CustomMaskTool::Erase: return "Erase";
        case CustomMaskTool::Select: return "Select";
        case CustomMaskTool::Rectangle: return "Rectangle";
        case CustomMaskTool::Ellipse: return "Ellipse";
        case CustomMaskTool::Polygon: return "Polygon";
        case CustomMaskTool::FreeformPath: return "FreeformPath";
    }
    return "Brush";
}

CustomMaskTool CustomMaskToolFromString(const std::string& value) {
    if (value == "Erase") return CustomMaskTool::Erase;
    if (value == "Select") return CustomMaskTool::Select;
    if (value == "Rectangle") return CustomMaskTool::Rectangle;
    if (value == "Ellipse") return CustomMaskTool::Ellipse;
    if (value == "Polygon") return CustomMaskTool::Polygon;
    if (value == "FreeformPath") return CustomMaskTool::FreeformPath;
    return CustomMaskTool::Brush;
}

std::vector<unsigned char> EncodeCustomMaskRasterU16(const std::vector<float>& raster) {
    std::vector<unsigned char> bytes;
    bytes.resize(raster.size() * 2);
    for (std::size_t i = 0; i < raster.size(); ++i) {
        const float clamped = std::clamp(raster[i], 0.0f, 1.0f);
        const auto value = static_cast<std::uint16_t>(std::lround(clamped * 65535.0f));
        bytes[i * 2 + 0] = static_cast<unsigned char>(value & 0xffu);
        bytes[i * 2 + 1] = static_cast<unsigned char>((value >> 8) & 0xffu);
    }
    return bytes;
}

std::vector<float> DecodeCustomMaskRasterU16(
    const nlohmann::json& encoded,
    int width,
    int height) {
    if (!encoded.is_binary()) {
        return {};
    }
    const std::size_t expected =
        static_cast<std::size_t>(std::max(0, width)) * static_cast<std::size_t>(std::max(0, height));
    const auto& bytes = encoded.get_binary();
    if (expected == 0 || bytes.empty()) {
        return {};
    }
    if (expected > std::numeric_limits<std::size_t>::max() / 2u ||
        bytes.size() != expected * 2u) {
        return {};
    }

    std::vector<float> raster;
    try {
        raster.resize(expected);
    } catch (const std::bad_alloc&) {
        return {};
    } catch (const std::length_error&) {
        return {};
    }
    for (std::size_t i = 0; i < expected; ++i) {
        const std::uint16_t value =
            static_cast<std::uint16_t>(bytes[i * 2 + 0]) |
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[i * 2 + 1]) << 8);
        raster[i] = static_cast<float>(value) / 65535.0f;
    }
    return raster;
}

nlohmann::json SerializeCustomMaskObject(const CustomMaskObject& object) {
    nlohmann::json points = nlohmann::json::array();
    for (const Vec2& point : object.points) {
        points.push_back({ { "x", point.x }, { "y", point.y } });
    }

    return {
        { "id", object.id },
        { "type", CustomMaskObjectTypeToString(object.type) },
        { "operation", CustomMaskOperationToString(object.operation) },
        { "points", std::move(points) },
        { "enabled", object.enabled },
        { "invert", object.invert },
        { "strength", object.strength },
        { "feather", object.feather },
        { "blur", object.blur }
    };
}

CustomMaskObject DeserializeCustomMaskObject(const nlohmann::json& value) {
    CustomMaskObject object;
    if (!value.is_object()) {
        return object;
    }
    object.id = value.value("id", object.id);
    object.type = CustomMaskObjectTypeFromString(value.value("type", std::string("Rectangle")));
    object.operation = CustomMaskOperationFromString(value.value("operation", std::string("Add")));
    object.enabled = value.value("enabled", object.enabled);
    object.invert = value.value("invert", object.invert);
    const auto finiteOr = [](float candidate, float fallback) {
        return std::isfinite(candidate) ? candidate : fallback;
    };
    object.strength = std::clamp(
        finiteOr(value.value("strength", object.strength), 1.0f),
        0.0f,
        1.0f);
    object.feather = std::clamp(
        finiteOr(value.value("feather", object.feather), 0.0f),
        0.0f,
        1.0f);
    object.blur = std::clamp(
        finiteOr(value.value("blur", object.blur), 0.0f),
        0.0f,
        static_cast<float>(kMaximumCustomMaskDimension));
    const bool legacyDefaultShapeFeather =
        value.contains("feather") &&
        std::abs(object.feather - 0.02f) <= 0.00001f &&
        (object.type == CustomMaskObjectType::Rectangle ||
         object.type == CustomMaskObjectType::Ellipse ||
         object.type == CustomMaskObjectType::Polygon) &&
        object.blur <= 0.00001f;
    if (legacyDefaultShapeFeather) {
        object.feather = 0.0f;
    }
    const nlohmann::json points = value.value("points", nlohmann::json::array());
    if (points.is_array()) {
        for (const nlohmann::json& pointJson : points) {
            if (object.points.size() >=
                kMaximumCustomMaskPointsPerObject) {
                break;
            }
            if (!pointJson.is_object()) continue;
            Vec2 point;
            point.x = std::clamp(
                finiteOr(pointJson.value("x", 0.0f), 0.0f),
                0.0f,
                1.0f);
            point.y = std::clamp(
                finiteOr(pointJson.value("y", 0.0f), 0.0f),
                0.0f,
                1.0f);
            object.points.push_back(point);
        }
    }
    return object;
}

} // namespace

nlohmann::json SerializeCustomMaskPayload(const CustomMaskPayload& payload) {
    nlohmann::json objects = nlohmann::json::array();
    for (const CustomMaskObject& object : payload.objects) {
        objects.push_back(SerializeCustomMaskObject(object));
    }

    return {
        { "schemaVersion", payload.schemaVersion },
        { "referenceMode", CustomMaskReferenceModeToString(payload.referenceMode) },
        { "referenceNodeId", payload.referenceNodeId },
        { "referenceSocketId", payload.referenceSocketId },
        { "width", payload.width },
        { "height", payload.height },
        { "aspectLocked", payload.aspectLocked },
        { "rasterFormat", "u16le-normalized" },
        { "rasterLayer", nlohmann::json::binary(EncodeCustomMaskRasterU16(payload.rasterLayer)) },
        { "objects", std::move(objects) },
        { "nextObjectId", payload.nextObjectId },
        { "globalOps", {
            { "invert", payload.invert },
            { "blurRadius", payload.blurRadius },
            { "expandContract", payload.expandContract }
        } },
        { "editor", {
            { "activeTool", CustomMaskToolToString(payload.activeTool) },
            { "brushSize", payload.brushSize },
            { "brushSoftness", payload.brushSoftness },
            { "brushOpacity", payload.brushOpacity },
            { "showCanvasReferenceImage", payload.showCanvasReferenceImage },
            { "showCanvasMaskImpact", payload.showCanvasMaskImpact },
            { "showCanvasMaskStrength", payload.showCanvasMaskStrength },
            { "selectedObjectId", payload.selectedObjectId }
        } }
    };
}

CustomMaskPayload DeserializeCustomMaskPayload(const nlohmann::json& value) {
    CustomMaskPayload payload;
    if (!value.is_object()) {
        payload.rasterLayer.assign(
            static_cast<std::size_t>(payload.width) * static_cast<std::size_t>(payload.height),
            0.0f);
        return payload;
    }

    payload.schemaVersion = value.value("schemaVersion", payload.schemaVersion);
    payload.referenceMode = CustomMaskReferenceModeFromString(value.value("referenceMode", std::string("CustomSize")));
    payload.referenceNodeId = value.value("referenceNodeId", payload.referenceNodeId);
    payload.referenceSocketId = value.value("referenceSocketId", payload.referenceSocketId);
    payload.width = std::clamp(
        value.value("width", payload.width),
        1,
        kMaximumCustomMaskDimension);
    payload.height = std::clamp(
        value.value("height", payload.height),
        1,
        kMaximumCustomMaskDimension);
    payload.aspectLocked = value.value("aspectLocked", payload.aspectLocked);
    const auto rasterIt = value.find("rasterLayer");
    payload.rasterLayer = rasterIt != value.end()
        ? DecodeCustomMaskRasterU16(*rasterIt, payload.width, payload.height)
        : std::vector<float>{};

    const nlohmann::json objects = value.value("objects", nlohmann::json::array());
    if (objects.is_array()) {
        for (const nlohmann::json& objectJson : objects) {
            if (payload.objects.size() >=
                kMaximumCustomMaskObjectCount) {
                break;
            }
            payload.objects.push_back(DeserializeCustomMaskObject(objectJson));
        }
    }
    constexpr int maximumSafeObjectId =
        std::numeric_limits<int>::max() - 65536;
    std::unordered_set<int> usedObjectIds;
    usedObjectIds.reserve(payload.objects.size());
    int nextAvailableObjectId = 1;
    for (CustomMaskObject& object : payload.objects) {
        if (object.id <= 0 ||
            object.id > maximumSafeObjectId ||
            usedObjectIds.count(object.id) != 0) {
            while (usedObjectIds.count(nextAvailableObjectId) != 0 &&
                   nextAvailableObjectId < maximumSafeObjectId) {
                ++nextAvailableObjectId;
            }
            object.id = nextAvailableObjectId;
        }
        usedObjectIds.insert(object.id);
        if (object.id >= nextAvailableObjectId &&
            object.id < maximumSafeObjectId) {
            nextAvailableObjectId = object.id + 1;
        }
    }
    payload.nextObjectId = std::clamp(
        value.value("nextObjectId", payload.nextObjectId),
        1,
        maximumSafeObjectId);
    payload.nextObjectId =
        std::max(payload.nextObjectId, nextAvailableObjectId);

    const nlohmann::json globalOps = value.value("globalOps", nlohmann::json::object());
    if (globalOps.is_object()) {
        payload.invert = globalOps.value("invert", payload.invert);
        payload.blurRadius = globalOps.value("blurRadius", payload.blurRadius);
        payload.expandContract = globalOps.value("expandContract", payload.expandContract);
    }

    const nlohmann::json editor = value.value("editor", nlohmann::json::object());
    if (editor.is_object()) {
        payload.activeTool = CustomMaskToolFromString(editor.value("activeTool", std::string("Brush")));
        payload.brushSize = editor.value("brushSize", payload.brushSize);
        payload.brushSoftness = editor.value("brushSoftness", payload.brushSoftness);
        payload.brushOpacity = editor.value("brushOpacity", payload.brushOpacity);
        payload.showCanvasReferenceImage = editor.value("showCanvasReferenceImage", payload.showCanvasReferenceImage);
        payload.showCanvasMaskImpact = editor.value("showCanvasMaskImpact", payload.showCanvasMaskImpact);
        payload.showCanvasMaskStrength = editor.value("showCanvasMaskStrength", payload.showCanvasMaskStrength);
        payload.selectedObjectId = editor.value("selectedObjectId", payload.selectedObjectId);
    }

    const std::size_t expected =
        static_cast<std::size_t>(payload.width) * static_cast<std::size_t>(payload.height);
    if (!payload.rasterLayer.empty() &&
        payload.rasterLayer.size() != expected) {
        std::vector<float>().swap(payload.rasterLayer);
    }
    return payload;
}

} // namespace EditorNodeGraph
