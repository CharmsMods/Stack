#include "Renderer/RenderPipeline.h"
#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace {

int RawLocalRangeRegionMaskModeToShader(const std::string& mode) {
    if (mode == "linear-gradient") {
        return 1;
    }
    if (mode == "radial-gradient") {
        return 2;
    }
    if (mode == "luminance-range") {
        return 3;
    }
    return 0;
}

void UploadRawLocalRangeRegionMaskUniforms(
    unsigned int program,
    const Stack::RawRecipe::RawLocalRangeRecipe& localRange,
    int width,
    int height) {
    constexpr float kDegreesToRadians = 0.01745329251994329577f;
    const int mode = localRange.regionMaskEnabled
        ? RawLocalRangeRegionMaskModeToShader(localRange.regionMaskMode)
        : 0;
    glUniform1i(glGetUniformLocation(program, "uRegionMaskEnabled"), localRange.regionMaskEnabled ? 1 : 0);
    glUniform1i(glGetUniformLocation(program, "uRegionMaskMode"), mode);
    glUniform1i(glGetUniformLocation(program, "uRegionMaskInvert"), localRange.regionMaskInvert ? 1 : 0);
    glUniform2f(
        glGetUniformLocation(program, "uRegionMaskCenter"),
        localRange.regionMaskCenterX,
        localRange.regionMaskCenterY);
    glUniform1f(
        glGetUniformLocation(program, "uRegionMaskAngleRadians"),
        localRange.regionMaskAngleDegrees * kDegreesToRadians);
    glUniform1f(glGetUniformLocation(program, "uRegionMaskSize"), localRange.regionMaskSize);
    glUniform1f(glGetUniformLocation(program, "uRegionMaskFeather"), localRange.regionMaskFeather);
    glUniform2f(
        glGetUniformLocation(program, "uRegionMaskEvRange"),
        localRange.regionMaskLowEv,
        localRange.regionMaskHighEv);
    glUniform1f(
        glGetUniformLocation(program, "uImageAspect"),
        height > 0 ? static_cast<float>(std::max(width, 1)) / static_cast<float>(height) : 1.0f);
    glUniform1i(glGetUniformLocation(program, "uColorMaskEnabled"), localRange.colorMaskEnabled ? 1 : 0);
    glUniform3f(
        glGetUniformLocation(program, "uColorMaskTarget"),
        localRange.colorMaskTargetR,
        localRange.colorMaskTargetG,
        localRange.colorMaskTargetB);
    glUniform1f(glGetUniformLocation(program, "uColorMaskHueWidth"), localRange.colorMaskHueWidth);
    glUniform1f(glGetUniformLocation(program, "uColorMaskFeather"), localRange.colorMaskFeather);
    glUniform1f(glGetUniformLocation(program, "uColorMaskMinChroma"), localRange.colorMaskMinChroma);
}

} // namespace

void RenderPipeline::EnsureMaskPrograms() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* maskFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform int uKind;
        uniform float uValue;
        uniform float uAngle;
        uniform float uOffset;
        uniform float uScale;
        uniform vec2 uCenter;
        uniform float uRadius;
        uniform float uFeather;
        uniform int uInvert;
        float hash(vec2 p) {
            return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
        }
        float noise(vec2 p) {
            vec2 i = floor(p);
            vec2 f = fract(p);
            vec2 u = f * f * (3.0 - 2.0 * f);
            return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
                       mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
        }
        void main() {
            vec2 uv = vTexCoord;
            float maskValue = clamp(uValue, 0.0, 1.0);
            if (uKind == 1) {
                float radiansAngle = radians(uAngle);
                vec2 dir = vec2(cos(radiansAngle), sin(radiansAngle));
                maskValue = dot(uv - vec2(0.5), dir) * max(uScale, 0.001) + 0.5 + uOffset;
                maskValue = clamp(maskValue, 0.0, 1.0);
            } else if (uKind == 2) {
                float d = distance(uv, uCenter);
                float feather = max(uFeather, 0.0001);
                maskValue = 1.0 - smoothstep(max(0.0, uRadius - feather), uRadius + feather, d);
                maskValue = clamp(maskValue, 0.0, 1.0);
            } else if (uKind == 3) {
                float n = noise(uv * max(uScale * 96.0, 1.0) + vec2(uOffset * 37.0, uAngle * 0.071));
                maskValue = clamp((n - 0.5) * max(uValue * 4.0, 0.001) + 0.5, 0.0, 1.0);
            }
            if (uInvert != 0) {
                maskValue = 1.0 - maskValue;
            }
            FragColor = vec4(maskValue, maskValue, maskValue, 1.0);
        }
    )";

    static const char* blendFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uOriginal;
        uniform sampler2D uProcessed;
        uniform sampler2D uMask;
        void main() {
            vec4 originalColor = texture(uOriginal, vTexCoord);
            vec4 processedColor = texture(uProcessed, vTexCoord);
            float maskValue = clamp(texture(uMask, vTexCoord).r, 0.0, 1.0);
            FragColor = mix(originalColor, processedColor, maskValue);
        }
    )";

    static const char* maskCombineFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uMaskA;
        uniform sampler2D uMaskB;
        uniform int uMode;
        void main() {
            float a = clamp(texture(uMaskA, vTexCoord).r, 0.0, 1.0);
            float b = clamp(texture(uMaskB, vTexCoord).r, 0.0, 1.0);
            float v = 0.0;
            if (uMode == 0) {
                v = max(a, b);
            } else if (uMode == 1) {
                v = a * (1.0 - b);
            } else if (uMode == 2) {
                v = a * b;
            } else {
                v = abs(a - b);
            }
            FragColor = vec4(v, v, v, 1.0);
        }
    )";

    if (!m_MaskProgram) {
        m_MaskProgram = GLHelpers::CreateShaderProgram(vertexSrc, maskFragSrc);
    }
    if (!m_MaskCombineProgram) {
        m_MaskCombineProgram = GLHelpers::CreateShaderProgram(vertexSrc, maskCombineFragSrc);
    }
    if (!m_MaskBlendProgram) {
        m_MaskBlendProgram = GLHelpers::CreateShaderProgram(vertexSrc, blendFragSrc);
    }
}

