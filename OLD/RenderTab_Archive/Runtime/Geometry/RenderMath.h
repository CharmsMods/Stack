#pragma once

struct RenderFloat2 {
    float x = 0.0f;
    float y = 0.0f;

    float* Data() { return &x; }
    const float* Data() const { return &x; }
};

struct RenderFloat3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    float* Data() { return &x; }
    const float* Data() const { return &x; }
};

struct RenderBounds {
    RenderFloat3 min;
    RenderFloat3 max;
};

RenderFloat3 MakeRenderFloat3(float x, float y, float z);
RenderFloat2 MakeRenderFloat2(float x, float y);
RenderFloat2 Add(const RenderFloat2& a, const RenderFloat2& b);
RenderFloat2 Subtract(const RenderFloat2& a, const RenderFloat2& b);
RenderFloat2 Scale(const RenderFloat2& value, float scalar);
RenderFloat3 Add(const RenderFloat3& a, const RenderFloat3& b);
RenderFloat3 Subtract(const RenderFloat3& a, const RenderFloat3& b);
RenderFloat3 Scale(const RenderFloat3& value, float scalar);
float Dot(const RenderFloat3& a, const RenderFloat3& b);
RenderFloat3 Cross(const RenderFloat3& a, const RenderFloat3& b);
float Length(const RenderFloat3& value);
RenderFloat3 Normalize(const RenderFloat3& value);
bool NearlyEqual(float a, float b, float epsilon = 0.0001f);
bool NearlyEqual(const RenderFloat2& a, const RenderFloat2& b, float epsilon = 0.0001f);
bool NearlyEqual(const RenderFloat3& a, const RenderFloat3& b, float epsilon = 0.0001f);
