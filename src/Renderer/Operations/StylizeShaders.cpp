#include "Renderer/Operations/StylizeShaders.h"
namespace Stack::Renderer::Operations {
const char* StylizeVertex() {return R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";}
const char* CellShadingFragment() {return R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec2 uRes;
uniform int uScalar;
uniform vec3 uLumaWeights;
uniform vec3 uComponents;
uniform vec2 uSourceSize;
uniform int uLevels;
uniform float uBias;
uniform float uGamma;
uniform int uQuantMode;
uniform int uBandMap;
uniform int uEdgeMethod;
uniform float uEdgeStr;
uniform float uEdgeThick;
uniform float uColorPreserve;
uniform int uEdgeEnable;

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

float getLuma(vec3 c) {
    return (uScalar==1 ? c.r : (uQuantMode==1 ? dot(c,uComponents)/max(1.0,dot(uComponents,vec3(1))) : dot(c,uLumaWeights)));
}

float quantize(float value) {
    value = clamp(value + uBias, 0.0, 1.0);
    value = pow(value, max(0.001, uGamma));

    float levels = float(max(2, uLevels));
    float quantized = floor(value * (levels-1.0) + 0.5) / (levels-1.0);

    if (uBandMap == 1) {
        quantized = smoothstep(0.0, 1.0, quantized);
    } else if (uBandMap == 2) {
        quantized = min(levels-1.0,floor(value * levels)) / (levels-1.0);
    }

    return quantized;
}

float sobel(vec2 uv) {
    vec2 px = vec2(uEdgeThick) / max(uSourceSize,vec2(1));
    float l00 = getLuma(texture(uInputTex, uv + vec2(-px.x, -px.y)).rgb);
    float l10 = getLuma(texture(uInputTex, uv + vec2(0.0, -px.y)).rgb);
    float l20 = getLuma(texture(uInputTex, uv + vec2(px.x, -px.y)).rgb);
    float l01 = getLuma(texture(uInputTex, uv + vec2(-px.x, 0.0)).rgb);
    float l21 = getLuma(texture(uInputTex, uv + vec2(px.x, 0.0)).rgb);
    float l02 = getLuma(texture(uInputTex, uv + vec2(-px.x, px.y)).rgb);
    float l12 = getLuma(texture(uInputTex, uv + vec2(0.0, px.y)).rgb);
    float l22 = getLuma(texture(uInputTex, uv + vec2(px.x, px.y)).rgb);

    float gx = l00 + 2.0 * l01 + l02 - (l20 + 2.0 * l21 + l22);
    float gy = l00 + 2.0 * l10 + l20 - (l02 + 2.0 * l12 + l22);
    return sqrt(gx * gx + gy * gy);
}

float robertsCross(vec2 uv) {
    vec2 px = vec2(uEdgeThick) / max(uSourceSize,vec2(1));
    float l00 = getLuma(texture(uInputTex, uv).rgb);
    float l10 = getLuma(texture(uInputTex, uv + vec2(px.x, 0.0)).rgb);
    float l01 = getLuma(texture(uInputTex, uv + vec2(0.0, px.y)).rgb);
    float l11 = getLuma(texture(uInputTex, uv + px).rgb);
    float gx = l00 - l11;
    float gy = l10 - l01;
    return sqrt(gx * gx + gy * gy);
}

float laplacian(vec2 uv) {
    vec2 px = vec2(uEdgeThick) / max(uSourceSize,vec2(1));
    float left = getLuma(texture(uInputTex, uv + vec2(-px.x, 0.0)).rgb);
    float right = getLuma(texture(uInputTex, uv + vec2(px.x, 0.0)).rgb);
    float up = getLuma(texture(uInputTex, uv + vec2(0.0, -px.y)).rgb);
    float down = getLuma(texture(uInputTex, uv + vec2(0.0, px.y)).rgb);
    float center = getLuma(texture(uInputTex, uv).rgb);
    return abs(left + right + up + down - 4.0 * center) * 2.0;
}

void main() {
    vec4 base = texture(uInputTex, vUV);
    vec3 color = base.rgb;
    vec3 result = color;

    if (uQuantMode == 0) {
        float q = quantize(getLuma(color));
        vec3 monochrome = vec3(q);
        vec3 hsv = rgb2hsv(color);
        hsv.z = q;
        vec3 preserved = hsv2rgb(hsv);
        result = mix(monochrome, preserved, clamp(uColorPreserve, 0.0, 1.0));
    } else if (uQuantMode == 1) {
        result.r = quantize(color.r);
        result.g = quantize(color.g);
        result.b = quantize(color.b);
    } else {
        vec3 hsv = rgb2hsv(color);
        hsv.z = quantize(hsv.z);
        result = hsv2rgb(hsv);
    }

    if (uEdgeEnable == 1) {
        float edge = 0.0;
        if (uEdgeMethod == 0) edge = sobel(vUV);
        else if (uEdgeMethod == 1) edge = robertsCross(vUV);
        else edge = laplacian(vUV);

        edge = smoothstep(0.1, 1.0, edge * 2.0);
        result = mix(result, vec3(0.0), edge * clamp(uEdgeStr, 0.0, 2.0));
    }

    FragColor = vec4(clamp(result, 0.0, 1.0) * uComponents, base.a);
}
)";}
const char* PaletteReconstructorFragment() {return R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec3 uPalette[16];
uniform int uPaletteSize;
uniform float uBlend;
uniform float uSmoothing;
uniform int uSmoothingType;
uniform vec2 uSourceSize;

vec3 getWorkingColor(vec2 uv) {
    vec3 base = texture(uInputTex, uv).rgb;
    if (uSmoothing <= 0.0) {
        return base;
    }

    vec2 texel = 1.0 / max(uSourceSize, vec2(1.0));
    float radius = uSmoothing / 10.0;
    vec3 sum = vec3(0.0);
    float totalWeight = 0.0;

    for (float y = -1.0; y <= 1.0; y += 1.0) {
        for (float x = -1.0; x <= 1.0; x += 1.0) {
            vec2 offset = vec2(x, y) * texel * radius;
            float weight = 1.0;
            if (uSmoothingType == 1) {
                float distSq = x * x + y * y;
                weight = exp(-distSq);
            }

            sum += texture(uInputTex, uv + offset).rgb * weight;
            totalWeight += weight;
        }
    }

    return sum / max(totalWeight, 0.0001);
}

void main() {
    vec4 source = texture(uInputTex, vUV);
    if (uPaletteSize <= 0) {
        FragColor = source;
        return;
    }

    vec3 working = getWorkingColor(vUV);
    vec3 bestColor = uPalette[0];
    float minDistance = 1e10;

    for (int i = 0; i < 16; ++i) {
        if (i >= uPaletteSize) break;
        float distanceToColor = distance(working, uPalette[i]);
        if (distanceToColor < minDistance) {
            minDistance = distanceToColor;
            bestColor = uPalette[i];
        }
    }

    vec3 result = mix(working, bestColor, clamp(uBlend, 0.0, 1.0));
    FragColor = vec4(clamp(result, 0.0, 1.0), source.a);
}
)";}
const char* HalftoningFragment() {return R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec2 uRes;
uniform int uScalar;
uniform vec3 uLumaWeights;
uniform vec3 uComponents;
uniform vec2 uSourceSize;
uniform float uSize;
uniform float uIntensity;
uniform float uSharpness;
uniform int uPattern;
uniform int uColorMode;
uniform int uGray;
uniform int uInvert;

float getPattern(vec2 uv, float angle) {
    float s = sin(angle), c = cos(angle);
    vec2 pixel = uv * uSourceSize;
    vec2 p = vec2(c * pixel.x - s * pixel.y, s * pixel.x + c * pixel.y) / max(1.0,uSize);
    vec2 grid = fract(p) - 0.5;
    
    float d = 0.0;
    if (uPattern == 0) { // Circle
        d = length(grid) * 2.0;
    } else if (uPattern == 1) { // Line
        d = abs(grid.y) * 2.0;
    } else if (uPattern == 2) { // Cross
        d = min(abs(grid.x), abs(grid.y)) * 2.0;
    } else { // Diamond
        d = (abs(grid.x) + abs(grid.y));
    }
    
    return d;
}

void main() {
    vec4 col = texture(uInputTex, vUV);
    vec3 outRGB = vec3(0.0);
    col.rgb=clamp(col.rgb,0.0,1.0);
    
    if (uColorMode == 0) { // Luminance
        float l = (uScalar==1 ? col.r : dot(col.rgb,uLumaWeights));
        float pat = getPattern(vUV, 0.785);
        float thresh = 1.0 - l * uIntensity;
        float softness = max(0.0001,1.0 - uSharpness);
        float val = smoothstep(thresh - softness, thresh + softness, pat);
        if (uInvert == 1) val = 1.0 - val;
        outRGB = vec3(val);
    } else if (uColorMode == 1) { // RGB
        float pR = getPattern(vUV, 0.26);
        float pG = getPattern(vUV, 1.30);
        float pB = getPattern(vUV, 0.0);
        float soft = max(0.0001,1.0 - uSharpness);
        float r = smoothstep((1.0 - col.r*uIntensity) - soft, (1.0 - col.r*uIntensity) + soft, pR);
        float g = smoothstep((1.0 - col.g*uIntensity) - soft, (1.0 - col.g*uIntensity) + soft, pG);
        float b = smoothstep((1.0 - col.b*uIntensity) - soft, (1.0 - col.b*uIntensity) + soft, pB);
        outRGB = vec3(r, g, b);
        if (uInvert == 1) outRGB = 1.0 - outRGB;
    } else { // CMY / CMYK
        vec3 cmy = (1.0 - col.rgb)*uIntensity;
        float k = 0.0;
        if (uColorMode == 3) { // CMYK
            k = min(min(cmy.x, cmy.y), cmy.z);
            cmy = k>=1.0 ? vec3(0.0) : (cmy - k) / max(0.000001,1.0 - k);
        }
        float pC = getPattern(vUV, 0.26);
        float pM = getPattern(vUV, 1.30);
        float pY = getPattern(vUV, 0.0);
        float pK = getPattern(vUV, 0.785);
        float soft = max(0.0001,1.0 - uSharpness);
        float hC = 1.0 - smoothstep(cmy.x - soft, cmy.x + soft, pC);
        float hM = 1.0 - smoothstep(cmy.y - soft, cmy.y + soft, pM);
        float hY = 1.0 - smoothstep(cmy.z - soft, cmy.z + soft, pY);
        float hK = 1.0 - smoothstep(k - soft, k + soft, pK);
        vec3 resCMY = vec3(hC, hM, hY);
        if (uColorMode == 3) resCMY += vec3(hK);
        outRGB = 1.0 - clamp(resCMY, 0.0, 1.0);
        if (uInvert == 1) outRGB = 1.0 - outRGB;
    }
    
    if (uGray == 1) {
        float l = dot(outRGB,uLumaWeights);
        outRGB = vec3(l);
    }
    
    FragColor = vec4(outRGB * uComponents, col.a);
}
)";}
}
