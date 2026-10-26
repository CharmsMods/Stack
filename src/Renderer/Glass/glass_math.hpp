#pragma once

// Original research scaffold for Stack. No OpenGL, ImGui, or math-library dependency.
// Geometry units are physical framebuffer pixels; z increases toward the viewer.

#include <algorithm>
#include <cmath>

namespace stack::glass {

struct Vec2 { float x = 0.0f, y = 0.0f; };
struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x+b.x, a.y+b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x-b.x, a.y-b.y}; }
inline Vec2 operator-(Vec2 a) { return {-a.x, -a.y}; }
inline Vec2 operator*(Vec2 a, float b) { return {a.x*b, a.y*b}; }
inline Vec2 operator*(float b, Vec2 a) { return a*b; }
inline Vec2 operator/(Vec2 a, float b) { return {a.x/b, a.y/b}; }
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 operator*(Vec3 a, float b) { return {a.x*b, a.y*b, a.z*b}; }
inline Vec3 operator*(float b, Vec3 a) { return a*b; }
inline Vec3 operator/(Vec3 a, float b) { return {a.x/b, a.y/b, a.z/b}; }

inline float dot(Vec2 a, Vec2 b) { return a.x*b.x+a.y*b.y; }
inline float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline float length(Vec2 a) { return std::sqrt(dot(a,a)); }
inline float length(Vec3 a) { return std::sqrt(dot(a,a)); }
inline Vec2 normalized(Vec2 a, Vec2 fallback = {1.0f,0.0f}) {
    const float l=length(a); return l>1.0e-12f ? a/l : fallback;
}
inline Vec3 normalized(Vec3 a, Vec3 fallback = {0.0f,0.0f,1.0f}) {
    const float l=length(a); return l>1.0e-12f ? a/l : fallback;
}
inline bool finite(Vec2 a) { return std::isfinite(a.x)&&std::isfinite(a.y); }
inline bool finite(Vec3 a) { return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z); }
inline float clamp(float x, float a, float b) { return std::max(a,std::min(b,x)); }
inline float mix(float a, float b, float t) { return a+(b-a)*t; }
inline Vec2 mix(Vec2 a, Vec2 b, float t) { return a+(b-a)*t; }
inline Vec3 mix(Vec3 a, Vec3 b, float t) { return a+(b-a)*t; }
inline float signNonzero(float x) { return x<0.0f ? -1.0f : 1.0f; }
inline Vec3 reflected(Vec3 incident, Vec3 unitNormal) {
    return incident-unitNormal*(2.0f*dot(incident,unitNormal));
}

struct Bounds2 { Vec2 minimum, maximum; };
struct Bounds3 { Vec3 minimum, maximum; };
struct Ray3 { Vec3 origin, direction; };

inline Bounds2 padded(Bounds2 b, float amount) {
    return {b.minimum-Vec2{amount,amount}, b.maximum+Vec2{amount,amount}};
}
inline Bounds2 united(Bounds2 a, Bounds2 b) {
    return {{std::min(a.minimum.x,b.minimum.x),std::min(a.minimum.y,b.minimum.y)},
            {std::max(a.maximum.x,b.maximum.x),std::max(a.maximum.y,b.maximum.y)}};
}

// Returns the positive ray exit from a containing AABB. The caller checks that
// the ray starts inside the actual glass. No assumed framebuffer dimensions.
inline bool rayBoundsInterval(const Ray3& ray, Bounds3 b, float& nearT, float& farT) {
    if (!finite(ray.origin)||!finite(ray.direction)||!finite(b.minimum)||!finite(b.maximum)) return false;
    nearT=-INFINITY; farT=INFINITY;
    const float o[3]={ray.origin.x,ray.origin.y,ray.origin.z};
    const float d[3]={ray.direction.x,ray.direction.y,ray.direction.z};
    const float mn[3]={b.minimum.x,b.minimum.y,b.minimum.z};
    const float mx[3]={b.maximum.x,b.maximum.y,b.maximum.z};
    for (int i=0;i<3;++i) {
        if (mn[i]>mx[i]) return false;
        if (std::abs(d[i])<1.0e-12f) {
            if (o[i]<mn[i]||o[i]>mx[i]) return false;
        } else {
            float a=(mn[i]-o[i])/d[i], z=(mx[i]-o[i])/d[i];
            if (a>z) std::swap(a,z);
            nearT=std::max(nearT,a); farT=std::min(farT,z);
        }
    }
    return farT>=std::max(nearT,0.0f)&&std::isfinite(farT);
}

} // namespace stack::glass
