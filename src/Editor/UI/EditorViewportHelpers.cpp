#include "EditorViewportHelpers.h"

#include <algorithm>
#include <cmath>

namespace EditorViewportHelpers {
namespace {

bool SameSide(const ImVec2& a, const ImVec2& b, const ImVec2& p1, const ImVec2& p2) {
    const float cp1 = (b.x - a.x) * (p1.y - a.y) - (b.y - a.y) * (p1.x - a.x);
    const float cp2 = (b.x - a.x) * (p2.y - a.y) - (b.y - a.y) * (p2.x - a.x);
    return cp1 * cp2 >= 0.0f;
}

} // namespace

AffineTransform2D Inverse(const AffineTransform2D& matrix) {
    const float determinant = matrix.m00 * matrix.m11 - matrix.m01 * matrix.m10;
    if (std::abs(determinant) <= 1e-8f) {
        return {};
    }

    const float inverseDeterminant = 1.0f / determinant;
    AffineTransform2D inverse;
    inverse.m00 = matrix.m11 * inverseDeterminant;
    inverse.m01 = -matrix.m01 * inverseDeterminant;
    inverse.m02 = (matrix.m01 * matrix.m12 - matrix.m11 * matrix.m02) * inverseDeterminant;
    inverse.m10 = -matrix.m10 * inverseDeterminant;
    inverse.m11 = matrix.m00 * inverseDeterminant;
    inverse.m12 = (matrix.m10 * matrix.m02 - matrix.m00 * matrix.m12) * inverseDeterminant;
    return inverse;
}

AffineTransform2D BuildLocalTransform(const EditorModule::CompositeSceneItem& item) {
    const float baseWidth = item.isScalable ? 256.0f : std::max(1.0f, static_cast<float>(item.textureWidth));
    const float baseHeight = item.isScalable ? 256.0f : std::max(1.0f, static_cast<float>(item.textureHeight));
    const float width = baseWidth * std::max(0.0001f, item.scale.x);
    const float height = baseHeight * std::max(0.0001f, item.scale.y);
    const float cosR = std::cos(item.rotation);
    const float sinR = std::sin(item.rotation);

    AffineTransform2D matrix;
    matrix.m00 = cosR * std::max(0.0001f, item.scale.x);
    matrix.m01 = -sinR * std::max(0.0001f, item.scale.y);
    matrix.m10 = sinR * std::max(0.0001f, item.scale.x);
    matrix.m11 = cosR * std::max(0.0001f, item.scale.y);
    matrix.m02 = item.position.x + width * 0.5f - matrix.m00 * baseWidth * 0.5f - matrix.m01 * baseHeight * 0.5f;
    matrix.m12 = item.position.y + height * 0.5f - matrix.m10 * baseWidth * 0.5f - matrix.m11 * baseHeight * 0.5f;
    return matrix;
}

ImVec2 TransformPoint(const AffineTransform2D& matrix, const ImVec2& point) {
    return ImVec2(
        matrix.m00 * point.x + matrix.m01 * point.y + matrix.m02,
        matrix.m10 * point.x + matrix.m11 * point.y + matrix.m12);
}

std::array<ImVec2, 4> ComputeSceneQuadWorld(const EditorModule::CompositeSceneItem& item) {
    const AffineTransform2D local = BuildLocalTransform(item);
    const float width = item.isScalable ? 256.0f : std::max(1.0f, static_cast<float>(item.textureWidth));
    const float height = item.isScalable ? 256.0f : std::max(1.0f, static_cast<float>(item.textureHeight));
    return {
        TransformPoint(local, ImVec2(0.0f, 0.0f)),
        TransformPoint(local, ImVec2(width, 0.0f)),
        TransformPoint(local, ImVec2(width, height)),
        TransformPoint(local, ImVec2(0.0f, height))
    };
}

ImVec2 WorldToScreen(const ImVec2& canvasCenter, float zoom, float panX, float panY, const ImVec2& worldPoint) {
    return ImVec2(
        canvasCenter.x + (worldPoint.x - panX) * zoom,
        canvasCenter.y + (worldPoint.y - panY) * zoom);
}

ImVec2 ScreenToWorld(const ImVec2& canvasCenter, float zoom, float panX, float panY, const ImVec2& screenPoint) {
    return ImVec2(
        panX + (screenPoint.x - canvasCenter.x) / std::max(0.001f, zoom),
        panY + (screenPoint.y - canvasCenter.y) / std::max(0.001f, zoom));
}

bool PointInQuad(const std::array<ImVec2, 4>& quad, const ImVec2& point) {
    return SameSide(quad[0], quad[1], point, quad[2]) &&
           SameSide(quad[1], quad[2], point, quad[3]) &&
           SameSide(quad[2], quad[3], point, quad[0]) &&
           SameSide(quad[3], quad[0], point, quad[1]);
}

EditorModule::CompositeFloatRect QuadBounds(const std::array<ImVec2, 4>& quad) {
    float minX = quad[0].x;
    float minY = quad[0].y;
    float maxX = quad[0].x;
    float maxY = quad[0].y;
    for (const ImVec2& point : quad) {
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }
    return { minX, minY, std::max(1.0f, maxX - minX), std::max(1.0f, maxY - minY) };
}

EditorModule::CompositeFloatRect MakeNormalizedRect(const float x1, const float y1, const float x2, const float y2) {
    const float left = std::min(x1, x2);
    const float top = std::min(y1, y2);
    const float right = std::max(x1, x2);
    const float bottom = std::max(y1, y2);
    return { left, top, std::max(1.0f, right - left), std::max(1.0f, bottom - top) };
}

float DistanceToPoint(const ImVec2& a, const ImVec2& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

ImVec2 Midpoint(const ImVec2& a, const ImVec2& b) {
    return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
}

ImVec2 NormalizeOrZero(const ImVec2& value) {
    const float length = std::hypot(value.x, value.y);
    if (length <= 1e-6f) {
        return ImVec2(0.0f, 0.0f);
    }
    return ImVec2(value.x / length, value.y / length);
}

} // namespace EditorViewportHelpers
