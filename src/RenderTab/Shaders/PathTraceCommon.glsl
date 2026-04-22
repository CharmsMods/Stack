const float kPi = 3.14159265359;
const float kTau = 6.28318530718;
const float kEpsilon = 0.0001;
const float kRayMaxDistance = 10000.0;
const int kDebugModeNone = 0;
const int kDebugModeSelectedRayLog = 1;
const int kDebugModeRefractedSourceClass = 2;
const int kDebugModeSelfHitHeatmap = 3;
const int kDebugDecisionNone = 0;
const int kDebugDecisionReflect = 1;
const int kDebugDecisionRefract = 2;
const int kDebugDecisionThinSheetPass = 3;
const int kDebugDecisionTotalInternalReflection = 4;
const int kDebugSourceClassNone = 0;
const int kDebugSourceClassEmissive = 1;
const int kDebugSourceClassDark = 2;
const int kDebugSourceClassMiss = 3;
const int kDebugMaxBounceCount = 8;
const int kLayerTypeBaseDiffuse = 0;
const int kLayerTypeBaseMetal = 1;
const int kLayerTypeBaseDielectric = 2;
const int kLayerTypeClearCoat = 3;

struct MaterialData {
    vec4 baseCoefficients;
    vec4 emissionCoefficients;
    vec4 absorptionCoefficients;
    vec4 params0;
    vec4 params1;
    vec4 params2;
    ivec4 layerInfo;
};

struct MaterialLayerData {
    vec4 coefficients;
    vec4 params0;
    vec4 params1;
};

struct SphereData {
    vec4 centerRadius;
    vec4 surface;
};

struct TriangleData {
    vec4 a;
    vec4 b;
    vec4 c;
    vec4 normalA;
    vec4 normalB;
    vec4 normalC;
    vec4 uvAuvB;
    vec4 uvC;
    vec4 surface;
};

struct PrimitiveRefData {
    ivec4 meta;
};

struct BvhNodeData {
    vec4 minBounds;
    vec4 maxBounds;
    ivec4 meta0;
    ivec4 meta1;
};

struct LightData {
    vec4 positionType;
    vec4 directionRange;
    vec4 colorCoefficients;
    vec4 params;
    vec4 basis0;
    vec4 basis1;
};

struct EnvironmentData {
    vec4 zenithCoefficients;
    vec4 horizonCoefficients;
    vec4 groundCoefficients;
    vec4 params;
};

struct RayState {
    vec4 origin;
    vec4 direction;
    vec4 throughput;
    vec4 radiance;
    vec4 wavelengths;
    vec4 wavelengthPdfs;
    vec4 absorptionCoefficients;
    ivec4 meta;
    vec4 params;
};

struct HitState {
    vec4 hit0;
    vec4 hit1;
    vec4 hit2;
    vec4 hit3;
};

struct ShadowRay {
    vec4 originMaxDistance;
    vec4 directionContributionScale;
    vec4 contribution;
    ivec4 meta;
};

struct DebugRayBounce {
    vec4 hitInfo;
    vec4 geometricNormal;
    vec4 shadingNormal;
    vec4 etaInfo;
    vec4 spawnedOrigin;
    vec4 spawnedDirection;
};

layout(std430, binding = 0) readonly buffer MaterialBuffer {
    MaterialData uMaterials[];
};

layout(std430, binding = 1) readonly buffer MaterialLayerBuffer {
    MaterialLayerData uMaterialLayers[];
};

layout(std430, binding = 2) readonly buffer SphereBuffer {
    SphereData uSpheres[];
};

layout(std430, binding = 3) readonly buffer TriangleBuffer {
    TriangleData uTriangles[];
};

layout(std430, binding = 4) readonly buffer PrimitiveRefBuffer {
    PrimitiveRefData uPrimitiveRefs[];
};

layout(std430, binding = 5) readonly buffer BvhBuffer {
    BvhNodeData uBvhNodes[];
};

layout(std430, binding = 6) readonly buffer LightBuffer {
    LightData uLights[];
};

layout(std430, binding = 7) readonly buffer EnvironmentBuffer {
    EnvironmentData uEnvironment;
};

layout(std430, binding = 8) buffer RayStateBuffer {
    RayState uRayStates[];
};

layout(std430, binding = 9) buffer HitStateBuffer {
    HitState uHitStates[];
};

layout(std430, binding = 10) buffer ActiveQueueBuffer {
    uint uActiveQueue[];
};

layout(std430, binding = 11) buffer NextQueueBuffer {
    uint uNextQueue[];
};

layout(std430, binding = 12) buffer ShadowQueueBuffer {
    ShadowRay uShadowQueue[];
};

layout(std430, binding = 13) buffer QueueCountBuffer {
    uint uActiveCount;
    uint uNextCount;
    uint uShadowCount;
    uint uPixelCountStored;
};

layout(std430, binding = 14) buffer DebugRayLogBuffer {
    ivec4 uDebugRayHeader;
    DebugRayBounce uDebugRayBounces[kDebugMaxBounceCount];
};

uniform ivec2 uResolution;
uniform int uPixelCount;
uniform int uSampleIndex;
uniform int uEpoch;
uniform int uBounceIndex;
uniform int uMaxBounces;
uniform int uSphereCount;
uniform int uTriangleCount;
uniform int uPrimitiveRefCount;
uniform int uBvhNodeCount;
uniform int uLightCount;
uniform int uAccumulationEnabled;
uniform vec3 uCameraPosition;
uniform float uYawDegrees;
uniform float uPitchDegrees;
uniform float uFieldOfViewDegrees;
uniform float uFocusDistance;
uniform float uApertureRadius;
uniform float uExposure;
uniform int uDebugMode;
uniform ivec2 uDebugPixel;