void RenderPipeline::EnsureMixProgram() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uImageA;
        uniform sampler2D uImageB;
        uniform sampler2D uFactorMask;
        uniform int uHasFactorMask;
        uniform float uFactor;
        uniform int uBlendMode;
        void main() {
            vec4 a = texture(uImageA, vTexCoord);
            vec4 b = texture(uImageB, vTexCoord);
            float factor = uFactor;
            if (uHasFactorMask != 0) {
                factor *= clamp(texture(uFactorMask, vTexCoord).r, 0.0, 1.0);
            }

            vec4 blended = b;
            if (uBlendMode == 1) {
                blended = (a + b) * 0.5;
            } else if (uBlendMode == 2) {
                blended = a + b;
            } else if (uBlendMode == 3) {
                blended = a * b;
            } else if (uBlendMode == 4) {
                blended = 1.0 - (1.0 - a) * (1.0 - b);
            } else if (uBlendMode == 5) {
                float outA = b.a + a.a * (1.0 - b.a);
                vec3 outRgb = b.rgb * b.a + a.rgb * (1.0 - b.a);
                if (outA > 0.0001) {
                    outRgb /= outA;
                }
                blended = vec4(outRgb, outA);
            }
            FragColor = mix(a, blended, factor);
        }
    )";

    if (!m_MixProgram) {
        m_MixProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

void RenderPipeline::EnsureLutProgram() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;

        uniform sampler2D uImage;
        uniform sampler2D uLut1D;
        uniform sampler2D uShaper1D;
        uniform sampler3D uLut3D;
        uniform int uHasLut1D;
        uniform int uHasShaper1D;
        uniform int uHasLut3D;
        uniform int uInputTransform;
        uniform int uOutputTransform;
        uniform vec3 uLut1DDomainMin;
        uniform vec3 uLut1DDomainMax;
        uniform vec3 uShaperDomainMin;
        uniform vec3 uShaperDomainMax;
        uniform vec3 uLut3DDomainMin;
        uniform vec3 uLut3DDomainMax;

        float linearToSrgbChannel(float value) {
            float v = max(value, 0.0);
            if (v <= 0.0031308) {
                return 12.92 * v;
            }
            return 1.055 * pow(v, 1.0 / 2.4) - 0.055;
        }

        float srgbToLinearChannel(float value) {
            float v = clamp(value, 0.0, 1.0);
            if (v <= 0.04045) {
                return v / 12.92;
            }
            return pow((v + 0.055) / 1.055, 2.4);
        }

        float gammaEncode22(float value) {
            return pow(max(value, 0.0), 1.0 / 2.2);
        }

        float gammaDecode22(float value) {
            return pow(clamp(value, 0.0, 1.0), 2.2);
        }

        vec3 applyTransfer(vec3 color, int mode) {
            if (mode == 1) {
                return vec3(
                    linearToSrgbChannel(color.r),
                    linearToSrgbChannel(color.g),
                    linearToSrgbChannel(color.b));
            }
            if (mode == 2) {
                return vec3(
                    gammaEncode22(color.r),
                    gammaEncode22(color.g),
                    gammaEncode22(color.b));
            }
            if (mode == 3) {
                return vec3(
                    srgbToLinearChannel(color.r),
                    srgbToLinearChannel(color.g),
                    srgbToLinearChannel(color.b));
            }
            if (mode == 4) {
                return vec3(
                    gammaDecode22(color.r),
                    gammaDecode22(color.g),
                    gammaDecode22(color.b));
            }
            return color;
        }

        float remapToUnit(float value, float domainMin, float domainMax) {
            float span = domainMax - domainMin;
            if (abs(span) < 1e-6) {
                return 0.0;
            }
            return clamp((value - domainMin) / span, 0.0, 1.0);
        }

        float lutCoord1D(float value, float domainMin, float domainMax, sampler2D lutTex) {
            float t = remapToUnit(value, domainMin, domainMax);
            int size = textureSize(lutTex, 0).x;
            return ((t * float(max(size - 1, 0))) + 0.5) / float(max(size, 1));
        }

        vec3 apply1DStage(vec3 color, sampler2D lutTex, vec3 domainMin, vec3 domainMax) {
            float coordR = lutCoord1D(color.r, domainMin.r, domainMax.r, lutTex);
            float coordG = lutCoord1D(color.g, domainMin.g, domainMax.g, lutTex);
            float coordB = lutCoord1D(color.b, domainMin.b, domainMax.b, lutTex);
            vec3 sampleR = texture(lutTex, vec2(coordR, 0.5)).rgb;
            vec3 sampleG = texture(lutTex, vec2(coordG, 0.5)).rgb;
            vec3 sampleB = texture(lutTex, vec2(coordB, 0.5)).rgb;
            return vec3(sampleR.r, sampleG.g, sampleB.b);
        }

        vec3 apply3DStage(vec3 color, sampler3D lutTex, vec3 domainMin, vec3 domainMax) {
            vec3 coord = vec3(
                remapToUnit(color.r, domainMin.r, domainMax.r),
                remapToUnit(color.g, domainMin.g, domainMax.g),
                remapToUnit(color.b, domainMin.b, domainMax.b));
            vec3 size = vec3(textureSize(lutTex, 0));
            coord = ((coord * (size - 1.0)) + 0.5) / size;
            return texture(lutTex, coord).rgb;
        }

        void main() {
            vec4 source = texture(uImage, vTexCoord);
            vec3 color = applyTransfer(source.rgb, uInputTransform);

            if (uHasLut1D != 0) {
                color = apply1DStage(color, uLut1D, uLut1DDomainMin, uLut1DDomainMax);
            }
            if (uHasShaper1D != 0) {
                color = apply1DStage(color, uShaper1D, uShaperDomainMin, uShaperDomainMax);
            }
            if (uHasLut3D != 0) {
                color = apply3DStage(color, uLut3D, uLut3DDomainMin, uLut3DDomainMax);
            }

            color = applyTransfer(color, uOutputTransform);
            FragColor = vec4(color, source.a);
        }
    )";

    if (!m_LutProgram) {
        m_LutProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

void RenderPipeline::EnsureDataMathProgram() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uDataA;
        uniform sampler2D uDataB;
        uniform int uHasA;
        uniform int uHasB;
        uniform int uScalarA;
        uniform int uScalarB;
        uniform int uMode;
        uniform float uConstantA;
        uniform float uConstantB;
        uniform float uMinValue;
        uniform float uMaxValue;
        uniform float uOutMin;
        uniform float uOutMax;
        uniform int uScalarOutput;

        vec4 readData(sampler2D tex, int hasInput, int scalarInput, float fallbackValue) {
            if (hasInput == 0) {
                return vec4(fallbackValue, fallbackValue, fallbackValue, fallbackValue);
            }
            vec4 value = texture(tex, vTexCoord);
            if (scalarInput != 0) {
                return vec4(value.r, value.r, value.r, 1.0);
            }
            return value;
        }

        void main() {
            vec4 a = readData(uDataA, uHasA, uScalarA, uConstantA);
            vec4 b = readData(uDataB, uHasB, uScalarB, uConstantB);
            vec4 result = a;

            if (uMode == 0) {
                result = clamp(a, vec4(uMinValue), vec4(uMaxValue));
            } else if (uMode == 1) {
                result = a + b;
            } else if (uMode == 2) {
                result = a - b;
            } else if (uMode == 3) {
                result = a * b;
            } else if (uMode == 4) {
                result = a / max(abs(b), vec4(0.00001));
            } else if (uMode == 5) {
                result = (a + b) * 0.5;
            } else if (uMode == 6) {
                result = min(a, b);
            } else if (uMode == 7) {
                result = max(a, b);
            } else if (uMode == 8) {
                result = abs(a - b);
            } else if (uMode == 9) {
                float span = max(uMaxValue - uMinValue, 0.00001);
                result = mix(vec4(uOutMin), vec4(uOutMax), clamp((a - vec4(uMinValue)) / span, 0.0, 1.0));
            }

            if (uScalarOutput != 0) {
                float v = result.r;
                FragColor = vec4(v, v, v, 1.0);
            } else {
                FragColor = result;
            }
        }
    )";

    if (!m_DataMathProgram) {
        m_DataMathProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

void RenderPipeline::EnsureHdrMergeProgram() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInput1;
        uniform sampler2D uInput2;
        uniform sampler2D uInput3;
        uniform int uHasInput2;
        uniform int uHasInput3;
        uniform vec3 uExposureEv;
        uniform vec2 uTexelSize;
        uniform vec2 uInputOffsetPx[3];
        uniform int uReferenceIndex;
        uniform vec3 uAlignmentConfidence;
        uniform float uDeghostStrength;
        uniform int uMotionPriority;
        uniform vec3 uClipThreshold;
        uniform vec3 uClipFeather;
        uniform vec3 uBlackThreshold;
        uniform vec3 uBlackFeather;
        uniform vec3 uReadNoise;
        uniform int uNoiseAware;
        uniform int uDebugView;

        float luminance(vec3 rgb) {
            return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        }

        vec2 shiftedUv(vec2 uv, int index) {
            return clamp(uv + uInputOffsetPx[index] * uTexelSize, vec2(0.0), vec2(1.0));
        }

        vec4 mergeSample(vec3 sourceRgb, int index, float exposureEv, out float clipRisk, out float blackLimit) {
            vec3 positiveRgb = max(sourceRgb, vec3(0.0));
            float sourceMax = max(max(positiveRgb.r, positiveRgb.g), positiveRgb.b);
            float sourceLum = luminance(positiveRgb);
            float clipThreshold = max(uClipThreshold[index], 0.0);
            float clipFeather = max(uClipFeather[index], 0.0001);
            float blackThreshold = max(uBlackThreshold[index], 0.0);
            float blackFeather = max(uBlackFeather[index], 0.0001);
            clipRisk = smoothstep(clipThreshold - clipFeather, clipThreshold + clipFeather, sourceMax);
            blackLimit = 1.0 - smoothstep(blackThreshold, blackThreshold + blackFeather, sourceLum);

            float clipWeight = 1.0 - clipRisk;
            float blackWeight = 1.0 - blackLimit;
            float exposureScale = exp2(-exposureEv);
            vec3 normalizedRgb = positiveRgb * exposureScale;
            float weight = clipWeight * blackWeight;

            if (uNoiseAware != 0) {
                float normalizedLum = max(luminance(normalizedRgb), 0.0);
                float readNoise = max(uReadNoise[index], 0.0);
                float variance = readNoise * readNoise * exposureScale * exposureScale + normalizedLum + 0.000001;
                weight *= clamp(1.0 / variance, 0.0, 1000000.0);
            }
            return vec4(normalizedRgb * weight, weight);
        }

        float motionWeight(vec3 referenceRgb, vec3 candidateRgb, float confidence, float disagreementStrength) {
            float refLum = max(luminance(referenceRgb), 0.00003);
            float candLum = max(luminance(candidateRgb), 0.00003);
            float lumDelta = abs(candLum - refLum) / max(max(refLum, candLum), 0.03);
            float colorDelta = length(candidateRgb - referenceRgb) / max(max(refLum, candLum), 0.05);
            float disagreement = smoothstep(0.08, 0.30, lumDelta * 0.80 + colorDelta * 0.45);
            disagreement = max(disagreement, (1.0 - confidence) * 0.55);
            float weight = 1.0 - disagreement * disagreementStrength * (uMotionPriority == 0 ? 1.0 : 0.72);
            if (uMotionPriority == 0) {
                weight = mix(weight, 0.0, smoothstep(0.55, 0.92, disagreement) * disagreementStrength);
            }
            return clamp(weight, 0.0, 1.0);
        }

        void main() {
            vec3 source1 = texture(uInput1, shiftedUv(vTexCoord, 0)).rgb;
            vec3 source2 = texture(uInput2, shiftedUv(vTexCoord, 1)).rgb;
            vec3 source3 = texture(uInput3, shiftedUv(vTexCoord, 2)).rgb;

            vec3 normalized1 = max(source1, vec3(0.0)) * exp2(-uExposureEv.x);
            vec3 normalized2 = max(source2, vec3(0.0)) * exp2(-uExposureEv.y);
            vec3 normalized3 = max(source3, vec3(0.0)) * exp2(-uExposureEv.z);

            float clip1 = 0.0;
            float clip2 = 0.0;
            float clip3 = 0.0;
            float black1 = 0.0;
            float black2 = 0.0;
            float black3 = 0.0;

            vec4 merged1 = mergeSample(source1, 0, uExposureEv.x, clip1, black1);
            vec4 merged2 = (uHasInput2 != 0) ? mergeSample(source2, 1, uExposureEv.y, clip2, black2) : vec4(0.0);
            vec4 merged3 = (uHasInput3 != 0) ? mergeSample(source3, 2, uExposureEv.z, clip3, black3) : vec4(0.0);

            vec3 referenceRgb = normalized1;
            if (uReferenceIndex == 1) {
                referenceRgb = normalized2;
            } else if (uReferenceIndex == 2) {
                referenceRgb = normalized3;
            }

            float motionMask1 = 0.0;
            float motionMask2 = 0.0;
            float motionMask3 = 0.0;
            float rejected1 = 0.0;
            float rejected2 = 0.0;
            float rejected3 = 0.0;
            if (uDeghostStrength > 0.0001) {
                if (uReferenceIndex != 0) {
                    float weight = motionWeight(referenceRgb, normalized1, uAlignmentConfidence.x, uDeghostStrength);
                    motionMask1 = 1.0 - weight;
                    rejected1 = motionMask1;
                    merged1.rgb *= weight;
                    merged1.a *= weight;
                }
                if (uHasInput2 != 0 && uReferenceIndex != 1) {
                    float weight = motionWeight(referenceRgb, normalized2, uAlignmentConfidence.y, uDeghostStrength);
                    motionMask2 = 1.0 - weight;
                    rejected2 = motionMask2;
                    merged2.rgb *= weight;
                    merged2.a *= weight;
                }
                if (uHasInput3 != 0 && uReferenceIndex != 2) {
                    float weight = motionWeight(referenceRgb, normalized3, uAlignmentConfidence.z, uDeghostStrength);
                    motionMask3 = 1.0 - weight;
                    rejected3 = motionMask3;
                    merged3.rgb *= weight;
                    merged3.a *= weight;
                }
            }

            vec3 weightedRgb = merged1.rgb + merged2.rgb + merged3.rgb;
            float weightSum = merged1.a + merged2.a + merged3.a;
            vec3 result = weightSum > 0.000001 ? weightedRgb / weightSum : referenceRgb;
            if (uDeghostStrength > 0.0001) {
                float strongestMotion = 0.0;
                if (uReferenceIndex != 0) {
                    strongestMotion = max(strongestMotion, motionMask1);
                }
                if (uHasInput2 != 0 && uReferenceIndex != 1) {
                    strongestMotion = max(strongestMotion, motionMask2);
                }
                if (uHasInput3 != 0 && uReferenceIndex != 2) {
                    strongestMotion = max(strongestMotion, motionMask3);
                }
                float fallbackStrength = smoothstep(0.58, 0.90, strongestMotion) * uDeghostStrength;
                if (uMotionPriority != 0) {
                    fallbackStrength *= 0.60;
                }
                result = mix(result, referenceRgb, clamp(fallbackStrength, 0.0, 1.0));
            }

            if (uDebugView == 1) {
                float denom = max(weightSum, 0.000001);
                FragColor = vec4(merged1.a / denom, merged2.a / denom, merged3.a / denom, 1.0);
            } else if (uDebugView == 2) {
                FragColor = vec4(clip1, (uHasInput2 != 0) ? clip2 : 0.0, (uHasInput3 != 0) ? clip3 : 0.0, 1.0);
            } else if (uDebugView == 3) {
                FragColor = vec4(black1, (uHasInput2 != 0) ? black2 : 0.0, (uHasInput3 != 0) ? black3 : 0.0, 1.0);
            } else if (uDebugView == 4) {
                FragColor = vec4(uAlignmentConfidence.x, uAlignmentConfidence.y, uAlignmentConfidence.z, 1.0);
            } else if (uDebugView == 5) {
                FragColor = vec4(motionMask1, motionMask2, motionMask3, 1.0);
            } else if (uDebugView == 6) {
                FragColor = vec4(rejected1, rejected2, rejected3, 1.0);
            } else {
                FragColor = vec4(result, 1.0);
            }
        }
    )";

    if (!m_HdrMergeProgram) {
        m_HdrMergeProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

void RenderPipeline::EnsureUtilityPrograms() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* maskUtilityFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputMask;
        uniform int uKind;
        uniform float uBlackPoint;
        uniform float uWhitePoint;
        uniform float uGamma;
        uniform float uThreshold;
        uniform float uSoftness;
        uniform int uInvert;
        void main() {
            float v = clamp(texture(uInputMask, vTexCoord).r, 0.0, 1.0);
            if (uKind == 0) {
                v = 1.0 - v;
            } else if (uKind == 1) {
                float denom = max(uWhitePoint - uBlackPoint, 0.0001);
                v = clamp((v - uBlackPoint) / denom, 0.0, 1.0);
                v = pow(v, 1.0 / max(uGamma, 0.001));
                if (uInvert != 0) v = 1.0 - v;
            } else if (uKind == 2) {
                float softness = max(uSoftness, 0.0001);
                v = smoothstep(uThreshold - softness, uThreshold + softness, v);
                if (uInvert != 0) v = 1.0 - v;
            }
            FragColor = vec4(v, v, v, 1.0);
        }
    )";

    static const char* imageToMaskFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform int uKind;
        uniform float uLow;
        uniform float uHigh;
        uniform float uSoftness;
        uniform int uInvert;
        uniform int uSampleCount;
        uniform vec3 uSampleRgb;
        uniform float uSampleLuma;
        uniform vec3 uExtraSampleRgb[4];
        uniform float uExtraSampleLuma[4];
        uniform vec2 uSampleUv;
        uniform float uToneSimilarity;
        uniform float uColorSimilarity;
        uniform float uRegionRadius;
        uniform float uRegionFeather;
        uniform float uEdgeSensitivity;
        uniform float uLocalCoherence;
        uniform vec2 uTexelSize;
        vec3 chromaOf(vec3 rgb) {
            float sum = max(rgb.r + rgb.g + rgb.b, 0.00001);
            return rgb / sum;
        }
        float matchAgainstSeed(vec3 rgb, float lum, vec3 sampleChroma, float softnessScale, float sampleLuma, float toneSimilarity, float colorSimilarity) {
            float toneDistance = abs(lum - sampleLuma) / max(toneSimilarity, 0.0001);
            vec3 pixelChroma = chromaOf(max(rgb, vec3(0.0)));
            float colorDistance = distance(pixelChroma, sampleChroma) / max(colorSimilarity, 0.0001);
            float toneMatch = 1.0 - smoothstep(1.0, 1.0 + softnessScale, toneDistance);
            float colorMatch = 1.0 - smoothstep(1.0, 1.0 + softnessScale, colorDistance);
            return toneMatch * colorMatch;
        }
        float bestSampleMatch(vec3 rgb, float lum, float softnessScale) {
            float best = 0.0;
            vec3 primaryChroma = chromaOf(max(uSampleRgb, vec3(0.0)));
            best = max(best, matchAgainstSeed(rgb, lum, primaryChroma, softnessScale, uSampleLuma, uToneSimilarity, uColorSimilarity));
            for (int i = 0; i < 4; ++i) {
                if (i + 1 >= uSampleCount) {
                    break;
                }
                vec3 sampleChroma = chromaOf(max(uExtraSampleRgb[i], vec3(0.0)));
                best = max(best, matchAgainstSeed(rgb, lum, sampleChroma, softnessScale, uExtraSampleLuma[i], uToneSimilarity, uColorSimilarity));
            }
            return best;
        }
        void main() {
            vec4 c = texture(uInputImage, vTexCoord);
            float lum = dot(c.rgb, vec3(0.2126, 0.7152, 0.0722));
            float v = 0.0;
            if (uKind == 0) {
                float denom = max(uHigh - uLow, 0.0001);
                v = clamp((lum - uLow) / denom, 0.0, 1.0);
                if (uSoftness > 0.0001) {
                    v = smoothstep(0.5 - uSoftness, 0.5 + uSoftness, v);
                }
            } else {
                float softnessScale = max(uSoftness, 0.0001) * 3.0;
                float baseMatch = bestSampleMatch(c.rgb, lum, softnessScale);
                float spatialDistance = distance(vTexCoord, uSampleUv);
                float spatialRadius = clamp(uRegionRadius, 0.05, 1.0);
                float spatialSoftness = mix(0.08, 0.30, clamp(uRegionFeather, 0.0, 1.0));
                float spatialMatch = 1.0 - smoothstep(spatialRadius, spatialRadius + spatialSoftness, spatialDistance);
                vec3 rgbX = texture(uInputImage, clamp(vTexCoord + vec2(uTexelSize.x, 0.0), vec2(0.0), vec2(1.0))).rgb -
                            texture(uInputImage, clamp(vTexCoord - vec2(uTexelSize.x, 0.0), vec2(0.0), vec2(1.0))).rgb;
                vec3 rgbY = texture(uInputImage, clamp(vTexCoord + vec2(0.0, uTexelSize.y), vec2(0.0), vec2(1.0))).rgb -
                            texture(uInputImage, clamp(vTexCoord - vec2(0.0, uTexelSize.y), vec2(0.0), vec2(1.0))).rgb;
                float edgeStrength = length(rgbX) + length(rgbY);
                float edgeThreshold = mix(0.75, 0.08, clamp(uEdgeSensitivity, 0.0, 1.0));
                float edgePenalty = smoothstep(edgeThreshold, edgeThreshold + 0.25, edgeStrength);
                float edgeAware = 1.0 - edgePenalty * (1.0 - baseMatch) * clamp(uEdgeSensitivity, 0.0, 1.0);
                float coherenceRadius = mix(1.0, 5.0, clamp(uRegionFeather, 0.0, 1.0));
                vec2 coherenceOffset = uTexelSize * coherenceRadius;
                vec3 rgbPx = texture(uInputImage, clamp(vTexCoord + vec2(coherenceOffset.x, 0.0), vec2(0.0), vec2(1.0))).rgb;
                vec3 rgbNx = texture(uInputImage, clamp(vTexCoord - vec2(coherenceOffset.x, 0.0), vec2(0.0), vec2(1.0))).rgb;
                vec3 rgbPy = texture(uInputImage, clamp(vTexCoord + vec2(0.0, coherenceOffset.y), vec2(0.0), vec2(1.0))).rgb;
                vec3 rgbNy = texture(uInputImage, clamp(vTexCoord - vec2(0.0, coherenceOffset.y), vec2(0.0), vec2(1.0))).rgb;
                float lumPx = dot(rgbPx, vec3(0.2126, 0.7152, 0.0722));
                float lumNx = dot(rgbNx, vec3(0.2126, 0.7152, 0.0722));
                float lumPy = dot(rgbPy, vec3(0.2126, 0.7152, 0.0722));
                float lumNy = dot(rgbNy, vec3(0.2126, 0.7152, 0.0722));
                float coherenceSum = baseMatch;
                coherenceSum += bestSampleMatch(rgbPx, lumPx, softnessScale);
                coherenceSum += bestSampleMatch(rgbNx, lumNx, softnessScale);
                coherenceSum += bestSampleMatch(rgbPy, lumPy, softnessScale);
                coherenceSum += bestSampleMatch(rgbNy, lumNy, softnessScale);
                float coherenceAvg = coherenceSum / 5.0;
                float coherenceBoost = mix(1.0, smoothstep(0.25, 0.70, coherenceAvg), clamp(uLocalCoherence, 0.0, 1.0));
                v = baseMatch * spatialMatch * edgeAware * coherenceBoost;
            }
            if (uInvert != 0) v = 1.0 - v;
            FragColor = vec4(v, v, v, 1.0);
        }
    )";

    static const char* imageGeneratorFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform int uKind;
        uniform vec4 uColorA;
        uniform vec4 uColorB;
        uniform float uAngle;
        uniform float uOffset;
        void main() {
            if (uKind == 0) {
                FragColor = uColorA;
                return;
            }
            if (uKind == 2) {
                float dist = max(abs(vTexCoord.x - 0.5), abs(vTexCoord.y - 0.5)) - 0.34;
                float delta = fwidth(dist);
                float mask = 1.0 - smoothstep(-0.5 * delta, 0.5 * delta, dist);
                FragColor = vec4(uColorA.rgb, uColorA.a * mask);
                return;
            }
            if (uKind == 3) {
                float d = distance(vTexCoord, vec2(0.5));
                float dist = d - 0.34;
                float delta = fwidth(dist);
                float mask = 1.0 - smoothstep(-0.5 * delta, 0.5 * delta, dist);
                FragColor = vec4(uColorA.rgb, uColorA.a * mask);
                return;
            }
            float radiansAngle = radians(uAngle);
            vec2 dir = vec2(cos(radiansAngle), sin(radiansAngle));
            float t = clamp(dot(vTexCoord - vec2(0.5), dir) + 0.5 + uOffset, 0.0, 1.0);
            FragColor = mix(uColorA, uColorB, t);
        }
    )";

    if (!m_MaskUtilityProgram) {
        m_MaskUtilityProgram = GLHelpers::CreateShaderProgram(vertexSrc, maskUtilityFragSrc);
    }
    if (!m_ImageToMaskProgram) {
        m_ImageToMaskProgram = GLHelpers::CreateShaderProgram(vertexSrc, imageToMaskFragSrc);
    }
    if (!m_ImageGeneratorProgram) {
        m_ImageGeneratorProgram = GLHelpers::CreateShaderProgram(vertexSrc, imageGeneratorFragSrc);
    }
}

