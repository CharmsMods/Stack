#include "Editor/EditorModule.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

const char* CustomMaskToolLabel(EditorNodeGraph::CustomMaskTool tool) {
    switch (tool) {
        case EditorNodeGraph::CustomMaskTool::Brush: return "Brush";
        case EditorNodeGraph::CustomMaskTool::Erase: return "Erase";
        case EditorNodeGraph::CustomMaskTool::Select: return "Select";
        case EditorNodeGraph::CustomMaskTool::Rectangle: return "Rectangle";
        case EditorNodeGraph::CustomMaskTool::Ellipse: return "Ellipse";
        case EditorNodeGraph::CustomMaskTool::Polygon: return "Polygon";
        case EditorNodeGraph::CustomMaskTool::FreeformPath: return "Freeform Path";
    }
    return "Brush";
}

const char* CustomMaskObjectTypeLabel(EditorNodeGraph::CustomMaskObjectType type) {
    switch (type) {
        case EditorNodeGraph::CustomMaskObjectType::Rectangle: return "Rectangle";
        case EditorNodeGraph::CustomMaskObjectType::Ellipse: return "Ellipse";
        case EditorNodeGraph::CustomMaskObjectType::Polygon: return "Polygon";
        case EditorNodeGraph::CustomMaskObjectType::FreeformPath: return "Freeform Path";
    }
    return "Rectangle";
}

const char* CustomMaskOperationLabel(EditorNodeGraph::CustomMaskOperation operation) {
    switch (operation) {
        case EditorNodeGraph::CustomMaskOperation::Add: return "Add";
        case EditorNodeGraph::CustomMaskOperation::Subtract: return "Subtract";
        case EditorNodeGraph::CustomMaskOperation::Intersect: return "Intersect";
        case EditorNodeGraph::CustomMaskOperation::Exclude: return "Exclude";
    }
    return "Add";
}

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float SmoothStepFloat(float edge0, float edge1, float value) {
    const float denom = std::max(0.000001f, edge1 - edge0);
    const float t = std::clamp((value - edge0) / denom, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void EnsureCustomMaskRaster(EditorNodeGraph::CustomMaskPayload& payload) {
    payload.width = std::clamp(payload.width, 1, 8192);
    payload.height = std::clamp(payload.height, 1, 8192);
    const std::size_t expected =
        static_cast<std::size_t>(payload.width) * static_cast<std::size_t>(payload.height);
    if (payload.rasterLayer.size() != expected) {
        payload.rasterLayer.assign(expected, 0.0f);
    }
}

void ResizeCustomMaskRaster(EditorNodeGraph::CustomMaskPayload& payload, int newWidth, int newHeight) {
    newWidth = std::clamp(newWidth, 1, 8192);
    newHeight = std::clamp(newHeight, 1, 8192);
    EnsureCustomMaskRaster(payload);
    if (newWidth == payload.width && newHeight == payload.height) {
        return;
    }

    std::vector<float> resized(static_cast<std::size_t>(newWidth) * static_cast<std::size_t>(newHeight), 0.0f);
    for (int y = 0; y < newHeight; ++y) {
        const float v = newHeight > 1 ? static_cast<float>(y) / static_cast<float>(newHeight - 1) : 0.0f;
        const int srcY = std::clamp(static_cast<int>(std::round(v * static_cast<float>(payload.height - 1))), 0, payload.height - 1);
        for (int x = 0; x < newWidth; ++x) {
            const float u = newWidth > 1 ? static_cast<float>(x) / static_cast<float>(newWidth - 1) : 0.0f;
            const int srcX = std::clamp(static_cast<int>(std::round(u * static_cast<float>(payload.width - 1))), 0, payload.width - 1);
            resized[static_cast<std::size_t>(y) * static_cast<std::size_t>(newWidth) + static_cast<std::size_t>(x)] =
                payload.rasterLayer[static_cast<std::size_t>(srcY) * static_cast<std::size_t>(payload.width) + static_cast<std::size_t>(srcX)];
        }
    }
    payload.width = newWidth;
    payload.height = newHeight;
    payload.rasterLayer = std::move(resized);
}

void ApplyBrush(EditorNodeGraph::CustomMaskPayload& payload, float u, float v, bool erase) {
    EnsureCustomMaskRaster(payload);
    const float radius = std::max(1.0f, payload.brushSize) * 0.5f;
    const float centerX = u * static_cast<float>(payload.width - 1);
    const float centerY = v * static_cast<float>(payload.height - 1);
    const int minX = std::max(0, static_cast<int>(std::floor(centerX - radius)));
    const int maxX = std::min(payload.width - 1, static_cast<int>(std::ceil(centerX + radius)));
    const int minY = std::max(0, static_cast<int>(std::floor(centerY - radius)));
    const int maxY = std::min(payload.height - 1, static_cast<int>(std::ceil(centerY + radius)));
    const float inner = radius * (1.0f - std::clamp(payload.brushSoftness, 0.0f, 1.0f));
    const float opacity = std::clamp(payload.brushOpacity, 0.0f, 1.0f);

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float dx = static_cast<float>(x) - centerX;
            const float dy = static_cast<float>(y) - centerY;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d > radius) {
                continue;
            }
            const float falloff = d <= inner
                ? 1.0f
                : 1.0f - std::clamp((d - inner) / std::max(0.0001f, radius - inner), 0.0f, 1.0f);
            float& value = payload.rasterLayer[static_cast<std::size_t>(y) * static_cast<std::size_t>(payload.width) + static_cast<std::size_t>(x)];
            const float target = erase ? 0.0f : 1.0f;
            value = Clamp01(value + (target - value) * opacity * falloff);
        }
    }
}

