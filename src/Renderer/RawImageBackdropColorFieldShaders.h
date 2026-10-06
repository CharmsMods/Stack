#pragma once
#include <string>

namespace Stack::Renderer::RawImageBackdropColorFieldShaders {
// Compact the unbounded photo plane radially, rather than clamping its axes
// to a rectangular atlas edge. Both generation and presentation use this map.
inline constexpr const char* Coordinates = R"(
vec2 colorFieldCoordinate(vec2 imagePoint, vec2 photoAspect) {
    vec2 p = (imagePoint-.5)*photoAspect;
    return .5+.5*p/(2.0+length(p));
}
vec2 colorFieldPhotoPoint(vec2 coordinate) {
    vec2 p = 2.0*coordinate-1.0;
    float radius = length(p);
    // Texels outside the compact disk represent its directional far limit.
    if (radius > .999) p *= .999/radius;
    return 2.0*p/max(.001,1.0-min(radius,.999));
}
)";

inline const std::string Generate = std::string(R"(#version 430 core
in vec2 uv; out vec4 color;
uniform sampler2D image;
uniform vec4 photoBounds;
uniform vec2 photoAspect;
)") + Coordinates + R"(
void main() {
    vec2 p = colorFieldPhotoPoint(uv);
    vec2 outside = max(abs(p)-.5*photoAspect,vec2(0));
    float distance = length(outside);
    // Circular Gaussian weights widen with distance. Linear variance growth
    // retains broad directional color differences even in the far surround.
    float baseSigma = max(.18,.7*max(photoAspect.x,photoAspect.y)/16.0);
    float variance = baseSigma*baseSigma+.45*distance;
    // Subtract the nearest sample's squared distance from every exponent.
    // This common factor cancels on normalization and avoids underflow far
    // outside the photo, including unusually wide panoramas.
    vec2 nearestCell = clamp(floor((p/photoAspect+.5)*16.0),vec2(0),vec2(15));
    vec2 nearest = ((nearestCell+.5)/16.0-.5)*photoAspect;
    vec3 sum = vec3(0);
    float total = 0.0;
    for (int y=0; y<16; ++y) for (int x=0; x<16; ++x) {
        vec2 samplePoint = (vec2(x,y)+.5)/16.0;
        vec2 offset = nearest-(samplePoint-.5)*photoAspect;
        float relativeSquaredDistance = max(0.0,dot(offset,offset)+2.0*dot(p-nearest,offset));
        float weight = exp(-.5*relativeSquaredDistance/variance);
        vec2 sourcePoint = mix(photoBounds.xy,photoBounds.zw,samplePoint);
        sum += weight*textureLod(image,sourcePoint,0.0).rgb;
        total += weight;
    }
    color = vec4(sum/max(.000001,total),1);
})";
}
