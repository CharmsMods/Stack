#include "RenderTransform.h"

#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;

float DegreesToRadians(float degrees) {
    return degrees * (kPi / 180.0f);
}

RenderFloat3 RotateAroundX(const RenderFloat3& point, float radians) {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return MakeRenderFloat3(
        point.x,
        point.y * cosine - point.z * sine,
        point.y * sine + point.z * cosine);
}

RenderFloat3 RotateAroundY(const RenderFloat3& point, float radians) {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return MakeRenderFloat3(
        point.x * cosine + point.z * sine,
        point.y,
        -point.x * sine + point.z * cosine);
}

RenderFloat3 RotateAroundZ(const RenderFloat3& point, float radians) {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return MakeRenderFloat3(
        point.x * cosine - point.y * sine,
        point.x * sine + point.y * cosine,
        point.z);
}

} // namespace

bool NearlyEqual(const RenderTransform& a, const RenderTransform& b, float epsilon) {
    return NearlyEqual(a.translation, b.translation, epsilon) &&
        NearlyEqual(a.rotationDegrees, b.rotationDegrees, epsilon) &&
        NearlyEqual(a.scale, b.scale, epsilon);
}

RenderFloat3 TransformPoint(const RenderTransform& transform, const RenderFloat3& point) {
    RenderFloat3 result = MakeRenderFloat3(
        point.x * transform.scale.x,
        point.y * transform.scale.y,
        point.z * transform.scale.z);

    result = RotateAroundX(result, DegreesToRadians(transform.rotationDegrees.x));
    result = RotateAroundY(result, DegreesToRadians(transform.rotationDegrees.y));
    result = RotateAroundZ(result, DegreesToRadians(transform.rotationDegrees.z));

    result.x += transform.translation.x;
    result.y += transform.translation.y;
    result.z += transform.translation.z;
    return result;
}

RenderFloat3 TransformDirection(const RenderTransform& transform, const RenderFloat3& direction) {
    RenderFloat3 result = direction;
    result = RotateAroundX(result, DegreesToRadians(transform.rotationDegrees.x));
    result = RotateAroundY(result, DegreesToRadians(transform.rotationDegrees.y));
    result = RotateAroundZ(result, DegreesToRadians(transform.rotationDegrees.z));
    return Normalize(result);
}
