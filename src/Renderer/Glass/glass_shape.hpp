#pragma once

#include "glass_math.hpp"

namespace stack::glass {

struct ShapeSample {
    float distance = 0.0f; // signed distance or conservative distance bound, px
    Vec2 gradient = {1.0f,0.0f}; // actual derivative, NOT always a unit vector
};

inline ShapeSample sampleCircle(Vec2 point, Vec2 center, float radiusPx) {
    const Vec2 p=point-center;
    return {length(p)-radiusPx, normalized(p)};
}

inline ShapeSample sampleRoundedBox(Vec2 point, Vec2 center, Vec2 halfSizePx, float radiusPx) {
    const Vec2 p=point-center;
    const float r=clamp(radiusPx,0.0f,std::min(halfSizePx.x,halfSizePx.y));
    const Vec2 q={std::abs(p.x)-halfSizePx.x+r,std::abs(p.y)-halfSizePx.y+r};
    const Vec2 outside={std::max(q.x,0.0f),std::max(q.y,0.0f)};
    const float l=length(outside);
    const float d=l+std::min(std::max(q.x,q.y),0.0f)-r;
    Vec2 n;
    if (l>1.0e-12f) n={outside.x/l*signNonzero(p.x),outside.y/l*signNonzero(p.y)};
    else if (q.x>q.y) n={signNonzero(p.x),0.0f};
    else n={0.0f,signNonzero(p.y)};
    return {d,n};
}

inline ShapeSample sampleCapsule(Vec2 point, Vec2 a, Vec2 b, float radiusPx) {
    const Vec2 ab=b-a;
    const float ab2=dot(ab,ab);
    const float t=ab2>1.0e-12f ? clamp(dot(point-a,ab)/ab2,0.0f,1.0f) : 0.0f;
    return sampleCircle(point,a+ab*t,radiusPx);
}

inline ShapeSample hardUnion(ShapeSample a, ShapeSample b) {
    return a.distance<=b.distance ? a : b;
}

// Compact polynomial smooth minimum. It inflates the silhouette by up to k/4
// for a pair. Make group bounds conservative and never union unrelated widgets.
// Derivative is the weighted combination, without an extra normalization.
inline ShapeSample smoothUnion(ShapeSample a, ShapeSample b, float blendPx) {
    if (!(blendPx>0.0f)) return hardUnion(a,b);
    const float h=clamp(0.5f+0.5f*(b.distance-a.distance)/blendPx,0.0f,1.0f);
    return {mix(b.distance,a.distance,h)-blendPx*h*(1.0f-h),mix(b.gradient,a.gradient,h)};
}

struct LensProfile {
    float bevelPx = 8.0f;
    float halfThicknessPx = 3.0f;
    float centerZPx = 8.0f;
    bool valid() const {
        return std::isfinite(bevelPx)&&std::isfinite(halfThicknessPx)&&
               std::isfinite(centerZPx)&&bevelPx>0.0f&&halfThicknessPx>0.0f;
    }
};

struct LensSample {
    float distanceBound = 0.0f;
    Vec3 gradient = {0.0f,0.0f,1.0f};
    Vec3 outwardNormal = {0.0f,0.0f,1.0f};
};

// A closed, elliptical-bevel volume. For an exact/1-Lipschitz footprint d,
// phi=min(b,h)*(length(max(0,(d+b)/b),(z-z0)/h)-1)
// is a conservative signed distance bound. It is NOT the exact 3D distance.
// Circle radius R + bevel R makes an exact oblate/prolate spheroid surface.
template<class Footprint>
inline LensSample sampleLens(Vec3 p, const LensProfile& profile, const Footprint& footprint) {
    const ShapeSample f=footprint(Vec2{p.x,p.y});
    const float b=profile.bevelPx,h=profile.halfThicknessPx;
    const float q=std::max(0.0f,(f.distance+b)/b);
    const float v=(p.z-profile.centerZPx)/h;
    const float l=std::sqrt(q*q+v*v);
    const float scale=std::min(b,h);
    Vec3 grad={q/b*f.gradient.x,q/b*f.gradient.y,v/h};
    if (l>1.0e-12f) grad=grad*(scale/l);
    else grad={0.0f,0.0f,0.0f};
    return {scale*(l-1.0f),grad,normalized(grad)};
}

inline Bounds3 lensBounds(Bounds2 footprintBounds, const LensProfile& p) {
    return {{footprintBounds.minimum.x,footprintBounds.minimum.y,p.centerZPx-p.halfThicknessPx},
            {footprintBounds.maximum.x,footprintBounds.maximum.y,p.centerZPx+p.halfThicknessPx}};
}

template<class Footprint>
inline bool frontSurface(Vec2 point, const LensProfile& p, const Footprint& footprint,
                         Vec3& position, Vec3& outwardNormal) {
    if (!p.valid()||!finite(point)) return false;
    const ShapeSample f=footprint(point);
    if (!std::isfinite(f.distance)||!finite(f.gradient)||f.distance>0.0f) return false;
    const float q=clamp((f.distance+p.bevelPx)/p.bevelPx,0.0f,1.0f);
    position={point.x,point.y,p.centerZPx+p.halfThicknessPx*std::sqrt(std::max(0.0f,1.0f-q*q))};
    outwardNormal=sampleLens(position,p,footprint).outwardNormal;
    return finite(outwardNormal);
}

} // namespace stack::glass