void RenderPipeline::EnsureChannelPrograms() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* splitFragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform int uChannel; // 0 = R, 1 = G, 2 = B, 3 = A
        void main() {
            vec4 col = texture(uInputImage, vTexCoord);
            float v = col.r;
            if (uChannel == 1) v = col.g;
            else if (uChannel == 2) v = col.b;
            else if (uChannel == 3) v = col.a;
            FragColor = vec4(v, v, v, 1.0);
        }
    )";

    static const char* combineFragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uTexR;
        uniform sampler2D uTexG;
        uniform sampler2D uTexB;
        uniform sampler2D uTexA;
        uniform int uHasR;
        uniform int uHasG;
        uniform int uHasB;
        uniform int uHasA;
        void main() {
            float r = (uHasR != 0) ? texture(uTexR, vTexCoord).r : 0.0;
            float g = (uHasG != 0) ? texture(uTexG, vTexCoord).r : 0.0;
            float b = (uHasB != 0) ? texture(uTexB, vTexCoord).r : 0.0;
            float a = (uHasA != 0) ? texture(uTexA, vTexCoord).r : 1.0;
            FragColor = vec4(r, g, b, a);
        }
    )";

    if (!m_ChannelSplitProgram) {
        m_ChannelSplitProgram = GLHelpers::CreateShaderProgram(vertexSrc, splitFragmentSrc);
    }
    if (!m_ChannelCombineProgram) {
        m_ChannelCombineProgram = GLHelpers::CreateShaderProgram(vertexSrc, combineFragmentSrc);
    }
}

