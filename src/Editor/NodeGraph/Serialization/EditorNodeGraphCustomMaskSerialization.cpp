#include "EditorNodeGraphCustomMaskSerialization.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace EditorNodeGraph {
namespace {

std::vector<unsigned char> ReadBinaryJson(const nlohmann::json& value) {
    if (!value.is_binary()) {
        return {};
    }

    const auto& binaryValue = value.get_binary();
    return std::vector<unsigned char>(binaryValue.begin(), binaryValue.end());
}

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

std::vector<float> DecodeCustomMaskRasterU16(const std::vector<unsigned char>& bytes, int width, int height) {
    const std::size_t expected =
        static_cast<std::size_t>(std::max(0, width)) * static_cast<std::size_t>(std::max(0, height));
    std::vector<float> raster(expected, 0.0f);
    const std::size_t count = std::min(expected, bytes.size() / 2);
    for (std::size_t i = 0; i < count; ++i) {
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
    object.strength = value.value("strength", object.strength);
    object.feather = value.value("feather", object.feather);
    object.blur = value.value("blur", object.blur);
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
            if (!pointJson.is_object()) continue;
            Vec2 point;
            point.x = pointJson.value("x", 0.0f);
            point.y = pointJson.value("y", 0.0f);
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
    payload.width = std::clamp(value.value("width", payload.width), 1, 8192);
    payload.height = std::clamp(value.value("height", payload.height), 1, 8192);
    payload.aspectLocked = value.value("aspectLocked", payload.aspectLocked);
    payload.rasterLayer = DecodeCustomMaskRasterU16(ReadBinaryJson(value.value("rasterLayer", nlohmann::json())), payload.width, payload.height);

    const nlohmann::json objects = value.value("objects", nlohmann::json::array());
    if (objects.is_array()) {
        for (const nlohmann::json& objectJson : objects) {
            payload.objects.push_back(DeserializeCustomMaskObject(objectJson));
        }
    }
    payload.nextObjectId = value.value("nextObjectId", payload.nextObjectId);
    for (const CustomMaskObject& object : payload.objects) {
        payload.nextObjectId = std::max(payload.nextObjectId, object.id + 1);
    }

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
    if (payload.rasterLayer.size() != expected) {
        payload.rasterLayer.assign(expected, 0.0f);
    }
    return payload;
}

} // namespace EditorNodeGraph
