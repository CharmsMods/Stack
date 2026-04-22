#version 430 core

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec4 oObjectId;

in vec3 vWorldPosition;
in vec3 vWorldNormal;
in vec2 vUv;
in vec3 vTint;
flat in int vMaterialIndex;
flat in int vObjectId;

struct GpuRasterMaterial {
    vec4 baseColor;
    vec4 emissionColorStrength;
    vec4 surfaceParams;
    vec4 dielectricParams;
    vec4 mediumParams0;
    vec4 mediumParams1;
    ivec4 textureRefs;
};

struct GpuRasterLight {
    vec4 positionType;
    vec4 directionRange;
    vec4 colorIntensity;
    vec4 sizeAndAngles;
    vec4 rightEnabled;
    vec4 upData;
};

layout(std430, binding = 0) readonly buffer MaterialBuffer {
    GpuRasterMaterial uMaterials[];
};

layout(std430, binding = 1) readonly buffer LightBuffer {
    GpuRasterLight uLights[];
};

uniform sampler2DArray uBaseColorTextures;
uniform sampler2DArray uMaterialParamTextures;
uniform sampler2DArray uEmissiveTextures;
uniform sampler2DArray uNormalTextures;
uniform vec3 uCameraPosition;
uniform int uEnvironmentEnabled;
uniform float uEnvironmentIntensity;
uniform int uFogEnabled;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uFogAnisotropy;
uniform int uMaterialCount;
uniform int uLightCount;

const float kEpsilon = 0.0001;

vec4 encodeObjectId(int objectId) {
    int safeId = max(objectId, 0);
    int r = safeId & 255;
    int g = (safeId >> 8) & 255;
    int b = (safeId >> 16) & 255;
    return vec4(vec3(r, g, b) / 255.0, 1.0);
}

float resolveTextureLayer(int textureIndex) {
    return textureIndex >= 0 ? float(textureIndex + 1) : 0.0;
}

vec3 sampleTextureArrayRgb(sampler2DArray textureArray, int textureIndex, vec2 uv) {
    return texture(textureArray, vec3(uv, resolveTextureLayer(textureIndex))).rgb;
}

vec3 computeWorldSpaceNormal(vec3 geometricNormal, int normalTextureIndex, vec2 uv) {
    if (normalTextureIndex < 0) {
        return normalize(geometricNormal);
    }

    vec3 tangentNormal = texture(uNormalTextures, vec3(uv, resolveTextureLayer(normalTextureIndex))).xyz * 2.0 - 1.0;
    vec3 positionDx = dFdx(vWorldPosition);
    vec3 positionDy = dFdy(vWorldPosition);
    vec2 uvDx = dFdx(uv);
    vec2 uvDy = dFdy(uv);
    float determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
    if (abs(determinant) < kEpsilon) {
        return normalize(geometricNormal);
    }

    float invDeterminant = 1.0 / determinant;
    vec3 tangent = normalize((positionDx * uvDy.y - positionDy * uvDx.y) * invDeterminant);
    vec3 bitangent = normalize((positionDy * uvDx.x - positionDx * uvDy.x) * invDeterminant);
    mat3 tbn = mat3(tangent, bitangent, normalize(geometricNormal));
    return normalize(tbn * tangentNormal);
}

vec3 evaluateEnvironment(vec3 direction) {
    if (uEnvironmentEnabled == 0 || uEnvironmentIntensity <= kEpsilon) {
        return vec3(0.0);
    }

    float upAmount = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 zenith = vec3(0.18, 0.28, 0.54);
    vec3 horizon = vec3(0.78, 0.84, 0.92);
    vec3 ground = vec3(0.05, 0.06, 0.08);
    vec3 sky = mix(horizon, zenith, pow(upAmount, 0.65));
    if (direction.y < 0.0) {
        float groundBlend = clamp(direction.y + 1.0, 0.0, 1.0);
        sky = mix(ground, horizon * 0.22, groundBlend);
    }

    return sky * uEnvironmentIntensity;
}

float henyeyGreenstein(float cosTheta, float g) {
    float gSquared = g * g;
    float denominator = pow(max(1.0 + gSquared - 2.0 * g * cosTheta, 0.001), 1.5);
    return (1.0 - gSquared) / max(4.0 * 3.14159265359 * denominator, 0.001);
}

bool isLightEnabled(GpuRasterLight light) {
    return light.rightEnabled.w > 0.5;
}

int getLightType(GpuRasterLight light) {
    return int(round(light.positionType.w));
}

vec3 getLightPosition(GpuRasterLight light) {
    return light.positionType.xyz;
}

vec3 getLightDirection(GpuRasterLight light) {
    return normalize(light.directionRange.xyz);
}

vec3 getLightColor(GpuRasterLight light) {
    return light.colorIntensity.rgb;
}

float getLightIntensity(GpuRasterLight light) {
    return max(light.colorIntensity.w, 0.0);
}

int countSunLights() {
    int count = 0;
    for (int i = 0; i < uLightCount; ++i) {
        if (isLightEnabled(uLights[i]) && getLightType(uLights[i]) == 3) {
            ++count;
        }
    }
    return count;
}

float evaluateSpotFactor(GpuRasterLight light, vec3 worldPosition) {
    vec3 toSurface = normalize(worldPosition - getLightPosition(light));
    vec3 forward = getLightDirection(light);
    float cosTheta = dot(forward, toSurface);
    float innerCos = cos(radians(light.sizeAndAngles.z));
    float outerCos = cos(radians(light.sizeAndAngles.w));
    if (cosTheta <= outerCos) {
        return 0.0;
    }
    if (cosTheta >= innerCos || innerCos <= outerCos) {
        return 1.0;
    }
    return clamp((cosTheta - outerCos) / max(innerCos - outerCos, kEpsilon), 0.0, 1.0);
}

