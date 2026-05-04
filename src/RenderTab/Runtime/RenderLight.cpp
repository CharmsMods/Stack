#include "RenderLight.h"

#include <algorithm>
#include <cmath>

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
        NearlyEqual(a.laserWavelengthNm, b.laserWavelengthNm, epsilon) &&
        NearlyEqual(a.laserLinewidthNm, b.laserLinewidthNm, epsilon) &&
        NearlyEqual(a.laserApertureRadius, b.laserApertureRadius, epsilon) &&
        NearlyEqual(a.laserBeamWaistRadius, b.laserBeamWaistRadius, epsilon) &&
        NearlyEqual(a.laserBeamQuality, b.laserBeamQuality, epsilon) &&
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
    case RenderLightType::Laser:
        return "Laser";
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

    if (light.type == RenderLightType::Laser) {
        const RenderFloat3 axis = Normalize(GetRenderLightDirection(light));
        const float beamLength = std::max(light.range, 0.5f);
        const float wavelengthMeters = std::max(light.laserWavelengthNm, 380.0f) * 1e-9f;
        const float beamWaist = std::max(light.laserBeamWaistRadius, 0.0001f);
        const float beamQuality = std::max(light.laserBeamQuality, 1.0f);
        const float divergence = std::clamp((beamQuality * wavelengthMeters) / (3.14159265359f * beamWaist), 0.0001f, 0.5f);
        const float startRadius = std::max(0.001f, light.laserApertureRadius);
        const float endRadius = startRadius + beamLength * std::tan(divergence);
        const RenderFloat3 endPoint = Add(light.transform.translation, Scale(axis, beamLength));
        bounds.min = MakeRenderFloat3(
            std::min(light.transform.translation.x - startRadius, endPoint.x - endRadius),
            std::min(light.transform.translation.y - startRadius, endPoint.y - endRadius),
            std::min(light.transform.translation.z - startRadius, endPoint.z - endRadius));
        bounds.max = MakeRenderFloat3(
            std::max(light.transform.translation.x + startRadius, endPoint.x + endRadius),
            std::max(light.transform.translation.y + startRadius, endPoint.y + endRadius),
            std::max(light.transform.translation.z + startRadius, endPoint.z + endRadius));
        return bounds;
    }

    const float radius = light.type == RenderLightType::Sun
        ? 1.5f
        : (light.type == RenderLightType::Spot ? 0.4f : 0.25f);
    bounds.min = Subtract(light.transform.translation, MakeRenderFloat3(radius, radius, radius));
    bounds.max = Add(light.transform.translation, MakeRenderFloat3(radius, radius, radius));
    return bounds;
}
