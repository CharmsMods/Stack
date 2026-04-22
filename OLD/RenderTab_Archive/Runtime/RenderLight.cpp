#include "RenderLight.h"

bool NearlyEqual(const RenderLight& a, const RenderLight& b, float epsilon) {
    return a.name == b.name &&
        a.type == b.type &&
        NearlyEqual(a.transform, b.transform, epsilon) &&
        NearlyEqual(a.color, b.color, epsilon) &&
        NearlyEqual(a.intensity, b.intensity, epsilon) &&
        NearlyEqual(a.areaSize, b.areaSize, epsilon) &&
        NearlyEqual(a.range, b.range, epsilon) &&
        NearlyEqual(a.innerConeDegrees, b.innerConeDegrees, epsilon) &&
        NearlyEqual(a.outerConeDegrees, b.outerConeDegrees, epsilon) &&
        a.enabled == b.enabled;
}

const char* GetRenderLightTypeLabel(RenderLightType type) {
    switch (type) {
    case RenderLightType::RectArea:
        return "Rect Area";
    case RenderLightType::Point:
        return "Point";
    case RenderLightType::Spot:
        return "Spot";
    case RenderLightType::Sun:
        return "Sun";
    }

    return "Unknown";
}

RenderFloat3 GetRenderLightDirection(const RenderLight& light) {
    return TransformDirection(light.transform, MakeRenderFloat3(1.0f, 0.0f, 0.0f));
}

RenderFloat3 GetRenderLightUp(const RenderLight& light) {
    return TransformDirection(light.transform, MakeRenderFloat3(0.0f, 1.0f, 0.0f));
}

RenderFloat3 GetRenderLightRight(const RenderLight& light) {
    return TransformDirection(light.transform, MakeRenderFloat3(0.0f, 0.0f, 1.0f));
}

RenderBounds ComputeBounds(const RenderLight& light) {
    RenderBounds bounds {};

    if (light.type == RenderLightType::RectArea) {
        const RenderFloat3 center = light.transform.translation;
        const RenderFloat3 up = Scale(GetRenderLightUp(light), light.areaSize.y * 0.5f);
        const RenderFloat3 right = Scale(GetRenderLightRight(light), light.areaSize.x * 0.5f);
        const RenderFloat3 points[4] = {
            Add(center, Add(up, right)),
            Add(center, Subtract(up, right)),
            Add(center, Add(Scale(up, -1.0f), right)),
            Add(center, Subtract(Scale(up, -1.0f), right))
        };

        bounds.min = points[0];
        bounds.max = points[0];
        for (int i = 1; i < 4; ++i) {
            bounds.min.x = points[i].x < bounds.min.x ? points[i].x : bounds.min.x;
            bounds.min.y = points[i].y < bounds.min.y ? points[i].y : bounds.min.y;
            bounds.min.z = points[i].z < bounds.min.z ? points[i].z : bounds.min.z;
            bounds.max.x = points[i].x > bounds.max.x ? points[i].x : bounds.max.x;
            bounds.max.y = points[i].y > bounds.max.y ? points[i].y : bounds.max.y;
            bounds.max.z = points[i].z > bounds.max.z ? points[i].z : bounds.max.z;
        }
        return bounds;
    }

    const float radius = light.type == RenderLightType::Sun
        ? 1.5f
        : (light.type == RenderLightType::Spot ? 0.4f : 0.25f);
    bounds.min = Subtract(light.transform.translation, MakeRenderFloat3(radius, radius, radius));
    bounds.max = Add(light.transform.translation, MakeRenderFloat3(radius, radius, radius));
    return bounds;
}