void RenderPipeline::EnsureAutoGainStatsProgram() {
    if (m_AutoGainStatsProgram) {
        return;
    }

    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform vec2 uSourceTexelSize;

        float luma(vec3 rgb) {
            return dot(max(rgb, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
        }

        float logLuma(vec2 uv) {
            return log2(max(luma(texture(uInputImage, uv).rgb), 0.00003));
        }

        void main() {
            vec3 rgb = max(texture(uInputImage, vTexCoord).rgb, vec3(0.0));
            float lum = luma(rgb);
            float maxChannel = max(max(rgb.r, rgb.g), rgb.b);
            float minChannel = min(min(rgb.r, rgb.g), rgb.b);
            float saturation = maxChannel > 0.00003 ? (maxChannel - minChannel) / maxChannel : 0.0;
            float gx = abs(logLuma(vTexCoord + vec2(uSourceTexelSize.x, 0.0)) - logLuma(vTexCoord - vec2(uSourceTexelSize.x, 0.0)));
            float gy = abs(logLuma(vTexCoord + vec2(0.0, uSourceTexelSize.y)) - logLuma(vTexCoord - vec2(0.0, uSourceTexelSize.y)));
            float textureProxy = clamp((gx + gy) * 0.5, 0.0, 1.0);
            FragColor = vec4(lum, maxChannel, saturation, textureProxy);
        }
    )";

    m_AutoGainStatsProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
}

void RenderPipeline::EnsureRawDetailFusionPrograms() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* metricsFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform float uSmoothGradientProtection;
        uniform float uTextureSensitivity;
        uniform float uSkyBias;
        uniform float uEstimatedNoiseFloor;
        uniform float uAutoNoiseProtection;
        uniform float uAutoHighlightProtection;
        uniform float uChannelSaturationRisk;
        uniform vec2 uTexelSize;

        vec3 rgbAt(vec2 uv) {
            return max(texture(uInputImage, uv).rgb, vec3(0.0));
        }

        float luma(vec3 rgb) {
            return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        }

        float lumaAt(vec2 uv) {
            return luma(rgbAt(uv));
        }

        float logLumaAt(vec2 uv) {
            return log2(max(lumaAt(uv), 0.00003));
        }

        void main() {
            vec3 centerRgb = rgbAt(vTexCoord);
            float centerLum = luma(centerRgb);
            float centerLog = log2(max(centerLum, 0.00003));
            vec2 texel = uTexelSize;

            float l = logLumaAt(vTexCoord - vec2(texel.x, 0.0));
            float r = logLumaAt(vTexCoord + vec2(texel.x, 0.0));
            float d = logLumaAt(vTexCoord - vec2(0.0, texel.y));
            float u = logLumaAt(vTexCoord + vec2(0.0, texel.y));
            float l2 = logLumaAt(vTexCoord - vec2(texel.x * 2.0, 0.0));
            float r2 = logLumaAt(vTexCoord + vec2(texel.x * 2.0, 0.0));
            float d2 = logLumaAt(vTexCoord - vec2(0.0, texel.y * 2.0));
            float u2 = logLumaAt(vTexCoord + vec2(0.0, texel.y * 2.0));

            float gradient = length(vec2(r - l, u - d));
            float second = abs(l - centerLog * 2.0 + r) + abs(d - centerLog * 2.0 + u);
            float broadGradient = length(vec2(r2 - l2, u2 - d2));
            float broadSecond = abs(l2 - centerLog * 2.0 + r2) + abs(d2 - centerLog * 2.0 + u2);

            vec3 meanRgb = vec3(0.0);
            float meanLum = 0.0;
            float meanLog = 0.0;
            float samples = 0.0;
            for (int y = -2; y <= 2; ++y) {
                for (int x = -2; x <= 2; ++x) {
                    vec2 uv = vTexCoord + vec2(x, y) * texel;
                    vec3 rgb = rgbAt(uv);
                    float lum = luma(rgb);
                    meanRgb += rgb;
                    meanLum += lum;
                    meanLog += log2(max(lum, 0.00003));
                    samples += 1.0;
                }
            }
            meanRgb /= max(samples, 1.0);
            meanLum /= max(samples, 1.0);
            meanLog /= max(samples, 1.0);

            float variance = 0.0;
            float chromaVariance = 0.0;
            for (int y = -2; y <= 2; ++y) {
                for (int x = -2; x <= 2; ++x) {
                    vec2 uv = vTexCoord + vec2(x, y) * texel;
                    vec3 rgb = rgbAt(uv);
                    float logLum = log2(max(luma(rgb), 0.00003));
                    variance += pow(logLum - meanLog, 2.0);
                    chromaVariance += length((rgb - vec3(luma(rgb))) - (meanRgb - vec3(meanLum)));
                }
            }
            variance /= max(samples, 1.0);
            chromaVariance /= max(samples, 1.0);

            float textureSensitivity = clamp(uTextureSensitivity, 0.0, 1.0);
            float smoothProtect = clamp(uSmoothGradientProtection, 0.0, 1.0);
            float skyBias = clamp(uSkyBias, 0.0, 1.0);

            float trueEdge = smoothstep(mix(0.08, 0.025, textureSensitivity), mix(0.42, 0.15, textureSensitivity), gradient + second * 3.25);
            trueEdge = max(trueEdge, smoothstep(0.18, 0.95, broadSecond * 4.0));

            float textureDetail = smoothstep(mix(0.010, 0.003, textureSensitivity), mix(0.090, 0.030, textureSensitivity), sqrt(max(variance, 0.0)) + second * 1.5);
            textureDetail *= 1.0 - smoothstep(0.035, 0.22, broadGradient) * 0.55;

            float lowTexture = 1.0 - smoothstep(mix(0.005, 0.018, textureSensitivity), mix(0.060, 0.16, textureSensitivity), sqrt(max(variance, 0.0)) + chromaVariance);
            float rampLike = smoothstep(0.010, 0.18, broadGradient) * (1.0 - smoothstep(0.050, 0.34, broadSecond * 2.0));
            float lowSaturation = 1.0 - smoothstep(0.08, 0.42, length(centerRgb - vec3(centerLum)));
            float blueSkyHint = smoothstep(0.0, 0.14, centerRgb.b - max(centerRgb.r, centerRgb.g) * 0.72);
            float brightEnough = smoothstep(0.010, 0.22, centerLum);
            float smoothGradient = max(lowTexture * rampLike, lowTexture * mix(lowSaturation, max(lowSaturation, blueSkyHint), skyBias) * brightEnough);
            smoothGradient = clamp(smoothGradient * mix(0.35, 1.35, smoothProtect) * (1.0 - trueEdge * 0.92), 0.0, 1.0);

            float debandRisk = smoothGradient * (1.0 - textureDetail) * smoothstep(0.012, 0.30, broadGradient);
            float centerChroma = length(centerRgb - vec3(centerLum));
            float maxChannel = max(max(centerRgb.r, centerRgb.g), centerRgb.b);
            float minChannel = min(min(centerRgb.r, centerRgb.g), centerRgb.b);
            float saturation = maxChannel > 0.00003 ? (maxChannel - minChannel) / maxChannel : 0.0;
            float shadowNoise = (1.0 - smoothstep(uEstimatedNoiseFloor * 2.0, uEstimatedNoiseFloor * 9.0, centerLum)) *
                (1.0 - trueEdge * 0.65);
            float sceneSaturationRisk = smoothstep(0.35, 0.92, saturation) * smoothstep(0.18, 1.08, maxChannel);
            float chromaArtifact = trueEdge *
                smoothstep(0.010 + centerLum * 0.025, 0.16 + centerLum * 0.20, centerChroma + chromaVariance * 1.8) *
                (1.0 - smoothstep(0.08, 0.85, lowTexture));
            chromaArtifact = max(chromaArtifact, sceneSaturationRisk * clamp(uChannelSaturationRisk, 0.0, 1.0) * 0.75);
            textureDetail *= 1.0 - chromaArtifact * mix(0.45, 0.85, clamp(uAutoHighlightProtection, 0.0, 1.0));
            textureDetail *= 1.0 - shadowNoise * mix(0.25, 0.95, clamp(uAutoNoiseProtection, 0.0, 1.0));
            FragColor = vec4(clamp(trueEdge, 0.0, 1.0), clamp(textureDetail, 0.0, 1.0), smoothGradient, clamp(max(max(debandRisk, chromaArtifact), shadowNoise * 0.75), 0.0, 1.0));
        }
    )";

    static const char* analysisFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform sampler2D uMetrics;
        uniform sampler2D uManualMask;
        uniform int uHasManualMask;
        uniform int uMode;
        uniform float uMinEv;
        uniform float uMaxEv;
        uniform float uBaseEv;
        uniform int uSampleCount;
        uniform float uBaseRadiusPercent;
        uniform float uHighlightProtection;
        uniform float uShadowLiftLimit;
        uniform float uNoiseProtection;
        uniform float uDetailWeight;
        uniform float uWellExposedTarget;
        uniform float uSmoothGradientProtection;
        uniform float uSkyBias;
        uniform float uEstimatedNoiseFloor;
        uniform float uChannelSaturationRisk;
        uniform float uClippingRatio;
        uniform int uInvertMask;
        uniform float uMaskBlackPoint;
        uniform float uMaskWhitePoint;
        uniform float uMaskGamma;
        uniform float uManualBlend;
        uniform vec2 uTexelSize;

        float lumaAt(vec2 uv) {
            vec3 rgb = max(texture(uInputImage, uv).rgb, vec3(0.0));
            return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        }

        vec3 rgbAt(vec2 uv) {
            return max(texture(uInputImage, uv).rgb, vec3(0.0));
        }

        float shapedMask() {
            float v = uHasManualMask != 0 ? texture(uManualMask, vTexCoord).r : 0.5;
            float denom = max(uMaskWhitePoint - uMaskBlackPoint, 0.0001);
            v = clamp((v - uMaskBlackPoint) / denom, 0.0, 1.0);
            v = pow(v, 1.0 / max(uMaskGamma, 0.001));
            if (uInvertMask != 0) v = 1.0 - v;
            return v;
        }

        float logLumaAt(vec2 uv) {
            return log2(max(lumaAt(uv), 0.00003));
        }

        float edgeAwareBaseLogLuma(float centerLog, float centerLum, vec4 centerMetrics) {
            float longEdge = max(1.0 / max(uTexelSize.x, 0.000001), 1.0 / max(uTexelSize.y, 0.000001));
            float desiredRadius = max(2.0, longEdge * clamp(uBaseRadiusPercent, 0.002, 0.030));
            int sampleExtent = clamp((uSampleCount + 3) / 4, 2, 8);
            float sampleStep = max(1.0, desiredRadius / float(sampleExtent));
            float radius2 = desiredRadius * desiredRadius;
            float centerEdge = clamp(centerMetrics.r, 0.0, 1.0);
            float centerSmooth = clamp(centerMetrics.b, 0.0, 1.0);
            float sum = 0.0;
            float weightSum = 0.0;
            for (int y = -8; y <= 8; ++y) {
                for (int x = -8; x <= 8; ++x) {
                    if (abs(x) > sampleExtent || abs(y) > sampleExtent) continue;
                    vec2 offsetPx = vec2(x, y) * sampleStep;
                    float dist2 = dot(offsetPx, offsetPx);
                    if (dist2 > radius2) continue;
                    vec2 uv = vTexCoord + offsetPx * uTexelSize;
                    float sampleLog = logLumaAt(uv);
                    float sampleLum = lumaAt(uv);
                    vec4 sampleMetrics = texture(uMetrics, uv);
                    float sampleEdge = clamp(sampleMetrics.r, 0.0, 1.0);
                    float sampleSmooth = clamp(sampleMetrics.b, 0.0, 1.0);
                    float spatial = exp(-dist2 / max(1.0, radius2 * 0.42));
                    float edgeStop = max(centerEdge, sampleEdge);
                    float rangeScale = mix(1.35, 0.28, edgeStop);
                    float logRange = exp(-abs(sampleLog - centerLog) / max(0.0001, rangeScale));
                    float linearRange = exp(-abs(sampleLum - centerLum) / max(0.0001, mix(0.42, 0.055, edgeStop)));
                    float smoothAffinity = 1.0 - abs(sampleSmooth - centerSmooth);
                    float w = spatial * logRange * linearRange;
                    w = mix(w, max(w, 0.25 + smoothAffinity * 0.35), centerSmooth * clamp(uSmoothGradientProtection, 0.0, 1.0) * (1.0 - edgeStop));
                    w *= 1.0 - edgeStop * 0.72;
                    sum += sampleLog * w;
                    weightSum += w;
                }
            }
            return weightSum > 0.0 ? sum / weightSum : centerLog;
        }

        void main() {
            vec3 centerRgb = rgbAt(vTexCoord);
            float lum = dot(centerRgb, vec3(0.2126, 0.7152, 0.0722));
            float maxChannel = max(max(centerRgb.r, centerRgb.g), centerRgb.b);
            float minChannel = min(min(centerRgb.r, centerRgb.g), centerRgb.b);
            float channelDominance = maxChannel > 0.00003 ? (maxChannel - minChannel) / maxChannel : 0.0;
            float saturatedBright = smoothstep(0.45, 0.92, channelDominance) * smoothstep(0.28, 1.08, maxChannel);
            float globalHighlightPressure = clamp(uClippingRatio * 8.0 + uChannelSaturationRisk * 2.2, 0.0, 1.0);
            vec4 metrics = texture(uMetrics, vTexCoord);
            float trueEdge = clamp(metrics.r, 0.0, 1.0);
            float textureDetail = clamp(metrics.g, 0.0, 1.0);
            float smoothGradient = clamp(metrics.b, 0.0, 1.0);
            float chromaArtifact = clamp(metrics.a, 0.0, 1.0);
            float smoothProtect = clamp(uSmoothGradientProtection, 0.0, 1.0);

            float evSpan = max(0.0001, uMaxEv - uMinEv);
            float minAbsEv = min(uMinEv, uMaxEv) + uBaseEv;
            float maxAbsEv = max(uMinEv, uMaxEv) + uBaseEv;
            float target = clamp(uWellExposedTarget, 0.10, 0.55);
            float safeLum = max(lum, 0.00003);
            float centerLog = log2(safeLum);
            float baseLog = edgeAwareBaseLogLuma(centerLog, safeLum, metrics);
            float baseLum = exp2(baseLog);
            float targetLog = log2(max(target, 0.00003));
            float zone = baseLog - targetLog;

            float shadowCurve = smoothstep(0.15, 2.25, -zone);
            float highlightCurve = smoothstep(0.10, 2.10, zone);
            float maxShadowBoost = max(0.0, uMaxEv);
            float maxHighlightCompress = max(0.0, -uMinEv);

            float clipRisk = smoothstep(0.82, 1.18, baseLum);
            float saturatedClipRisk = max(clipRisk, saturatedBright * mix(0.35, 1.0, globalHighlightPressure));
            float adaptiveNoiseFloor = max(0.00003, uEstimatedNoiseFloor);
            float deepShadow = 1.0 - smoothstep(adaptiveNoiseFloor * 1.5, mix(adaptiveNoiseFloor * 5.0, adaptiveNoiseFloor * 18.0, clamp(uNoiseProtection, 0.0, 1.0)), safeLum);
            float blackRisk = 1.0 - smoothstep(0.004, mix(0.018, 0.12, clamp(uNoiseProtection, 0.0, 1.0)), baseLum);
            float snrConfidence = smoothstep(adaptiveNoiseFloor * 3.0, adaptiveNoiseFloor * 18.0, safeLum);
            snrConfidence *= 1.0 - deepShadow * clamp(uNoiseProtection, 0.0, 1.0) * 0.35;

            float specularOrLuminous = max(
                saturatedClipRisk,
                smoothstep(0.78, 1.25, baseLum) * (1.0 - textureDetail * 0.70) * mix(0.55, 1.0, globalHighlightPressure));
            float gradientGate = mix(1.0, 1.0 - max(smoothGradient * 0.70, chromaArtifact * 0.55), smoothProtect);
            float haloGate = mix(1.0, 1.0 - trueEdge * 0.35, clamp(uSkyBias, 0.0, 1.0));
            float shadowGate = snrConfidence * gradientGate * haloGate * (1.0 - chromaArtifact * 0.80) * (1.0 - saturatedBright * 0.85);
            shadowGate *= mix(1.0, 0.20, deepShadow * clamp(uShadowLiftLimit, 0.0, 1.0) * clamp(uNoiseProtection, 0.0, 1.0));

            float highlightGate = mix(1.0, 1.0 - specularOrLuminous * 0.92, clamp(uHighlightProtection, 0.0, 1.0));
            highlightGate *= mix(1.0, 1.0 - trueEdge * 0.25, clamp(uSkyBias, 0.0, 1.0));
            highlightGate *= mix(1.0, 1.0 - smoothGradient * 0.35, smoothProtect);

            float detailConfidence = mix(0.85, 1.15, clamp(uDetailWeight, 0.0, 1.0) * textureDetail * (1.0 - chromaArtifact));
            float delta = shadowCurve * maxShadowBoost * shadowGate * detailConfidence -
                highlightCurve * maxHighlightCompress * highlightGate;
            float autoEv = clamp(uBaseEv + delta, minAbsEv, maxAbsEv);
            float bestEv = autoEv;
            float highlightSafety = 1.0 - saturatedClipRisk;
            float shadowProtection = clamp(snrConfidence * (1.0 - blackRisk * 0.45), 0.0, 1.0);
            float confidence = clamp(mix(shadowGate, highlightGate, highlightCurve) * (1.0 - chromaArtifact * 0.50), 0.0, 1.0);
            float manualEv = mix(uMinEv, uMaxEv, shapedMask()) + uBaseEv;
            float ev = autoEv;
            if (uMode == 0) {
                ev = manualEv;
            } else if (uMode == 2) {
                ev = mix(autoEv, manualEv, clamp(uManualBlend, 0.0, 1.0));
            }
            ev = clamp(ev, minAbsEv, maxAbsEv);
            float evNorm = clamp((ev - (uMinEv + uBaseEv)) / evSpan, 0.0, 1.0);
            float sampleNorm = clamp((bestEv - (uMinEv + uBaseEv)) / evSpan, 0.0, 1.0);
            FragColor = vec4(evNorm, confidence, highlightSafety, mix(shadowProtection, sampleNorm, 0.45));
        }
    )";

    static const char* smoothFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uAnalysis;
        uniform sampler2D uMetrics;
        uniform sampler2D uInputImage;
        uniform int uRadius;
        uniform int uSmoothAreaRadius;
        uniform float uEdgeAwareness;
        uniform float uHaloGuard;
        uniform float uSmoothGradientProtection;
        uniform float uMaskDebandDither;
        uniform vec2 uTexelSize;

        float lumaAt(vec2 uv) {
            vec3 rgb = max(texture(uInputImage, uv).rgb, vec3(0.0));
            return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
        }

        float logLumaAt(vec2 uv) {
            return log2(max(lumaAt(uv), 0.00003));
        }

        void main() {
            vec4 center = texture(uAnalysis, vTexCoord);
            vec4 centerMetrics = texture(uMetrics, vTexCoord);
            int radius = clamp(uRadius, 0, 16);
            int smoothAreaRadius = clamp(uSmoothAreaRadius, 0, 32);
            float smoothGradient = clamp(centerMetrics.b, 0.0, 1.0);
            float smoothProtect = clamp(uSmoothGradientProtection, 0.0, 1.0);
            int effectiveRadius = max(radius, int(round(float(smoothAreaRadius) * smoothGradient * smoothProtect)));
            if (effectiveRadius <= 0) {
                FragColor = center;
                return;
            }
            float centerLum = lumaAt(vTexCoord);
            float centerLogLum = logLumaAt(vTexCoord);
            float edgeAware = clamp(uEdgeAwareness, 0.0, 1.0);
            float haloGuard = clamp(uHaloGuard, 0.0, 1.0);
            float localEdge = max(centerMetrics.r, clamp((abs(logLumaAt(vTexCoord + vec2(uTexelSize.x, 0.0)) - logLumaAt(vTexCoord - vec2(uTexelSize.x, 0.0))) +
                abs(logLumaAt(vTexCoord + vec2(0.0, uTexelSize.y)) - logLumaAt(vTexCoord - vec2(0.0, uTexelSize.y)))) * 0.55, 0.0, 1.0));
            float edgeScale = mix(2.2, 0.22, edgeAware);
            float linearEdgeScale = mix(0.55, 0.035, edgeAware);
            float haloScale = mix(3.0, 0.75, haloGuard);
            float smoothRadiusScale = mix(1.0, mix(1.0, 0.20, localEdge), haloGuard);
            smoothRadiusScale *= mix(1.0, 1.85, smoothGradient * smoothProtect);
            float sum = 0.0;
            float weightSum = 0.0;
            for (int y = -32; y <= 32; ++y) {
                for (int x = -32; x <= 32; ++x) {
                    if (abs(x) > effectiveRadius || abs(y) > effectiveRadius) continue;
                    vec2 uv = vTexCoord + vec2(x, y) * uTexelSize;
                    vec4 sampleMetrics = texture(uMetrics, uv);
                    float distance2 = float(x * x + y * y);
                    float spatial = exp(-distance2 / max(1.0, float(effectiveRadius * effectiveRadius) * smoothRadiusScale) * haloScale);
                    float logDiff = abs(logLumaAt(uv) - centerLogLum);
                    float linearDiff = abs(lumaAt(uv) - centerLum);
                    float sameSmoothRegion = 1.0 - abs(clamp(sampleMetrics.b, 0.0, 1.0) - smoothGradient);
                    float edgeStop = max(localEdge, clamp(sampleMetrics.r, 0.0, 1.0));
                    float rangeW = exp(-logDiff / max(0.0001, edgeScale)) *
                        exp(-linearDiff / max(0.0001, linearEdgeScale));
                    rangeW = mix(rangeW, max(rangeW, 0.35 + sameSmoothRegion * 0.45), smoothGradient * smoothProtect * (1.0 - edgeStop));
                    rangeW *= 1.0 - smoothstep(mix(3.0, 0.75, edgeAware), mix(5.0, 1.55, edgeAware), logDiff) * haloGuard;
                    rangeW *= 1.0 - edgeStop * haloGuard * 0.75;
                    float w = spatial * rangeW;
                    sum += texture(uAnalysis, uv).r * w;
                    weightSum += w;
                }
            }
            float smoothed = weightSum > 0.0 ? sum / weightSum : center.r;
            float preserve = smoothstep(0.12, 0.88, localEdge) * haloGuard;
            preserve *= 1.0 - smoothGradient * smoothProtect * 0.75;
            center.r = mix(smoothed, center.r, preserve);
            if (uMaskDebandDither > 0.0 && centerMetrics.a > 0.0) {
                float n = fract(sin(dot(vTexCoord * vec2(8192.0, 4096.0), vec2(12.9898, 78.233))) * 43758.5453);
                center.r = clamp(center.r + (n - 0.5) * uMaskDebandDither * centerMetrics.a * 0.006, 0.0, 1.0);
            }
            FragColor = center;
        }
    )";

    static const char* applyFragSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform sampler2D uExposureMap;
        uniform sampler2D uMetrics;
        uniform int uHasMask;
        uniform float uMinEv;
        uniform float uMaxEv;
        uniform float uBaseEv;
        uniform float uStrength;
        uniform float uEstimatedNoiseFloor;
        uniform float uChannelSaturationRisk;
        uniform int uDebugView;
        uniform int uMaskOutput;

        void main() {
            vec4 inputColor = texture(uInputImage, vTexCoord);
            vec4 map = texture(uExposureMap, vTexCoord);
            vec4 metrics = texture(uMetrics, vTexCoord);
            float ev = uHasMask != 0 ? mix(uMinEv + uBaseEv, uMaxEv + uBaseEv, clamp(map.r, 0.0, 1.0)) : 0.0;
            float gain = exp2(ev * clamp(uStrength, 0.0, 1.0));
            vec3 fused = max(inputColor.rgb, vec3(0.0)) * gain;
            if (uMaskOutput != 0 || uDebugView == 1) {
                FragColor = vec4(vec3(map.r), 1.0);
            } else if (uDebugView == 2) {
                FragColor = vec4(vec3(map.g), 1.0);
            } else if (uDebugView == 3) {
                FragColor = vec4(vec3(map.b), 1.0);
            } else if (uDebugView == 4) {
                FragColor = vec4(vec3(map.a), 1.0);
            } else if (uDebugView == 5) {
                float bands = floor(map.r * 8.999) / 8.0;
                FragColor = vec4(bands, 1.0 - bands, abs(0.5 - bands) * 2.0, 1.0);
            } else if (uDebugView == 6) {
                FragColor = vec4(vec3(metrics.b), 1.0);
            } else if (uDebugView == 7) {
                FragColor = vec4(vec3(metrics.r), 1.0);
            } else if (uDebugView == 8) {
                FragColor = vec4(vec3(metrics.g), 1.0);
            } else if (uDebugView == 9) {
                FragColor = vec4(vec3(metrics.a), 1.0);
            } else if (uDebugView == 10) {
                float rangePreview = clamp((uMaxEv - uMinEv) / 12.0, 0.0, 1.0);
                FragColor = vec4(map.r, rangePreview, clamp((uBaseEv + 4.0) / 8.0, 0.0, 1.0), 1.0);
            } else if (uDebugView == 11) {
                float lum = dot(max(inputColor.rgb, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722));
                float snr = smoothstep(uEstimatedNoiseFloor * 2.0, uEstimatedNoiseFloor * 18.0, lum);
                FragColor = vec4(vec3(snr * (1.0 - metrics.a * 0.45)), 1.0);
            } else if (uDebugView == 12) {
                FragColor = vec4(vec3(map.b), 1.0);
            } else if (uDebugView == 13) {
                float maxChannel = max(max(inputColor.r, inputColor.g), inputColor.b);
                float minChannel = min(min(inputColor.r, inputColor.g), inputColor.b);
                float sat = maxChannel > 0.00003 ? (maxChannel - minChannel) / maxChannel : 0.0;
                FragColor = vec4(vec3(clamp(max(sat, uChannelSaturationRisk) * smoothstep(0.25, 1.05, maxChannel), 0.0, 1.0)), 1.0);
            } else if (uDebugView == 14) {
                FragColor = vec4(vec3(clamp(metrics.a * (1.0 - metrics.g), 0.0, 1.0)), 1.0);
            } else {
                FragColor = vec4(fused, inputColor.a);
            }
        }
    )";

    if (!m_RawDetailFusionAnalysisProgram) {
        m_RawDetailFusionAnalysisProgram = GLHelpers::CreateShaderProgram(vertexSrc, analysisFragSrc);
    }
    if (!m_RawDetailFusionMetricsProgram) {
        m_RawDetailFusionMetricsProgram = GLHelpers::CreateShaderProgram(vertexSrc, metricsFragSrc);
    }
    if (!m_RawDetailFusionSmoothProgram) {
        m_RawDetailFusionSmoothProgram = GLHelpers::CreateShaderProgram(vertexSrc, smoothFragSrc);
    }
    if (!m_RawDetailFusionApplyProgram) {
        m_RawDetailFusionApplyProgram = GLHelpers::CreateShaderProgram(vertexSrc, applyFragSrc);
    }
}

