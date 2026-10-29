#pragma once
#include "RawImageBackdropNoiseShaders.h"
#include "RawImageBackdropColorFieldShaders.h"
#include "RawImageBackdropDisplayShaders.h"
#include <string>
#include "RawImageBackdropUniforms.h"
namespace Stack::Renderer::RawImageBackdropShaders {
inline constexpr const char* Vertex = R"(#version 430 core
out vec2 uv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    uv = p; gl_Position = vec4(p * 2.0 - 1.0, 0, 1);
})";
inline const std::string Copy = std::string(R"(#version 430 core
in vec2 uv; out vec4 color;
)")
+ BackdropUniforms::Declare("sampler2D", "image", BackdropUniforms::Copy::Image)
+ BackdropUniforms::Declare("sampler2D", "previous", BackdropUniforms::Copy::Previous)
+ BackdropUniforms::Declare("vec4", "rect", BackdropUniforms::Copy::Rect)
+ BackdropUniforms::Declare("vec4", "textureRect", BackdropUniforms::Copy::TextureRect)
+ BackdropUniforms::Declare("vec4", "extendEdges", BackdropUniforms::Copy::ExtendEdges)
+ BackdropUniforms::Declare("vec2", "previousOffset", BackdropUniforms::Copy::PreviousOffset)
+ BackdropUniforms::Declare("vec2", "previousScale", BackdropUniforms::Copy::PreviousScale)
+ BackdropUniforms::Declare("float", "fadeAmount", BackdropUniforms::Copy::FadeAmount)
+ BackdropUniforms::Declare("bool", "encodedSrgb", BackdropUniforms::Copy::EncodedSrgb)
+ BackdropUniforms::Declare("bool", "visibleColorOnly", BackdropUniforms::Copy::VisibleColorOnly) + RawImageBackdropDisplayShaders::Functions + R"(
void main() {
    vec2 local = (uv - rect.xy) / (rect.zw - rect.xy);
    if ((local.x < 0.0 && extendEdges.x < .5) || (local.y < 0.0 && extendEdges.y < .5) ||
        (local.x > 1.0 && extendEdges.z < .5) || (local.y > 1.0 && extendEdges.w < .5)) discard;
    vec2 p = mix(textureRect.xy, textureRect.zw, clamp(local, 0.0, 1.0));
    color = visibleColorOnly ? backdropVisibleSample(image,p) : texture(image,p);
    if (fadeAmount < 1.0) {
        vec2 previousPoint = previousOffset + p * previousScale;
        vec4 a = visibleColorOnly ? backdropVisibleSample(previous,previousPoint) : texture(previous,previousPoint);
        float alpha = mix(a.a, color.a, fadeAmount);
        vec3 ar = encodedSrgb ? backdropDisplayDecode(a.rgb) : a.rgb;
        vec3 br = encodedSrgb ? backdropDisplayDecode(color.rgb) : color.rgb;
        vec3 rgb = alpha > .000001 ? mix(ar*a.a, br*color.a, fadeAmount)/alpha : vec3(0);
        color = vec4(encodedSrgb ? backdropDisplayEncode(rgb) : rgb, alpha);
    }
    // Match the ordinary photo's framebuffer clipping before the edge blend.
    // Only the cached surround atlas changes to display-linear light; the
    // sharp copy retains exactly the window RGB representation.
    color.rgb = backdropDisplayRange(color.rgb);
    if (visibleColorOnly) color.rgb = backdropDisplayDecode(color.rgb);
})";
inline constexpr const char* Blur = R"(#version 430 core
in vec2 uv; out vec4 color;
uniform sampler2D image;
uniform vec4 photo;
uniform vec2 direction;
uniform float sigma;
uniform int radius;
vec2 sourcePoint(vec2 p) {
    vec2 texel = .5 / vec2(textureSize(image, 0));
    vec2 lo = min(max(photo.xy, texel), vec2(1)-texel);
    vec2 hi = max(lo, min(photo.zw, vec2(1)-texel));
    vec2 span = max(hi-lo, texel);
    // Mirrored boundaries gather interior colors instead of stretching a row
    // of edge pixels. This also works when a kernel exceeds a small image.
    vec2 phase = mod(p-lo, 2.0*span);
    return clamp(lo + span - abs(phase-span), lo, hi);
}
void main() {
    vec4 sum = texture(image, sourcePoint(uv));
    float total = 1.0;
    // Adjacent Gaussian weights share a bilinear fetch. Two separable passes
    // cover the kernel instead of sparse 2D taps over box-filtered mipmaps.
    for (int i = 1; i <= 12; i += 2) {
        if (i > radius) break;
        float a = exp(-.5*float(i*i)/(sigma*sigma));
        float b = exp(-.5*float((i+1)*(i+1))/(sigma*sigma));
        float weight = a+b;
        vec2 offset = direction * (float(i)+b/weight);
        sum += weight * (texture(image, sourcePoint(uv+offset)) + texture(image, sourcePoint(uv-offset)));
        total += 2.0*weight;
    }
    color = sum/total;
})";
inline const std::string Present = std::string(R"(#version 430 core
in vec2 uv; out vec4 color;
)")
+ BackdropUniforms::Declare("sampler2D", "image", BackdropUniforms::Present::Image)
+ BackdropUniforms::Declare("sampler2D", "blurred[7]", BackdropUniforms::Present::Blurred)
+ BackdropUniforms::Declare("sampler2D", "blurColorField", BackdropUniforms::Present::ColorField)
+ BackdropUniforms::Declare("float", "blurSigma[7]", BackdropUniforms::Present::Sigma)
+ BackdropUniforms::Declare("vec4", "photo", BackdropUniforms::Present::Photo)
+ BackdropUniforms::Declare("vec4", "controls", BackdropUniforms::Present::Controls)
+ BackdropUniforms::Declare("vec4", "blurPhotoBounds", BackdropUniforms::Present::BlurPhotoBounds)
+ BackdropUniforms::Declare("vec2", "screenSize", BackdropUniforms::Present::ScreenSize)
+ BackdropUniforms::Declare("vec3", "background", BackdropUniforms::Present::Background)
+ BackdropUniforms::Declare("bool", "extendImage", BackdropUniforms::Present::ExtendImage)
+ BackdropUniforms::Declare("float", "extensionOpacity", BackdropUniforms::Present::ExtensionOpacity)
+ BackdropUniforms::Declare("float", "edgeOverlap", BackdropUniforms::Present::EdgeOverlap)
+ BackdropUniforms::Declare("float", "controlsAmount", BackdropUniforms::Present::ControlsAmount)
+ BackdropUniforms::Declare("sampler2DArray", "noiseVariance", BackdropUniforms::Present::NoiseVariance)
+ BackdropUniforms::Declare("sampler2DArray", "noiseCovariance", BackdropUniforms::Present::NoiseCovariance)
+ BackdropUniforms::Declare("sampler2DArray", "noiseGrain", BackdropUniforms::Present::NoiseGrain)
+ BackdropUniforms::Declare("sampler2DArray", "noiseBrightnessVariance", BackdropUniforms::Present::NoiseBrightnessVariance)
+ BackdropUniforms::Declare("sampler2DArray", "noiseBrightnessCovariance", BackdropUniforms::Present::NoiseBrightnessCovariance)
+ BackdropUniforms::Declare("vec2", "noisePhysicalSize", BackdropUniforms::Present::NoisePhysicalSize)
+ BackdropUniforms::Declare("vec2", "noiseNativeExtent", BackdropUniforms::Present::NoiseNativeExtent)
+ BackdropUniforms::Declare("bool", "noiseEnabled", BackdropUniforms::Present::NoiseEnabled) + RawImageBackdropDisplayShaders::Functions + RawImageBackdropColorFieldShaders::Coordinates + RawImageBackdropNoiseShaders::Synthesis + R"(
vec3 sampleLevel(int level, vec2 p) {
    // Constant sampler indices also work on drivers requiring uniform indices.
    if (level == 0) return texture(blurred[0], p).rgb;
    if (level == 1) return texture(blurred[1], p).rgb;
    if (level == 2) return texture(blurred[2], p).rgb;
    if (level == 3) return texture(blurred[3], p).rgb;
    if (level == 4) return texture(blurred[4], p).rgb;
    if (level == 5) return texture(blurred[5], p).rgb;
    return texture(blurred[6], p).rgb;
}
vec3 gaussian(vec2 p, float sigma, vec3 sharp) {
    // Returns display-linear light. Only the optional sharp endpoint needs
    // decoding; the cached levels already contain that representation.
    float previous = 0.0;
    for (int i = 0; i < 7; ++i) {
        if (sigma <= blurSigma[i]) {
            vec3 a = i == 0 ? backdropDisplayDecode(sharp) : sampleLevel(i-1, p);
            vec3 b = sampleLevel(i, p);
            float t = clamp((sigma*sigma-previous*previous) /
                max(.0001, blurSigma[i]*blurSigma[i]-previous*previous), 0.0, 1.0);
            return mix(a, b, t);
        }
        previous = blurSigma[i];
    }
    return sampleLevel(6, p);
}
float controlBlurCoverage() {
    if (controlsAmount <= 0.0) return 0.0;
    vec2 halfSize = max(vec2(0), (controls.zw-controls.xy)*.5*screenSize);
    vec2 center = (controls.xy+controls.zw)*.5*screenSize;
    float corner = min(32.0, min(halfSize.x, halfSize.y));
    vec2 q = abs(uv*screenSize-center)-halfSize+corner;
    float distance = length(max(q,vec2(0)))+min(max(q.x,q.y),0.0)-corner;
    // Feather inward, leaving the empty caption and the gap beside controls clear.
    float feather = min(48.0, max(1.0, min(halfSize.x, halfSize.y)));
    return smoothstep(0.0, feather, -distance)*controlsAmount;
}
void main() {
    vec2 outside = max(max(photo.xy-uv, uv-photo.zw), vec2(0))*screenSize;
    float distance = length(outside);
    // Only feather photo edges actually inside the window. When an axis is
    // covered, its edge must not introduce a blur strip at the screen border.
    vec4 sides = vec4((photo.xy-uv)*screenSize, (uv-photo.zw)*screenSize);
    if (photo.x <= 0.0) sides.x = -1e6;
    if (photo.y <= 0.0) sides.y = -1e6;
    if (photo.z >= 1.0) sides.z = -1e6;
    if (photo.w >= 1.0) sides.w = -1e6;
    float signedDistance = max(max(sides.x,sides.y),max(sides.z,sides.w));
    if (distance > 0.0 && !extendImage) { color = vec4(background, 1.0); return; }
    // The cached Gaussian includes padding around the complete photo. Keep
    // outside coordinates here: clamping before the Gaussian lookup stretches
    // one filtered edge row/column into persistent bands instead of sampling
    // colors that have spread in two dimensions through the surrounding area.
    vec2 photoSize = max((photo.zw-photo.xy)*screenSize, vec2(.000001));
    vec2 imagePoint = (uv-photo.xy)/max(photo.zw-photo.xy,vec2(.000001));
    vec2 p = blurPhotoBounds.xy + imagePoint*(blurPhotoBounds.zw-blurPhotoBounds.xy);
    // The copy already extends boundary texels. Clamping this sharp sample to
    // the geometric photo edge resampled a different subpixel position outside
    // the photo, exposing a thin line beside the ordinary image draw.
    vec3 sharp = texture(image, uv).rgb;
    vec3 result = sharp;
    float sigma = 0.0;
    float blurredWeight = 0.0;
    if (extendImage && signedDistance > -edgeOverlap) {
        // Finish the sharp/Gaussian crossfade at the photo boundary. The copy
        // clamps outside pixels to the outermost photo row/column; keeping any
        // of that sharp color outside stretches its noise or color bias into
        // a stripe. The Gaussian radius still grows through the surround.
        // Keep the Gaussian join local before transitioning to the broad
        // color field. Its radius and transition distances follow photo scale.
        float shortEdge = min(photoSize.x, photoSize.y);
        float baseSigma = max(blurSigma[0], min(10.0, .015*shortEdge));
        float maximumSigma = max(baseSigma, .30*shortEdge);
        sigma = mix(baseSigma, maximumSigma, 1.0-exp(-distance/max(1.0,.45*shortEdge)));
        vec3 soft = gaussian(p, sigma, sharp);
        // Keep the local join, then replace rectangular edge repetition with
        // a normalized mixture of circular Gaussian color contributions. The
        // compact radial lookup keeps all outside positions in photo space.
        float fieldWeight = smoothstep(.05,.5,distance/max(.000001,shortEdge));
        if (fieldWeight > 0.0) {
            vec2 fieldPoint = colorFieldCoordinate(imagePoint,photoSize/shortEdge);
            soft = mix(soft,texture(blurColorField,fieldPoint).rgb,fieldWeight);
        }
        // Gaussian levels and the broad field contain display-linear light.
        // Rejoin the window RGB photo and measure/add grain in that domain.
        soft = backdropDisplayEncode(soft);
        // An edge entering the window on zoom-out gains its feather gradually
        // instead of suddenly softening a full strip at the window boundary.
        vec4 visibleMargins = vec4(photo.xy*screenSize, (vec2(1)-photo.zw)*screenSize);
        vec4 weights = smoothstep(vec4(-edgeOverlap), vec4(0), sides) *
            smoothstep(vec4(0), vec4(edgeOverlap), visibleMargins);
        float feather = max(max(weights.x,weights.y),max(weights.z,weights.w));
        // Window-entry feathering affects only the photo interior. Outside
        // color must not change when the same edge approaches a window border.
        blurredWeight = distance > 0.0 ? 1.0 : feather*extensionOpacity;
        result = mix(sharp, soft, blurredWeight);
    }
    float coverage = controlBlurCoverage();
    if (coverage > 0.0) {
        result = mix(result, backdropDisplayEncode(gaussian(p, max(sigma,64.0), sharp)), coverage);
        blurredWeight = 1.0-(1.0-blurredWeight)*(1.0-coverage);
    }
    result += matchedNoise(blurredWeight, coverage>0.0 ? max(sigma,64.0) : sigma,result);
    // Only the generated surround follows the presentation toggle. Real photo
    // pixels and their update fades remain independent of this appearance.
    if (distance > 0.0) result = mix(background, result, extensionOpacity);
    color = vec4(result, 1.0);
})";
}
