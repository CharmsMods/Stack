#include "Composite/CompositeModule.h"
#include "Composite/Internal/CompositeModuleInternal.h"

#include "Editor/EditorModule.h"
#include "Library/LibraryManager.h"
#include "Renderer/GLHelpers.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <unordered_set>

namespace {

using json = StackBinaryFormat::json;

constexpr float kCanvasContextDragThreshold = 6.0f;
constexpr float kExportBoundsHandleScreenRadius = 7.0f;
constexpr float kLayerHandleScreenRadius = 5.0f;
constexpr float kSnapThresholdScreenPixels = 10.0f;
constexpr const char* kStagePreviewVertexShader = R"GLSL(
#version 430 core
layout(location = 0) out vec2 vUv;
void main() {
    const vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    const vec2 pos = positions[gl_VertexID];
    gl_Position = vec4(pos, 0.0, 1.0);
    vUv = pos * 0.5 + 0.5;
}
)GLSL";
constexpr const char* kStagePreviewFragmentShader = R"GLSL(
#version 430 core
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

uniform sampler2D uPrevTex;
uniform sampler2D uLayerTex;
uniform vec4 uWorldRect;
uniform mat4 uWorldToLayer;
uniform vec2 uLayerSize;
uniform float uLayerOpacity;
uniform int uFlipX;
uniform int uFlipY;
uniform int uBlendMode;

float clamp01(const float value) {
    return clamp(value, 0.0, 1.0);
}

float blendOverlay(const float src, const float dst) {
    return (dst <= 0.5) ? (2.0 * src * dst) : (1.0 - 2.0 * (1.0 - src) * (1.0 - dst));
}

float blendHardLight(const float src, const float dst) {
    return (src <= 0.5) ? (2.0 * src * dst) : (1.0 - 2.0 * (1.0 - src) * (1.0 - dst));
}

float softLightCurve(const float value) {
    if (value <= 0.25) {
        return ((16.0 * value - 12.0) * value + 4.0) * value;
    }
    return sqrt(value);
}

float blendSoftLight(const float src, const float dst) {
    if (src <= 0.5) {
        return dst - (1.0 - 2.0 * src) * dst * (1.0 - dst);
    }
    return dst + (2.0 * src - 1.0) * (softLightCurve(dst) - dst);
}

void rgbToHsl(const vec3 rgb, out float hue, out float saturation, out float lightness) {
    const float r = clamp01(rgb.r);
    const float g = clamp01(rgb.g);
    const float b = clamp01(rgb.b);
    const float maxValue = max(max(r, g), b);
    const float minValue = min(min(r, g), b);
    const float delta = maxValue - minValue;

    lightness = (maxValue + minValue) * 0.5;
    if (delta <= 1e-6) {
        hue = 0.0;
        saturation = 0.0;
        return;
    }

    saturation = (lightness > 0.5)
        ? (delta / (2.0 - maxValue - minValue))
        : (delta / (maxValue + minValue));

    if (maxValue == r) {
        hue = (g - b) / delta + (g < b ? 6.0 : 0.0);
    } else if (maxValue == g) {
        hue = (b - r) / delta + 2.0;
    } else {
        hue = (r - g) / delta + 4.0;
    }

    hue /= 6.0;
}

float hueToRgb(const float p, const float q, float t) {
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0 / 2.0) return q;
    if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    return p;
}

vec3 hslToRgb(const float hue, const float saturation, const float lightness) {
    if (saturation <= 1e-6) {
        return vec3(lightness);
    }

    const float q = (lightness < 0.5)
        ? (lightness * (1.0 + saturation))
        : (lightness + saturation - lightness * saturation);
    const float p = 2.0 * lightness - q;

    return vec3(
        hueToRgb(p, q, hue + 1.0 / 3.0),
        hueToRgb(p, q, hue),
        hueToRgb(p, q, hue - 1.0 / 3.0));
}

vec3 blendRgb(const int mode, const vec3 srcRgb, const vec3 dstRgb) {
    switch (mode) {
    case 1:
        return srcRgb * dstRgb;
    case 2:
        return 1.0 - (1.0 - srcRgb) * (1.0 - dstRgb);
    case 3:
        return min(vec3(1.0), srcRgb + dstRgb);
    case 4:
        return vec3(
            blendOverlay(srcRgb.r, dstRgb.r),
            blendOverlay(srcRgb.g, dstRgb.g),
            blendOverlay(srcRgb.b, dstRgb.b));
    case 5:
        return vec3(
            blendSoftLight(srcRgb.r, dstRgb.r),
            blendSoftLight(srcRgb.g, dstRgb.g),
            blendSoftLight(srcRgb.b, dstRgb.b));
    case 6:
        return vec3(
            blendHardLight(srcRgb.r, dstRgb.r),
            blendHardLight(srcRgb.g, dstRgb.g),
            blendHardLight(srcRgb.b, dstRgb.b));
    case 7:
    case 8: {
        float srcHue = 0.0;
        float srcSat = 0.0;
        float srcLight = 0.0;
        float dstHue = 0.0;
        float dstSat = 0.0;
        float dstLight = 0.0;
        rgbToHsl(srcRgb, srcHue, srcSat, srcLight);
        rgbToHsl(dstRgb, dstHue, dstSat, dstLight);
        const float hue = srcHue;
        const float saturation = (mode == 8) ? srcSat : dstSat;
        return hslToRgb(hue, saturation, dstLight);
    }
    case 0:
    default:
        return srcRgb;
    }
}

vec4 compositeSourceOver(const vec4 dst, const vec4 src, const int mode) {
    const float srcAlpha = clamp01(src.a);
    if (srcAlpha <= 1e-6) {
        return dst;
    }

    const float dstAlpha = clamp01(dst.a);
    const vec3 srcRgb = clamp(src.rgb, 0.0, 1.0);
    const vec3 dstRgb = clamp(dst.rgb, 0.0, 1.0);
    const vec3 blendedRgb = blendRgb(mode, srcRgb, dstRgb);
    const float outAlpha = srcAlpha + dstAlpha * (1.0 - srcAlpha);
    if (outAlpha <= 1e-6) {
        return vec4(0.0);
    }

    const vec3 outRgb = (blendedRgb * srcAlpha + dstRgb * dstAlpha * (1.0 - srcAlpha)) / outAlpha;
    return vec4(clamp(outRgb, 0.0, 1.0), clamp01(outAlpha));
}

vec4 sampleLayer(const vec2 worldPoint) {
    const vec4 localPoint = uWorldToLayer * vec4(worldPoint, 0.0, 1.0);
    const float width = max(uLayerSize.x, 0.0001);
    const float height = max(uLayerSize.y, 0.0001);

    if (localPoint.x < 0.0 || localPoint.x > width || localPoint.y < 0.0 || localPoint.y > height) {
        return vec4(0.0);
    }

    float u = clamp(localPoint.x / width, 0.0, 1.0);
    float v = clamp(localPoint.y / height, 0.0, 1.0);
    if (uFlipX != 0) {
        u = 1.0 - u;
    }
    if (uFlipY != 0) {
        v = 1.0 - v;
    }

    vec4 sampled = texture(uLayerTex, vec2(u, 1.0 - v));
    sampled.a *= clamp01(uLayerOpacity);
    return sampled;
}

void main() {
    const vec4 dst = texture(uPrevTex, vUv);
    const vec2 worldPoint = vec2(
        uWorldRect.x + vUv.x * uWorldRect.z,
        uWorldRect.y + (1.0 - vUv.y) * uWorldRect.w);
    const vec4 src = sampleLayer(worldPoint);
    fragColor = compositeSourceOver(dst, src, uBlendMode);
}
)GLSL";

std::string TrimLeadingWhitespace(const std::string& value) {
    std::size_t index = 0;
    while (index < value.size() && std::isspace(static_cast<unsigned char>(value[index])) != 0) {
        ++index;
    }
    return value.substr(index);
}

bool StartsWith(const std::string& value, const char* prefix) {
    return value.rfind(prefix, 0) == 0;
}

bool IsCompositeDockWindowName(const std::string& name) {
    return name == kCompositeLayersWindowName ||
           name == kCompositeSelectedWindowName ||
           name == kCompositeViewWindowName ||
           name == kCompositeExportWindowName ||
           name == kCompositeCanvasWindowName;
}

bool ParseDockSettingsLine(const std::string& rawLine, ImGuiID& outId, ImGuiID& outParentId) {
    const std::string line = TrimLeadingWhitespace(rawLine);
    const char* cursor = line.c_str();
    if (StartsWith(line, "DockNode")) {
        cursor += 8;
    } else if (StartsWith(line, "DockSpace")) {
        cursor += 9;
    } else {
        return false;
    }

    while (*cursor == ' ') {
        ++cursor;
    }

    unsigned int id = 0;
    unsigned int parentId = 0;
    int read = 0;
    if (std::sscanf(cursor, "ID=0x%08X%n", &id, &read) != 1) {
        return false;
    }
    cursor += read;

    if (std::sscanf(cursor, " Parent=0x%08X%n", &parentId, &read) == 1) {
        outParentId = parentId;
    } else {
        outParentId = 0;
    }

    outId = id;
    return true;
}

std::string CaptureCompositeWorkspaceIni(const ImGuiID dockspaceId) {
    size_t iniSize = 0;
    const char* iniData = ImGui::SaveIniSettingsToMemory(&iniSize);
    if (iniData == nullptr || iniSize == 0) {
        return {};
    }

    std::istringstream stream(std::string(iniData, iniSize));
    std::vector<std::string> windowSections;

    struct DockEntry {
        std::string line;
        ImGuiID id = 0;
        ImGuiID parentId = 0;
        bool parsed = false;
    };
    std::vector<DockEntry> dockingEntries;

    bool inWindowSection = false;
    bool keepWindowSection = false;
    bool inDockingSection = false;
    std::string currentWindowSection;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (!line.empty() && line.front() == '[') {
            if (inWindowSection && keepWindowSection && !currentWindowSection.empty()) {
                windowSections.push_back(currentWindowSection);
            }

            inWindowSection = false;
            keepWindowSection = false;
            inDockingSection = false;
            currentWindowSection.clear();

            if (StartsWith(line, "[Window][")) {
                const std::string windowName = line.substr(9, line.size() - 10);
                inWindowSection = true;
                keepWindowSection = IsCompositeDockWindowName(windowName);
                if (keepWindowSection) {
                    currentWindowSection = line + "\n";
                }
                continue;
            }

            if (line == "[Docking][Data]") {
                inDockingSection = true;
                continue;
            }
        }

        if (inWindowSection) {
            if (keepWindowSection) {
                currentWindowSection += line + "\n";
            }
            continue;
        }

        if (inDockingSection) {
            if (line.empty()) {
                continue;
            }
            DockEntry entry;
            entry.line = line;
            entry.parsed = ParseDockSettingsLine(line, entry.id, entry.parentId);
            dockingEntries.push_back(std::move(entry));
        }
    }

    if (inWindowSection && keepWindowSection && !currentWindowSection.empty()) {
        windowSections.push_back(currentWindowSection);
    }

    std::unordered_set<ImGuiID> keepDockNodeIds;
    if (dockspaceId != 0) {
        keepDockNodeIds.insert(dockspaceId);
        bool added = true;
        while (added) {
            added = false;
            for (const DockEntry& entry : dockingEntries) {
                if (!entry.parsed || keepDockNodeIds.find(entry.id) != keepDockNodeIds.end()) {
                    continue;
                }
                if (entry.parentId != 0 && keepDockNodeIds.find(entry.parentId) != keepDockNodeIds.end()) {
                    keepDockNodeIds.insert(entry.id);
                    added = true;
                }
            }
        }
    }

    std::string filtered;
    for (const std::string& section : windowSections) {
        filtered += section;
        filtered += "\n";
    }

    bool wroteDockingHeader = false;
    for (const DockEntry& entry : dockingEntries) {
        if (dockspaceId != 0 && (!entry.parsed || keepDockNodeIds.find(entry.id) == keepDockNodeIds.end())) {
            continue;
        }
        if (!wroteDockingHeader) {
            filtered += "[Docking][Data]\n";
            wroteDockingHeader = true;
        }
        filtered += entry.line;
        filtered += "\n";
    }
    if (wroteDockingHeader) {
        filtered += "\n";
    }

    return filtered;
}

} // namespace