void RenderPipeline::EnsureRawDevelopmentToneCurveProgram() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform int uToneCurvePointCount;
        uniform vec2 uToneCurvePoints[12];

        float applyToneCurveChannel(float value) {
            if (uToneCurvePointCount < 2 || value <= 0.0) {
                return value;
            }

            vec2 previous = uToneCurvePoints[0];
            for (int i = 1; i < 12; ++i) {
                if (i >= uToneCurvePointCount) {
                    break;
                }
                vec2 current = uToneCurvePoints[i];
                if (value <= current.x) {
                    float span = max(current.x - previous.x, 0.00001);
                    float t = clamp((value - previous.x) / span, 0.0, 1.0);
                    return mix(previous.y, current.y, t);
                }
                previous = current;
            }
            return value + (previous.y - previous.x);
        }

        void main() {
            vec4 color = texture(uInputImage, vTexCoord);
            vec3 rgb = vec3(
                applyToneCurveChannel(color.r),
                applyToneCurveChannel(color.g),
                applyToneCurveChannel(color.b));
            FragColor = vec4(rgb, color.a);
        }
    )";

    if (!m_RawDevelopmentToneCurveProgram) {
        m_RawDevelopmentToneCurveProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

unsigned int RenderPipeline::RenderRawDevelopmentToneCurve(
    unsigned int inputTexture,
    const std::vector<Raw::RawToneCurvePoint>& points) {
    if (!inputTexture || points.size() < 2) {
        return 0;
    }

    EnsureRawDevelopmentToneCurveProgram();
    if (!m_RawDevelopmentToneCurveProgram) {
        return 0;
    }

    unsigned int outputTexture = CreateGraphRenderTargetTexture();
    if (!outputTexture) {
        return 0;
    }

    const bool rendered = RenderIntoGraphTargetTexture(outputTexture, [&](unsigned int) {
        glUseProgram(m_RawDevelopmentToneCurveProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_RawDevelopmentToneCurveProgram, "uInputImage"), 0);

        const int count = std::min<int>(static_cast<int>(points.size()), 12);
        glUniform1i(glGetUniformLocation(m_RawDevelopmentToneCurveProgram, "uToneCurvePointCount"), count);
        for (int i = 0; i < count; ++i) {
            char uniformName[64];
            std::snprintf(uniformName, sizeof(uniformName), "uToneCurvePoints[%d]", i);
            glUniform2f(
                glGetUniformLocation(m_RawDevelopmentToneCurveProgram, uniformName),
                std::clamp(points[static_cast<std::size_t>(i)].input, 0.0f, 1.0f),
                std::clamp(points[static_cast<std::size_t>(i)].output, 0.0f, 1.0f));
        }

        m_Quad.Draw();
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
    });

    glActiveTexture(GL_TEXTURE0);
    if (!rendered) {
        glDeleteTextures(1, &outputTexture);
        return 0;
    }
    return outputTexture;
}