void DrawCustomMaskBrushPreview(
    ImDrawList* drawList,
    const ImVec2& center,
    float radius,
    float softness,
    float opacity,
    bool erase) {
    if (!drawList || radius <= 0.5f) {
        return;
    }

    const float safeOpacity = std::clamp(opacity, 0.03f, 1.0f);
    const float safeSoftness = std::clamp(softness, 0.0f, 1.0f);
    const float innerRadius = std::max(0.0f, radius * (1.0f - safeSoftness));
    const ImVec4 paintColor = erase
        ? ImVec4(0.03f, 0.035f, 0.04f, 1.0f)
        : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    const ImU32 outline = erase
        ? IM_COL32(255, 122, 164, 230)
        : IM_COL32(120, 190, 255, 230);
    const ImU32 falloffOutline = erase
        ? IM_COL32(255, 122, 164, 105)
        : IM_COL32(120, 190, 255, 105);

    const int steps = std::clamp(static_cast<int>(std::ceil(radius / 7.0f)), 8, 28);
    for (int i = steps; i >= 1; --i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float ringRadius = std::max(0.5f, radius * t);
        float strength = 1.0f;
        if (ringRadius > innerRadius) {
            strength = 1.0f - std::clamp((ringRadius - innerRadius) / std::max(0.001f, radius - innerRadius), 0.0f, 1.0f);
        }
        const float alpha = safeOpacity * (0.10f + 0.42f * strength);
        ImVec4 color = paintColor;
        color.w = alpha;
        drawList->AddCircleFilled(center, ringRadius, ImGui::ColorConvertFloat4ToU32(color), 64);
    }

    if (innerRadius > 1.0f && innerRadius < radius - 1.0f) {
        drawList->AddCircle(center, innerRadius, falloffOutline, 64, 1.0f);
    }
    drawList->AddCircle(center, radius, outline, 96, 1.6f);
}

EditorNodeGraph::CustomMaskObject* FindCustomMaskObject(EditorNodeGraph::CustomMaskPayload& payload, int id) {
    for (EditorNodeGraph::CustomMaskObject& object : payload.objects) {
        if (object.id == id) {
            return &object;
        }
    }
    return nullptr;
}

float CombinePreviewMaskValue(float base, float value, EditorNodeGraph::CustomMaskOperation operation) {
    switch (operation) {
        case EditorNodeGraph::CustomMaskOperation::Add: return std::max(base, value);
        case EditorNodeGraph::CustomMaskOperation::Subtract: return base * (1.0f - value);
        case EditorNodeGraph::CustomMaskOperation::Intersect: return base * value;
        case EditorNodeGraph::CustomMaskOperation::Exclude: return std::abs(base - value);
    }
    return base;
}

bool PointInPreviewPolygon(const std::vector<EditorNodeGraph::Vec2>& points, float u, float v) {
    if (points.size() < 3) {
        return false;
    }
    bool inside = false;
    for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
        const bool intersects = ((points[i].y > v) != (points[j].y > v)) &&
            (u < (points[j].x - points[i].x) * (v - points[i].y) / std::max(0.000001f, points[j].y - points[i].y) + points[i].x);
        if (intersects) {
            inside = !inside;
        }
    }
    return inside;
}

float DistanceToPreviewSegment(float u, float v, const EditorNodeGraph::Vec2& a, const EditorNodeGraph::Vec2& b) {
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float denom = abx * abx + aby * aby;
    const float t = denom > 0.000001f
        ? std::clamp(((u - a.x) * abx + (v - a.y) * aby) / denom, 0.0f, 1.0f)
        : 0.0f;
    const float dx = u - (a.x + abx * t);
    const float dy = v - (a.y + aby * t);
    return std::sqrt(dx * dx + dy * dy);
}

float EvaluatePreviewObject(const EditorNodeGraph::CustomMaskObject& object, float u, float v, int width, int height) {
    if (!object.enabled || object.points.empty()) {
        return 0.0f;
    }
    float value = 0.0f;
    const float feather = std::max(0.0f, object.feather);
    if ((object.type == EditorNodeGraph::CustomMaskObjectType::Rectangle ||
         object.type == EditorNodeGraph::CustomMaskObjectType::Ellipse) &&
        object.points.size() >= 2) {
        const float minX = std::min(object.points[0].x, object.points[1].x);
        const float maxX = std::max(object.points[0].x, object.points[1].x);
        const float minY = std::min(object.points[0].y, object.points[1].y);
        const float maxY = std::max(object.points[0].y, object.points[1].y);
        if (maxX > minX && maxY > minY) {
            if (object.type == EditorNodeGraph::CustomMaskObjectType::Rectangle) {
                const float outsideX = std::max(std::max(minX - u, 0.0f), u - maxX);
                const float outsideY = std::max(std::max(minY - v, 0.0f), v - maxY);
                const float outside = std::sqrt(outsideX * outsideX + outsideY * outsideY);
                const float insideDist = std::min(std::min(u - minX, maxX - u), std::min(v - minY, maxY - v));
                const bool inside = u >= minX && u <= maxX && v >= minY && v <= maxY;
                if (inside) {
                    value = feather > 0.0f ? std::clamp(insideDist / feather, 0.0f, 1.0f) : 1.0f;
                } else {
                    value = feather > 0.0f ? 1.0f - std::clamp(outside / feather, 0.0f, 1.0f) : 0.0f;
                }
            } else {
                const float cx = (minX + maxX) * 0.5f;
                const float cy = (minY + maxY) * 0.5f;
                const float rx = std::max(0.0001f, (maxX - minX) * 0.5f);
                const float ry = std::max(0.0001f, (maxY - minY) * 0.5f);
                const float d = std::sqrt(((u - cx) * (u - cx)) / (rx * rx) + ((v - cy) * (v - cy)) / (ry * ry));
                const float normFeather = feather / std::max(0.0001f, std::min(rx, ry));
                value = normFeather > 0.0f
                    ? 1.0f - SmoothStepFloat(1.0f - normFeather, 1.0f + normFeather, d)
                    : (d <= 1.0f ? 1.0f : 0.0f);
            }
        }
    } else if (object.type == EditorNodeGraph::CustomMaskObjectType::Polygon) {
        value = PointInPreviewPolygon(object.points, u, v) ? 1.0f : 0.0f;
    } else if (object.type == EditorNodeGraph::CustomMaskObjectType::FreeformPath && object.points.size() >= 2) {
        float minDistance = 1.0f;
        for (std::size_t i = 1; i < object.points.size(); ++i) {
            minDistance = std::min(minDistance, DistanceToPreviewSegment(u, v, object.points[i - 1], object.points[i]));
        }
        const float radius = std::max(1.0f, object.blur) / static_cast<float>(std::max(1, std::max(width, height)));
        const float softness = std::max(feather, radius);
        value = softness > 0.0f
            ? 1.0f - std::clamp((minDistance - radius) / softness, 0.0f, 1.0f)
            : (minDistance <= radius ? 1.0f : 0.0f);
    }
    if (object.invert) {
        value = 1.0f - value;
    }
    return Clamp01(value * std::clamp(object.strength, 0.0f, 1.0f));
}