CompositeModule::CompositeModule() = default;
CompositeModule::~CompositeModule() { Shutdown(); }

void CompositeModule::Initialize() {
    if (m_Initialized) {
        return;
    }

    m_Initialized = true;
    m_StagePreviewDirty = true;
    m_CanvasFocused = false;
}

void CompositeModule::Shutdown() {
    ClearLayersGpu();
    ClearStagePreviewGpu();
    if (m_StagePreviewProgram != 0) {
        glDeleteProgram(m_StagePreviewProgram);
        m_StagePreviewProgram = 0;
    }
    if (m_StagePreviewVao != 0) {
        glDeleteVertexArrays(1, &m_StagePreviewVao);
        m_StagePreviewVao = 0;
    }
    m_Layers.clear();
    m_Initialized = false;
}

void CompositeModule::MarkDocumentDirty() {
    m_Dirty = true;
    m_StagePreviewDirty = true;
}

void CompositeModule::MarkStageDirty() {
    m_StagePreviewDirty = true;
}

CompositeSnapModePreset CompositeModule::GetSnapModePreset() const {
    if (!m_SnapEnabled) {
        return CompositeSnapModePreset::Off;
    }

    const bool stepSnapsDisabled =
        m_GridSize <= 0.0f &&
        m_RotateSnapStep <= 0.0f &&
        m_ScaleSnapStep <= 0.0f;
    if (m_SnapToObjects &&
        m_SnapToCenters &&
        m_SnapToCanvasCenter &&
        !m_SnapToExportBounds &&
        !m_SnapToSpacing &&
        stepSnapsDisabled) {
        return CompositeSnapModePreset::ObjectOnly;
    }

    if (m_SnapToObjects &&
        m_SnapToCenters &&
        m_SnapToCanvasCenter &&
        m_SnapToExportBounds &&
        m_SnapToSpacing &&
        m_GridSize > 0.0f &&
        m_RotateSnapStep > 0.0f &&
        m_ScaleSnapStep > 0.0f) {
        return CompositeSnapModePreset::Full;
    }

    return CompositeSnapModePreset::Custom;
}

void CompositeModule::RememberSnapStepDefaults() {
    if (m_GridSize > 0.0f) {
        m_LastNonZeroGridSize = m_GridSize;
    }
    if (m_RotateSnapStep > 0.0f) {
        m_LastNonZeroRotateSnapStep = m_RotateSnapStep;
    }
    if (m_ScaleSnapStep > 0.0f) {
        m_LastNonZeroScaleSnapStep = m_ScaleSnapStep;
    }
}

void CompositeModule::ApplySnapModePreset(const CompositeSnapModePreset preset) {
    if (preset == CompositeSnapModePreset::Custom) {
        return;
    }

    RememberSnapStepDefaults();
    switch (preset) {
    case CompositeSnapModePreset::Full:
        m_SnapEnabled = true;
        m_SnapToObjects = true;
        m_SnapToCenters = true;
        m_SnapToCanvasCenter = true;
        m_SnapToExportBounds = true;
        m_SnapToSpacing = true;
        m_GridSize = (m_LastNonZeroGridSize > 0.0f) ? m_LastNonZeroGridSize : 24.0f;
        m_RotateSnapStep = (m_LastNonZeroRotateSnapStep > 0.0f) ? m_LastNonZeroRotateSnapStep : 15.0f;
        m_ScaleSnapStep = (m_LastNonZeroScaleSnapStep > 0.0f) ? m_LastNonZeroScaleSnapStep : 0.1f;
        break;
    case CompositeSnapModePreset::ObjectOnly:
        m_SnapEnabled = true;
        m_SnapToObjects = true;
        m_SnapToCenters = true;
        m_SnapToCanvasCenter = true;
        m_SnapToExportBounds = false;
        m_SnapToSpacing = false;
        m_GridSize = 0.0f;
        m_RotateSnapStep = 0.0f;
        m_ScaleSnapStep = 0.0f;
        break;
    case CompositeSnapModePreset::Off:
        m_SnapEnabled = false;
        break;
    case CompositeSnapModePreset::Custom:
        break;
    }
}

void CompositeModule::ClearLayersGpu() {
    for (CompositeLayer& layer : m_Layers) {
        if (layer.tex != 0) {
            glDeleteTextures(1, &layer.tex);
            layer.tex = 0;
        }
    }
}

void CompositeModule::ClearStagePreviewGpu() {
    for (int index = 0; index < 2; ++index) {
        if (m_StagePreviewFbo[index] != 0) {
            glDeleteFramebuffers(1, &m_StagePreviewFbo[index]);
            m_StagePreviewFbo[index] = 0;
        }
        if (m_StagePreviewTex[index] != 0) {
            glDeleteTextures(1, &m_StagePreviewTex[index]);
            m_StagePreviewTex[index] = 0;
        }
    }

    m_StagePreviewDisplayIndex = 0;
    m_StagePreviewTexW = 0;
    m_StagePreviewTexH = 0;
}

void CompositeModule::SyncLayerTextures() {
    for (CompositeLayer& layer : m_Layers) {
        if (layer.tex != 0 || layer.rgba.empty() || layer.imgW <= 0 || layer.imgH <= 0) {
            continue;
        }

        layer.tex = GLHelpers::CreateTextureFromPixels(layer.rgba.data(), layer.imgW, layer.imgH, 4);
    }
}

bool CompositeModule::EnsureStageCompositeProgram() {
    if (m_StagePreviewProgram != 0) {
        return true;
    }

    m_StagePreviewProgram = GLHelpers::CreateShaderProgram(kStagePreviewVertexShader, kStagePreviewFragmentShader);
    if (m_StagePreviewProgram == 0) {
        return false;
    }

    glUseProgram(m_StagePreviewProgram);
    glUniform1i(glGetUniformLocation(m_StagePreviewProgram, "uPrevTex"), 0);
    glUniform1i(glGetUniformLocation(m_StagePreviewProgram, "uLayerTex"), 1);
    glUseProgram(0);

    if (m_StagePreviewVao == 0) {
        glGenVertexArrays(1, &m_StagePreviewVao);
    }

    return m_StagePreviewVao != 0;
}

bool CompositeModule::EnsureStagePreviewTargets(const int width, const int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (m_StagePreviewTexW == width &&
        m_StagePreviewTexH == height &&
        m_StagePreviewTex[0] != 0 &&
        m_StagePreviewTex[1] != 0 &&
        m_StagePreviewFbo[0] != 0 &&
        m_StagePreviewFbo[1] != 0) {
        return true;
    }

    ClearStagePreviewGpu();

    for (int index = 0; index < 2; ++index) {
        m_StagePreviewTex[index] = GLHelpers::CreateEmptyTexture(width, height);
        if (m_StagePreviewTex[index] == 0) {
            ClearStagePreviewGpu();
            return false;
        }

        m_StagePreviewFbo[index] = GLHelpers::CreateFBO(m_StagePreviewTex[index]);
        if (m_StagePreviewFbo[index] == 0) {
            ClearStagePreviewGpu();
            return false;
        }
    }

    m_StagePreviewTexW = width;
    m_StagePreviewTexH = height;
    m_StagePreviewDisplayIndex = 0;
    return true;
}