vec3 evaluateLightContribution(
    GpuRasterLight light,
    vec3 normal,
    vec3 viewDirection,
    vec3 diffuseColor,
    vec3 specularColor,
    float roughness) {
    int lightType = getLightType(light);
    vec3 lightDirection = vec3(0.0);
    float attenuation = 1.0;

    if (lightType == 3) {
        lightDirection = -getLightDirection(light);
    } else {
        vec3 toLight = getLightPosition(light) - vWorldPosition;
        float distanceSquared = dot(toLight, toLight);
        if (distanceSquared <= kEpsilon) {
            return vec3(0.0);
        }

        float distanceToLight = sqrt(distanceSquared);
        if (distanceToLight > light.directionRange.w) {
            return vec3(0.0);
        }

        lightDirection = toLight / distanceToLight;
        if (lightType == 0) {
            float areaExtent = max(light.sizeAndAngles.x, light.sizeAndAngles.y);
            attenuation = 1.0 / max(distanceSquared / max(areaExtent, 0.25), 0.35);
        } else {
            attenuation = 1.0 / max(distanceSquared, 0.2);
        }

        if (lightType == 2) {
            attenuation *= evaluateSpotFactor(light, vWorldPosition);
            if (attenuation <= kEpsilon) {
                return vec3(0.0);
            }
        }
    }

    float ndotl = max(dot(normal, lightDirection), 0.0);
    if (ndotl <= 0.0) {
        return vec3(0.0);
    }

    vec3 halfVector = normalize(viewDirection + lightDirection);
    float ndoth = max(dot(normal, halfVector), 0.0);
    float specularExponent = mix(4.0, 96.0, 1.0 - roughness);
    vec3 radiance = getLightColor(light) * getLightIntensity(light) * attenuation;
    vec3 direct = diffuseColor * ndotl;
    direct += specularColor * pow(ndoth, specularExponent) * 0.35;
    return direct * radiance;
}

void main() {
    int materialIndex = clamp(vMaterialIndex, 0, max(uMaterialCount - 1, 0));
    GpuRasterMaterial material = uMaterials[materialIndex];

    vec3 baseColor = material.baseColor.rgb * vTint;
    baseColor *= sampleTextureArrayRgb(uBaseColorTextures, material.textureRefs.x, vUv);

    vec3 emissive = material.emissionColorStrength.rgb * material.emissionColorStrength.a;
    if (material.textureRefs.z >= 0) {
        emissive *= sampleTextureArrayRgb(uEmissiveTextures, material.textureRefs.z, vUv);
    }

    vec3 materialParams = sampleTextureArrayRgb(uMaterialParamTextures, material.textureRefs.y, vUv);
    float roughness = clamp(material.surfaceParams.x * materialParams.g, 0.02, 1.0);
    float metallic = clamp(material.surfaceParams.y * materialParams.b, 0.0, 1.0);
    float transmission = clamp(material.dielectricParams.x, 0.0, 1.0);
    float transmissionRoughness = clamp(material.mediumParams1.x, 0.0, 1.0);
    vec3 absorptionTint = clamp(material.mediumParams0.rgb, vec3(0.0), vec3(1.0));

    vec3 normal = computeWorldSpaceNormal(vWorldNormal, material.textureRefs.w, vUv);
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    float ambientFactor = 0.08;
    vec3 diffuseColor = baseColor * (1.0 - metallic);
    vec3 specularColor = mix(vec3(0.04), baseColor, metallic);

    vec3 environment = evaluateEnvironment(normal);
    vec3 litColor = diffuseColor * (ambientFactor + environment);
    for (int lightIndex = 0; lightIndex < uLightCount; ++lightIndex) {
        GpuRasterLight light = uLights[lightIndex];
        if (!isLightEnabled(light)) {
            continue;
        }
        litColor += evaluateLightContribution(light, normal, viewDirection, diffuseColor, specularColor, roughness);
    }
    litColor += emissive;

    if (transmission > kEpsilon) {
        vec3 throughColor = evaluateEnvironment(-viewDirection);
        throughColor *= mix(vec3(1.0), absorptionTint, 0.45);
        float glassBlend = transmission * mix(1.0, 0.75, transmissionRoughness);
        litColor = mix(litColor, throughColor + specularColor * 0.2, clamp(glassBlend, 0.0, 1.0));
    }

    if (uFogEnabled != 0 && uFogDensity > kEpsilon) {
        vec3 rayDirection = normalize(vWorldPosition - uCameraPosition);
        float distanceToSurface = max(length(vWorldPosition - uCameraPosition), 0.0);
        float transmittance = exp(-distanceToSurface * uFogDensity);
        vec3 fogScatter = evaluateEnvironment(rayDirection) * 0.25;
        for (int lightIndex = 0; lightIndex < uLightCount; ++lightIndex) {
            GpuRasterLight light = uLights[lightIndex];
            if (!isLightEnabled(light) || getLightType(light) != 3) {
                continue;
            }

            vec3 sunDirection = -getLightDirection(light);
            float phase = henyeyGreenstein(clamp(dot(rayDirection, sunDirection), -1.0, 1.0), uFogAnisotropy);
            fogScatter += getLightColor(light) * getLightIntensity(light) * phase * 18.0;
        }
        litColor = litColor * transmittance + uFogColor * fogScatter * (1.0 - transmittance);
    }

    oColor = vec4(clamp(litColor, 0.0, 32.0), 1.0);
    oObjectId = encodeObjectId(vObjectId);
}