void RenderPipeline::EnsureRawDevelopmentLocalRangeProgram() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform int uLocalRangePointCount;
        uniform vec2 uLocalRangePoints[12];
        uniform float uStrength;
        uniform float uMiddleGrey;
        uniform float uSmoothness;
        uniform float uEdgeProtection;
        uniform float uDetailProtection;
        uniform float uHighlightProtection;
        uniform vec2 uTexelSize;
        uniform int uRegionMaskEnabled;
        uniform int uRegionMaskMode;
        uniform int uRegionMaskInvert;
        uniform vec2 uRegionMaskCenter;
        uniform float uRegionMaskAngleRadians;
        uniform float uRegionMaskSize;
        uniform float uRegionMaskFeather;
        uniform vec2 uRegionMaskEvRange;
        uniform float uImageAspect;
        uniform int uColorMaskEnabled;
        uniform vec3 uColorMaskTarget;
        uniform float uColorMaskHueWidth;
        uniform float uColorMaskFeather;
        uniform float uColorMaskMinChroma;

        float evaluateLocalRangeDelta(float sceneEv) {
            if (uLocalRangePointCount < 2 || uStrength <= 0.0001) {
                return 0.0;
            }

            vec2 previous = uLocalRangePoints[0];
            if (sceneEv <= previous.x) {
                return previous.y;
            }

            for (int i = 1; i < 12; ++i) {
                if (i >= uLocalRangePointCount) {
                    break;
                }
                vec2 current = uLocalRangePoints[i];
                if (sceneEv <= current.x) {
                    float span = max(current.x - previous.x, 0.0001);
                    float t = clamp((sceneEv - previous.x) / span, 0.0, 1.0);
                    return mix(previous.y, current.y, t);
                }
                previous = current;
            }
            return previous.y;
        }

        float lumaOf(vec3 rgb) {
            return max(dot(max(rgb, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722)), 0.000001);
        }

        float colorChroma(vec3 rgb) {
            vec3 positive = max(rgb, vec3(0.0));
            float maxChannel = max(max(positive.r, positive.g), positive.b);
            if (maxChannel <= 0.000001) {
                return 0.0;
            }
            float minChannel = min(min(positive.r, positive.g), positive.b);
            return clamp((maxChannel - minChannel) / maxChannel, 0.0, 1.0);
        }

        vec3 colorDirection(vec3 rgb) {
            vec3 positive = max(rgb, vec3(0.0));
            float len = length(positive);
            if (len <= 0.000001) {
                return vec3(0.57735026);
            }
            return positive / len;
        }

        float sceneEvAt(vec2 uv) {
            vec3 rgb = texture(uInputImage, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
            return log2(lumaOf(rgb) / max(uMiddleGrey, 0.000001));
        }

        float edgeAwareWeight(float evDifference) {
            float diff = abs(evDifference);
            float edgeProtection = clamp(uEdgeProtection, 0.0, 1.0);
            float detailProtection = clamp(uDetailProtection, 0.0, 1.0);
            float sigma = max(mix(2.40, 0.45, edgeProtection), 0.05);
            float rangeWeight = exp(-(diff * diff) / (2.0 * sigma * sigma));
            float textureLow = mix(0.12, 0.35, detailProtection);
            float textureHigh = mix(0.55, 1.25, detailProtection);
            float textureInclusion = 1.0 - smoothstep(textureLow, textureHigh, diff);
            rangeWeight = max(rangeWeight, textureInclusion * detailProtection);
            return mix(1.0, clamp(rangeWeight, 0.0, 1.0), edgeProtection);
        }

        void accumulateSceneEv(vec2 offset, float spatialWeight, float centerSceneEv, inout float weightedEv, inout float weightSum) {
            float sampleSceneEv = sceneEvAt(vTexCoord + offset);
            float weight = spatialWeight * edgeAwareWeight(sampleSceneEv - centerSceneEv);
            weightedEv += sampleSceneEv * weight;
            weightSum += weight;
        }

        float edgeAwareSceneEv(float centerSceneEv) {
            float smoothness = clamp(uSmoothness, 0.0, 1.0);
            if (smoothness <= 0.0001) {
                return centerSceneEv;
            }

            vec2 nearRadius = uTexelSize * mix(2.0, 18.0, smoothness);
            vec2 farRadius = uTexelSize * mix(5.0, 44.0, smoothness);
            float weightedEv = centerSceneEv;
            float weightSum = 1.0;

            accumulateSceneEv(vec2( nearRadius.x, 0.0), 1.00, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(-nearRadius.x, 0.0), 1.00, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(0.0,  nearRadius.y), 1.00, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(0.0, -nearRadius.y), 1.00, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2( nearRadius.x,  nearRadius.y), 0.70, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(-nearRadius.x,  nearRadius.y), 0.70, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2( nearRadius.x, -nearRadius.y), 0.70, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(-nearRadius.x, -nearRadius.y), 0.70, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2( farRadius.x, 0.0), 0.45, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(-farRadius.x, 0.0), 0.45, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(0.0,  farRadius.y), 0.45, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(0.0, -farRadius.y), 0.45, centerSceneEv, weightedEv, weightSum);

            float smoothedSceneEv = weightedEv / max(weightSum, 0.0001);
            return mix(centerSceneEv, smoothedSceneEv, smoothness);
        }

        float protectedLocalDeltaEv(float mapSceneEv) {
            float deltaEv = uStrength * evaluateLocalRangeDelta(mapSceneEv);
            if (deltaEv > 0.0) {
                float highlightEnd = max(uLocalRangePoints[max(uLocalRangePointCount - 1, 0)].x, 1.5001);
                float highlightZone = smoothstep(1.5, highlightEnd, mapSceneEv);
                deltaEv *= 1.0 - clamp(uHighlightProtection, 0.0, 1.0) * highlightZone * 0.85;
            }
            return deltaEv;
        }

        float regionMaskValue(float mapSceneEv) {
            if (uRegionMaskEnabled == 0 || uRegionMaskMode == 0) {
                return 1.0;
            }

            float mask = 1.0;
            if (uRegionMaskMode == 1) {
                vec2 direction = vec2(cos(uRegionMaskAngleRadians), sin(uRegionMaskAngleRadians));
                float projection = dot(vTexCoord - uRegionMaskCenter, direction);
                float softWidth = max(uRegionMaskSize * mix(0.08, 1.0, clamp(uRegionMaskFeather, 0.0, 1.0)), 0.001);
                mask = smoothstep(-softWidth, softWidth, projection);
            } else if (uRegionMaskMode == 2) {
                vec2 delta = vTexCoord - uRegionMaskCenter;
                delta.x *= max(uImageAspect, 0.001);
                float distanceFromCenter = length(delta);
                float feather = uRegionMaskSize * clamp(uRegionMaskFeather, 0.0, 1.0);
                float inner = max(uRegionMaskSize - feather, 0.0);
                float outer = uRegionMaskSize + feather;
                mask = 1.0 - smoothstep(inner, outer, distanceFromCenter);
            } else if (uRegionMaskMode == 3) {
                float featherEv = max(clamp(uRegionMaskFeather, 0.0, 1.0) * 4.0, 0.02);
                float lowMask = smoothstep(uRegionMaskEvRange.x - featherEv, uRegionMaskEvRange.x, mapSceneEv);
                float highMask = 1.0 - smoothstep(uRegionMaskEvRange.y, uRegionMaskEvRange.y + featherEv, mapSceneEv);
                mask = lowMask * highMask;
            }

            mask = clamp(mask, 0.0, 1.0);
            return uRegionMaskInvert != 0 ? 1.0 - mask : mask;
        }

        float colorMaskValue(vec3 rgb) {
            if (uColorMaskEnabled == 0) {
                return 1.0;
            }

            vec3 targetDirection = colorDirection(uColorMaskTarget);
            vec3 sampleDirection = colorDirection(rgb);
            float directionDistance = length(targetDirection - sampleDirection);
            float feather = max(clamp(uColorMaskFeather, 0.0, 1.0) * 0.65, 0.015);
            float hueMask = 1.0 - smoothstep(
                clamp(uColorMaskHueWidth, 0.02, 1.20),
                clamp(uColorMaskHueWidth, 0.02, 1.20) + feather,
                directionDistance);

            float targetChroma = colorChroma(uColorMaskTarget);
            float sampleChroma = colorChroma(rgb);
            float minChroma = clamp(uColorMaskMinChroma, 0.0, 1.0);
            float chromaMask = 1.0;
            if (targetChroma >= 0.08) {
                chromaMask = smoothstep(minChroma, min(minChroma + 0.12, 1.0), sampleChroma);
            } else {
                float neutralFeather = max(clamp(uColorMaskFeather, 0.0, 1.0) * 0.25, 0.04);
                chromaMask = 1.0 - smoothstep(minChroma, min(minChroma + neutralFeather, 1.0), sampleChroma);
            }
            return clamp(hueMask * chromaMask, 0.0, 1.0);
        }

        void main() {
            vec4 color = texture(uInputImage, vTexCoord);
            vec3 rgb = max(color.rgb, vec3(0.0));
            float luma = lumaOf(rgb);
            float sceneEv = log2(luma / max(uMiddleGrey, 0.000001));
            float mapSceneEv = edgeAwareSceneEv(sceneEv);
            float deltaEv = protectedLocalDeltaEv(mapSceneEv) * regionMaskValue(mapSceneEv) * colorMaskValue(rgb);
            float scale = exp2(deltaEv);
            FragColor = vec4(color.rgb * scale, color.a);
        }
    )";

    if (!m_RawDevelopmentLocalRangeProgram) {
        m_RawDevelopmentLocalRangeProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

void RenderPipeline::EnsureRawDevelopmentLocalRangeOverlayProgram() {
    static const char* vertexSrc = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 vTexCoord;
        void main() {
            vTexCoord = aTex;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    static const char* fragmentSrc = R"(
        #version 330 core
        in vec2 vTexCoord;
        out vec4 FragColor;
        uniform sampler2D uInputImage;
        uniform int uLocalRangePointCount;
        uniform vec2 uLocalRangePoints[12];
        uniform float uStrength;
        uniform float uMiddleGrey;
        uniform int uOverlayMode;
        uniform float uSmoothness;
        uniform float uEdgeProtection;
        uniform float uDetailProtection;
        uniform float uHighlightProtection;
        uniform vec2 uTexelSize;
        uniform int uRegionMaskEnabled;
        uniform int uRegionMaskMode;
        uniform int uRegionMaskInvert;
        uniform vec2 uRegionMaskCenter;
        uniform float uRegionMaskAngleRadians;
        uniform float uRegionMaskSize;
        uniform float uRegionMaskFeather;
        uniform vec2 uRegionMaskEvRange;
        uniform float uImageAspect;
        uniform int uColorMaskEnabled;
        uniform vec3 uColorMaskTarget;
        uniform float uColorMaskHueWidth;
        uniform float uColorMaskFeather;
        uniform float uColorMaskMinChroma;

        float evaluateLocalRangeDelta(float sceneEv) {
            if (uLocalRangePointCount < 2 || uStrength <= 0.0001) {
                return 0.0;
            }

            vec2 previous = uLocalRangePoints[0];
            if (sceneEv <= previous.x) {
                return previous.y;
            }

            for (int i = 1; i < 12; ++i) {
                if (i >= uLocalRangePointCount) {
                    break;
                }
                vec2 current = uLocalRangePoints[i];
                if (sceneEv <= current.x) {
                    float span = max(current.x - previous.x, 0.0001);
                    float t = clamp((sceneEv - previous.x) / span, 0.0, 1.0);
                    return mix(previous.y, current.y, t);
                }
                previous = current;
            }
            return previous.y;
        }

        float lumaOf(vec3 rgb) {
            return max(dot(max(rgb, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722)), 0.000001);
        }

        float colorChroma(vec3 rgb) {
            vec3 positive = max(rgb, vec3(0.0));
            float maxChannel = max(max(positive.r, positive.g), positive.b);
            if (maxChannel <= 0.000001) {
                return 0.0;
            }
            float minChannel = min(min(positive.r, positive.g), positive.b);
            return clamp((maxChannel - minChannel) / maxChannel, 0.0, 1.0);
        }

        vec3 colorDirection(vec3 rgb) {
            vec3 positive = max(rgb, vec3(0.0));
            float len = length(positive);
            if (len <= 0.000001) {
                return vec3(0.57735026);
            }
            return positive / len;
        }

        float sceneEvAt(vec2 uv) {
            vec3 rgb = texture(uInputImage, clamp(uv, vec2(0.0), vec2(1.0))).rgb;
            return log2(lumaOf(rgb) / max(uMiddleGrey, 0.000001));
        }

        float edgeAwareWeight(float evDifference) {
            float diff = abs(evDifference);
            float edgeProtection = clamp(uEdgeProtection, 0.0, 1.0);
            float detailProtection = clamp(uDetailProtection, 0.0, 1.0);
            float sigma = max(mix(2.40, 0.45, edgeProtection), 0.05);
            float rangeWeight = exp(-(diff * diff) / (2.0 * sigma * sigma));
            float textureLow = mix(0.12, 0.35, detailProtection);
            float textureHigh = mix(0.55, 1.25, detailProtection);
            float textureInclusion = 1.0 - smoothstep(textureLow, textureHigh, diff);
            rangeWeight = max(rangeWeight, textureInclusion * detailProtection);
            return mix(1.0, clamp(rangeWeight, 0.0, 1.0), edgeProtection);
        }

        void accumulateSceneEv(vec2 offset, float spatialWeight, float centerSceneEv, inout float weightedEv, inout float weightSum) {
            float sampleSceneEv = sceneEvAt(vTexCoord + offset);
            float weight = spatialWeight * edgeAwareWeight(sampleSceneEv - centerSceneEv);
            weightedEv += sampleSceneEv * weight;
            weightSum += weight;
        }

        float edgeAwareSceneEv(float centerSceneEv) {
            float smoothness = clamp(uSmoothness, 0.0, 1.0);
            if (smoothness <= 0.0001) {
                return centerSceneEv;
            }

            vec2 nearRadius = uTexelSize * mix(2.0, 18.0, smoothness);
            vec2 farRadius = uTexelSize * mix(5.0, 44.0, smoothness);
            float weightedEv = centerSceneEv;
            float weightSum = 1.0;

            accumulateSceneEv(vec2( nearRadius.x, 0.0), 1.00, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(-nearRadius.x, 0.0), 1.00, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(0.0,  nearRadius.y), 1.00, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(0.0, -nearRadius.y), 1.00, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2( nearRadius.x,  nearRadius.y), 0.70, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(-nearRadius.x,  nearRadius.y), 0.70, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2( nearRadius.x, -nearRadius.y), 0.70, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(-nearRadius.x, -nearRadius.y), 0.70, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2( farRadius.x, 0.0), 0.45, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(-farRadius.x, 0.0), 0.45, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(0.0,  farRadius.y), 0.45, centerSceneEv, weightedEv, weightSum);
            accumulateSceneEv(vec2(0.0, -farRadius.y), 0.45, centerSceneEv, weightedEv, weightSum);

            float smoothedSceneEv = weightedEv / max(weightSum, 0.0001);
            return mix(centerSceneEv, smoothedSceneEv, smoothness);
        }

        float protectedLocalDeltaEv(float mapSceneEv) {
            float deltaEv = uStrength * evaluateLocalRangeDelta(mapSceneEv);
            if (deltaEv > 0.0) {
                float highlightEnd = max(uLocalRangePoints[max(uLocalRangePointCount - 1, 0)].x, 1.5001);
                float highlightZone = smoothstep(1.5, highlightEnd, mapSceneEv);
                deltaEv *= 1.0 - clamp(uHighlightProtection, 0.0, 1.0) * highlightZone * 0.85;
            }
            return deltaEv;
        }

        float regionMaskValue(float mapSceneEv) {
            if (uRegionMaskEnabled == 0 || uRegionMaskMode == 0) {
                return 1.0;
            }

            float mask = 1.0;
            if (uRegionMaskMode == 1) {
                vec2 direction = vec2(cos(uRegionMaskAngleRadians), sin(uRegionMaskAngleRadians));
                float projection = dot(vTexCoord - uRegionMaskCenter, direction);
                float softWidth = max(uRegionMaskSize * mix(0.08, 1.0, clamp(uRegionMaskFeather, 0.0, 1.0)), 0.001);
                mask = smoothstep(-softWidth, softWidth, projection);
            } else if (uRegionMaskMode == 2) {
                vec2 delta = vTexCoord - uRegionMaskCenter;
                delta.x *= max(uImageAspect, 0.001);
                float distanceFromCenter = length(delta);
                float feather = uRegionMaskSize * clamp(uRegionMaskFeather, 0.0, 1.0);
                float inner = max(uRegionMaskSize - feather, 0.0);
                float outer = uRegionMaskSize + feather;
                mask = 1.0 - smoothstep(inner, outer, distanceFromCenter);
            } else if (uRegionMaskMode == 3) {
                float featherEv = max(clamp(uRegionMaskFeather, 0.0, 1.0) * 4.0, 0.02);
                float lowMask = smoothstep(uRegionMaskEvRange.x - featherEv, uRegionMaskEvRange.x, mapSceneEv);
                float highMask = 1.0 - smoothstep(uRegionMaskEvRange.y, uRegionMaskEvRange.y + featherEv, mapSceneEv);
                mask = lowMask * highMask;
            }

            mask = clamp(mask, 0.0, 1.0);
            return uRegionMaskInvert != 0 ? 1.0 - mask : mask;
        }

        float colorMaskValue(vec3 rgb) {
            if (uColorMaskEnabled == 0) {
                return 1.0;
            }

            vec3 targetDirection = colorDirection(uColorMaskTarget);
            vec3 sampleDirection = colorDirection(rgb);
            float directionDistance = length(targetDirection - sampleDirection);
            float feather = max(clamp(uColorMaskFeather, 0.0, 1.0) * 0.65, 0.015);
            float hueMask = 1.0 - smoothstep(
                clamp(uColorMaskHueWidth, 0.02, 1.20),
                clamp(uColorMaskHueWidth, 0.02, 1.20) + feather,
                directionDistance);

            float targetChroma = colorChroma(uColorMaskTarget);
            float sampleChroma = colorChroma(rgb);
            float minChroma = clamp(uColorMaskMinChroma, 0.0, 1.0);
            float chromaMask = 1.0;
            if (targetChroma >= 0.08) {
                chromaMask = smoothstep(minChroma, min(minChroma + 0.12, 1.0), sampleChroma);
            } else {
                float neutralFeather = max(clamp(uColorMaskFeather, 0.0, 1.0) * 0.25, 0.04);
                chromaMask = 1.0 - smoothstep(minChroma, min(minChroma + neutralFeather, 1.0), sampleChroma);
            }
            return clamp(hueMask * chromaMask, 0.0, 1.0);
        }

        void main() {
            vec4 color = texture(uInputImage, vTexCoord);
            vec3 rgb = max(color.rgb, vec3(0.0));
            float luma = lumaOf(rgb);
            float sceneEv = log2(luma / max(uMiddleGrey, 0.000001));
            float mapSceneEv = edgeAwareSceneEv(sceneEv);
            float regionMask = regionMaskValue(mapSceneEv);
            float colorMask = colorMaskValue(rgb);
            float qualificationMask = regionMask * colorMask;

            if (uOverlayMode == 3) {
                if ((uRegionMaskEnabled == 0 || uRegionMaskMode == 0) && uColorMaskEnabled == 0) {
                    FragColor = vec4(0.0);
                    return;
                }
                vec3 offColor = vec3(0.02, 0.08, 0.10);
                vec3 onColor = vec3(0.04, 0.86, 0.72);
                float alpha = 0.18 + smoothstep(0.02, 0.85, qualificationMask) * 0.48;
                FragColor = vec4(mix(offColor, onColor, qualificationMask), alpha);
                return;
            }

            float deltaEv = protectedLocalDeltaEv(mapSceneEv) * qualificationMask;
            float magnitude = clamp(abs(deltaEv) / 2.0, 0.0, 1.0);

            if (magnitude <= 0.0001) {
                FragColor = vec4(0.0);
                return;
            }

            vec3 liftColor = vec3(0.04, 0.82, 0.82);
            vec3 compressColor = vec3(1.0, 0.24, 0.48);
            vec3 neutralColor = vec3(0.92, 0.96, 1.0);

            float alpha = smoothstep(0.02, 0.45, magnitude);
            vec3 overlayColor = deltaEv >= 0.0 ? liftColor : compressColor;
            if (uOverlayMode == 1) {
                overlayColor = mix(neutralColor, overlayColor, 0.35);
                alpha *= 0.42;
            } else {
                overlayColor = mix(neutralColor, overlayColor, clamp(magnitude * 1.4, 0.0, 1.0));
                alpha *= 0.72;
            }

            FragColor = vec4(overlayColor, alpha);
        }
    )";

    if (!m_RawDevelopmentLocalRangeOverlayProgram) {
        m_RawDevelopmentLocalRangeOverlayProgram = GLHelpers::CreateShaderProgram(vertexSrc, fragmentSrc);
    }
}

unsigned int RenderPipeline::RenderRawDevelopmentLocalRange(
    unsigned int inputTexture,
    const Stack::RawRecipe::RawLocalRangeRecipe& localRange) {
    const Stack::RawRecipe::RawLocalRangeRecipe sanitized =
        Stack::RawRecipe::SanitizeLocalRangeRecipe(localRange);
    if (!inputTexture || !Stack::RawRecipe::IsLocalRangeEnabled(sanitized) || sanitized.points.size() < 2) {
        return 0;
    }

    EnsureRawDevelopmentLocalRangeProgram();
    if (!m_RawDevelopmentLocalRangeProgram) {
        return 0;
    }

    unsigned int outputTexture = CreateGraphRenderTargetTexture();
    if (!outputTexture) {
        return 0;
    }

    const bool rendered = RenderIntoGraphTargetTexture(outputTexture, [&](unsigned int) {
        glUseProgram(m_RawDevelopmentLocalRangeProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uInputImage"), 0);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uStrength"), sanitized.strength);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uMiddleGrey"), sanitized.middleGrey);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uSmoothness"), sanitized.smoothness);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uEdgeProtection"), sanitized.edgeProtection);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uDetailProtection"), sanitized.detailProtection);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uHighlightProtection"), sanitized.highlightProtection);
        glUniform2f(
            glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uTexelSize"),
            m_Width > 0 ? 1.0f / static_cast<float>(m_Width) : 0.0f,
            m_Height > 0 ? 1.0f / static_cast<float>(m_Height) : 0.0f);
        UploadRawLocalRangeRegionMaskUniforms(
            m_RawDevelopmentLocalRangeProgram,
            sanitized,
            m_Width,
            m_Height);

        const int count = std::min<int>(static_cast<int>(sanitized.points.size()), 12);
        glUniform1i(glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, "uLocalRangePointCount"), count);
        for (int i = 0; i < count; ++i) {
            char uniformName[64];
            std::snprintf(uniformName, sizeof(uniformName), "uLocalRangePoints[%d]", i);
            const Stack::RawRecipe::RawLocalRangePoint& point = sanitized.points[static_cast<std::size_t>(i)];
            glUniform2f(
                glGetUniformLocation(m_RawDevelopmentLocalRangeProgram, uniformName),
                point.ev,
                point.deltaEv);
        }

        m_Quad.Draw();
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
    });

    glActiveTexture(GL_TEXTURE0);
    if (!rendered) {
        glDeleteTextures(1, &outputTexture);
        return 0;
    }
    return outputTexture;
}