void CompositeModule::RenderStagePreviewTexture(
    const std::vector<CompositeLayer>& allLayers,
    const std::vector<const CompositeLayer*>& layers,
    const float viewX,
    const float viewY,
    const float viewWidth,
    const float viewHeight,
    const int width,
    const int height) {

    if (!EnsureStageCompositeProgram() || !EnsureStagePreviewTargets(width, height)) {
        return;
    }

    GLint previousFramebuffer = 0;
    GLint previousViewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);

    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glBindVertexArray(m_StagePreviewVao);
    glUseProgram(m_StagePreviewProgram);
    glViewport(0, 0, width, height);

    glBindFramebuffer(GL_FRAMEBUFFER, m_StagePreviewFbo[0]);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    int srcIndex = 0;
    int dstIndex = 1;
    for (const CompositeLayer* layer : layers) {
        if (!layer || !layer->visible || layer->tex == 0) {
            continue;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, m_StagePreviewFbo[dstIndex]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_StagePreviewTex[srcIndex]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, layer->tex);

        glUniform4f(glGetUniformLocation(m_StagePreviewProgram, "uWorldRect"), viewX, viewY, viewWidth, viewHeight);
        const AffineTransform2D worldTransform = BuildWorldTransform(allLayers, *layer);
        const AffineTransform2D inverseWorldTransform = Inverse(worldTransform);
        const std::array<float, 16> glMatrix = AffineToGlMatrix4(inverseWorldTransform);

        glUniformMatrix4fv(glGetUniformLocation(m_StagePreviewProgram, "uWorldToLayer"), 1, GL_FALSE, glMatrix.data());
        glUniform2f(
            glGetUniformLocation(m_StagePreviewProgram, "uLayerSize"),
            std::max(1.0f, LayerBaseWidth(*layer)),
            std::max(1.0f, LayerBaseHeight(*layer)));
        glUniform1f(glGetUniformLocation(m_StagePreviewProgram, "uLayerOpacity"), Clamp01(layer->opacity));
        glUniform1i(glGetUniformLocation(m_StagePreviewProgram, "uFlipX"), layer->flipX ? 1 : 0);
        glUniform1i(glGetUniformLocation(m_StagePreviewProgram, "uFlipY"), layer->flipY ? 1 : 0);
        glUniform1i(glGetUniformLocation(m_StagePreviewProgram, "uBlendMode"), static_cast<int>(layer->blendMode));
        glDrawArrays(GL_TRIANGLES, 0, 3);

        std::swap(srcIndex, dstIndex);
    }

    m_StagePreviewDisplayIndex = srcIndex;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);

    if (blendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (cullEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (scissorEnabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
}

void CompositeModule::NewProject() {
    ClearLayersGpu();
    ClearStagePreviewGpu();
    m_Layers.clear();
    m_SelectedId.clear();
    m_ShowLayersWindow = true;
    m_ShowSelectedWindow = true;
    m_ShowViewWindow = true;
    m_ShowExportWindow = true;
    m_WorkspaceLayoutIni.clear();
    m_PendingWorkspaceLayoutLoad = false;
    m_PendingWorkspaceLayoutReset = true;
    m_SuspendWorkspaceLayoutDirtyTracking = true;
    m_RightMousePressedOnCanvas = false;
    m_ViewZoom = 1.0f;
    m_ViewPanX = 0.0f;
    m_ViewPanY = 0.0f;
    m_ShowChecker = true;
    m_SnapEnabled = false;
    m_SnapToObjects = true;
    m_SnapToCenters = true;
    m_SnapToCanvasCenter = true;
    m_SnapToExportBounds = false;
    m_SnapToSpacing = true;
    m_LimitProjectResolution = true;
    m_GridSize = 24.0f;
    m_RotateSnapStep = 15.0f;
    m_ScaleSnapStep = 0.1f;
    m_LastNonZeroGridSize = m_GridSize;
    m_LastNonZeroRotateSnapStep = m_RotateSnapStep;
    m_LastNonZeroScaleSnapStep = m_ScaleSnapStep;
    m_ExportSettings = CompositeExportSettings {};
    m_ActiveExportHandle = ExportHandleType::None;
    m_ExportPanelActive = false;
    m_MiddleMousePanActive = false;
    m_OpenSavePopup = false;
    m_ProjectName = "Untitled Composite";
    m_ProjectFileName.clear();
    m_PendingOpenInEditorRequest = false;
    m_Dirty = false;
    m_StagePreviewDirty = true;
    m_LastStagePreviewZoom = -1.0f;
    m_LastStagePreviewPanX = 0.0f;
    m_LastStagePreviewPanY = 0.0f;
    m_LastStagePreviewCanvasW = 0;
    m_LastStagePreviewCanvasH = 0;
}

bool CompositeModule::HasLayers() const {
    return !m_Layers.empty();
}

CompositeLayer* CompositeModule::GetSelectedLayer() {
    return FindLayerById(m_Layers, m_SelectedId);
}

bool CompositeModule::ConsumePendingOpenInEditorRequest() {
    const bool pending = m_PendingOpenInEditorRequest;
    m_PendingOpenInEditorRequest = false;
    return pending;
}

void CompositeModule::TriggerAddImage() {
    const std::string path = FileDialogs::OpenImageFileDialog("Add image to composite");
    if (!path.empty()) {
        AddImageLayerFromFile(path);
    }
}

void CompositeModule::TriggerAddProject() {
    const std::string path = FileDialogs::OpenProjectFileDialog("Add project to composite");
    if (!path.empty()) {
        AddProjectLayerFromFile(path);
    }
}

void CompositeModule::TriggerAddFromLibrary() {
    m_LibraryPickerMode = LibraryPickerMode::AddProjectLayer;
    m_LibraryPickerTargetLayerId.clear();
    m_ShowLibraryPicker = true;
}

void CompositeModule::TriggerSaveToLibrary() {
    if (!HasLayers()) {
        return;
    }
    if (m_ProjectFileName.empty()) {
        m_OpenSavePopup = true;
        return;
    }

    LibraryManager::Get().RequestSaveCompositeProject(m_ProjectName, this, m_ProjectFileName);
}

void CompositeModule::TriggerExportPng() {
    if (!HasLayers()) {
        return;
    }
    const std::string exportPath = FileDialogs::SavePngFileDialog("Export Composite PNG", "composite_export.png");
    if (!exportPath.empty()) {
        ExportCurrentPng(exportPath);
    }
}

void CompositeModule::AddShapeLayer(const LayerKind kind) {
    if (kind != LayerKind::ShapeRect && kind != LayerKind::ShapeCircle) {
        return;
    }

    CompositeLayer layer;
    layer.id = NewLayerId();
    layer.kind = kind;
    layer.name = (kind == LayerKind::ShapeCircle) ? "Circle" : "Square";
    layer.fillColor = { 0.86f, 0.46f, 0.18f, 1.0f };
    layer.logicalW = 256;
    layer.logicalH = 256;
    layer.scaleX = 1.0f;
    layer.scaleY = 1.0f;

    int maxZ = -1;
    for (const CompositeLayer& existing : m_Layers) {
        maxZ = std::max(maxZ, existing.z);
    }
    layer.z = maxZ + 1;
    const FloatRect viewRect = ComputeViewWorldRect(m_CanvasW, m_CanvasH, m_ViewZoom, m_ViewPanX, m_ViewPanY);
    const ImVec2 viewCenter(viewRect.x + viewRect.width * 0.5f, viewRect.y + viewRect.height * 0.5f);
    layer.x = viewCenter.x - LayerWorldWidth(layer) * 0.5f;
    layer.y = viewCenter.y - LayerWorldHeight(layer) * 0.5f;

    if (!RegenerateGeneratedLayerTexture(layer)) {
        return;
    }

    m_Layers.push_back(std::move(layer));
    m_SelectedId = m_Layers.back().id;
    MarkDocumentDirty();
}

void CompositeModule::AddTextLayer() {
    CompositeLayer layer;
    layer.id = NewLayerId();
    layer.kind = LayerKind::Text;
    layer.name = "Text";
    layer.textContent = "Text";
    layer.textFontSize = 72.0f;
    layer.fillColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    layer.scaleX = 1.0f;
    layer.scaleY = 1.0f;
    layer.preserveAspectRatio = true;

    int maxZ = -1;
    for (const CompositeLayer& existing : m_Layers) {
        maxZ = std::max(maxZ, existing.z);
    }
    layer.z = maxZ + 1;

    if (!RegenerateGeneratedLayerTexture(layer)) {
        return;
    }

    const FloatRect viewRect = ComputeViewWorldRect(m_CanvasW, m_CanvasH, m_ViewZoom, m_ViewPanX, m_ViewPanY);
    const ImVec2 viewCenter(viewRect.x + viewRect.width * 0.5f, viewRect.y + viewRect.height * 0.5f);
    layer.x = viewCenter.x - LayerWorldWidth(layer) * 0.5f;
    layer.y = viewCenter.y - LayerWorldHeight(layer) * 0.5f;

    m_Layers.push_back(std::move(layer));
    m_SelectedId = m_Layers.back().id;
    MarkDocumentDirty();
}

bool CompositeModule::ConvertSelectedLayerToEditorProject() {
    CompositeLayer* selected = GetSelectedLayer();
    if (!selected) {
        return false;
    }

    if (selected->kind == LayerKind::EditorProject) {
        return true;
    }

    if (!BuildLayerSourcePngIfMissing(*selected)) {
        return false;
    }

    selected->kind = LayerKind::EditorProject;
    selected->embeddedProjectJson = json::array().dump();
    selected->generatedFromImage = true;
    selected->linkedProjectFileName.clear();
    selected->linkedProjectName.clear();
    MarkDocumentDirty();
    return true;
}

bool CompositeModule::OpenSelectedLayerInEditor() {
    CompositeLayer* selected = GetSelectedLayer();
    if (!selected) {
        return false;
    }

    if (selected->kind != LayerKind::EditorProject && !ConvertSelectedLayerToEditorProject()) {
        return false;
    }

    m_PendingOpenInEditorRequest = true;
    return true;
}

void CompositeModule::DuplicateSelectedLayer() {
    CompositeLayer* selected = GetSelectedLayer();
    if (!selected) {
        return;
    }

    CompositeLayer copy = *selected;
    copy.id = NewLayerId();
    copy.name = selected->name + " Copy";
    copy.tex = 0;
    copy.linkedProjectFileName.clear();
    copy.linkedProjectName.clear();

    int maxZ = -1;
    for (const CompositeLayer& layer : m_Layers) {
        maxZ = std::max(maxZ, layer.z);
    }
    copy.z = maxZ + 1;

    if (!copy.rgba.empty() && copy.imgW > 0 && copy.imgH > 0) {
        copy.tex = GLHelpers::CreateTextureFromPixels(copy.rgba.data(), copy.imgW, copy.imgH, 4);
    }

    m_Layers.push_back(std::move(copy));
    m_SelectedId = m_Layers.back().id;
    MarkDocumentDirty();
}

void CompositeModule::RemoveSelectedLayers() {
    CompositeLayer* selected = GetSelectedLayer();
    if (!selected || selected->locked) {
        return;
    }

    const std::string selectedId = m_SelectedId;
    DetachChildrenFromParent(m_Layers, selectedId);
    m_Layers.erase(
        std::remove_if(
            m_Layers.begin(),
            m_Layers.end(),
            [&](CompositeLayer& layer) {
                if (layer.id != selectedId) {
                    return false;
                }

                if (layer.tex != 0) {
                    glDeleteTextures(1, &layer.tex);
                    layer.tex = 0;
                }
                return true;
            }),
        m_Layers.end());

    m_SelectedId.clear();
    MarkDocumentDirty();
}

void CompositeModule::BeginRenameSelectedLayer() {
    CompositeLayer* selected = GetSelectedLayer();
    if (!selected) {
        return;
    }

    m_RenameLayerId = selected->id;
    std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", selected->name.c_str());
    m_OpenRenamePopup = true;
}

bool CompositeModule::UpdateLinkedProjectFromSelectedLayer() {
    CompositeLayer* selected = GetSelectedLayer();
    if (!selected ||
        selected->kind != LayerKind::EditorProject ||
        selected->linkedProjectFileName.empty() ||
        selected->embeddedProjectJson.empty()) {
        return false;
    }

    if (!BuildLayerSourcePngIfMissing(*selected)) {
        return false;
    }

    json pipelineData;
    try {
        pipelineData = json::parse(selected->embeddedProjectJson);
    } catch (...) {
        return false;
    }

    std::vector<uint8_t> renderedTopLeft = selected->rgba;
    LibraryManager::FlipImageRowsInPlace(renderedTopLeft, selected->imgW, selected->imgH, 4);

    const std::string projectName = selected->linkedProjectName.empty() ? selected->name : selected->linkedProjectName;
    const bool success = LibraryManager::Get().OverwriteEditorProject(
        selected->linkedProjectFileName,
        projectName,
        selected->originalSourcePng,
        pipelineData,
        renderedTopLeft,
        selected->imgW,
        selected->imgH);
    if (success) {
        selected->linkedProjectName = projectName;
    }
    return success;
}

void CompositeModule::ResetWorkspaceLayout(const bool markDirty) {
    m_ShowLayersWindow = true;
    m_ShowSelectedWindow = true;
    m_ShowViewWindow = true;
    m_ShowExportWindow = true;
    m_ActiveExportHandle = ExportHandleType::None;
    m_ExportPanelActive = false;
    m_WorkspaceLayoutIni.clear();
    m_PendingWorkspaceLayoutLoad = false;
    m_PendingWorkspaceLayoutReset = true;
    m_SuspendWorkspaceLayoutDirtyTracking = true;
    if (markDirty) {
        MarkDocumentDirty();
    }
}

void CompositeModule::BuildDefaultWorkspaceLayout(const unsigned int dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetWindowSize());

    ImGuiID mainDockId = dockspaceId;
    ImGuiID leftDockId = ImGui::DockBuilderSplitNode(mainDockId, ImGuiDir_Left, 0.28f, nullptr, &mainDockId);

    ImGui::DockBuilderDockWindow(kCompositeLayersWindowName, leftDockId);
    ImGui::DockBuilderDockWindow(kCompositeCanvasWindowName, mainDockId);
    ImGui::DockBuilderDockWindow(kCompositeSelectedWindowName, leftDockId);
    ImGui::DockBuilderDockWindow(kCompositeViewWindowName, leftDockId);
    ImGui::DockBuilderDockWindow(kCompositeExportWindowName, leftDockId);
    ImGui::DockBuilderFinish(dockspaceId);
}

void CompositeModule::CaptureWorkspaceLayout() {
    if (m_WorkspaceDockId == 0) {
        return;
    }

    const std::string currentLayout = CaptureCompositeWorkspaceIni(m_WorkspaceDockId);
    if (currentLayout.empty()) {
        return;
    }

    if (currentLayout != m_WorkspaceLayoutIni) {
        m_WorkspaceLayoutIni = currentLayout;
        if (!m_SuspendWorkspaceLayoutDirtyTracking) {
            MarkDocumentDirty();
        }
    }

    m_SuspendWorkspaceLayoutDirtyTracking = false;
}

bool CompositeModule::ShouldShowExportBoundsOverlay() const {
    return HasLayers() &&
           m_ExportSettings.boundsMode == CompositeExportBoundsMode::Custom &&
           (m_ExportPanelActive || m_ActiveExportHandle != ExportHandleType::None);
}

float CompositeModule::GetCurrentExportOutputAspectRatio() const {
    if (m_ExportSettings.boundsMode == CompositeExportBoundsMode::Custom) {
        const FloatRect customBounds {
            m_ExportSettings.customX,
            m_ExportSettings.customY,
            m_ExportSettings.customWidth,
            m_ExportSettings.customHeight
        };
        if (IsRectValid(customBounds)) {
            return RectAspectRatio(customBounds);
        }
    } else {
        std::vector<const CompositeLayer*> layers;
        layers.reserve(m_Layers.size());
        for (const CompositeLayer& layer : m_Layers) {
            if (!layer.visible || layer.rgba.empty() || layer.imgW <= 0 || layer.imgH <= 0) {
                continue;
            }
            layers.push_back(&layer);
        }

        FloatRect autoBounds;
        if (ComputeAutoBounds(m_Layers, layers, autoBounds)) {
            return RectAspectRatio(autoBounds);
        }
    }

    return ExportAspectRatioValue(m_ExportSettings);
}

void CompositeModule::UpdateCustomExportAspectFromBounds() {
    m_ExportSettings.customWidth = std::max(1.0f, m_ExportSettings.customWidth);
    m_ExportSettings.customHeight = std::max(1.0f, m_ExportSettings.customHeight);
    m_ExportSettings.customAspectRatio =
        std::max(1.0f, m_ExportSettings.customWidth) / std::max(1.0f, m_ExportSettings.customHeight);
}

void CompositeModule::SyncExportResolutionFromWidth() {
    m_ExportSettings.outputWidth = std::max(1, m_ExportSettings.outputWidth);
    const float ratio = GetCurrentExportOutputAspectRatio();
    m_ExportSettings.outputHeight = std::max(
        1,
        static_cast<int>(std::round(static_cast<float>(m_ExportSettings.outputWidth) / std::max(0.0001f, ratio))));
}

void CompositeModule::SyncExportResolutionFromHeight() {
    m_ExportSettings.outputHeight = std::max(1, m_ExportSettings.outputHeight);
    const float ratio = GetCurrentExportOutputAspectRatio();
    m_ExportSettings.outputWidth = std::max(
        1,
        static_cast<int>(std::round(static_cast<float>(m_ExportSettings.outputHeight) * std::max(0.0001f, ratio))));
}

void CompositeModule::RenderStage() {
    m_CanvasFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    struct SnapGuideLine {
        ImVec2 a;
        ImVec2 b;
        ImU32 color;
    };

    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    m_CanvasW = std::max(1.0f, canvasSize.x);
    m_CanvasH = std::max(1.0f, canvasSize.y);
    const ImVec2 canvasMax(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec4 childBg = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
    const ImVec4 windowBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const ImVec4 checkerDark = ImVec4(
        childBg.x * 0.78f,
        childBg.y * 0.78f,
        childBg.z * 0.78f,
        childBg.w);
    const ImVec4 checkerLight = ImVec4(
        (childBg.x + windowBg.x) * 0.5f,
        (childBg.y + windowBg.y) * 0.5f,
        (childBg.z + windowBg.z) * 0.5f,
        std::max(childBg.w, windowBg.w));

    if (m_ShowChecker) {
        const float checkerSize = 16.0f;
        for (float y = canvasPos.y; y < canvasMax.y; y += checkerSize) {
            for (float x = canvasPos.x; x < canvasMax.x; x += checkerSize) {
                const int cellX = static_cast<int>(std::floor((x - canvasPos.x) / checkerSize));
                const int cellY = static_cast<int>(std::floor((y - canvasPos.y) / checkerSize));
                const ImU32 color = ImGui::GetColorU32(((cellX + cellY) & 1) ? checkerLight : checkerDark);
                drawList->AddRectFilled(
                    ImVec2(x, y),
                    ImVec2(std::min(x + checkerSize, canvasMax.x), std::min(y + checkerSize, canvasMax.y)),
                    color);
            }
        }
    } else {
        drawList->AddRectFilled(canvasPos, canvasMax, ImGui::GetColorU32(childBg));
    }

    SyncLayerTextures();

    const ImVec2 canvasCenter(canvasPos.x + canvasSize.x * 0.5f, canvasPos.y + canvasSize.y * 0.5f);
    const int canvasPixelW = std::max(1, static_cast<int>(std::round(canvasSize.x)));
    const int canvasPixelH = std::max(1, static_cast<int>(std::round(canvasSize.y)));
    const bool showExportBoundsOverlay = ShouldShowExportBoundsOverlay();
    std::vector<const CompositeLayer*> visibleLayers;
    visibleLayers.reserve(m_Layers.size());
    for (const CompositeLayer& layer : m_Layers) {
        if (!layer.visible || layer.rgba.empty() || layer.tex == 0) {
            continue;
        }
        visibleLayers.push_back(&layer);
    }

    std::sort(visibleLayers.begin(), visibleLayers.end(), [](const CompositeLayer* a, const CompositeLayer* b) {
        return a->z < b->z;
    });

    RefreshVisibleTextRasterQuality();

    const bool viewChanged =
        m_LastStagePreviewCanvasW != canvasPixelW ||
        m_LastStagePreviewCanvasH != canvasPixelH ||
        std::abs(m_LastStagePreviewZoom - m_ViewZoom) > 0.0001f ||
        std::abs(m_LastStagePreviewPanX - m_ViewPanX) > 0.25f ||
        std::abs(m_LastStagePreviewPanY - m_ViewPanY) > 0.25f;

    if (m_StagePreviewDirty || viewChanged) {
        if (!visibleLayers.empty()) {
            const FloatRect viewRect = ComputeViewWorldRect(canvasSize.x, canvasSize.y, m_ViewZoom, m_ViewPanX, m_ViewPanY);
            RenderStagePreviewTexture(
                m_Layers,
                visibleLayers,
                viewRect.x,
                viewRect.y,
                viewRect.width,
                viewRect.height,
                canvasPixelW,
                canvasPixelH);
        } else {
            ClearStagePreviewGpu();
        }

        m_StagePreviewDirty = false;
        m_LastStagePreviewCanvasW = canvasPixelW;
        m_LastStagePreviewCanvasH = canvasPixelH;
        m_LastStagePreviewZoom = m_ViewZoom;
        m_LastStagePreviewPanX = m_ViewPanX;
        m_LastStagePreviewPanY = m_ViewPanY;
    }

    if (m_StagePreviewTex[m_StagePreviewDisplayIndex] != 0) {
        drawList->AddImage(
            (ImTextureID)(intptr_t)m_StagePreviewTex[m_StagePreviewDisplayIndex],
            canvasPos,
            canvasMax,
            ImVec2(0, 1),
            ImVec2(1, 0));
    } else {
        drawList->AddText(ImVec2(canvasPos.x + 20.0f, canvasPos.y + 20.0f), IM_COL32(180, 180, 185, 255), "Add layers to start compositing.");
    }

    if (showExportBoundsOverlay) {
        const ImVec2 exportTopLeft = WorldToScreen(
            canvasCenter,
            m_ViewZoom,
            m_ViewPanX,
            m_ViewPanY,
            ImVec2(m_ExportSettings.customX, m_ExportSettings.customY));
        const ImVec2 exportBottomRight = WorldToScreen(
            canvasCenter,
            m_ViewZoom,
            m_ViewPanX,
            m_ViewPanY,
            ImVec2(
                m_ExportSettings.customX + m_ExportSettings.customWidth,
                m_ExportSettings.customY + m_ExportSettings.customHeight));
        drawList->AddRect(exportTopLeft, exportBottomRight, IM_COL32(90, 190, 255, 255), 0.0f, 0, 2.0f);

        const bool lockedExportAspect = m_ExportSettings.aspectPreset != CompositeExportAspectPreset::Custom;
        const ImVec2 topCenter((exportTopLeft.x + exportBottomRight.x) * 0.5f, exportTopLeft.y);
        const ImVec2 bottomCenter((exportTopLeft.x + exportBottomRight.x) * 0.5f, exportBottomRight.y);
        const ImVec2 leftCenter(exportTopLeft.x, (exportTopLeft.y + exportBottomRight.y) * 0.5f);
        const ImVec2 rightCenter(exportBottomRight.x, (exportTopLeft.y + exportBottomRight.y) * 0.5f);
        std::array<ImVec2, 8> handlePoints = {
            exportTopLeft,
            topCenter,
            ImVec2(exportBottomRight.x, exportTopLeft.y),
            rightCenter,
            exportBottomRight,
            bottomCenter,
            ImVec2(exportTopLeft.x, exportBottomRight.y),
            leftCenter
        };

        const int handleCount = lockedExportAspect ? 4 : 8;
        const int handleIndices[] = { 0, 2, 4, 6, 1, 3, 5, 7 };
        for (int handleIndex = 0; handleIndex < handleCount; ++handleIndex) {
            const ImVec2& handlePoint = handlePoints[handleIndices[handleIndex]];
            drawList->AddRectFilled(
                ImVec2(handlePoint.x - kExportBoundsHandleScreenRadius, handlePoint.y - kExportBoundsHandleScreenRadius),
                ImVec2(handlePoint.x + kExportBoundsHandleScreenRadius, handlePoint.y + kExportBoundsHandleScreenRadius),
                IM_COL32(90, 190, 255, 255),
                2.0f);
            drawList->AddRect(
                ImVec2(handlePoint.x - kExportBoundsHandleScreenRadius, handlePoint.y - kExportBoundsHandleScreenRadius),
                ImVec2(handlePoint.x + kExportBoundsHandleScreenRadius, handlePoint.y + kExportBoundsHandleScreenRadius),
                IM_COL32(12, 16, 20, 255),
                2.0f);
        }
    }

    CompositeLayer* selectedLayer = GetSelectedLayer();
    std::array<ImVec2, 4> selectedWorldQuad {};
    std::array<ImVec2, 4> selectedScreenQuad {};
    ImVec2 selectedScreenTopCenter {};
    ImVec2 selectedScreenBottomCenter {};
    ImVec2 selectedScreenLeftCenter {};
    ImVec2 selectedScreenRightCenter {};
    ImVec2 selectedRotationHandle {};
    if (selectedLayer) {
        selectedWorldQuad = ComputeLayerQuadWorld(m_Layers, *selectedLayer);
        for (int index = 0; index < 4; ++index) {
            selectedScreenQuad[index] = WorldToScreen(canvasCenter, m_ViewZoom, m_ViewPanX, m_ViewPanY, selectedWorldQuad[index]);
        }

        selectedScreenTopCenter = ImVec2(
            (selectedScreenQuad[0].x + selectedScreenQuad[1].x) * 0.5f,
            (selectedScreenQuad[0].y + selectedScreenQuad[1].y) * 0.5f);
        selectedScreenBottomCenter = ImVec2(
            (selectedScreenQuad[2].x + selectedScreenQuad[3].x) * 0.5f,
            (selectedScreenQuad[2].y + selectedScreenQuad[3].y) * 0.5f);
        selectedScreenLeftCenter = ImVec2(
            (selectedScreenQuad[0].x + selectedScreenQuad[3].x) * 0.5f,
            (selectedScreenQuad[0].y + selectedScreenQuad[3].y) * 0.5f);
        selectedScreenRightCenter = ImVec2(
            (selectedScreenQuad[1].x + selectedScreenQuad[2].x) * 0.5f,
            (selectedScreenQuad[1].y + selectedScreenQuad[2].y) * 0.5f);
        const ImVec2 selectedWorldCenter(
            (selectedWorldQuad[0].x + selectedWorldQuad[1].x + selectedWorldQuad[2].x + selectedWorldQuad[3].x) * 0.25f,
            (selectedWorldQuad[0].y + selectedWorldQuad[1].y + selectedWorldQuad[2].y + selectedWorldQuad[3].y) * 0.25f);
        ImVec2 rotationDirection(
            selectedScreenTopCenter.x - WorldToScreen(canvasCenter, m_ViewZoom, m_ViewPanX, m_ViewPanY, selectedWorldCenter).x,
            selectedScreenTopCenter.y - WorldToScreen(canvasCenter, m_ViewZoom, m_ViewPanX, m_ViewPanY, selectedWorldCenter).y);
        const float rotationLength = std::hypot(rotationDirection.x, rotationDirection.y);
        if (rotationLength > 1e-6f) {
            rotationDirection.x /= rotationLength;
            rotationDirection.y /= rotationLength;
        } else {
            rotationDirection = ImVec2(0.0f, -1.0f);
        }
        selectedRotationHandle = ImVec2(
            selectedScreenTopCenter.x + rotationDirection.x * 26.0f,
            selectedScreenTopCenter.y + rotationDirection.y * 26.0f);

        const ImU32 outlineColor = selectedLayer->locked ? IM_COL32(180, 180, 190, 255) : IM_COL32(255, 170, 0, 255);
        drawList->AddPolyline(selectedScreenQuad.data(), 4, outlineColor, true, 2.0f);
        if (!selectedLayer->locked) {
            for (const ImVec2& point : selectedScreenQuad) {
                drawList->AddCircleFilled(point, kLayerHandleScreenRadius, IM_COL32(255, 255, 255, 255));
                drawList->AddCircle(point, kLayerHandleScreenRadius, IM_COL32(0, 0, 0, 255));
            }
            if (!selectedLayer->preserveAspectRatio) {
                const ImVec2 edgeHandles[] = {
                    selectedScreenTopCenter,
                    selectedScreenRightCenter,
                    selectedScreenBottomCenter,
                    selectedScreenLeftCenter
                };
                for (const ImVec2& point : edgeHandles) {
                    drawList->AddRectFilled(
                        ImVec2(point.x - kLayerHandleScreenRadius, point.y - kLayerHandleScreenRadius),
                        ImVec2(point.x + kLayerHandleScreenRadius, point.y + kLayerHandleScreenRadius),
                        IM_COL32(255, 255, 255, 255),
                        2.0f);
                    drawList->AddRect(
                        ImVec2(point.x - kLayerHandleScreenRadius, point.y - kLayerHandleScreenRadius),
                        ImVec2(point.x + kLayerHandleScreenRadius, point.y + kLayerHandleScreenRadius),
                        IM_COL32(0, 0, 0, 255),
                        2.0f);
                }
            }
            drawList->AddLine(selectedScreenTopCenter, selectedRotationHandle, outlineColor, 2.0f);
            drawList->AddCircleFilled(selectedRotationHandle, kLayerHandleScreenRadius + 1.0f, IM_COL32(240, 190, 110, 255));
            drawList->AddCircle(selectedRotationHandle, kLayerHandleScreenRadius + 1.0f, IM_COL32(0, 0, 0, 255));
        }
    }

    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("composite_stage_interact", canvasSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 mousePos = ImGui::GetMousePos();
    const ImVec2 mouseWorld = ScreenToWorld(canvasCenter, m_ViewZoom, m_ViewPanX, m_ViewPanY, mousePos);
    const bool middleMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    std::vector<SnapGuideLine> snapGuides;

    auto worldToScreen = [&](const ImVec2& worldPoint) {
        return WorldToScreen(canvasCenter, m_ViewZoom, m_ViewPanX, m_ViewPanY, worldPoint);
    };

    auto addGuideWorld = [&](const ImVec2& a, const ImVec2& b, const ImU32 color) {
        snapGuides.push_back({ a, b, color });
    };

    auto beginHandleInteraction = [&](CompositeLayer& layer, const HandleType handleType) {
        const AffineTransform2D layerParentWorld = GetParentWorldTransform(m_Layers, layer);
        const ImVec2 layerMouseLocal = TransformPoint(Inverse(layerParentWorld), mouseWorld);
        const ImVec2 layerCenter = LayerLocalCenter(layer);
        const ImVec2 layerAxisX = LayerLocalAxisX(layer);
        const ImVec2 layerAxisY = LayerLocalAxisY(layer);

        m_ActiveHandle = handleType;
        m_StartScaleX = layer.scaleX;
        m_StartScaleY = layer.scaleY;
        m_StartRotation = layer.rotation;
        m_StartX = layer.x;
        m_StartY = layer.y;
        m_StartWidth = LayerWorldWidth(layer);
        m_StartHeight = LayerWorldHeight(layer);
        const float halfW = m_StartWidth * 0.5f;
        const float halfH = m_StartHeight * 0.5f;

        switch (handleType) {
        case HandleType::ResizeTopLeft:
            m_ResizeAnchorX = layerCenter.x + layerAxisX.x * halfW + layerAxisY.x * halfH;
            m_ResizeAnchorY = layerCenter.y + layerAxisX.y * halfW + layerAxisY.y * halfH;
            break;
        case HandleType::ResizeTopRight:
            m_ResizeAnchorX = layerCenter.x - layerAxisX.x * halfW + layerAxisY.x * halfH;
            m_ResizeAnchorY = layerCenter.y - layerAxisX.y * halfW + layerAxisY.y * halfH;
            break;
        case HandleType::ResizeBottomRight:
            m_ResizeAnchorX = layerCenter.x - layerAxisX.x * halfW - layerAxisY.x * halfH;
            m_ResizeAnchorY = layerCenter.y - layerAxisX.y * halfW - layerAxisY.y * halfH;
            break;
        case HandleType::ResizeBottomLeft:
            m_ResizeAnchorX = layerCenter.x + layerAxisX.x * halfW - layerAxisY.x * halfH;
            m_ResizeAnchorY = layerCenter.y + layerAxisX.y * halfW - layerAxisY.y * halfH;
            break;
        case HandleType::ResizeLeft:
            m_ResizeAnchorX = layerCenter.x + layerAxisX.x * halfW;
            m_ResizeAnchorY = layerCenter.y + layerAxisX.y * halfW;
            break;
        case HandleType::ResizeRight:
            m_ResizeAnchorX = layerCenter.x - layerAxisX.x * halfW;
            m_ResizeAnchorY = layerCenter.y - layerAxisX.y * halfW;
            break;
        case HandleType::ResizeTop:
            m_ResizeAnchorX = layerCenter.x + layerAxisY.x * halfH;
            m_ResizeAnchorY = layerCenter.y + layerAxisY.y * halfH;
            break;
        case HandleType::ResizeBottom:
            m_ResizeAnchorX = layerCenter.x - layerAxisY.x * halfH;
            m_ResizeAnchorY = layerCenter.y - layerAxisY.y * halfH;
            break;
        case HandleType::Rotate:
            m_StartMouseAngle = std::atan2(layerMouseLocal.y - layerCenter.y, layerMouseLocal.x - layerCenter.x) - layer.rotation;
            break;
        case HandleType::Move:
        case HandleType::None:
        default:
            break;
        }
    };

    auto addSpacingGuidesForMove = [&](const FloatRect& activeBounds, float& targetX, float& targetY) {
        if (!m_SnapToSpacing) {
            return;
        }

        FloatRect movedBounds = activeBounds;
        movedBounds.x = targetX;
        movedBounds.y = targetY;
        const float threshold = kSnapThresholdScreenPixels / std::max(0.001f, m_ViewZoom);

        for (const CompositeLayer& first : m_Layers) {
            if (!first.visible || first.id == m_SelectedId || IsLayerDescendantOf(m_Layers, first.id, m_SelectedId)) {
                continue;
            }
            const FloatRect a = ComputeLayerBoundsWorld(m_Layers, first);
            for (const CompositeLayer& second : m_Layers) {
                if (!second.visible ||
                    second.id == m_SelectedId ||
                    second.id == first.id ||
                    IsLayerDescendantOf(m_Layers, second.id, m_SelectedId) ||
                    IsLayerDescendantOf(m_Layers, second.id, first.id)) {
                    continue;
                }
                const FloatRect b = ComputeLayerBoundsWorld(m_Layers, second);

                if (a.x + a.width <= movedBounds.x && movedBounds.x + movedBounds.width <= b.x) {
                    const float desiredX = (a.x + a.width + b.x - movedBounds.width) * 0.5f;
                    if (std::abs(desiredX - targetX) <= threshold) {
                        targetX = desiredX;
                        const float guideY = std::max(a.y, std::max(b.y, movedBounds.y));
                        addGuideWorld(ImVec2(a.x + a.width, guideY), ImVec2(desiredX, guideY), IM_COL32(160, 110, 255, 255));
                        addGuideWorld(ImVec2(desiredX + movedBounds.width, guideY), ImVec2(b.x, guideY), IM_COL32(160, 110, 255, 255));
                    }
                }

                if (a.y + a.height <= movedBounds.y && movedBounds.y + movedBounds.height <= b.y) {
                    const float desiredY = (a.y + a.height + b.y - movedBounds.height) * 0.5f;
                    if (std::abs(desiredY - targetY) <= threshold) {
                        targetY = desiredY;
                        const float guideX = std::max(a.x, std::max(b.x, movedBounds.x));
                        addGuideWorld(ImVec2(guideX, a.y + a.height), ImVec2(guideX, desiredY), IM_COL32(160, 110, 255, 255));
                        addGuideWorld(ImVec2(guideX, desiredY + movedBounds.height), ImVec2(guideX, b.y), IM_COL32(160, 110, 255, 255));
                    }
                }
            }
        }
    };

    auto applyMoveSnapping = [&](const CompositeLayer& referenceLayer, const AffineTransform2D& parentInverse, float& targetWorldX, float& targetWorldY) {
        if (!m_SnapEnabled) {
            return;
        }

        if (m_GridSize > 0.0f) {
            targetWorldX = std::round(targetWorldX / m_GridSize) * m_GridSize;
            targetWorldY = std::round(targetWorldY / m_GridSize) * m_GridSize;
        }

        auto buildActiveBounds = [&](const float worldX, const float worldY) {
            CompositeLayer temp = referenceLayer;
            const ImVec2 targetLocal = TransformPoint(parentInverse, ImVec2(worldX, worldY));
            temp.x = targetLocal.x;
            temp.y = targetLocal.y;
            return ComputeLayerBoundsWorld(m_Layers, temp);
        };

        FloatRect activeBounds = buildActiveBounds(targetWorldX, targetWorldY);
        const float activeLeft = activeBounds.x;
        const float activeCenterX = activeBounds.x + activeBounds.width * 0.5f;
        const float activeRight = activeBounds.x + activeBounds.width;
        const float activeTop = activeBounds.y;
        const float activeCenterY = activeBounds.y + activeBounds.height * 0.5f;
        const float activeBottom = activeBounds.y + activeBounds.height;
        const float threshold = kSnapThresholdScreenPixels / std::max(0.001f, m_ViewZoom);

        float bestDeltaX = std::numeric_limits<float>::max();
        float bestDeltaY = std::numeric_limits<float>::max();
        bool snappedX = false;
        bool snappedY = false;
        ImVec2 bestXGuideA {};
        ImVec2 bestXGuideB {};
        ImVec2 bestYGuideA {};
        ImVec2 bestYGuideB {};

        auto considerX = [&](const float delta, const float guideX, const float y1, const float y2) {
            if (std::abs(delta) <= threshold && (!snappedX || std::abs(delta) < std::abs(bestDeltaX))) {
                snappedX = true;
                bestDeltaX = delta;
                bestXGuideA = ImVec2(guideX, y1);
                bestXGuideB = ImVec2(guideX, y2);
            }
        };
        auto considerY = [&](const float delta, const float guideY, const float x1, const float x2) {
            if (std::abs(delta) <= threshold && (!snappedY || std::abs(delta) < std::abs(bestDeltaY))) {
                snappedY = true;
                bestDeltaY = delta;
                bestYGuideA = ImVec2(x1, guideY);
                bestYGuideB = ImVec2(x2, guideY);
            }
        };

        for (const CompositeLayer& otherLayer : m_Layers) {
            if (!otherLayer.visible ||
                otherLayer.id == referenceLayer.id ||
                IsLayerDescendantOf(m_Layers, otherLayer.id, referenceLayer.id)) {
                continue;
            }

            const FloatRect otherBounds = ComputeLayerBoundsWorld(m_Layers, otherLayer);
            const float otherLeft = otherBounds.x;
            const float otherCenterX = otherBounds.x + otherBounds.width * 0.5f;
            const float otherRight = otherBounds.x + otherBounds.width;
            const float otherTop = otherBounds.y;
            const float otherCenterY = otherBounds.y + otherBounds.height * 0.5f;
            const float otherBottom = otherBounds.y + otherBounds.height;

            if (m_SnapToObjects) {
                considerX(otherLeft - activeLeft, otherLeft, std::min(activeBounds.y, otherBounds.y), std::max(activeBottom, otherBottom));
                considerX(otherRight - activeRight, otherRight, std::min(activeBounds.y, otherBounds.y), std::max(activeBottom, otherBottom));
                considerY(otherTop - activeTop, otherTop, std::min(activeBounds.x, otherBounds.x), std::max(activeRight, otherRight));
                considerY(otherBottom - activeBottom, otherBottom, std::min(activeBounds.x, otherBounds.x), std::max(activeRight, otherRight));
            }
            if (m_SnapToCenters) {
                considerX(otherCenterX - activeCenterX, otherCenterX, std::min(activeBounds.y, otherBounds.y), std::max(activeBottom, otherBottom));
                considerY(otherCenterY - activeCenterY, otherCenterY, std::min(activeBounds.x, otherBounds.x), std::max(activeRight, otherRight));
            }
        }

        if (m_SnapToCanvasCenter) {
            considerX(-activeCenterX, 0.0f, activeBounds.y, activeBottom);
            considerY(-activeCenterY, 0.0f, activeBounds.x, activeRight);
        }

        if (m_SnapToExportBounds && m_ExportSettings.boundsMode == CompositeExportBoundsMode::Custom) {
            const FloatRect exportBounds {
                m_ExportSettings.customX,
                m_ExportSettings.customY,
                m_ExportSettings.customWidth,
                m_ExportSettings.customHeight
            };
            if (IsRectValid(exportBounds)) {
                const float exportLeft = exportBounds.x;
                const float exportCenterX = exportBounds.x + exportBounds.width * 0.5f;
                const float exportRight = exportBounds.x + exportBounds.width;
                const float exportTop = exportBounds.y;
                const float exportCenterY = exportBounds.y + exportBounds.height * 0.5f;
                const float exportBottom = exportBounds.y + exportBounds.height;
                considerX(exportLeft - activeLeft, exportLeft, std::min(activeBounds.y, exportBounds.y), std::max(activeBottom, exportBottom));
                considerX(exportRight - activeRight, exportRight, std::min(activeBounds.y, exportBounds.y), std::max(activeBottom, exportBottom));
                considerY(exportTop - activeTop, exportTop, std::min(activeBounds.x, exportBounds.x), std::max(activeRight, exportRight));
                considerY(exportBottom - activeBottom, exportBottom, std::min(activeBounds.x, exportBounds.x), std::max(activeRight, exportRight));
                considerX(exportCenterX - activeCenterX, exportCenterX, std::min(activeBounds.y, exportBounds.y), std::max(activeBottom, exportBottom));
                considerY(exportCenterY - activeCenterY, exportCenterY, std::min(activeBounds.x, exportBounds.x), std::max(activeRight, exportRight));
            }
        }

        if (snappedX) {
            targetWorldX += bestDeltaX;
            addGuideWorld(bestXGuideA, bestXGuideB, IM_COL32(80, 200, 255, 255));
        }
        if (snappedY) {
            targetWorldY += bestDeltaY;
            addGuideWorld(bestYGuideA, bestYGuideB, IM_COL32(80, 200, 255, 255));
        }

        activeBounds = buildActiveBounds(targetWorldX, targetWorldY);
        addSpacingGuidesForMove(activeBounds, targetWorldX, targetWorldY);
    };

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        m_MiddleMousePanActive = true;
        m_ActiveHandle = HandleType::None;
        m_ActiveExportHandle = ExportHandleType::None;
    }
    if (m_MiddleMousePanActive && middleMouseDown) {
        m_ViewPanX += ImGui::GetIO().MouseDelta.x;
        m_ViewPanY += ImGui::GetIO().MouseDelta.y;
        MarkStageDirty();
    }
    if (m_MiddleMousePanActive && ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
        m_MiddleMousePanActive = false;
    }

    if (!m_MiddleMousePanActive && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_RightMousePressedOnCanvas = true;
        m_RightMousePressX = mousePos.x;
        m_RightMousePressY = mousePos.y;
    }

    if (!m_MiddleMousePanActive && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered) {
        m_ActiveHandle = HandleType::None;
        m_ActiveExportHandle = ExportHandleType::None;

        if (showExportBoundsOverlay) {
            const ImVec2 exportTopLeft = WorldToScreen(
                canvasCenter,
                m_ViewZoom,
                m_ViewPanX,
                m_ViewPanY,
                ImVec2(m_ExportSettings.customX, m_ExportSettings.customY));
            const ImVec2 exportBottomRight = WorldToScreen(
                canvasCenter,
                m_ViewZoom,
                m_ViewPanX,
                m_ViewPanY,
                ImVec2(
                    m_ExportSettings.customX + m_ExportSettings.customWidth,
                    m_ExportSettings.customY + m_ExportSettings.customHeight));
            const ImVec2 topRight(exportBottomRight.x, exportTopLeft.y);
            const ImVec2 bottomLeft(exportTopLeft.x, exportBottomRight.y);
            const ImVec2 topCenter((exportTopLeft.x + exportBottomRight.x) * 0.5f, exportTopLeft.y);
            const ImVec2 bottomCenter((exportTopLeft.x + exportBottomRight.x) * 0.5f, exportBottomRight.y);
            const ImVec2 leftCenter(exportTopLeft.x, (exportTopLeft.y + exportBottomRight.y) * 0.5f);
            const ImVec2 rightCenter(exportBottomRight.x, (exportTopLeft.y + exportBottomRight.y) * 0.5f);
            const bool lockedExportAspect = m_ExportSettings.aspectPreset != CompositeExportAspectPreset::Custom;

            auto distanceTo = [&](const ImVec2& point) {
                return std::hypot(mousePos.x - point.x, mousePos.y - point.y);
            };

            const float threshold = 12.0f;
            const float minX = std::min(exportTopLeft.x, exportBottomRight.x);
            const float maxX = std::max(exportTopLeft.x, exportBottomRight.x);
            const float minY = std::min(exportTopLeft.y, exportBottomRight.y);
            const float maxY = std::max(exportTopLeft.y, exportBottomRight.y);
            const bool insideExportRect =
                mousePos.x >= minX &&
                mousePos.x <= maxX &&
                mousePos.y >= minY &&
                mousePos.y <= maxY;

            if (distanceTo(exportTopLeft) <= threshold) {
                m_ActiveExportHandle = ExportHandleType::TopLeft;
            } else if (distanceTo(topRight) <= threshold) {
                m_ActiveExportHandle = ExportHandleType::TopRight;
            } else if (distanceTo(exportBottomRight) <= threshold) {
                m_ActiveExportHandle = ExportHandleType::BottomRight;
            } else if (distanceTo(bottomLeft) <= threshold) {
                m_ActiveExportHandle = ExportHandleType::BottomLeft;
            } else if (!lockedExportAspect && distanceTo(topCenter) <= threshold) {
                m_ActiveExportHandle = ExportHandleType::Top;
            } else if (!lockedExportAspect && distanceTo(bottomCenter) <= threshold) {
                m_ActiveExportHandle = ExportHandleType::Bottom;
            } else if (!lockedExportAspect && distanceTo(leftCenter) <= threshold) {
                m_ActiveExportHandle = ExportHandleType::Left;
            } else if (!lockedExportAspect && distanceTo(rightCenter) <= threshold) {
                m_ActiveExportHandle = ExportHandleType::Right;
            } else if (insideExportRect) {
                m_ActiveExportHandle = ExportHandleType::Move;
            }

            if (m_ActiveExportHandle != ExportHandleType::None) {
                m_ExportDragStartX = m_ExportSettings.customX;
                m_ExportDragStartY = m_ExportSettings.customY;
                m_ExportDragStartWidth = m_ExportSettings.customWidth;
                m_ExportDragStartHeight = m_ExportSettings.customHeight;
                m_ExportDragStartMouseWorldX = mouseWorld.x;
                m_ExportDragStartMouseWorldY = mouseWorld.y;
            }
        }

        selectedLayer = GetSelectedLayer();
        if (m_ActiveExportHandle == ExportHandleType::None && selectedLayer && !selectedLayer->locked) {
            auto distanceTo = [&](const ImVec2& point) {
                return std::hypot(mousePos.x - point.x, mousePos.y - point.y);
            };

            const float threshold = 12.0f;
            if (distanceTo(selectedRotationHandle) < threshold) {
                beginHandleInteraction(*selectedLayer, HandleType::Rotate);
            } else if (distanceTo(selectedScreenQuad[0]) < threshold) {
                beginHandleInteraction(*selectedLayer, HandleType::ResizeTopLeft);
            } else if (distanceTo(selectedScreenQuad[1]) < threshold) {
                beginHandleInteraction(*selectedLayer, HandleType::ResizeTopRight);
            } else if (distanceTo(selectedScreenQuad[2]) < threshold) {
                beginHandleInteraction(*selectedLayer, HandleType::ResizeBottomRight);
            } else if (distanceTo(selectedScreenQuad[3]) < threshold) {
                beginHandleInteraction(*selectedLayer, HandleType::ResizeBottomLeft);
            } else if (!selectedLayer->preserveAspectRatio && distanceTo(selectedScreenTopCenter) < threshold) {
                beginHandleInteraction(*selectedLayer, HandleType::ResizeTop);
            } else if (!selectedLayer->preserveAspectRatio && distanceTo(selectedScreenRightCenter) < threshold) {
                beginHandleInteraction(*selectedLayer, HandleType::ResizeRight);
            } else if (!selectedLayer->preserveAspectRatio && distanceTo(selectedScreenBottomCenter) < threshold) {
                beginHandleInteraction(*selectedLayer, HandleType::ResizeBottom);
            } else if (!selectedLayer->preserveAspectRatio && distanceTo(selectedScreenLeftCenter) < threshold) {
                beginHandleInteraction(*selectedLayer, HandleType::ResizeLeft);
            } else {
                float hitU = 0.0f;
                float hitV = 0.0f;
                if (MapWorldToLayerUv(m_Layers, *selectedLayer, mouseWorld.x, mouseWorld.y, hitU, hitV)) {
                    beginHandleInteraction(*selectedLayer, HandleType::Move);
                }
            }
        }

        if (m_ActiveExportHandle == ExportHandleType::None && m_ActiveHandle == HandleType::None) {
            CompositeLayer* hitLayer = FindTopMostVisibleLayerAtWorldPoint(m_Layers, mouseWorld.x, mouseWorld.y);
            if (hitLayer != nullptr) {
                m_SelectedId = hitLayer->id;
                if (!hitLayer->locked) {
                    beginHandleInteraction(*hitLayer, HandleType::Move);
                }
            } else {
                m_SelectedId.clear();
            }
        }
    }

    selectedLayer = GetSelectedLayer();
    if (!m_MiddleMousePanActive && m_ActiveExportHandle != ExportHandleType::None && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float deltaX = mouseWorld.x - m_ExportDragStartMouseWorldX;
        const float deltaY = mouseWorld.y - m_ExportDragStartMouseWorldY;
        const bool lockedAspect = m_ExportSettings.aspectPreset != CompositeExportAspectPreset::Custom;
        const float ratio = ExportAspectRatioValue(m_ExportSettings);

        FloatRect newBounds {
            m_ExportDragStartX,
            m_ExportDragStartY,
            m_ExportDragStartWidth,
            m_ExportDragStartHeight
        };

        auto applyCornerWithAspect = [&](const float anchorX, const float anchorY, const bool dragLeft, const bool dragTop) {
            const float widthByX = std::max(1.0f, std::abs(mouseWorld.x - anchorX));
            const float heightByX = std::max(1.0f, widthByX / std::max(0.0001f, ratio));
            const float heightByY = std::max(1.0f, std::abs(mouseWorld.y - anchorY));
            const float widthByY = std::max(1.0f, heightByY * std::max(0.0001f, ratio));

            const float xCornerByX = dragLeft ? (anchorX - widthByX) : (anchorX + widthByX);
            const float yCornerByX = dragTop ? (anchorY - heightByX) : (anchorY + heightByX);
            const float xCornerByY = dragLeft ? (anchorX - widthByY) : (anchorX + widthByY);
            const float yCornerByY = dragTop ? (anchorY - heightByY) : (anchorY + heightByY);

            const float errorByX =
                std::pow(mouseWorld.x - xCornerByX, 2.0f) +
                std::pow(mouseWorld.y - yCornerByX, 2.0f);
            const float errorByY =
                std::pow(mouseWorld.x - xCornerByY, 2.0f) +
                std::pow(mouseWorld.y - yCornerByY, 2.0f);

            const float chosenWidth = (errorByX <= errorByY) ? widthByX : widthByY;
            const float chosenHeight = std::max(1.0f, chosenWidth / std::max(0.0001f, ratio));
            const float left = dragLeft ? (anchorX - chosenWidth) : anchorX;
            const float top = dragTop ? (anchorY - chosenHeight) : anchorY;
            newBounds = { left, top, chosenWidth, chosenHeight };
        };

        if (m_ActiveExportHandle == ExportHandleType::Move) {
            newBounds.x = m_ExportDragStartX + deltaX;
            newBounds.y = m_ExportDragStartY + deltaY;
        } else if (!lockedAspect) {
            float x1 = m_ExportDragStartX;
            float y1 = m_ExportDragStartY;
            float x2 = m_ExportDragStartX + m_ExportDragStartWidth;
            float y2 = m_ExportDragStartY + m_ExportDragStartHeight;

            if (m_ActiveExportHandle == ExportHandleType::Left ||
                m_ActiveExportHandle == ExportHandleType::TopLeft ||
                m_ActiveExportHandle == ExportHandleType::BottomLeft) {
                x1 += deltaX;
            }
            if (m_ActiveExportHandle == ExportHandleType::Right ||
                m_ActiveExportHandle == ExportHandleType::TopRight ||
                m_ActiveExportHandle == ExportHandleType::BottomRight) {
                x2 += deltaX;
            }
            if (m_ActiveExportHandle == ExportHandleType::Top ||
                m_ActiveExportHandle == ExportHandleType::TopLeft ||
                m_ActiveExportHandle == ExportHandleType::TopRight) {
                y1 += deltaY;
            }
            if (m_ActiveExportHandle == ExportHandleType::Bottom ||
                m_ActiveExportHandle == ExportHandleType::BottomLeft ||
                m_ActiveExportHandle == ExportHandleType::BottomRight) {
                y2 += deltaY;
            }

            newBounds = MakeNormalizedRect(x1, y1, x2, y2);
        } else {
            const float startRight = m_ExportDragStartX + m_ExportDragStartWidth;
            const float startBottom = m_ExportDragStartY + m_ExportDragStartHeight;
            switch (m_ActiveExportHandle) {
            case ExportHandleType::TopLeft:
                applyCornerWithAspect(startRight, startBottom, true, true);
                break;
            case ExportHandleType::TopRight:
                applyCornerWithAspect(m_ExportDragStartX, startBottom, false, true);
                break;
            case ExportHandleType::BottomRight:
                applyCornerWithAspect(m_ExportDragStartX, m_ExportDragStartY, false, false);
                break;
            case ExportHandleType::BottomLeft:
                applyCornerWithAspect(startRight, m_ExportDragStartY, true, false);
                break;
            default:
                break;
            }
        }

        const bool changed =
            std::abs(newBounds.x - m_ExportSettings.customX) > 0.0001f ||
            std::abs(newBounds.y - m_ExportSettings.customY) > 0.0001f ||
            std::abs(newBounds.width - m_ExportSettings.customWidth) > 0.0001f ||
            std::abs(newBounds.height - m_ExportSettings.customHeight) > 0.0001f;
        if (changed) {
            m_ExportSettings.customX = newBounds.x;
            m_ExportSettings.customY = newBounds.y;
            m_ExportSettings.customWidth = std::max(1.0f, newBounds.width);
            m_ExportSettings.customHeight = std::max(1.0f, newBounds.height);
            if (m_ExportSettings.aspectPreset == CompositeExportAspectPreset::Custom) {
                UpdateCustomExportAspectFromBounds();
            }
            MarkDocumentDirty();
        }
    } else if (!m_MiddleMousePanActive && active && m_ActiveHandle != HandleType::None && selectedLayer && !selectedLayer->locked) {
        const AffineTransform2D activeParentWorld = GetParentWorldTransform(m_Layers, *selectedLayer);
        const AffineTransform2D activeParentInverse = Inverse(activeParentWorld);
        const ImVec2 activeMouseLocal = TransformPoint(activeParentInverse, mouseWorld);
        const ImVec2 activeLocalCenter = LayerLocalCenter(*selectedLayer);
        const float baseWidth = std::max(1.0f, LayerBaseWidth(*selectedLayer));
        const float baseHeight = std::max(1.0f, LayerBaseHeight(*selectedLayer));
        bool changed = false;

        if (m_ActiveHandle == HandleType::Move) {
            const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            const ImVec2 worldDelta(
                delta.x / std::max(0.001f, m_ViewZoom),
                delta.y / std::max(0.001f, m_ViewZoom));
            const ImVec2 startWorldTopLeft = TransformPoint(activeParentWorld, ImVec2(m_StartX, m_StartY));
            float targetWorldX = startWorldTopLeft.x + worldDelta.x;
            float targetWorldY = startWorldTopLeft.y + worldDelta.y;
            applyMoveSnapping(*selectedLayer, activeParentInverse, targetWorldX, targetWorldY);
            const ImVec2 targetLocal = TransformPoint(activeParentInverse, ImVec2(targetWorldX, targetWorldY));
            if (std::abs(targetLocal.x - selectedLayer->x) > 0.0001f || std::abs(targetLocal.y - selectedLayer->y) > 0.0001f) {
                selectedLayer->x = targetLocal.x;
                selectedLayer->y = targetLocal.y;
                changed = true;
            }
        } else if (m_ActiveHandle == HandleType::Rotate) {
            float targetRotation = std::atan2(activeMouseLocal.y - activeLocalCenter.y, activeMouseLocal.x - activeLocalCenter.x) - m_StartMouseAngle;
            if (m_SnapEnabled && m_RotateSnapStep > 0.0f) {
                float degrees = RadiansToDegrees(targetRotation);
                degrees = std::round(degrees / m_RotateSnapStep) * m_RotateSnapStep;
                targetRotation = DegreesToRadians(degrees);
            }
            if (std::abs(targetRotation - selectedLayer->rotation) > 0.0001f) {
                selectedLayer->rotation = targetRotation;
                changed = true;
            }
        } else {
            const ImVec2 anchor(m_ResizeAnchorX, m_ResizeAnchorY);
            const ImVec2 axisX(std::cos(m_StartRotation), std::sin(m_StartRotation));
            const ImVec2 axisY(-std::sin(m_StartRotation), std::cos(m_StartRotation));
            const ImVec2 delta(activeMouseLocal.x - anchor.x, activeMouseLocal.y - anchor.y);
            const float alongX = delta.x * axisX.x + delta.y * axisX.y;
            const float alongY = delta.x * axisY.x + delta.y * axisY.y;
            const bool preserveAspect = selectedLayer->preserveAspectRatio;
            const float aspectRatio = std::max(0.0001f, m_StartWidth / std::max(1.0f, m_StartHeight));

            float newWidth = m_StartWidth;
            float newHeight = m_StartHeight;
            float signX = 0.0f;
            float signY = 0.0f;

            if (preserveAspect) {
                auto applyCornerWithAspect = [&](const bool dragLeft, const bool dragTop) {
                    signX = dragLeft ? -1.0f : 1.0f;
                    signY = dragTop ? -1.0f : 1.0f;

                    const float widthByX = std::max(1.0f, std::abs(alongX));
                    const float heightByX = std::max(1.0f, widthByX / aspectRatio);
                    const float heightByY = std::max(1.0f, std::abs(alongY));
                    const float widthByY = std::max(1.0f, heightByY * aspectRatio);

                    const float errorByX = std::abs(std::abs(alongY) - heightByX);
                    const float errorByY = std::abs(std::abs(alongX) - widthByY);
                    if (errorByX <= errorByY) {
                        newWidth = widthByX;
                        newHeight = heightByX;
                    } else {
                        newWidth = widthByY;
                        newHeight = heightByY;
                    }
                };

                switch (m_ActiveHandle) {
                case HandleType::ResizeTopLeft:
                    applyCornerWithAspect(true, true);
                    break;
                case HandleType::ResizeTopRight:
                    applyCornerWithAspect(false, true);
                    break;
                case HandleType::ResizeBottomRight:
                    applyCornerWithAspect(false, false);
                    break;
                case HandleType::ResizeBottomLeft:
                    applyCornerWithAspect(true, false);
                    break;
                case HandleType::ResizeLeft:
                    signX = -1.0f;
                    signY = 0.0f;
                    newWidth = std::max(1.0f, -alongX);
                    newHeight = std::max(1.0f, newWidth / aspectRatio);
                    break;
                case HandleType::ResizeRight:
                    signX = 1.0f;
                    signY = 0.0f;
                    newWidth = std::max(1.0f, alongX);
                    newHeight = std::max(1.0f, newWidth / aspectRatio);
                    break;
                case HandleType::ResizeTop:
                    signX = 0.0f;
                    signY = -1.0f;
                    newHeight = std::max(1.0f, -alongY);
                    newWidth = std::max(1.0f, newHeight * aspectRatio);
                    break;
                case HandleType::ResizeBottom:
                    signX = 0.0f;
                    signY = 1.0f;
                    newHeight = std::max(1.0f, alongY);
                    newWidth = std::max(1.0f, newHeight * aspectRatio);
                    break;
                default:
                    break;
                }
            } else {
                switch (m_ActiveHandle) {
                case HandleType::ResizeTopLeft:
                    signX = -1.0f;
                    signY = -1.0f;
                    newWidth = std::max(1.0f, -alongX);
                    newHeight = std::max(1.0f, -alongY);
                    break;
                case HandleType::ResizeTopRight:
                    signX = 1.0f;
                    signY = -1.0f;
                    newWidth = std::max(1.0f, alongX);
                    newHeight = std::max(1.0f, -alongY);
                    break;
                case HandleType::ResizeBottomRight:
                    signX = 1.0f;
                    signY = 1.0f;
                    newWidth = std::max(1.0f, alongX);
                    newHeight = std::max(1.0f, alongY);
                    break;
                case HandleType::ResizeBottomLeft:
                    signX = -1.0f;
                    signY = 1.0f;
                    newWidth = std::max(1.0f, -alongX);
                    newHeight = std::max(1.0f, alongY);
                    break;
                case HandleType::ResizeLeft:
                    signX = -1.0f;
                    newWidth = std::max(1.0f, -alongX);
                    break;
                case HandleType::ResizeRight:
                    signX = 1.0f;
                    newWidth = std::max(1.0f, alongX);
                    break;
                case HandleType::ResizeTop:
                    signY = -1.0f;
                    newHeight = std::max(1.0f, -alongY);
                    break;
                case HandleType::ResizeBottom:
                    signY = 1.0f;
                    newHeight = std::max(1.0f, alongY);
                    break;
                default:
                    break;
                }
            }

            if (m_SnapEnabled && m_ScaleSnapStep > 0.0f) {
                newWidth = std::max(1.0f, std::round((newWidth / baseWidth) / m_ScaleSnapStep) * m_ScaleSnapStep * baseWidth);
                newHeight = std::max(1.0f, std::round((newHeight / baseHeight) / m_ScaleSnapStep) * m_ScaleSnapStep * baseHeight);
            }

            const ImVec2 newCenter(
                anchor.x + axisX.x * signX * newWidth * 0.5f + axisY.x * signY * newHeight * 0.5f,
                anchor.y + axisX.y * signX * newWidth * 0.5f + axisY.y * signY * newHeight * 0.5f);
            const float targetScaleX = std::max(0.01f, newWidth / baseWidth);
            const float targetScaleY = std::max(0.01f, newHeight / baseHeight);
            const float targetX = newCenter.x - newWidth * 0.5f;
            const float targetY = newCenter.y - newHeight * 0.5f;

            if (std::abs(targetScaleX - selectedLayer->scaleX) > 0.0001f ||
                std::abs(targetScaleY - selectedLayer->scaleY) > 0.0001f ||
                std::abs(targetX - selectedLayer->x) > 0.0001f ||
                std::abs(targetY - selectedLayer->y) > 0.0001f) {
                selectedLayer->scaleX = targetScaleX;
                selectedLayer->scaleY = targetScaleY;
                selectedLayer->x = targetX;
                selectedLayer->y = targetY;
                if (selectedLayer->kind == LayerKind::Text) {
                    RegenerateGeneratedLayerTexture(*selectedLayer);
                }
                changed = true;
            }
        }

        if (changed) {
            MarkDocumentDirty();
        }
    }

    for (const SnapGuideLine& guide : snapGuides) {
        drawList->AddLine(worldToScreen(guide.a), worldToScreen(guide.b), guide.color, 1.5f);
    }

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_ActiveHandle = HandleType::None;
        m_ActiveExportHandle = ExportHandleType::None;
    }

    if (!m_MiddleMousePanActive && hovered && m_RightMousePressedOnCanvas && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        const float dragDistance = std::hypot(mousePos.x - m_RightMousePressX, mousePos.y - m_RightMousePressY);
        if (dragDistance > kCanvasContextDragThreshold) {
            m_ViewPanX += ImGui::GetIO().MouseDelta.x;
            m_ViewPanY += ImGui::GetIO().MouseDelta.y;
            MarkStageDirty();
        }
    }

    if (!m_MiddleMousePanActive && m_RightMousePressedOnCanvas && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        const float dragDistance = std::hypot(mousePos.x - m_RightMousePressX, mousePos.y - m_RightMousePressY);
        if (hovered && dragDistance <= kCanvasContextDragThreshold) {
            CompositeLayer* hitLayer = FindTopMostVisibleLayerAtWorldPoint(m_Layers, mouseWorld.x, mouseWorld.y);
            CompositeLayer* currentSelected = GetSelectedLayer();
            if (currentSelected != nullptr && hitLayer != nullptr && hitLayer->id == currentSelected->id) {
                ImGui::OpenPopup("CompositeCanvasLayerContext");
            } else {
                ImGui::OpenPopup("CompositeCanvasContext");
            }
        }
        m_RightMousePressedOnCanvas = false;
    } else if (m_MiddleMousePanActive) {
        m_RightMousePressedOnCanvas = false;
    }

    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        m_ViewZoom = std::clamp(m_ViewZoom * (1.0f + ImGui::GetIO().MouseWheel * 0.1f), 0.05f, 32.0f);
        MarkStageDirty();
    }

    selectedLayer = GetSelectedLayer();
    if (hovered && ImGui::IsKeyPressed(ImGuiKey_Delete) && selectedLayer && !selectedLayer->locked) {
        RemoveSelectedLayers();
    }

    if ((hovered || m_CanvasFocused) && ImGui::IsKeyPressed(ImGuiKey_T) && !ImGui::GetIO().WantTextInput) {
        if (m_SnapEnabled) {
            ApplySnapModePreset(CompositeSnapModePreset::Off);
        } else {
            ApplySnapModePreset(CompositeSnapModePreset::Full);
        }
        MarkDocumentDirty();
    }

    if (ImGui::BeginPopup("CompositeCanvasLayerContext")) {
        RenderLayerContextMenu(GetSelectedLayer());
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("CompositeCanvasContext")) {
        RenderCanvasContextMenu();
        ImGui::EndPopup();
    }
}

