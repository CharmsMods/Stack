#pragma once
static const char* s_NoiseVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

// Recreated from Shaders/noise.frag and Shaders/composite.frag (Modular Studio V2)
static const char* s_NoiseFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform int   uType;
uniform int   uBlendMode;
uniform float uSeed;
uniform float uScale;
uniform vec2  uOrigRes;
uniform float uParamA;
uniform float uParamB;
uniform float uParamC;
uniform float uStrength; 
uniform float uSatStrength;
uniform float uSatImpact;
uniform float uOpacity;
uniform float uBlurriness; // 0..1 range internally (web is 0..100)

float hash12(vec2 p) {
    vec3 p3  = fract(vec3(p.xyx) * .1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

float IGN(vec2 p) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(p, magic.xy)));
}

float getBlue(vec2 p) {
    float white = IGN(p);
    float low = (IGN(p + vec2(1.0, 0.0)) + IGN(p - vec2(1.0, 0.0)) + IGN(p + vec2(0.0, 1.0)) + IGN(p - vec2(0.0, 1.0))) * 0.25;
    return clamp(white - low + 0.5, 0.0, 1.0); 
}

float perlin(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float overlay(float b, float n) {
    return b < 0.5 ? (2.0 * b * n) : (1.0 - 2.0 * (1.0 - b) * (1.0 - n));
}

// Procedural noise generator
vec3 generateNoise(vec2 pos, float s) {
    vec2 cell = floor(pos / s);
    vec3 n = vec3(0.0);
    
    if (uType == 1 || uType == 2) { // Grayscale
        n = vec3(hash12(cell + uSeed));
    } else if (uType == 0) { // Color White
        n = vec3(hash12(cell + uSeed), hash12(cell + uSeed + 1.23), hash12(cell + uSeed + 2.45));
    } else if (uType == 3) { // Blue Noise (Gray)
        n = vec3(getBlue(cell + uSeed * 11.0));
    } else if (uType == 4) { // Blue Noise (Color)
        n = vec3(getBlue(cell + uSeed * 11.0), getBlue(cell + uSeed * 17.0 + 1.23), getBlue(cell + uSeed * 23.0 + 2.45));
    } else if (uType == 5) { // Perlin (Cloudy)
        float octs = floor(uParamA * 7.0) + 1.0; 
        float persistence = 0.5 + (uParamC - 0.5) * 0.5;
        float noiseSum = 0.0;
        float amp = 1.0;
        float freq = 1.0 / (s * 10.0 + (uParamB * 50.0));
        for(int i = 0; i < 8; i++) {
            if(float(i) >= octs) break;
            noiseSum += perlin(pos * freq + uSeed * 1.5) * amp;
            amp *= persistence;
            freq *= 2.0;
        }
        n = vec3(noiseSum);
    } else if (uType == 6) { // Worley (Cellular)
        float jitter = uParamA;
        float density = 1.0 / (s * 5.0 + (uParamB * 20.0));
        vec2 p = pos * density;
        vec2 n_cell = floor(p);
        vec2 f_coord = fract(p);
        float d = 1.0;
        for(int y = -1; y <= 1; y++) {
            for(int x = -1; x <= 1; x++) {
                vec2 g = vec2(float(x), float(y));
                vec2 o = hash22(n_cell + g) * jitter;
                vec2 r = g + o - f_coord;
                float dist = mix(abs(r.x) + abs(r.y), length(r), uParamC);
                d = min(d, dist);
            }
        }
        n = vec3(d);
    } else if (uType == 7) { // Scanlines
        float thick = mix(0.1, 0.9, uParamA);
        float jitter = (hash12(vec2(uSeed)) - 0.5) * uParamB * 5.0;
        float line = sin((pos.y + jitter) / s * 3.14159) * 0.5 + 0.5;
        float val = step(thick, line);
        n = vec3(mix(val, val * hash12(cell + uSeed), uParamC));
    } else if (uType == 8) { // Speckle (Dust)
        float density = mix(0.8, 0.999, uParamA);
        float h = hash12(cell + uSeed);
        float speck = smoothstep(density, density + mix(0.01, 0.1, uParamB), h);
        float sizeVar = hash12(cell * 0.5 + uSeed);
        n = vec3(speck * mix(1.0, sizeVar, uParamC));
    } else if (uType == 9) { // Glitch
        float blockSize = s * (5.0 + uParamA * 50.0);
        float block = floor(pos.y / blockSize);
        float shift = (hash12(vec2(block, uSeed)) - 0.5) * uParamB * 100.0;
        float split = uParamC * 10.0;
        n = vec3(
            hash12(floor((pos + vec2(shift - split, 0.0)) / s) + uSeed),
            hash12(floor((pos + vec2(shift, 0.0)) / s) + uSeed),
            hash12(floor((pos + vec2(shift + split, 0.0)) / s) + uSeed)
        );
    } else if (uType == 10) { // Anisotropic (Fiber)
        float stretch = 0.01 + uParamA * 0.5;
        float rot = uParamB * 6.28;
        mat2 m = mat2(cos(rot), -sin(rot), sin(rot), cos(rot));
        vec2 p = (m * pos) * vec2(stretch, 1.0) / s;
        float h = hash12(floor(p) + uSeed);
        n = vec3(mix(h, h * hash12(cell + uSeed), uParamC));
    } else {
        n = vec3(hash12(cell + uSeed));
    }
    return n;
}

void main() {
    vec4 bc = texture(uInputTex, vUV);
    vec3 base = bc.rgb;
    vec2 pos = vUV * uOrigRes;
    float s = max(1.0, uScale);
    
    vec3 n;
    if (uBlurriness > 0.0) {
        // Simple 5-tap procedural blur
        float b = uBlurriness * s * 2.0;
        n = generateNoise(pos, s) * 0.4;
        n += generateNoise(pos + vec2(b, 0.0), s) * 0.15;
        n += generateNoise(pos + vec2(-b, 0.0), s) * 0.15;
        n += generateNoise(pos + vec2(0.0, b), s) * 0.15;
        n += generateNoise(pos + vec2(0.0, -b), s) * 0.15;
    } else {
        n = generateNoise(pos, s);
    }
    
    vec3 res;
    if (uType == 2) { // Blend (Sat)
        float noiseVal = n.r; 
        float centered = (noiseVal - 0.5) * 2.0;
        float delta = centered * (uSatStrength * (1.0 + uSatImpact/100.0));
        float lum = dot(base, vec3(0.2126, 0.7152, 0.0722));
        float effectStr = uStrength/50.0;
        res = mix(vec3(lum), base, 1.0 + delta * effectStr); 
    } else {
        vec3 lumVec = vec3(0.299, 0.587, 0.114);
        float gNoise = dot(n, lumVec);
        vec3 noiseLayer = mix(vec3(gNoise), n, uSatStrength);
        
        if (uBlendMode == 0) { res = noiseLayer; }
        else if (uBlendMode == 1) { 
            res.r = overlay(base.r, noiseLayer.r);
            res.g = overlay(base.g, noiseLayer.g);
            res.b = overlay(base.b, noiseLayer.b);
        }
        else if (uBlendMode == 2) { res = 1.0 - (1.0 - base) * (1.0 - noiseLayer); }
        else if (uBlendMode == 3) { res = base * noiseLayer; }
        else if (uBlendMode == 4) { res = base + noiseLayer; }
        else if (uBlendMode == 5) { res = abs(base - noiseLayer); }
    }

    float finalStr = uOpacity * (uStrength / 50.0);
    vec3 finalColor = mix(base, res, clamp(finalStr, 0.0, 1.0));
    FragColor = vec4(finalColor, bc.a);
}
)";