uint wangHash(uint seed) {
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

float randomFloat(inout uint state) {
    state = wangHash(state);
    return float(state) / 4294967296.0;
}

vec2 randomFloat2(inout uint state) {
    return vec2(randomFloat(state), randomFloat(state));
}

ivec2 PixelCoordFromIndex(uint pixelIndex) {
    return ivec2(int(pixelIndex % uint(max(uResolution.x, 1))), int(pixelIndex / uint(max(uResolution.x, 1))));
}

vec3 ForwardFromYawPitch(float yawDegrees, float pitchDegrees) {
    float yaw = radians(yawDegrees);
    float pitch = radians(pitchDegrees);
    return normalize(vec3(
        cos(pitch) * cos(yaw),
        sin(pitch),
        cos(pitch) * sin(yaw)));
}

vec3 RightFromForward(vec3 forward) {
    vec3 right = cross(forward, vec3(0.0, 1.0, 0.0));
    if (length(right) <= kEpsilon) {
        return vec3(1.0, 0.0, 0.0);
    }
    return normalize(right);
}

vec3 UpFromBasis(vec3 right, vec3 forward) {
    return normalize(cross(right, forward));
}

vec2 SampleConcentricDisk(vec2 xi) {
    vec2 offset = xi * 2.0 - 1.0;
    if (abs(offset.x) < kEpsilon && abs(offset.y) < kEpsilon) {
        return vec2(0.0);
    }

    float radius = 0.0;
    float theta = 0.0;
    if (abs(offset.x) > abs(offset.y)) {
        radius = offset.x;
        theta = 0.25 * kPi * (offset.y / offset.x);
    } else {
        radius = offset.y;
        theta = 0.5 * kPi - 0.25 * kPi * (offset.x / offset.y);
    }
    return radius * vec2(cos(theta), sin(theta));
}

vec4 SampleHeroWavelengths(inout uint rngState) {
    vec4 wavelengths = vec4(0.0);
    for (int lane = 0; lane < 4; ++lane) {
        float jitter = randomFloat(rngState);
        wavelengths[lane] = mix(380.0, 720.0, (float(lane) + jitter) / 4.0);
    }
    return wavelengths;
}

float Gaussian(float lambda, float meanValue, float sigma) {
    float x = (lambda - meanValue) / max(sigma, 0.001);
    return exp(-0.5 * x * x);
}

vec4 EvaluateSpectralBasis(vec4 coefficients, vec4 wavelengths) {
    vec4 basisR = vec4(
        Gaussian(wavelengths.x, 610.0, 42.0),
        Gaussian(wavelengths.y, 610.0, 42.0),
        Gaussian(wavelengths.z, 610.0, 42.0),
        Gaussian(wavelengths.w, 610.0, 42.0));
    vec4 basisG = vec4(
        Gaussian(wavelengths.x, 545.0, 34.0),
        Gaussian(wavelengths.y, 545.0, 34.0),
        Gaussian(wavelengths.z, 545.0, 34.0),
        Gaussian(wavelengths.w, 545.0, 34.0));
    vec4 basisB = vec4(
        Gaussian(wavelengths.x, 455.0, 28.0),
        Gaussian(wavelengths.y, 455.0, 28.0),
        Gaussian(wavelengths.z, 455.0, 28.0),
        Gaussian(wavelengths.w, 455.0, 28.0));
    vec4 basisW = vec4(
        Gaussian(wavelengths.x, 565.0, 120.0),
        Gaussian(wavelengths.y, 565.0, 120.0),
        Gaussian(wavelengths.z, 565.0, 120.0),
        Gaussian(wavelengths.w, 565.0, 120.0));
    return max(coefficients.x * basisR + coefficients.y * basisG + coefficients.z * basisB + coefficients.w * basisW, vec4(0.0));
}

vec3 WavelengthToDisplayRgb(float lambda) {
    float r = Gaussian(lambda, 610.0, 48.0) + 0.35 * Gaussian(lambda, 700.0, 35.0);
    float g = Gaussian(lambda, 545.0, 38.0);
    float b = Gaussian(lambda, 450.0, 32.0) + 0.18 * Gaussian(lambda, 500.0, 45.0);
    return max(vec3(r, g, b), vec3(0.0));
}

vec3 SpectralToDisplayRgb(vec4 spectralValues, vec4 wavelengths, vec4 wavelengthPdfs) {
    vec3 rgb = vec3(0.0);
    for (int lane = 0; lane < 4; ++lane) {
        rgb += spectralValues[lane] * WavelengthToDisplayRgb(wavelengths[lane]) / max(wavelengthPdfs[lane], 1.0);
    }
    return rgb * 0.25;
}

vec4 EvaluateEnvironmentSpectrum(vec3 direction, vec4 wavelengths) {
    if (uEnvironment.params.x < 0.5 || uEnvironment.params.y <= kEpsilon) {
        return vec4(0.0);
    }

    float upAmount = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec4 skyCoefficients = mix(uEnvironment.horizonCoefficients, uEnvironment.zenithCoefficients, pow(upAmount, 0.65));
    if (direction.y < 0.0) {
        float groundBlend = clamp(direction.y + 1.0, 0.0, 1.0);
        skyCoefficients = mix(uEnvironment.groundCoefficients, uEnvironment.horizonCoefficients * 0.22, groundBlend);
    }
    return EvaluateSpectralBasis(skyCoefficients, wavelengths) * uEnvironment.params.y;
}

float RaySpawnEpsilon(float hitT) {
    return max(1e-4, hitT * 1e-4);
}

vec3 OffsetRayOrigin(vec3 position, vec3 geometricNormal, vec3 direction, float hitT) {
    float signValue = dot(direction, geometricNormal) >= 0.0 ? 1.0 : -1.0;
    return position + geometricNormal * (signValue * RaySpawnEpsilon(hitT));
}

vec3 SafeInvDir(vec3 direction) {
    vec3 safeDirection = direction;
    if (abs(safeDirection.x) < kEpsilon) safeDirection.x = safeDirection.x < 0.0 ? -kEpsilon : kEpsilon;
    if (abs(safeDirection.y) < kEpsilon) safeDirection.y = safeDirection.y < 0.0 ? -kEpsilon : kEpsilon;
    if (abs(safeDirection.z) < kEpsilon) safeDirection.z = safeDirection.z < 0.0 ? -kEpsilon : kEpsilon;
    return 1.0 / safeDirection;
}

float IntersectAabb(vec3 rayOrigin, vec3 invDir, vec3 minimumBounds, vec3 maximumBounds, float maxDistance) {
    vec3 t0 = (minimumBounds - rayOrigin) * invDir;
    vec3 t1 = (maximumBounds - rayOrigin) * invDir;
    vec3 tMin = min(t0, t1);
    vec3 tMax = max(t0, t1);
    float entry = max(max(tMin.x, tMin.y), max(tMin.z, 0.0));
    float exit = min(min(tMax.x, tMax.y), tMax.z);
    if (exit < entry || entry >= maxDistance) {
        return -1.0;
    }
    return entry;
}

HitState MakeMissState() {
    HitState hit;
    hit.hit0 = vec4(-1.0, -1.0, -1.0, -1.0);
    hit.hit1 = vec4(0.0);
    hit.hit2 = vec4(0.0);
    hit.hit3 = vec4(0.0);
    return hit;
}

bool HitStateIsValid(HitState hit) {
    return hit.hit0.x > 0.0;
}

int HitMaterialIndex(HitState hit) {
    return int(round(hit.hit0.w));
}

int HitObjectId(HitState hit) {
    return hit.hit0.y < 0.5 ? int(round(hit.hit2.w)) : int(round(hit.hit3.z));
}

bool IsSelectedDebugPixel(uint rayIndex) {
    return uDebugMode == kDebugModeSelectedRayLog &&
        all(equal(PixelCoordFromIndex(rayIndex), uDebugPixel));
}

void ResetSelectedRayLog(uint rayIndex) {
    if (!IsSelectedDebugPixel(rayIndex)) {
        return;
    }
    uDebugRayHeader = ivec4(0, int(rayIndex), uDebugPixel.x, uDebugPixel.y);
}

void AppendSelectedRayLog(
    uint rayIndex,
    float hitT,
    int objectId,
    int materialIndex,
    bool frontFace,
    int insideMedium,
    vec3 geometricNormal,
    vec3 shadingNormal,
    float etaI,
    float etaT,
    float etaRatio,
    float fresnel,
    int decision,
    vec3 spawnedOrigin,
    vec3 spawnedDirection) {

    if (!IsSelectedDebugPixel(rayIndex)) {
        return;
    }

    int entryIndex = clamp(uDebugRayHeader.x, 0, kDebugMaxBounceCount - 1);
    DebugRayBounce entry;
    entry.hitInfo = vec4(hitT, float(objectId), float(materialIndex), frontFace ? 1.0 : 0.0);
    entry.geometricNormal = vec4(geometricNormal, insideMedium > 0 ? 1.0 : 0.0);
    entry.shadingNormal = vec4(shadingNormal, 0.0);
    entry.etaInfo = vec4(etaI, etaT, etaRatio, fresnel);
    entry.spawnedOrigin = vec4(spawnedOrigin, float(decision));
    entry.spawnedDirection = vec4(spawnedDirection, 0.0);
    uDebugRayBounces[entryIndex] = entry;
    if (uDebugRayHeader.x < kDebugMaxBounceCount) {
        uDebugRayHeader.x += 1;
    }
}

void UpdateSphereHit(uint sphereIndex, vec3 rayOrigin, vec3 rayDirection, inout HitState bestHit) {
    SphereData sphere = uSpheres[sphereIndex];
    vec3 oc = rayOrigin - sphere.centerRadius.xyz;
    float b = dot(oc, rayDirection);
    float c = dot(oc, oc) - sphere.centerRadius.w * sphere.centerRadius.w;
    float discriminant = b * b - c;
    if (discriminant < 0.0) {
        return;
    }

    float sqrtDiscriminant = sqrt(discriminant);
    float t = -b - sqrtDiscriminant;
    if (t <= kEpsilon) {
        t = -b + sqrtDiscriminant;
    }
    if (t <= kEpsilon) {
        return;
    }
    if (bestHit.hit0.x > 0.0 && t >= bestHit.hit0.x) {
        return;
    }

    vec3 hitPosition = rayOrigin + rayDirection * t;
    vec3 normal = normalize(hitPosition - sphere.centerRadius.xyz);
    bestHit.hit0 = vec4(t, 0.0, float(sphereIndex), sphere.surface.x);
    bestHit.hit1 = vec4(normal, dot(rayDirection, normal) < 0.0 ? 1.0 : 0.0);
    bestHit.hit2 = vec4(normal, sphere.surface.y);
    bestHit.hit3 = vec4(
        0.5 + atan(normal.z, normal.x) * (0.5 / kPi),
        0.5 - asin(clamp(normal.y, -1.0, 1.0)) * (1.0 / kPi),
        sphere.surface.y,
        0.0);
}

void UpdateTriangleHit(uint triangleIndex, vec3 rayOrigin, vec3 rayDirection, inout HitState bestHit) {
    TriangleData triangle = uTriangles[triangleIndex];
    vec3 edge1 = triangle.b.xyz - triangle.a.xyz;
    vec3 edge2 = triangle.c.xyz - triangle.a.xyz;
    vec3 p = cross(rayDirection, edge2);
    float determinant = dot(edge1, p);
    if (abs(determinant) < 0.000001) {
        return;
    }

    float inverseDeterminant = 1.0 / determinant;
    vec3 tvec = rayOrigin - triangle.a.xyz;
    float u = dot(tvec, p) * inverseDeterminant;
    if (u < 0.0 || u > 1.0) {
        return;
    }

    vec3 q = cross(tvec, edge1);
    float v = dot(rayDirection, q) * inverseDeterminant;
    if (v < 0.0 || u + v > 1.0) {
        return;
    }

    float t = dot(edge2, q) * inverseDeterminant;
    if (t <= kEpsilon) {
        return;
    }
    if (bestHit.hit0.x > 0.0 && t >= bestHit.hit0.x) {
        return;
    }

    float w = 1.0 - u - v;
    vec3 shadingNormal = normalize(triangle.normalA.xyz * w + triangle.normalB.xyz * u + triangle.normalC.xyz * v);
    vec3 geometricNormal = normalize(cross(edge1, edge2));
    if (length(shadingNormal) <= kEpsilon) {
        shadingNormal = geometricNormal;
    }
    if (dot(shadingNormal, geometricNormal) < 0.0) {
        shadingNormal *= -1.0;
    }

    bestHit.hit0 = vec4(t, 1.0, float(triangleIndex), triangle.surface.x);
    bestHit.hit1 = vec4(shadingNormal, dot(rayDirection, geometricNormal) < 0.0 ? 1.0 : 0.0);
    bestHit.hit2 = vec4(geometricNormal, triangle.surface.y);
    bestHit.hit3 = vec4(
        triangle.uvAuvB.xy * w + triangle.uvAuvB.zw * u + triangle.uvC.xy * v,
        triangle.surface.y,
        0.0);
}

HitState TraceScene(vec3 rayOrigin, vec3 rayDirection) {
    HitState bestHit = MakeMissState();
    if (uBvhNodeCount <= 0 || uPrimitiveRefCount <= 0) {
        for (uint sphereIndex = 0u; sphereIndex < uint(uSphereCount); ++sphereIndex) {
            UpdateSphereHit(sphereIndex, rayOrigin, rayDirection, bestHit);
        }
        for (uint triangleIndex = 0u; triangleIndex < uint(uTriangleCount); ++triangleIndex) {
            UpdateTriangleHit(triangleIndex, rayOrigin, rayDirection, bestHit);
        }
        return bestHit;
    }

    int stack[64];
    int stackSize = 0;
    stack[stackSize++] = 0;
    vec3 invDir = SafeInvDir(rayDirection);

    while (stackSize > 0) {
        int nodeIndex = stack[--stackSize];
        BvhNodeData node = uBvhNodes[nodeIndex];
        float maxDistance = HitStateIsValid(bestHit) ? bestHit.hit0.x : kRayMaxDistance;
        if (IntersectAabb(rayOrigin, invDir, node.minBounds.xyz, node.maxBounds.xyz, maxDistance) < 0.0) {
            continue;
        }

        if (node.meta0.w > 0) {
            for (int i = 0; i < node.meta0.w; ++i) {
                int primitiveIndex = node.meta0.z + i;
                PrimitiveRefData primitiveRef = uPrimitiveRefs[primitiveIndex];
                if (primitiveRef.meta.x == 0) {
                    UpdateSphereHit(uint(primitiveRef.meta.y), rayOrigin, rayDirection, bestHit);
                } else {
                    UpdateTriangleHit(uint(primitiveRef.meta.y), rayOrigin, rayDirection, bestHit);
                }
            }
            continue;
        }

        if (node.meta0.x >= 0) {
            stack[stackSize++] = node.meta0.x;
        }
        if (node.meta0.y >= 0) {
            stack[stackSize++] = node.meta0.y;
        }
    }

    return bestHit;
}

bool TraceAnyHit(vec3 rayOrigin, vec3 rayDirection, float maxDistance) {
    HitState hit = TraceScene(rayOrigin, rayDirection);
    return HitStateIsValid(hit) && hit.hit0.x < maxDistance;
}

vec3 BuildOrthonormalBasis(vec3 normal, out vec3 tangent, out vec3 bitangent) {
    tangent = abs(normal.z) < 0.999 ? normalize(cross(normal, vec3(0.0, 0.0, 1.0))) : normalize(cross(normal, vec3(0.0, 1.0, 0.0)));
    bitangent = normalize(cross(normal, tangent));
    return normal;
}

vec3 LocalToWorld(vec3 localDirection, vec3 normal) {
    vec3 tangent;
    vec3 bitangent;
    BuildOrthonormalBasis(normal, tangent, bitangent);
    return normalize(tangent * localDirection.x + bitangent * localDirection.y + normal * localDirection.z);
}

vec3 SampleCosineHemisphere(vec2 xi) {
    vec2 disk = SampleConcentricDisk(xi);
    float z = sqrt(max(0.0, 1.0 - dot(disk, disk)));
    return vec3(disk.x, disk.y, z);
}

vec3 SampleGlossyLobe(vec3 reflectionDirection, float roughness, vec2 xi) {
    float exponent = mix(128.0, 4.0, clamp(roughness, 0.0, 1.0));
    float cosTheta = pow(max(1.0 - xi.x, 0.0), 1.0 / (exponent + 1.0));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float phi = xi.y * kTau;
    vec3 localDirection = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    return LocalToWorld(localDirection, reflectionDirection);
}

float RoughnessToAlpha(float roughness) {
    return max(roughness * roughness, 0.001);
}

float Average4(vec4 value) {
    return 0.25 * (value.x + value.y + value.z + value.w);
}

vec3 SampleGgxHalfVector(vec3 normal, float roughness, vec2 xi) {
    float alpha = RoughnessToAlpha(roughness);
    float alphaSquared = alpha * alpha;
    float phi = kTau * xi.x;
    float cosTheta = sqrt(max((1.0 - xi.y) / max(1.0 + (alphaSquared - 1.0) * xi.y, kEpsilon), 0.0));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    vec3 local = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    return LocalToWorld(local, normal);
}

float GgxDistribution(float absNoH, float roughness) {
    float alpha = RoughnessToAlpha(roughness);
    float alphaSquared = alpha * alpha;
    float denominator = absNoH * absNoH * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(kPi * denominator * denominator, kEpsilon);
}

float SmithGgxLambda(float absNoV, float roughness) {
    float alpha = RoughnessToAlpha(roughness);
    float alphaSquared = alpha * alpha;
    float cosSquared = max(absNoV * absNoV, kEpsilon);
    float sinSquared = max(1.0 - cosSquared, 0.0);
    return 0.5 * (-1.0 + sqrt(max(1.0 + (alphaSquared * sinSquared) / cosSquared, 1.0)));
}

float SmithGgxShadowing(float absNoV, float absNoL, float roughness) {
    return 1.0 / max(1.0 + SmithGgxLambda(absNoV, roughness) + SmithGgxLambda(absNoL, roughness), kEpsilon);
}

float FresnelDielectric(float cosThetaI, float etaI, float etaT) {
    cosThetaI = clamp(cosThetaI, -1.0, 1.0);
    bool entering = cosThetaI > 0.0;
    if (!entering) {
        float temp = etaI;
        etaI = etaT;
        etaT = temp;
        cosThetaI = abs(cosThetaI);
    }
    float sinThetaI = sqrt(max(0.0, 1.0 - cosThetaI * cosThetaI));
    float sinThetaT = etaI / etaT * sinThetaI;
    if (sinThetaT >= 1.0) {
        return 1.0;
    }
    float cosThetaT = sqrt(max(0.0, 1.0 - sinThetaT * sinThetaT));
    float parallel = ((etaT * cosThetaI) - (etaI * cosThetaT)) / ((etaT * cosThetaI) + (etaI * cosThetaT));
    float perpendicular = ((etaI * cosThetaI) - (etaT * cosThetaT)) / ((etaI * cosThetaI) + (etaT * cosThetaT));
    return 0.5 * (parallel * parallel + perpendicular * perpendicular);
}

vec4 FresnelDielectricSpectrum(float cosThetaI, vec4 etaI, vec4 etaT) {
    vec4 spectrum = vec4(0.0);
    for (int lane = 0; lane < 4; ++lane) {
        spectrum[lane] = FresnelDielectric(cosThetaI, etaI[lane], etaT[lane]);
    }
    return spectrum;
}

bool RefractDirection(vec3 incident, vec3 normal, float eta, out vec3 refracted) {
    float cosThetaI = dot(-incident, normal);
    float sin2ThetaI = max(0.0, 1.0 - cosThetaI * cosThetaI);
    float sin2ThetaT = eta * eta * sin2ThetaI;
    if (sin2ThetaT > 1.0) {
        return false;
    }
    float cosThetaT = sqrt(max(0.0, 1.0 - sin2ThetaT));
    refracted = normalize(eta * incident + (eta * cosThetaI - cosThetaT) * normal);
    return true;
}

bool RefractThroughThinSheet(vec3 incident, vec3 microfacetNormal, float eta, out vec3 refracted) {
    vec3 internalDirection;
    if (!RefractDirection(incident, microfacetNormal, eta, internalDirection)) {
        return false;
    }
    if (!RefractDirection(internalDirection, -microfacetNormal, 1.0 / max(eta, kEpsilon), refracted)) {
        return false;
    }
    refracted = normalize(refracted);
    return true;
}

vec4 EvaluateAbsorption(vec4 absorptionCoefficients, vec4 wavelengths, float distance, float absorptionDistance) {
    vec4 absorptionColor = clamp(EvaluateSpectralBasis(absorptionCoefficients, wavelengths), vec4(0.001), vec4(1.0));
    vec4 sigmaA = -log(absorptionColor) / max(absorptionDistance, 0.01);
    return exp(-sigmaA * max(distance, 0.0));
}

vec4 EvaluateLightSpectrum(int lightIndex, vec4 wavelengths) {
    return EvaluateSpectralBasis(uLights[lightIndex].colorCoefficients, wavelengths) * uLights[lightIndex].basis1.w;
}

int LightTypeOf(LightData light) {
    return int(round(light.positionType.w));
}

bool LightEnabled(LightData light) {
    return light.basis0.w > 0.5;
}

vec3 LightDirection(LightData light) {
    return normalize(light.directionRange.xyz);
}

vec3 LightRight(LightData light) {
    return normalize(light.basis0.xyz);
}

vec3 LightUp(LightData light) {
    return normalize(light.basis1.xyz);
}

int MaterialLayerOffset(MaterialData material) { return max(material.layerInfo.x, 0); }
int MaterialLayerCount(MaterialData material) { return max(material.layerInfo.y, 0); }

MaterialLayerData GetMaterialLayer(MaterialData material, int localIndex) {
    return uMaterialLayers[MaterialLayerOffset(material) + localIndex];
}

int MaterialLayerType(MaterialLayerData layer) { return int(round(layer.params0.x)); }
float MaterialLayerWeight(MaterialLayerData layer) { return clamp(layer.params0.y, 0.0, 1.0); }
float MaterialLayerRoughness(MaterialLayerData layer) { return clamp(layer.params0.z, 0.0, 1.0); }
float MaterialLayerMetallic(MaterialLayerData layer) { return clamp(layer.params0.w, 0.0, 1.0); }
float MaterialLayerTransmission(MaterialLayerData layer) { return clamp(layer.params1.x, 0.0, 1.0); }
float MaterialLayerIor(MaterialLayerData layer) { return max(layer.params1.y, 1.0); }
float MaterialLayerTransmissionRoughness(MaterialLayerData layer) { return clamp(layer.params1.z, 0.0, 1.0); }
bool MaterialLayerThinWalled(MaterialLayerData layer) { return layer.params1.w > 0.5; }

int FindBaseLayerIndex(MaterialData material) {
    for (int localIndex = MaterialLayerCount(material) - 1; localIndex >= 0; --localIndex) {
        int layerType = MaterialLayerType(GetMaterialLayer(material, localIndex));
        if (layerType != kLayerTypeClearCoat) {
            return localIndex;
        }
    }
    return -1;
}

int FindClearCoatLayerIndex(MaterialData material) {
    for (int localIndex = 0; localIndex < MaterialLayerCount(material); ++localIndex) {
        MaterialLayerData layer = GetMaterialLayer(material, localIndex);
        if (MaterialLayerType(layer) == kLayerTypeClearCoat && MaterialLayerWeight(layer) > kEpsilon) {
            return localIndex;
        }
    }
    return -1;
}

vec4 EvaluateLayerSpectrum(MaterialLayerData layer, vec4 wavelengths) {
    return EvaluateSpectralBasis(layer.coefficients, wavelengths);
}

float MaterialRoughness(MaterialData material) {
    int baseLayerIndex = FindBaseLayerIndex(material);
    if (baseLayerIndex >= 0) {
        return MaterialLayerRoughness(GetMaterialLayer(material, baseLayerIndex));
    }
    return clamp(material.params0.x, 0.0, 1.0);
}

float MaterialMetallic(MaterialData material) {
    int baseLayerIndex = FindBaseLayerIndex(material);
    if (baseLayerIndex >= 0) {
        return MaterialLayerMetallic(GetMaterialLayer(material, baseLayerIndex));
    }
    return clamp(material.params0.y, 0.0, 1.0);
}

float MaterialTransmission(MaterialData material) {
    int baseLayerIndex = FindBaseLayerIndex(material);
    if (baseLayerIndex >= 0) {
        return MaterialLayerTransmission(GetMaterialLayer(material, baseLayerIndex));
    }
    return clamp(material.params0.z, 0.0, 1.0);
}

float MaterialIor(MaterialData material) {
    int baseLayerIndex = FindBaseLayerIndex(material);
    if (baseLayerIndex >= 0) {
        return MaterialLayerIor(GetMaterialLayer(material, baseLayerIndex));
    }
    return max(material.params0.w, 1.0);
}

float MaterialAbsorptionDistance(MaterialData material) { return max(material.params1.x, 0.01); }
bool MaterialThinWalled(MaterialData material) {
    int baseLayerIndex = FindBaseLayerIndex(material);
    if (baseLayerIndex >= 0) {
        return MaterialLayerThinWalled(GetMaterialLayer(material, baseLayerIndex));
    }
    return material.params1.y > 0.5;
}

int MaterialPreset(MaterialData material) {
    int baseLayerIndex = FindBaseLayerIndex(material);
    if (baseLayerIndex >= 0) {
        int baseType = MaterialLayerType(GetMaterialLayer(material, baseLayerIndex));
        if (baseType == kLayerTypeBaseMetal) {
            return 1;
        }
        if (baseType == kLayerTypeBaseDielectric) {
            return 2;
        }
        return 0;
    }
    return int(round(material.params1.z));
}

float MaterialTransmissionRoughness(MaterialData material) {
    int baseLayerIndex = FindBaseLayerIndex(material);
    if (baseLayerIndex >= 0) {
        return MaterialLayerTransmissionRoughness(GetMaterialLayer(material, baseLayerIndex));
    }
    return clamp(material.params1.w, 0.0, 1.0);
}

float MaterialDispersionScale(MaterialData material) { return max(material.params2.x, 0.0); }
bool MaterialHasClearCoat(MaterialData material) { return FindClearCoatLayerIndex(material) >= 0; }

float MaterialClearCoatWeight(MaterialData material) {
    int clearCoatIndex = FindClearCoatLayerIndex(material);
    return clearCoatIndex >= 0 ? MaterialLayerWeight(GetMaterialLayer(material, clearCoatIndex)) : 0.0;
}

float MaterialClearCoatRoughness(MaterialData material) {
    int clearCoatIndex = FindClearCoatLayerIndex(material);
    return clearCoatIndex >= 0 ? MaterialLayerRoughness(GetMaterialLayer(material, clearCoatIndex)) : 0.03;
}

float MaterialClearCoatIor(MaterialData material) {
    int clearCoatIndex = FindClearCoatLayerIndex(material);
    return clearCoatIndex >= 0 ? MaterialLayerIor(GetMaterialLayer(material, clearCoatIndex)) : 1.5;
}

float EvaluateDielectricEta(MaterialData material, float wavelength) {
    float lambdaMicrons = max(wavelength * 0.001, 0.38);
    float referenceMicrons = 0.55;
    float referenceTerm = 1.0 / (referenceMicrons * referenceMicrons);
    float wavelengthTerm = 1.0 / (lambdaMicrons * lambdaMicrons);
    return max(1.0, MaterialIor(material) + MaterialDispersionScale(material) * (wavelengthTerm - referenceTerm));
}

vec4 EvaluateDielectricEtaSpectrum(MaterialData material, vec4 wavelengths) {
    vec4 eta = vec4(1.0);
    for (int lane = 0; lane < 4; ++lane) {
        eta[lane] = EvaluateDielectricEta(material, wavelengths[lane]);
    }
    return eta;
}

vec4 EvaluateBaseSpectrum(MaterialData material, vec4 wavelengths) {
    int baseLayerIndex = FindBaseLayerIndex(material);
    if (baseLayerIndex >= 0) {
        return EvaluateLayerSpectrum(GetMaterialLayer(material, baseLayerIndex), wavelengths);
    }
    return EvaluateSpectralBasis(material.baseCoefficients, wavelengths);
}

vec4 EvaluateEmissionSpectrum(MaterialData material, vec4 wavelengths) {
    return EvaluateSpectralBasis(material.emissionCoefficients, wavelengths);
}

bool MaterialHasEmission(MaterialData material) {
    return max(material.emissionCoefficients.x, max(material.emissionCoefficients.y, max(material.emissionCoefficients.z, material.emissionCoefficients.w))) > 0.0001;
}

bool IsGlass(MaterialData material) {
    return MaterialTransmission(material) > 0.001 && MaterialPreset(material) == 2;
}

bool IsMetal(MaterialData material) {
    return MaterialPreset(material) == 1 || MaterialMetallic(material) > 0.5;
}

float EvaluateBsdfPdf(MaterialData material, vec3 normal, vec3 outgoing, vec3 incident, vec3 reflectionDirection) {
    if (IsGlass(material)) {
        return 1.0;
    }
    if (IsMetal(material)) {
        float exponent = mix(128.0, 4.0, MaterialRoughness(material));
        float cosAlpha = max(dot(normalize(reflectionDirection), normalize(incident)), 0.0);
        return ((exponent + 1.0) / (2.0 * kPi)) * pow(cosAlpha, exponent);
    }
    float ndotl = max(dot(normal, incident), 0.0);
    return ndotl / kPi;
}

vec4 EvaluateBsdf(MaterialData material, vec3 normal, vec3 outgoing, vec3 incident, vec3 reflectionDirection, vec4 wavelengths) {
    vec4 baseSpectrum = EvaluateBaseSpectrum(material, wavelengths);
    if (IsGlass(material)) {
        return baseSpectrum;
    }
    if (IsMetal(material)) {
        float exponent = mix(128.0, 4.0, MaterialRoughness(material));
        float cosAlpha = max(dot(normalize(reflectionDirection), normalize(incident)), 0.0);
        return baseSpectrum * ((exponent + 2.0) / (2.0 * kPi)) * pow(cosAlpha, exponent);
    }
    return baseSpectrum / kPi;
}

vec4 EvaluateSpotAttenuation(LightData light, vec3 hitPosition) {
    if (LightTypeOf(light) != 2) {
        return vec4(1.0);
    }
    vec3 toSurface = normalize(hitPosition - light.positionType.xyz);
    float cosTheta = dot(LightDirection(light), toSurface);
    float innerCos = cos(radians(light.params.z));
    float outerCos = cos(radians(light.params.w));
    float attenuation = cosTheta <= outerCos ? 0.0 : cosTheta >= innerCos ? 1.0 : clamp((cosTheta - outerCos) / max(innerCos - outerCos, kEpsilon), 0.0, 1.0);
    return vec4(attenuation);
}

bool SampleDirectLight(
    MaterialData material,
    vec3 hitPosition,
    float hitDistance,
    vec3 shadingNormal,
    vec3 geometricNormal,
    vec3 outgoing,
    vec4 wavelengths,
    uint rngState,
    out ShadowRay shadowRay) {

    shadowRay.originMaxDistance = vec4(0.0);
    shadowRay.directionContributionScale = vec4(0.0);
    shadowRay.contribution = vec4(0.0);
    shadowRay.meta = ivec4(-1);

    if (uLightCount <= 0 || IsGlass(material)) {
        return false;
    }

    int lightIndex = min(int(floor(float(uLightCount) * randomFloat(rngState))), max(uLightCount - 1, 0));
    LightData light = uLights[lightIndex];
    if (!LightEnabled(light)) {
        return false;
    }

    vec3 lightDirection = vec3(0.0);
    float lightDistance = kRayMaxDistance;
    float lightPdf = 1.0;
    vec4 radiance = EvaluateLightSpectrum(lightIndex, wavelengths);
    int lightType = LightTypeOf(light);

    if (lightType == 3) {
        lightDirection = -LightDirection(light);
        lightDistance = kRayMaxDistance;
    } else if (lightType == 1 || lightType == 2) {
        vec3 toLight = light.positionType.xyz - hitPosition;
        float distanceSquared = dot(toLight, toLight);
        if (distanceSquared <= kEpsilon) {
            return false;
        }
        lightDistance = sqrt(distanceSquared);
        if (lightDistance > light.directionRange.w) {
            return false;
        }
        lightDirection = toLight / lightDistance;
        radiance *= EvaluateSpotAttenuation(light, hitPosition);
        radiance /= max(distanceSquared, 0.01);
        if (lightType == 2 && radiance.x <= kEpsilon && radiance.y <= kEpsilon && radiance.z <= kEpsilon && radiance.w <= kEpsilon) {
            return false;
        }
    } else {
        vec2 xi = randomFloat2(rngState);
        vec2 centered = xi * 2.0 - 1.0;
        vec3 sampledPosition =
            light.positionType.xyz +
            LightRight(light) * (centered.x * light.params.x * 0.5) +
            LightUp(light) * (centered.y * light.params.y * 0.5);
        vec3 toLight = sampledPosition - hitPosition;
        float distanceSquared = dot(toLight, toLight);
        if (distanceSquared <= kEpsilon) {
            return false;
        }
        lightDistance = sqrt(distanceSquared);
        lightDirection = toLight / lightDistance;
        float lightCosine = max(dot(LightDirection(light), -lightDirection), 0.0);
        if (lightCosine <= kEpsilon) {
            return false;
        }
        float area = max(light.params.x * light.params.y, 0.001);
        lightPdf = (1.0 / area) * distanceSquared / lightCosine;
    }

    float ndotl = max(dot(shadingNormal, lightDirection), 0.0);
    if (ndotl <= kEpsilon) {
        return false;
    }

    vec3 reflectionDirection = reflect(-outgoing, shadingNormal);
    vec4 bsdf = EvaluateBsdf(material, shadingNormal, outgoing, lightDirection, reflectionDirection, wavelengths);
    float bsdfPdf = EvaluateBsdfPdf(material, shadingNormal, outgoing, lightDirection, reflectionDirection);
    float misWeight = lightType == 0 ? lightPdf / max(lightPdf + bsdfPdf, kEpsilon) : 1.0;
    vec4 contribution = bsdf * radiance * (ndotl * misWeight / max(lightPdf, kEpsilon));
    if (all(lessThanEqual(contribution, vec4(kEpsilon)))) {
        return false;
    }

    shadowRay.originMaxDistance = vec4(OffsetRayOrigin(hitPosition, geometricNormal, lightDirection, hitDistance), lightDistance);
    shadowRay.directionContributionScale = vec4(lightDirection, 1.0);
    shadowRay.contribution = contribution;
    return true;
}

vec4 SampleSurfaceBounce(
    MaterialData material,
    vec3 hitPosition,
    vec3 shadingNormal,
    vec3 geometricNormal,
    bool frontFace,
    vec3 outgoing,
    vec4 wavelengths,
    inout uint rngState,
    inout vec3 nextDirection,
    inout vec4 nextAbsorptionCoefficients,
    inout float nextAbsorptionDistance,
    inout int insideMedium,
    out bool isDelta,
    out int debugDecision,
    out vec4 debugEtaInfo) {

    isDelta = false;
    debugDecision = kDebugDecisionNone;
    debugEtaInfo = vec4(1.0, 1.0, 1.0, 0.0);
    vec4 baseSpectrum = EvaluateBaseSpectrum(material, wavelengths);
    vec3 incoming = normalize(-outgoing);
    vec3 surfaceNormal = frontFace ? geometricNormal : -geometricNormal;

    if (MaterialHasClearCoat(material) && frontFace && insideMedium == 0) {
        float clearCoatWeight = MaterialClearCoatWeight(material);
        float clearCoatRoughness = MaterialClearCoatRoughness(material);
        float clearCoatIor = MaterialClearCoatIor(material);
        float clearCoatFresnel = clearCoatWeight *
            FresnelDielectric(dot(incoming, surfaceNormal), 1.0, clearCoatIor);

        if (randomFloat(rngState) < clearCoatFresnel) {
            debugEtaInfo = vec4(1.0, clearCoatIor, 1.0 / max(clearCoatIor, kEpsilon), clearCoatFresnel);
            debugDecision = kDebugDecisionReflect;
            insideMedium = 0;
            nextAbsorptionCoefficients = vec4(0.0);
            nextAbsorptionDistance = 1.0;
            if (clearCoatRoughness <= 0.001) {
                isDelta = true;
                nextDirection = reflect(incoming, surfaceNormal);
            } else {
                vec3 reflectionDirection = reflect(-outgoing, surfaceNormal);
                nextDirection = SampleGlossyLobe(reflectionDirection, clearCoatRoughness, randomFloat2(rngState));
                if (dot(nextDirection, surfaceNormal) <= kEpsilon) {
                    nextDirection = reflect(incoming, surfaceNormal);
                }
            }
            return vec4(1.0);
        }
    }

    if (IsGlass(material)) {
        vec4 etaSpectrum = EvaluateDielectricEtaSpectrum(material, wavelengths);
        bool thinWalled = MaterialThinWalled(material);
        vec4 etaI = frontFace ? vec4(1.0) : etaSpectrum;
        vec4 etaT = frontFace ? etaSpectrum : vec4(1.0);
        float etaAverage = Average4(etaSpectrum);
        float etaIAvg = frontFace ? 1.0 : etaAverage;
        float etaTAvg = frontFace ? etaAverage : 1.0;
        float etaRatio = etaIAvg / max(etaTAvg, kEpsilon);
        float transmissionRoughness = MaterialTransmissionRoughness(material);
        debugEtaInfo = vec4(etaIAvg, etaTAvg, etaRatio, 0.0);

        if (transmissionRoughness <= 0.001) {
            isDelta = true;
            float fresnel = Average4(FresnelDielectricSpectrum(dot(incoming, surfaceNormal), etaI, etaT));
            debugEtaInfo.w = fresnel;
            if (randomFloat(rngState) < fresnel) {
                nextDirection = reflect(incoming, surfaceNormal);
                debugDecision = kDebugDecisionReflect;
                return baseSpectrum;
            }

            if (thinWalled) {
                nextDirection = incoming;
                insideMedium = 0;
                nextAbsorptionCoefficients = vec4(0.0);
                nextAbsorptionDistance = 1.0;
                debugDecision = kDebugDecisionThinSheetPass;
                return baseSpectrum;
            }

            vec3 refractedDirection;
            if (!RefractDirection(incoming, surfaceNormal, etaRatio, refractedDirection)) {
                nextDirection = reflect(incoming, surfaceNormal);
                debugDecision = kDebugDecisionTotalInternalReflection;
                return baseSpectrum;
            }

            nextDirection = refractedDirection;
            insideMedium = frontFace ? 1 : 0;
            nextAbsorptionCoefficients = insideMedium > 0 ? material.absorptionCoefficients : vec4(0.0);
            nextAbsorptionDistance = insideMedium > 0 ? MaterialAbsorptionDistance(material) : 1.0;
            debugDecision = kDebugDecisionRefract;
            return baseSpectrum;
        }

        vec3 microfacetNormal = SampleGgxHalfVector(surfaceNormal, transmissionRoughness, randomFloat2(rngState));
        if (dot(microfacetNormal, surfaceNormal) <= 0.0) {
            microfacetNormal = surfaceNormal;
        }

        vec4 fresnelSpectrum = FresnelDielectricSpectrum(dot(incoming, microfacetNormal), etaI, etaT);
        float fresnel = clamp(Average4(fresnelSpectrum), 0.02, 0.98);
        float absNoI = max(abs(dot(surfaceNormal, incoming)), kEpsilon);
        float absNoH = max(abs(dot(surfaceNormal, microfacetNormal)), kEpsilon);
        float distribution = GgxDistribution(absNoH, transmissionRoughness);

        if (randomFloat(rngState) < fresnel) {
            nextDirection = reflect(incoming, microfacetNormal);
            if (dot(nextDirection, surfaceNormal) <= kEpsilon) {
                nextDirection = reflect(incoming, surfaceNormal);
            }

            float absNoO = max(abs(dot(surfaceNormal, nextDirection)), kEpsilon);
            float geometry = SmithGgxShadowing(absNoI, absNoO, transmissionRoughness);
            float reflectionWeight = clamp(distribution * geometry * absNoO / max(4.0 * absNoI, kEpsilon), 0.2, 1.0);
            debugDecision = kDebugDecisionReflect;
            debugEtaInfo.w = fresnel;
            return max(baseSpectrum * reflectionWeight, vec4(0.05));
        }

        bool transmitted = false;
        if (thinWalled) {
            transmitted = RefractThroughThinSheet(incoming, microfacetNormal, etaRatio, nextDirection);
            insideMedium = 0;
            nextAbsorptionCoefficients = vec4(0.0);
            nextAbsorptionDistance = 1.0;
        } else {
            transmitted = RefractDirection(incoming, microfacetNormal, etaRatio, nextDirection);
            if (transmitted) {
                insideMedium = frontFace ? 1 : 0;
                nextAbsorptionCoefficients = insideMedium > 0 ? material.absorptionCoefficients : vec4(0.0);
                nextAbsorptionDistance = insideMedium > 0 ? MaterialAbsorptionDistance(material) : 1.0;
            }
        }

        if (!transmitted || dot(nextDirection, surfaceNormal) >= -kEpsilon) {
            nextDirection = reflect(incoming, surfaceNormal);
            debugDecision = kDebugDecisionTotalInternalReflection;
            debugEtaInfo.w = fresnel;
            return max(baseSpectrum * 0.45, vec4(0.05));
        }

        float absNoO = max(abs(dot(surfaceNormal, nextDirection)), kEpsilon);
        float geometry = SmithGgxShadowing(absNoI, absNoO, transmissionRoughness);
        float transmissionWeight = clamp(distribution * geometry * absNoI / max(absNoI + absNoO, kEpsilon), 0.18, 1.0);
        debugDecision = thinWalled ? kDebugDecisionThinSheetPass : kDebugDecisionRefract;
        debugEtaInfo.w = fresnel;
        return max(baseSpectrum * transmissionWeight, vec4(0.05));
    }

    insideMedium = 0;
    nextAbsorptionCoefficients = vec4(0.0);
    nextAbsorptionDistance = 1.0;

    if (IsMetal(material)) {
        vec3 reflectionDirection = reflect(-outgoing, shadingNormal);
        nextDirection = SampleGlossyLobe(reflectionDirection, MaterialRoughness(material), randomFloat2(rngState));
        float pdf = max(EvaluateBsdfPdf(material, shadingNormal, outgoing, nextDirection, reflectionDirection), kEpsilon);
        float ndotl = max(dot(shadingNormal, nextDirection), 0.0);
        return EvaluateBsdf(material, shadingNormal, outgoing, nextDirection, reflectionDirection, wavelengths) * (ndotl / pdf);
    }

    nextDirection = LocalToWorld(SampleCosineHemisphere(randomFloat2(rngState)), shadingNormal);
    return baseSpectrum;
}

vec3 TonemapAces(vec3 color) {
    vec3 numerator = color * (2.51 * color + 0.03);
    vec3 denominator = color * (2.43 * color + 0.59) + 0.14;
    return clamp(numerator / max(denominator, vec3(kEpsilon)), 0.0, 1.0);
}