struct CustomMaskCanvasImage {
    const std::vector<unsigned char>* pixels = nullptr;
    int width = 0;
    int height = 0;
    int channels = 0;

    bool Valid() const {
        return pixels && !pixels->empty() && width > 0 && height > 0 && channels > 0;
    }
};

float SampleCanvasImageLuma(const CustomMaskCanvasImage& image, float u, float v) {
    if (!image.Valid()) {
        return 0.0f;
    }
    const int x = std::clamp(
        static_cast<int>(std::round(std::clamp(u, 0.0f, 1.0f) * static_cast<float>(image.width - 1))),
        0,
        image.width - 1);
    const int y = std::clamp(
        static_cast<int>(std::round((1.0f - std::clamp(v, 0.0f, 1.0f)) * static_cast<float>(image.height - 1))),
        0,
        image.height - 1);
    const std::size_t index =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) *
        static_cast<std::size_t>(image.channels);
    if (index >= image.pixels->size()) {
        return 0.0f;
    }

    const float r = static_cast<float>((*image.pixels)[index + 0]) / 255.0f;
    const float g = image.channels > 1 && index + 1 < image.pixels->size()
        ? static_cast<float>((*image.pixels)[index + 1]) / 255.0f
        : r;
    const float b = image.channels > 2 && index + 2 < image.pixels->size()
        ? static_cast<float>((*image.pixels)[index + 2]) / 255.0f
        : r;
    return Clamp01(0.2126f * r + 0.7152f * g + 0.0722f * b);
}

} // namespace

bool EditorModule::HasActiveCustomMaskOverlay() const {
    return GetActiveCustomMaskPayload() != nullptr;
}

const EditorNodeGraph::CustomMaskPayload* EditorModule::GetActiveCustomMaskPayload() const {
    if (m_ActiveSubWindow != EditorSubWindow::ComplexNode) {
        return nullptr;
    }
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(m_ActiveComplexNodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::CustomMask) {
        return nullptr;
    }
    return &node->customMask;
}

float EditorModule::SampleCustomMaskForPreview(const EditorNodeGraph::CustomMaskPayload& payload, float u, float v) const {
    const int width = std::max(1, payload.width);
    const int height = std::max(1, payload.height);
    const int x = std::clamp(static_cast<int>(std::round(std::clamp(u, 0.0f, 1.0f) * static_cast<float>(width - 1))), 0, width - 1);
    const int y = std::clamp(static_cast<int>(std::round(std::clamp(v, 0.0f, 1.0f) * static_cast<float>(height - 1))), 0, height - 1);
    float value = 0.0f;
    const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
    if (index < payload.rasterLayer.size()) {
        value = payload.rasterLayer[index];
    }
    for (const EditorNodeGraph::CustomMaskObject& object : payload.objects) {
        value = CombinePreviewMaskValue(
            value,
            EvaluatePreviewObject(object, std::clamp(u, 0.0f, 1.0f), std::clamp(v, 0.0f, 1.0f), width, height),
            object.operation);
    }
    if (payload.invert) {
        value = 1.0f - value;
    }
    return Clamp01(value);
}

