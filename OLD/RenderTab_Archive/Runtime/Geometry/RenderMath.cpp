#include "RenderMath.h"

#include <cmath>

RenderFloat3 MakeRenderFloat3(float x, float y, float z) {
    return RenderFloat3 { x, y, z };
}

RenderFloat2 MakeRenderFloat2(float x, float y) {
    return RenderFloat2 { x, y };
}

RenderFloat2 Add(const RenderFloat2& a, const RenderFloat2& b) {
    return MakeRenderFloat2(a.x + b.x, a.y + b.y);
}

RenderFloat2 Subtract(const RenderFloat2& a, const RenderFloat2& b) {
    return MakeRenderFloat2(a.x - b.x, a.y - b.y);
}

RenderFloat2 Scale(const RenderFloat2& value, float scalar) {
    return MakeRenderFloat2(value.x * scalar, value.y * scalar);
}

RenderFloat3 Add(const RenderFloat3& a, const RenderFloat3& b) {
    return MakeRenderFloat3(a.x + b.x, a.y + b.y, a.z + b.z);
}

RenderFloat3 Subtract(const RenderFloat3& a, const RenderFloat3& b) {
    return MakeRenderFloat3(a.x - b.x, a.y - b.y, a.z - b.z);
}

RenderFloat3 Scale(const RenderFloat3& value, float scalar) {
    return MakeRenderFloat3(value.x * scalar, value.y * scalar, value.z * scalar);
}

float Dot(const RenderFloat3& a, const RenderFloat3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

RenderFloat3 Cross(const RenderFloat3& a, const RenderFloat3& b) {
    return MakeRenderFloat3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x);
}

float Length(const RenderFloat3& value) {
    return std::sqrt(Dot(value, value));
}

RenderFloat3 Normalize(const RenderFloat3& value) {
    const float length = Length(value);
    if (length < 0.0001f) {
        return MakeRenderFloat3(0.0f, 0.0f, 0.0f);
    }

    return Scale(value, 1.0f / length);
}

bool NearlyEqual(float a, float b, float epsilon) {
    return std::fabs(a - b) < epsilon;
}

bool NearlyEqual(const RenderFloat2& a, const RenderFloat2& b, float epsilon) {
    return NearlyEqual(a.x, b.x, epsilon) &&
        NearlyEqual(a.y, b.y, epsilon);
}

bool NearlyEqual(const RenderFloat3& a, const RenderFloat3& b, float epsilon) {
    return NearlyEqual(a.x, b.x, epsilon) &&
        NearlyEqual(a.y, b.y, epsilon) &&
        NearlyEqual(a.z, b.z, epsilon);
}