unsigned int RenderPipeline::RenderRawDevelopmentLocalRangeOverlay(
    unsigned int inputTexture,
    const Stack::RawRecipe::RawLocalRangeRecipe& localRange,
    const std::string& overlayMode) {
    const int mode = overlayMode == "affected-tones"
        ? 1
        : (overlayMode == "delta-map" ? 2 : (overlayMode == "region-mask" ? 3 : 0));
    const Stack::RawRecipe::RawLocalRangeRecipe sanitized =
        Stack::RawRecipe::SanitizeLocalRangeRecipe(localRange);
    const bool localRangeActive =
        Stack::RawRecipe::IsLocalRangeEnabled(sanitized) && sanitized.points.size() >= 2;
    const bool maskOverlayActive = mode == 3 && (sanitized.regionMaskEnabled || sanitized.colorMaskEnabled);
    if (!inputTexture ||
        mode == 0 ||
        (!localRangeActive && !maskOverlayActive)) {
        return 0;
    }

    EnsureRawDevelopmentLocalRangeOverlayProgram();
    if (!m_RawDevelopmentLocalRangeOverlayProgram) {
        return 0;
    }

    unsigned int outputTexture = CreateGraphRenderTargetTexture();
    if (!outputTexture) {
        return 0;
    }

    const bool rendered = RenderIntoGraphTargetTexture(outputTexture, [&](unsigned int) {
        glUseProgram(m_RawDevelopmentLocalRangeOverlayProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, inputTexture);
        glUniform1i(glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uInputImage"), 0);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uStrength"), sanitized.strength);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uMiddleGrey"), sanitized.middleGrey);
        glUniform1i(glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uOverlayMode"), mode);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uSmoothness"), sanitized.smoothness);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uEdgeProtection"), sanitized.edgeProtection);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uDetailProtection"), sanitized.detailProtection);
        glUniform1f(glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uHighlightProtection"), sanitized.highlightProtection);
        glUniform2f(
            glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uTexelSize"),
            m_Width > 0 ? 1.0f / static_cast<float>(m_Width) : 0.0f,
            m_Height > 0 ? 1.0f / static_cast<float>(m_Height) : 0.0f);
        UploadRawLocalRangeRegionMaskUniforms(
            m_RawDevelopmentLocalRangeOverlayProgram,
            sanitized,
            m_Width,
            m_Height);

        const int count = std::min<int>(static_cast<int>(sanitized.points.size()), 12);
        glUniform1i(glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, "uLocalRangePointCount"), count);
        for (int i = 0; i < count; ++i) {
            char uniformName[64];
            std::snprintf(uniformName, sizeof(uniformName), "uLocalRangePoints[%d]", i);
            const Stack::RawRecipe::RawLocalRangePoint& point = sanitized.points[static_cast<std::size_t>(i)];
            glUniform2f(
                glGetUniformLocation(m_RawDevelopmentLocalRangeOverlayProgram, uniformName),
                point.ev,
                point.deltaEv);
        }

        m_Quad.Draw();
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
    });

    glActiveTexture(GL_TEXTURE0);
    if (!rendered) {
        glDeleteTextures(1, &outputTexture);
        return 0;
    }
    return outputTexture;
}