void EditorModule::RenderCustomMaskControls(EditorNodeGraph::Node& node, float controlWidth, bool advanced) {
    (void)advanced;
    (void)controlWidth;
    if (node.kind != EditorNodeGraph::NodeKind::CustomMask) {
        return;
    }

    EditorNodeGraph::CustomMaskPayload& payload = node.customMask;
    EnsureCustomMaskRaster(payload);

    auto pushUndoSnapshot = [&](const EditorNodeGraph::CustomMaskPayload& snapshot) {
        auto& undo = m_CustomMaskUndoStacks[node.id];
        undo.push_back(snapshot);
        while (undo.size() > 64) {
            undo.pop_front();
        }
        m_CustomMaskRedoStacks[node.id].clear();
    };
    auto pushUndo = [&]() {
        pushUndoSnapshot(payload);
    };

    auto commitChange = [&]() {
        EnsureCustomMaskRaster(payload);
        MarkRenderDirty(node.id);
        MarkDirty();
    };

    auto findImageSizeBackwards = [&](auto&& self, int currentNodeId, int& outW, int& outH, std::vector<int>& visited) -> bool {
        if (std::find(visited.begin(), visited.end(), currentNodeId) != visited.end()) {
            return false;
        }
        visited.push_back(currentNodeId);
        const EditorNodeGraph::Node* current = m_NodeGraph.FindNode(currentNodeId);
        if (!current) {
            return false;
        }
        if (current->kind == EditorNodeGraph::NodeKind::Image &&
            current->image.width > 0 &&
            current->image.height > 0) {
            outW = current->image.width;
            outH = current->image.height;
            return true;
        }
        for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
            if (link.toNodeId != currentNodeId || !m_NodeGraph.IsRenderLink(link)) {
                continue;
            }
            const bool imageInput =
                link.toSocketId == EditorNodeGraph::kImageInputSocketId ||
                link.toSocketId == EditorNodeGraph::kMixInputASocketId ||
                link.toSocketId == EditorNodeGraph::kMixInputBSocketId ||
                EditorNodeGraph::IsDataMathInputSocketId(link.toSocketId) ||
                link.toSocketId == EditorNodeGraph::kDataMathBaseInputSocketId ||
                link.toSocketId == EditorNodeGraph::kHdrMergeInput1SocketId ||
                link.toSocketId == EditorNodeGraph::kHdrMergeInput2SocketId ||
                link.toSocketId == EditorNodeGraph::kHdrMergeInput3SocketId ||
                link.toSocketId == EditorNodeGraph::kImageToMaskInputSocketId;
            if (imageInput && self(self, link.fromNodeId, outW, outH, visited)) {
                return true;
            }
        }
        return false;
    };

    auto resolveReferenceSize = [&](int& outW, int& outH) -> bool {
        if (payload.referenceMode == EditorNodeGraph::CustomMaskReferenceMode::GraphNode &&
            payload.referenceNodeId > 0) {
            const EditorNodeGraph::Node* reference = m_NodeGraph.FindNode(payload.referenceNodeId);
            if (reference && reference->kind == EditorNodeGraph::NodeKind::Image &&
                reference->image.width > 0 && reference->image.height > 0) {
                outW = reference->image.width;
                outH = reference->image.height;
                return true;
            }
        }

        std::vector<int> queue;
        std::vector<int> visited;
        queue.push_back(node.id);
        for (std::size_t i = 0; i < queue.size(); ++i) {
            const int currentId = queue[i];
            if (std::find(visited.begin(), visited.end(), currentId) != visited.end()) {
                continue;
            }
            visited.push_back(currentId);
            const EditorNodeGraph::Node* current = m_NodeGraph.FindNode(currentId);
            if (!current) {
                continue;
            }
            if (current->kind == EditorNodeGraph::NodeKind::Output &&
                m_Pipeline.GetCanvasWidth() > 0 &&
                m_Pipeline.GetCanvasHeight() > 0) {
                outW = m_Pipeline.GetCanvasWidth();
                outH = m_Pipeline.GetCanvasHeight();
                return true;
            }

            for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
                if (link.fromNodeId != currentId || !m_NodeGraph.IsRenderLink(link)) {
                    continue;
                }
                std::vector<int> upstreamVisited;
                if (findImageSizeBackwards(findImageSizeBackwards, link.toNodeId, outW, outH, upstreamVisited)) {
                    return true;
                }
                if (std::find(queue.begin(), queue.end(), link.toNodeId) == queue.end()) {
                    queue.push_back(link.toNodeId);
                }
            }
        }

        if (m_Pipeline.GetCanvasWidth() > 0 && m_Pipeline.GetCanvasHeight() > 0) {
            outW = m_Pipeline.GetCanvasWidth();
            outH = m_Pipeline.GetCanvasHeight();
            return true;
        }
        return false;
    };

    auto resolveCanvasImage = [&]() {
        CustomMaskCanvasImage result;
        auto tryUseImageNode = [&](int imageNodeId) {
            const EditorNodeGraph::Node* imageNode = m_NodeGraph.FindNode(imageNodeId);
            if (!imageNode || imageNode->kind != EditorNodeGraph::NodeKind::Image ||
                imageNode->image.pixels.empty() ||
                imageNode->image.width <= 0 ||
                imageNode->image.height <= 0) {
                return false;
            }
            result.pixels = &imageNode->image.pixels;
            result.width = imageNode->image.width;
            result.height = imageNode->image.height;
            result.channels = std::max(1, imageNode->image.channels);
            return true;
        };

        if (payload.referenceMode == EditorNodeGraph::CustomMaskReferenceMode::GraphNode &&
            tryUseImageNode(payload.referenceNodeId)) {
            return result;
        }

        std::vector<int> queue { node.id };
        std::vector<int> visited;
        for (std::size_t index = 0; index < queue.size(); ++index) {
            const int currentId = queue[index];
            if (std::find(visited.begin(), visited.end(), currentId) != visited.end()) {
                continue;
            }
            visited.push_back(currentId);
            for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
                if (link.fromNodeId != currentId || !m_NodeGraph.IsRenderLink(link)) {
                    continue;
                }
                const EditorNodeGraph::Node* downstream = m_NodeGraph.FindNode(link.toNodeId);
                if (!downstream) {
                    continue;
                }

                if (downstream->kind == EditorNodeGraph::NodeKind::Layer &&
                    link.toSocketId == EditorNodeGraph::kMaskInputSocketId) {
                    if (tryUseImageNode(m_NodeGraph.ResolveReferenceSourceNodeId(downstream->id, EditorNodeGraph::kImageInputSocketId))) {
                        return result;
                    }
                } else if (downstream->kind == EditorNodeGraph::NodeKind::Mix &&
                    link.toSocketId == EditorNodeGraph::kMixFactorSocketId) {
                    if (tryUseImageNode(m_NodeGraph.ResolveReferenceSourceNodeId(downstream->id, EditorNodeGraph::kMixInputASocketId)) ||
                        tryUseImageNode(m_NodeGraph.ResolveReferenceSourceNodeId(downstream->id, EditorNodeGraph::kMixInputBSocketId))) {
                        return result;
                    }
                } else if (downstream->kind == EditorNodeGraph::NodeKind::DataMath &&
                    link.toSocketId == EditorNodeGraph::kMaskInputSocketId) {
                    if (tryUseImageNode(m_NodeGraph.ResolveReferenceSourceNodeId(downstream->id, EditorNodeGraph::kDataMathBaseInputSocketId))) {
                        return result;
                    }
                    for (int inputIndex = 0; inputIndex < EditorNodeGraph::kMaxDataMathInputCount; ++inputIndex) {
                        if (tryUseImageNode(m_NodeGraph.ResolveReferenceSourceNodeId(downstream->id, EditorNodeGraph::DataMathInputSocketId(inputIndex)))) {
                            return result;
                        }
                    }
                } else if (downstream->kind == EditorNodeGraph::NodeKind::Output) {
                    if (tryUseImageNode(m_NodeGraph.ResolveReferenceSourceNodeIdForOutput(downstream->id))) {
                        return result;
                    }
                }

                if (std::find(queue.begin(), queue.end(), link.toNodeId) == queue.end()) {
                    queue.push_back(link.toNodeId);
                }
            }
        }

        tryUseImageNode(m_NodeGraph.GetActiveImageNodeId());
        return result;
    };

    const bool rasterIsEmpty = std::all_of(payload.rasterLayer.begin(), payload.rasterLayer.end(), [](float value) {
        return value <= 0.0001f;
    });
    if (payload.objects.empty() && rasterIsEmpty && payload.width == 1024 && payload.height == 1024) {
        int referenceW = 0;
        int referenceH = 0;
        if (resolveReferenceSize(referenceW, referenceH) && referenceW > 0 && referenceH > 0 &&
            (payload.width != referenceW || payload.height != referenceH)) {
            ResizeCustomMaskRaster(payload, std::clamp(referenceW, 1, 8192), std::clamp(referenceH, 1, 8192));
            commitChange();
        }
    }

    const CustomMaskCanvasImage canvasImage = resolveCanvasImage();
    const float fullWidth = std::max(240.0f, ImGui::GetContentRegionAvail().x);
    const float lineHeight = ImGui::GetFrameHeight();

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 6.0f));

    ImGui::TextUnformatted("Mask Canvas");
    ImGui::SameLine();
    ImGui::TextDisabled("%d x %d", payload.width, payload.height);
    ImGui::SameLine();
    ImGui::TextDisabled("White applies, black blocks, gray is partial.");
    ImGui::SameLine();
    if (ImGui::Checkbox("Image", &payload.showCanvasReferenceImage)) {
        MarkDirty();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Magenta", &payload.showCanvasMaskImpact)) {
        MarkDirty();
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Strength", &payload.showCanvasMaskStrength)) {
        MarkDirty();
    }

    const float undoButtonW = 58.0f;
    const float toolButtonW = 66.0f;
    if (ImGui::Button("Undo", ImVec2(undoButtonW, 0.0f))) {
        auto& undo = m_CustomMaskUndoStacks[node.id];
        if (!undo.empty()) {
            m_CustomMaskRedoStacks[node.id].push_back(payload);
            payload = undo.back();
            undo.pop_back();
            commitChange();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Redo", ImVec2(undoButtonW, 0.0f))) {
        auto& redo = m_CustomMaskRedoStacks[node.id];
        if (!redo.empty()) {
            m_CustomMaskUndoStacks[node.id].push_back(payload);
            payload = redo.back();
            redo.pop_back();
            commitChange();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(undoButtonW, 0.0f))) {
        pushUndo();
        std::fill(payload.rasterLayer.begin(), payload.rasterLayer.end(), 0.0f);
        commitChange();
    }
    ImGui::SameLine();
    if (ImGui::Button("Fill", ImVec2(undoButtonW, 0.0f))) {
        pushUndo();
        std::fill(payload.rasterLayer.begin(), payload.rasterLayer.end(), 1.0f);
        commitChange();
    }

    ImGui::SameLine();
    auto toolButton = [&](EditorNodeGraph::CustomMaskTool tool, const char* label) {
        const bool selected = payload.activeTool == tool;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button(label, ImVec2(toolButtonW, 0.0f))) {
            payload.activeTool = tool;
            MarkDirty();
        }
        if (selected) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", CustomMaskToolLabel(tool));
        }
        ImGui::SameLine();
    };
    toolButton(EditorNodeGraph::CustomMaskTool::Brush, "Brush");
    toolButton(EditorNodeGraph::CustomMaskTool::Erase, "Erase");
    toolButton(EditorNodeGraph::CustomMaskTool::Select, "Select");
    toolButton(EditorNodeGraph::CustomMaskTool::Rectangle, "Rect");
    toolButton(EditorNodeGraph::CustomMaskTool::Ellipse, "Ellipse");
    toolButton(EditorNodeGraph::CustomMaskTool::Polygon, "Poly");
    toolButton(EditorNodeGraph::CustomMaskTool::FreeformPath, "Path");
    ImGui::NewLine();

    const float payloadAspect = static_cast<float>(payload.width) / static_cast<float>(std::max(1, payload.height));
    const bool useReferenceAspect = payload.showCanvasReferenceImage && canvasImage.Valid();
    const float displayAspect = useReferenceAspect
        ? static_cast<float>(canvasImage.width) / static_cast<float>(std::max(1, canvasImage.height))
        : payloadAspect;
    ImVec2 available = ImGui::GetContentRegionAvail();
    const float safeAspect = std::max(0.1f, displayAspect);
    float canvasW = std::max(240.0f, available.x);
    float canvasH = canvasW / safeAspect;
    const float maxCanvasH = std::max(260.0f, available.y * 0.58f);
    if (canvasH > maxCanvasH) {
        canvasH = maxCanvasH;
        canvasW = canvasH * safeAspect;
    }
    if (canvasH < 180.0f && 180.0f * safeAspect <= available.x) {
        canvasH = 180.0f;
        canvasW = canvasH * safeAspect;
    }
    canvasW = std::min(canvasW, available.x);
    canvasH = canvasW / safeAspect;
    if (canvasW < available.x - 1.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available.x - canvasW) * 0.5f);
    }
    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize(canvasW, canvasH);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(canvasMin, ImVec2(canvasMin.x + canvasW, canvasMin.y + canvasH), IM_COL32(5, 5, 6, 255));

    const float displayStep = std::max(1.0f, std::ceil(std::max(canvasW, canvasH) / 768.0f));
    const int previewCols = std::max(1, std::min(payload.width, static_cast<int>(std::ceil(canvasW / displayStep))));
    const int previewRows = std::max(1, std::min(payload.height, static_cast<int>(std::ceil(canvasH / displayStep))));
    for (int py = 0; py < previewRows; ++py) {
        for (int px = 0; px < previewCols; ++px) {
            const float u0 = (static_cast<float>(px) + 0.5f) / static_cast<float>(previewCols);
            const float v0 = (static_cast<float>(py) + 0.5f) / static_cast<float>(previewRows);
            const float sample = SampleCustomMaskForPreview(payload, u0, v0);
            float r = Clamp01(sample);
            float g = r;
            float bl = r;
            if (payload.showCanvasReferenceImage && canvasImage.Valid()) {
                const float luma = SampleCanvasImageLuma(canvasImage, u0, v0);
                r = luma;
                g = luma;
                bl = luma;
                if (payload.showCanvasMaskImpact && sample > 0.001f) {
                    const float impact = payload.showCanvasMaskStrength ? Clamp01(sample) : 1.0f;
                    r = r * (1.0f - impact) + 1.0f * impact;
                    g = g * (1.0f - impact) + 0.0f * impact;
                    bl = bl * (1.0f - impact) + 0.82f * impact;
                }
            }
            const unsigned char rr = static_cast<unsigned char>(std::round(Clamp01(r) * 255.0f));
            const unsigned char gg = static_cast<unsigned char>(std::round(Clamp01(g) * 255.0f));
            const unsigned char bb = static_cast<unsigned char>(std::round(Clamp01(bl) * 255.0f));
            const ImVec2 a(canvasMin.x + (static_cast<float>(px) / previewCols) * canvasW,
                           canvasMin.y + (static_cast<float>(py) / previewRows) * canvasH);
            const ImVec2 b(canvasMin.x + (static_cast<float>(px + 1) / previewCols) * canvasW + 0.5f,
                           canvasMin.y + (static_cast<float>(py + 1) / previewRows) * canvasH);
            drawList->AddRectFilled(a, b, IM_COL32(rr, gg, bb, 255));
        }
    }
    const ImVec2 canvasMax(canvasMin.x + canvasW, canvasMin.y + canvasH);
    drawList->AddRect(canvasMin, canvasMax, IM_COL32(125, 145, 165, 190), 2.0f);

    ImGui::InvisibleButton(
        "##CustomMaskPaintCanvas",
        canvasSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const bool brushLikeTool =
        payload.activeTool == EditorNodeGraph::CustomMaskTool::Brush ||
        payload.activeTool == EditorNodeGraph::CustomMaskTool::Erase;
    auto mouseToCanvasUv = [&](const ImVec2& mouse, float& outU, float& outV) {
        outU = std::clamp((mouse.x - canvasMin.x) / std::max(1.0f, canvasW), 0.0f, 1.0f);
        outV = std::clamp((mouse.y - canvasMin.y) / std::max(1.0f, canvasH), 0.0f, 1.0f);
    };

    if (brushLikeTool && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_CustomMaskBrushAdjustDrag.nodeId = node.id;
        m_CustomMaskBrushAdjustDrag.startMouse = ImGui::GetIO().MousePos;
        m_CustomMaskBrushAdjustDrag.currentMouse = m_CustomMaskBrushAdjustDrag.startMouse;
        m_CustomMaskBrushAdjustDrag.startSize = payload.brushSize;
        m_CustomMaskBrushAdjustDrag.startSoftness = payload.brushSoftness;
        m_CustomMaskBrushAdjustDrag.startOpacity = payload.brushOpacity;
        m_CustomMaskBrushAdjustDrag.adjusting = true;
    }
    const bool adjustingBrush =
        m_CustomMaskBrushAdjustDrag.adjusting &&
        m_CustomMaskBrushAdjustDrag.nodeId == node.id &&
        ImGui::IsMouseDown(ImGuiMouseButton_Right);
    if (adjustingBrush) {
        const ImGuiIO& io = ImGui::GetIO();
        m_CustomMaskBrushAdjustDrag.currentMouse = io.MousePos;
        const ImVec2 delta(
            io.MousePos.x - m_CustomMaskBrushAdjustDrag.startMouse.x,
            io.MousePos.y - m_CustomMaskBrushAdjustDrag.startMouse.y);
        const float fineScale = io.KeyShift ? 0.25f : 1.0f;
        payload.brushSize = std::clamp(
            m_CustomMaskBrushAdjustDrag.startSize + delta.x * 1.15f * fineScale,
            1.0f,
            512.0f);
        if (io.KeyAlt) {
            payload.brushOpacity = std::clamp(
                m_CustomMaskBrushAdjustDrag.startOpacity - delta.y * 0.0045f * fineScale,
                0.0f,
                1.0f);
        } else {
            payload.brushSoftness = std::clamp(
                m_CustomMaskBrushAdjustDrag.startSoftness - delta.y * 0.0045f * fineScale,
                0.0f,
                1.0f);
        }
        MarkDirty();
    } else if (m_CustomMaskBrushAdjustDrag.nodeId == node.id && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        m_CustomMaskBrushAdjustDrag = {};
    }

    if ((payload.activeTool == EditorNodeGraph::CustomMaskTool::Brush ||
         payload.activeTool == EditorNodeGraph::CustomMaskTool::Erase) && (hovered || active) && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!m_CustomMaskPaintingNodes.count(node.id)) {
            pushUndo();
            m_CustomMaskPaintingNodes.insert(node.id);
        }
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        float u = 0.0f;
        float v = 0.0f;
        mouseToCanvasUv(mouse, u, v);
        ApplyBrush(payload, u, v, payload.activeTool == EditorNodeGraph::CustomMaskTool::Erase);
        commitChange();
    }
    if (m_CustomMaskPaintingNodes.count(node.id) && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_CustomMaskPaintingNodes.erase(node.id);
        commitChange();
    }
    if (brushLikeTool &&
        (hovered || active || adjustingBrush || m_CustomMaskPaintingNodes.count(node.id))) {
        ImVec2 previewMouse = adjustingBrush
            ? m_CustomMaskBrushAdjustDrag.currentMouse
            : ImGui::GetIO().MousePos;
        previewMouse.x = std::clamp(previewMouse.x, canvasMin.x, canvasMax.x);
        previewMouse.y = std::clamp(previewMouse.y, canvasMin.y, canvasMax.y);
        const float pixelScaleX = canvasW / static_cast<float>(std::max(1, payload.width));
        const float pixelScaleY = canvasH / static_cast<float>(std::max(1, payload.height));
        const float brushRadius = std::max(2.0f, payload.brushSize * 0.5f * (pixelScaleX + pixelScaleY) * 0.5f);
        drawList->PushClipRect(canvasMin, canvasMax, true);
        DrawCustomMaskBrushPreview(
            drawList,
            previewMouse,
            brushRadius,
            payload.brushSoftness,
            payload.brushOpacity,
            payload.activeTool == EditorNodeGraph::CustomMaskTool::Erase);
        drawList->PopClipRect();
    }

    auto addObject = [&](EditorNodeGraph::CustomMaskObjectType type) {
        pushUndo();
        EditorNodeGraph::CustomMaskObject object;
        object.id = payload.nextObjectId++;
        object.type = type;
        object.operation = EditorNodeGraph::CustomMaskOperation::Add;
        object.feather = 0.0f;
        if (type == EditorNodeGraph::CustomMaskObjectType::Polygon) {
            object.points = { { 0.35f, 0.30f }, { 0.68f, 0.42f }, { 0.52f, 0.72f } };
        } else if (type == EditorNodeGraph::CustomMaskObjectType::FreeformPath) {
            object.points = { { 0.25f, 0.50f }, { 0.40f, 0.38f }, { 0.58f, 0.62f }, { 0.75f, 0.48f } };
            object.blur = 24.0f;
        } else {
            object.points = { { 0.25f, 0.25f }, { 0.75f, 0.75f } };
        }
        payload.selectedObjectId = object.id;
        payload.objects.push_back(std::move(object));
        commitChange();
    };

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (ImGui::BeginTabBar("##CustomMaskTabs")) {
        if (ImGui::BeginTabItem("Brush")) {
            ImGui::SetNextItemWidth(fullWidth * 0.30f);
            if (ImGui::SliderFloat("Size", &payload.brushSize, 1.0f, 512.0f, "%.0f px")) MarkDirty();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fullWidth * 0.30f);
            if (ImGui::SliderFloat("Softness", &payload.brushSoftness, 0.0f, 1.0f)) MarkDirty();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fullWidth * 0.30f);
            if (ImGui::SliderFloat("Opacity", &payload.brushOpacity, 0.0f, 1.0f)) MarkDirty();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Mask")) {
            const EditorNodeGraph::CustomMaskPayload beforeGlobalEdit = payload;
            bool globalChanged = false;
            globalChanged |= ImGui::Checkbox("Invert", &payload.invert);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fullWidth * 0.34f);
            globalChanged |= ImGui::SliderFloat("Blur", &payload.blurRadius, 0.0f, 64.0f, "%.1f px");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(fullWidth * 0.34f);
            globalChanged |= ImGui::SliderFloat("Expand / Contract", &payload.expandContract, -64.0f, 64.0f, "%.1f px");
            if (globalChanged) {
                pushUndoSnapshot(beforeGlobalEdit);
                commitChange();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Objects")) {
            if (ImGui::Button("Rect", ImVec2(72.0f, 0.0f))) addObject(EditorNodeGraph::CustomMaskObjectType::Rectangle);
            ImGui::SameLine();
            if (ImGui::Button("Ellipse", ImVec2(72.0f, 0.0f))) addObject(EditorNodeGraph::CustomMaskObjectType::Ellipse);
            ImGui::SameLine();
            if (ImGui::Button("Poly", ImVec2(72.0f, 0.0f))) addObject(EditorNodeGraph::CustomMaskObjectType::Polygon);
            ImGui::SameLine();
            if (ImGui::Button("Path", ImVec2(72.0f, 0.0f))) addObject(EditorNodeGraph::CustomMaskObjectType::FreeformPath);

            const float listWidth = std::min(210.0f, fullWidth * 0.34f);
            ImGui::BeginChild("##CustomMaskObjectList", ImVec2(listWidth, 130.0f), true);
            for (const EditorNodeGraph::CustomMaskObject& object : payload.objects) {
                char label[96];
                std::snprintf(label, sizeof(label), "%s #%d", CustomMaskObjectTypeLabel(object.type), object.id);
                if (ImGui::Selectable(label, payload.selectedObjectId == object.id)) {
                    payload.selectedObjectId = object.id;
                }
            }
            ImGui::EndChild();
            ImGui::SameLine();

            EditorNodeGraph::CustomMaskObject* selected = FindCustomMaskObject(payload, payload.selectedObjectId);
            ImGui::BeginChild("##CustomMaskObjectEditor", ImVec2(0.0f, 130.0f), false);
            if (selected) {
                const EditorNodeGraph::CustomMaskPayload beforeObjectEdit = payload;
                bool objectChanged = false;
                int type = static_cast<int>(selected->type);
                const char* objectTypes[] = { "Rectangle", "Ellipse", "Polygon", "Freeform Path" };
                ImGui::SetNextItemWidth(150.0f);
                if (ImGui::Combo("Type", &type, objectTypes, IM_ARRAYSIZE(objectTypes))) {
                    selected->type = static_cast<EditorNodeGraph::CustomMaskObjectType>(std::clamp(type, 0, 3));
                    objectChanged = true;
                }
                ImGui::SameLine();
                int operation = static_cast<int>(selected->operation);
                const char* operations[] = { "Add", "Subtract", "Intersect", "Exclude" };
                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::Combo("Operation", &operation, operations, IM_ARRAYSIZE(operations))) {
                    selected->operation = static_cast<EditorNodeGraph::CustomMaskOperation>(std::clamp(operation, 0, 3));
                    objectChanged = true;
                }
                ImGui::SameLine();
                objectChanged |= ImGui::Checkbox("On", &selected->enabled);
                ImGui::SameLine();
                objectChanged |= ImGui::Checkbox("Invert", &selected->invert);
                ImGui::SetNextItemWidth(fullWidth * 0.23f);
                objectChanged |= ImGui::SliderFloat("Strength", &selected->strength, 0.0f, 1.0f);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(fullWidth * 0.23f);
                objectChanged |= ImGui::SliderFloat("Feather", &selected->feather, 0.0f, 0.5f);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(fullWidth * 0.23f);
                objectChanged |= ImGui::SliderFloat("Radius", &selected->blur, 0.0f, 128.0f);

                if (selected->type == EditorNodeGraph::CustomMaskObjectType::Polygon ||
                    selected->type == EditorNodeGraph::CustomMaskObjectType::FreeformPath) {
                    if (ImGui::Button("Add Point", ImVec2(92.0f, 0.0f))) {
                        selected->points.push_back({ 0.5f, 0.5f });
                        objectChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Remove Point", ImVec2(108.0f, 0.0f)) && !selected->points.empty()) {
                        selected->points.pop_back();
                        objectChanged = true;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Duplicate", ImVec2(86.0f, 0.0f))) {
                    pushUndo();
                    EditorNodeGraph::CustomMaskObject copy = *selected;
                    copy.id = payload.nextObjectId++;
                    payload.selectedObjectId = copy.id;
                    payload.objects.push_back(std::move(copy));
                    commitChange();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                    ImGui::EndTabBar();
                    ImGui::PopStyleVar(2);
                    return;
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete", ImVec2(72.0f, 0.0f))) {
                    pushUndo();
                    payload.objects.erase(
                        std::remove_if(payload.objects.begin(), payload.objects.end(), [&](const EditorNodeGraph::CustomMaskObject& object) {
                            return object.id == payload.selectedObjectId;
                        }),
                        payload.objects.end());
                    payload.selectedObjectId = -1;
                    commitChange();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                    ImGui::EndTabBar();
                    ImGui::PopStyleVar(2);
                    return;
                }

                for (std::size_t i = 0; i < selected->points.size(); ++i) {
                    float point[2] = { selected->points[i].x, selected->points[i].y };
                    char label[64];
                    std::snprintf(label, sizeof(label), "P%zu", i + 1);
                    ImGui::SetNextItemWidth(170.0f);
                    if (ImGui::DragFloat2(label, point, 0.003f, 0.0f, 1.0f, "%.3f")) {
                        selected->points[i].x = std::clamp(point[0], 0.0f, 1.0f);
                        selected->points[i].y = std::clamp(point[1], 0.0f, 1.0f);
                        objectChanged = true;
                    }
                    if ((i % 3) != 2 && i + 1 < selected->points.size()) {
                        ImGui::SameLine();
                    }
                }

                if (objectChanged) {
                    pushUndoSnapshot(beforeObjectEdit);
                    commitChange();
                }
            } else {
                ImGui::TextDisabled("Select or add an object.");
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Reference")) {
            int width = payload.width;
            int height = payload.height;
            ImGui::SetNextItemWidth(120.0f);
            const bool widthChanged = ImGui::InputInt("W", &width, 16, 128);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            const bool heightChanged = ImGui::InputInt("H", &height, 16, 128);
            if ((widthChanged || heightChanged) && width > 0 && height > 0) {
                pushUndo();
                ResizeCustomMaskRaster(payload, width, height);
                commitChange();
            }
            ImGui::SameLine();
            if (ImGui::Button("Match Graph", ImVec2(110.0f, lineHeight))) {
                int referenceW = 0;
                int referenceH = 0;
                if (resolveReferenceSize(referenceW, referenceH) && referenceW > 0 && referenceH > 0) {
                    pushUndo();
                    ResizeCustomMaskRaster(payload, referenceW, referenceH);
                    commitChange();
                }
            }

            ImGui::SameLine();
            int referenceMode = payload.referenceMode == EditorNodeGraph::CustomMaskReferenceMode::GraphNode ? 1 : 0;
            const char* referenceModes[] = { "Custom Size", "Graph Node" };
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::Combo("Reference", &referenceMode, referenceModes, IM_ARRAYSIZE(referenceModes))) {
                pushUndo();
                payload.referenceMode = referenceMode == 1
                    ? EditorNodeGraph::CustomMaskReferenceMode::GraphNode
                    : EditorNodeGraph::CustomMaskReferenceMode::CustomSize;
                commitChange();
            }
            if (payload.referenceMode == EditorNodeGraph::CustomMaskReferenceMode::GraphNode) {
                ImGui::SameLine();
                int refNode = payload.referenceNodeId;
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::InputInt("Node", &refNode)) {
                    pushUndo();
                    payload.referenceNodeId = refNode;
                    commitChange();
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar(2);
}
