#include "Renderer/RenderPipeline.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <vector>

#ifndef GL_R32F
#define GL_R32F 0x822E
#endif

#ifndef GL_RED
#define GL_RED 0x1903
#endif

namespace {

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float SmoothStepFloat(float edge0, float edge1, float value) {
    const float denom = std::max(0.000001f, edge1 - edge0);
    const float t = std::clamp((value - edge0) / denom, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float CombineMaskValue(float base, float value, RenderCustomMaskOperation operation) {
    base = Clamp01(base);
    value = Clamp01(value);
    switch (operation) {
        case RenderCustomMaskOperation::Add: return std::max(base, value);
        case RenderCustomMaskOperation::Subtract: return base * (1.0f - value);
        case RenderCustomMaskOperation::Intersect: return base * value;
        case RenderCustomMaskOperation::Exclude: return std::abs(base - value);
    }
    return base;
}

bool CustomMaskPointInPolygon(const std::vector<RenderCustomMaskPoint>& points, float x, float y) {
    if (points.size() < 3) {
        return false;
    }
    bool inside = false;
    for (std::size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
        const float xi = points[i].x;
        const float yi = points[i].y;
        const float xj = points[j].x;
        const float yj = points[j].y;
        const bool intersects = ((yi > y) != (yj > y)) &&
            (x < (xj - xi) * (y - yi) / std::max(0.000001f, yj - yi) + xi);
        if (intersects) {
            inside = !inside;
        }
    }
    return inside;
}

float DistanceToSegment(float px, float py, const RenderCustomMaskPoint& a, const RenderCustomMaskPoint& b) {
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float denom = abx * abx + aby * aby;
    const float t = denom > 0.000001f
        ? std::clamp(((px - a.x) * abx + (py - a.y) * aby) / denom, 0.0f, 1.0f)
        : 0.0f;
    const float dx = px - (a.x + abx * t);
    const float dy = py - (a.y + aby * t);
    return std::sqrt(dx * dx + dy * dy);
}

float EvaluateCustomMaskObject(const RenderCustomMaskObject& object, float u, float v, int width, int height) {
    if (!object.enabled || object.points.empty()) {
        return 0.0f;
    }

    float value = 0.0f;
    const float feather = std::max(0.0f, object.feather);
    if ((object.type == RenderCustomMaskObjectType::Rectangle || object.type == RenderCustomMaskObjectType::Ellipse) &&
        object.points.size() >= 2) {
        const float minX = std::min(object.points[0].x, object.points[1].x);
        const float maxX = std::max(object.points[0].x, object.points[1].x);
        const float minY = std::min(object.points[0].y, object.points[1].y);
        const float maxY = std::max(object.points[0].y, object.points[1].y);
        if (maxX > minX && maxY > minY) {
            if (object.type == RenderCustomMaskObjectType::Rectangle) {
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
                value = normFeather > 0.0f ? 1.0f - SmoothStepFloat(1.0f - normFeather, 1.0f + normFeather, d) : (d <= 1.0f ? 1.0f : 0.0f);
            }
        }
    } else if (object.type == RenderCustomMaskObjectType::Polygon) {
        const bool inside = CustomMaskPointInPolygon(object.points, u, v);
        value = inside ? 1.0f : 0.0f;
    } else if (object.type == RenderCustomMaskObjectType::FreeformPath && object.points.size() >= 2) {
        float minDistance = std::numeric_limits<float>::max();
        for (std::size_t i = 1; i < object.points.size(); ++i) {
            minDistance = std::min(minDistance, DistanceToSegment(u, v, object.points[i - 1], object.points[i]));
        }
        const float radius = std::max(1.0f / static_cast<float>(std::max(width, height)), object.blur / static_cast<float>(std::max(1, std::max(width, height))));
        const float softness = std::max(feather, radius);
        value = softness > 0.0f ? 1.0f - std::clamp((minDistance - radius) / softness, 0.0f, 1.0f) : (minDistance <= radius ? 1.0f : 0.0f);
    }

    value = Clamp01(value);
    if (object.invert) {
        value = 1.0f - value;
    }
    return Clamp01(value * std::clamp(object.strength, 0.0f, 1.0f));
}

void BoxBlurMask(std::vector<float>& values, int width, int height, int radius) {
    if (radius <= 0 || values.empty() || width <= 0 || height <= 0) {
        return;
    }

    std::vector<float> horizontal(values.size(), 0.0f);
    for (int y = 0; y < height; ++y) {
        std::vector<float> prefix(static_cast<std::size_t>(width + 1), 0.0f);
        for (int x = 0; x < width; ++x) {
            prefix[static_cast<std::size_t>(x + 1)] =
                prefix[static_cast<std::size_t>(x)] + values[static_cast<std::size_t>(y) * width + x];
        }
        for (int x = 0; x < width; ++x) {
            const int lo = std::max(0, x - radius);
            const int hi = std::min(width - 1, x + radius);
            const float sum = prefix[static_cast<std::size_t>(hi + 1)] - prefix[static_cast<std::size_t>(lo)];
            horizontal[static_cast<std::size_t>(y) * width + x] = sum / static_cast<float>(hi - lo + 1);
        }
    }

    for (int x = 0; x < width; ++x) {
        std::vector<float> prefix(static_cast<std::size_t>(height + 1), 0.0f);
        for (int y = 0; y < height; ++y) {
            prefix[static_cast<std::size_t>(y + 1)] =
                prefix[static_cast<std::size_t>(y)] + horizontal[static_cast<std::size_t>(y) * width + x];
        }
        for (int y = 0; y < height; ++y) {
            const int lo = std::max(0, y - radius);
            const int hi = std::min(height - 1, y + radius);
            const float sum = prefix[static_cast<std::size_t>(hi + 1)] - prefix[static_cast<std::size_t>(lo)];
            values[static_cast<std::size_t>(y) * width + x] = sum / static_cast<float>(hi - lo + 1);
        }
    }
}

void MorphMask(std::vector<float>& values, int width, int height, int radius, bool expand) {
    if (radius <= 0 || values.empty() || width <= 0 || height <= 0) {
        return;
    }

    auto better = [expand](float a, float b) {
        return expand ? a >= b : a <= b;
    };

    std::vector<float> horizontal(values.size(), 0.0f);
    for (int y = 0; y < height; ++y) {
        std::deque<int> window;
        int nextAdd = 0;
        for (int x = 0; x < width; ++x) {
            const int targetAdd = std::min(width - 1, x + radius);
            while (nextAdd <= targetAdd) {
                while (!window.empty()) {
                    const float backValue = values[static_cast<std::size_t>(y) * width + window.back()];
                    const float addValue = values[static_cast<std::size_t>(y) * width + nextAdd];
                    if (better(backValue, addValue)) {
                        break;
                    }
                    window.pop_back();
                }
                window.push_back(nextAdd);
                ++nextAdd;
            }
            while (!window.empty() && window.front() < x - radius) {
                window.pop_front();
            }
            horizontal[static_cast<std::size_t>(y) * width + x] =
                values[static_cast<std::size_t>(y) * width + window.front()];
        }
    }

    for (int x = 0; x < width; ++x) {
        std::deque<int> window;
        int nextAdd = 0;
        for (int y = 0; y < height; ++y) {
            const int targetAdd = std::min(height - 1, y + radius);
            while (nextAdd <= targetAdd) {
                while (!window.empty()) {
                    const float backValue = horizontal[static_cast<std::size_t>(window.back()) * width + x];
                    const float addValue = horizontal[static_cast<std::size_t>(nextAdd) * width + x];
                    if (better(backValue, addValue)) {
                        break;
                    }
                    window.pop_back();
                }
                window.push_back(nextAdd);
                ++nextAdd;
            }
            while (!window.empty() && window.front() < y - radius) {
                window.pop_front();
            }
            values[static_cast<std::size_t>(y) * width + x] =
                horizontal[static_cast<std::size_t>(window.front()) * width + x];
        }
    }
}

std::vector<float> FlattenCustomMaskPayload(const RenderCustomMaskPayload& payload, int width, int height) {
    std::vector<float> values(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f);
    const int sourceW = std::max(1, payload.width);
    const int sourceH = std::max(1, payload.height);
    const bool hasRaster = payload.rasterLayer.size() >= static_cast<std::size_t>(sourceW) * static_cast<std::size_t>(sourceH);

    for (int y = 0; y < height; ++y) {
        const float v = height > 1 ? static_cast<float>(y) / static_cast<float>(height - 1) : 0.0f;
        const float maskV = 1.0f - v;
        const int srcY = std::clamp(static_cast<int>(std::round(maskV * static_cast<float>(sourceH - 1))), 0, sourceH - 1);
        for (int x = 0; x < width; ++x) {
            const float u = width > 1 ? static_cast<float>(x) / static_cast<float>(width - 1) : 0.0f;
            const int srcX = std::clamp(static_cast<int>(std::round(u * static_cast<float>(sourceW - 1))), 0, sourceW - 1);
            float value = hasRaster ? Clamp01(payload.rasterLayer[static_cast<std::size_t>(srcY) * sourceW + srcX]) : 0.0f;
            for (const RenderCustomMaskObject& object : payload.objects) {
                const float objectValue = EvaluateCustomMaskObject(object, u, maskV, width, height);
                value = CombineMaskValue(value, objectValue, object.operation);
            }
            values[static_cast<std::size_t>(y) * width + x] = Clamp01(value);
        }
    }

    const int morphRadius = static_cast<int>(std::round(std::abs(payload.expandContract)));
    if (morphRadius > 0) {
        MorphMask(values, width, height, morphRadius, payload.expandContract > 0.0f);
    }
    const int blurRadius = static_cast<int>(std::round(std::max(0.0f, payload.blurRadius)));
    if (blurRadius > 0) {
        BoxBlurMask(values, width, height, blurRadius);
    }
    if (payload.invert) {
        for (float& value : values) {
            value = 1.0f - Clamp01(value);
        }
    }
    return values;
}

} // namespace

unsigned int RenderPipeline::GenerateCustomMaskTexture(const RenderCustomMaskPayload& payload) {
    if (m_Width <= 0 || m_Height <= 0) {
        return 0;
    }

    const std::vector<float> values = FlattenCustomMaskPayload(payload, m_Width, m_Height);
    if (values.empty()) {
        return 0;
    }

    std::vector<float> red(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        red[i] = Clamp01(values[i]);
    }

    unsigned int texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, m_Width, m_Height, 0, GL_RED, GL_FLOAT, red.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}
