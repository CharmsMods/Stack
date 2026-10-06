#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabImageMapping.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"

#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>

#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif

namespace {
using Stack::RawRecipe::RawGradientMask;
using Stack::RawRecipe::RawGradientShape;

using CanvasMapping = Stack::Editor::RawLabInternal::RawLabImageMapping;

std::string NewGradientId() {
    static std::atomic<unsigned int> serial {0};
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return "gradient-" + std::to_string(now) + "-" +
        std::to_string(serial.fetch_add(1, std::memory_order_relaxed));
}

ImVec2 Point(const ImVec2& minimum, const ImVec2& size,
    const CanvasMapping& mapping, float u, float v) {
    const auto point = mapping.Canvas({u,v});
    return {minimum.x+size.x*point.x,minimum.y+size.y*point.y};
}

ImVec2 ImagePoint(const ImVec2& minimum, const ImVec2& size,
    const CanvasMapping& mapping, const RawGradientMask& mask, float x, float y) {
    return Point(minimum, size, mapping,
        mask.centerU + x / mapping.sourceAspect, mask.centerV + y);
}

float Distance(const ImVec2& a, const ImVec2& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

ImVec2 HandlePoint(const RawGradientMask& mask, int handle,
    const ImVec2& minimum, const ImVec2& size, const CanvasMapping& mapping) {
    if (handle == 0) return Point(minimum, size, mapping, mask.centerU, mask.centerV);
    const float cs = std::cos(mask.angleRadians), sn = std::sin(mask.angleRadians);
    if (mask.shape == RawGradientShape::Linear) {
        const float d = handle == 1 ? mask.lowBoundary :
            handle == 2 ? mask.highBoundary : 0.0f;
        if (handle == 3) return ImagePoint(minimum, size, mapping, mask, -sn * 0.25f, cs * 0.25f);
        const float tangent = handle == 1 ? 0.14f : -0.14f;
        return ImagePoint(minimum, size, mapping, mask,
            cs * d - sn * tangent, sn * d + cs * tangent);
    }
    const float radius = handle == 3 ? mask.radiusX * std::max(mask.innerScale, 0.08f) :
        handle == 4 ? mask.radiusX + 25.f / std::max(1.f,Distance(
            ImagePoint(minimum,size,mapping,mask,0,0),ImagePoint(minimum,size,mapping,mask,1,0))) : mask.radiusX;
    if (handle == 2) return ImagePoint(minimum, size, mapping, mask,
        -sn * mask.radiusY, cs * mask.radiusY);
    return ImagePoint(minimum, size, mapping, mask, cs * radius, sn * radius);
}

void DrawGuides(ImDrawList* draw, const ImVec2& minimum, const ImVec2& maximum,
    const RawGradientMask& mask, const CanvasMapping& mapping) {
    const ImVec2 size(maximum.x - minimum.x, maximum.y - minimum.y);
    const ImU32 lineColor = IM_COL32(255, 255, 255, 205);
    const ImU32 innerColor = IM_COL32(210, 235, 255, 170);
    draw->PushClipRect(minimum, maximum, true);
    if (mask.shape == RawGradientShape::Linear) {
        const float cs = std::cos(mask.angleRadians), sn = std::sin(mask.angleRadians);
        const float extent = std::max(size.x, size.y) / std::max(size.y, 1.0f) * 2.0f;
        for (int index = 0; index < 3; ++index) {
            const float d = index == 0 ? 0.0f :
                index == 1 ? mask.lowBoundary : mask.highBoundary;
            const ImVec2 a = ImagePoint(minimum, size, mapping, mask,
                cs * d - sn * extent, sn * d + cs * extent);
            const ImVec2 b = ImagePoint(minimum, size, mapping, mask,
                cs * d + sn * extent, sn * d - cs * extent);
            draw->AddLine(a, b, index == 0 ? innerColor : lineColor,
                index == 0 ? 1.2f : 1.6f);
        }
    } else {
        const float cs = std::cos(mask.angleRadians), sn = std::sin(mask.angleRadians);
        for (int ring = 0; ring < 2; ++ring) {
            const float scale = ring == 0 ? 1.0f : mask.innerScale;
            ImVec2 prior;
            for (int step = 0; step <= 64; ++step) {
                const float theta = 6.28318530718f * step / 64.0f;
                const float x = std::cos(theta) * mask.radiusX * scale;
                const float y = std::sin(theta) * mask.radiusY * scale;
                const ImVec2 current = ImagePoint(minimum, size, mapping, mask,
                    x * cs - y * sn, x * sn + y * cs);
                if (step != 0) draw->AddLine(prior, current,
                    ring == 0 ? lineColor : innerColor, 1.5f);
                prior = current;
            }
        }
    }
    const int handleCount = mask.shape == RawGradientShape::Linear ? 4 : 5;
    for (int handle = 0; handle < handleCount; ++handle) {
        const ImVec2 center = HandlePoint(mask, handle, minimum, size, mapping);
        draw->AddCircleFilled(center, 5.0f, IM_COL32(32, 36, 42, 240));
        draw->AddCircle(center, 5.0f, lineColor, 16, 1.5f);
    }
    draw->PopClipRect();
}

void DrawWeightOverlay(ImDrawList* draw, const ImVec2& minimum,
    const ImVec2& maximum, const RawGradientMask& mask,
    const CanvasMapping& mapping) {
    const ImVec2 size(maximum.x - minimum.x, maximum.y - minimum.y);
    const float cell = std::max(8.0f, std::max(size.x, size.y) / 160.0f);
    const float cs = std::cos(mask.angleRadians);
    const float sn = std::sin(mask.angleRadians);
    draw->PushClipRect(minimum, maximum, true);
    for (float y = minimum.y; y < maximum.y; y += cell) {
        const float bottom = std::min(y + cell, maximum.y);
        for (float x = minimum.x; x < maximum.x; x += cell) {
            const float right = std::min(x + cell, maximum.x);
            const auto color = [&](float px, float py) {
                const auto point = mapping.Local({(px-minimum.x)/size.x,(py-minimum.y)/size.y});
                const float u = point.x, v = point.y;
                const float weight = Stack::RawRecipe::EvaluateRawGradientMask(
                    mask, u, v, mapping.sourceAspect, cs, sn);
                return IM_COL32(245, 64, 65,
                    static_cast<int>(std::round(weight * 100.0f)));
            };
            draw->AddRectFilledMultiColor({x, y}, {right, bottom},
                color(x, y), color(right, y), color(right, bottom), color(x, bottom));
        }
    }
    draw->PopClipRect();
}
} // namespace

bool EditorModule::DrawRawWorkspaceLabGradientOverlay(
    const ImVec2& minimum, const ImVec2& maximum,
    const Stack::RawRecipe::RawGradientMask& mask,
    const std::array<double,9>& canvasToLocal,
    float sourceAspect) {
    const int width = std::max(1, static_cast<int>(std::lround(maximum.x - minimum.x)));
    const int height = std::max(1, static_cast<int>(std::lround(maximum.y - minimum.y)));
    if (!m_RawGradientOverlayProgram) {
        static const char* vertex = R"(
            #version 330 core
            out vec2 vUV;
            void main() {
                vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
                vUV = p;
                gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
            }
        )";
        static const char* fragment = R"(
            #version 330 core
            in vec2 vUV;
            out vec4 FragColor;
            uniform vec3 uCanvasU;
            uniform vec3 uCanvasV;
            uniform float uAspect;
            uniform vec2 uCenter;
            uniform float uAngle;
            uniform vec2 uLinearBounds;
            uniform vec2 uRadii;
            uniform float uInnerScale;
            uniform int uShape;
            uniform int uInvert;
            void main() {
                vec3 canvas = vec3(vUV.x,1.0-vUV.y,1.0);
                vec2 uv = vec2(dot(uCanvasU,canvas),dot(uCanvasV,canvas));
                vec2 p = vec2((uv.x - uCenter.x) * uAspect, uv.y - uCenter.y);
                float cs = cos(uAngle), sn = sin(uAngle);
                float t;
                if (uShape == 0) {
                    float d = dot(p, vec2(cs, sn));
                    t = clamp((d - uLinearBounds.x) /
                        max(uLinearBounds.y - uLinearBounds.x, 0.0001), 0.0, 1.0);
                } else {
                    vec2 q = vec2(p.x * cs + p.y * sn, -p.x * sn + p.y * cs);
                    float radius = length(q / max(uRadii, vec2(0.0001)));
                    t = clamp((radius - uInnerScale) /
                        max(1.0 - uInnerScale, 0.0001), 0.0, 1.0);
                }
                float weight = 1.0 - t * t * (3.0 - 2.0 * t);
                if (uInvert != 0) weight = 1.0 - weight;
                FragColor = vec4(245.0 / 255.0, 64.0 / 255.0,
                    65.0 / 255.0, weight * (100.0 / 255.0));
            }
        )";
        m_RawGradientOverlayProgram = GLHelpers::CreateShaderProgram(vertex, fragment);
    }
    if (!m_RawGradientOverlayProgram) return false;
    if (!m_RawGradientOverlayVertexArray)
        glGenVertexArrays(1, &m_RawGradientOverlayVertexArray);
    if (!m_RawGradientOverlayVertexArray) return false;

    if (!m_RawGradientOverlayTexture ||
        m_RawGradientOverlayWidth != width || m_RawGradientOverlayHeight != height) {
        if (m_RawGradientOverlayTexture)
            glDeleteTextures(1, &m_RawGradientOverlayTexture);
        m_RawGradientOverlayTexture = GLHelpers::CreateTextureFromData(
            nullptr, width, height, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
        m_RawGradientOverlayWidth = width;
        m_RawGradientOverlayHeight = height;
        m_RawGradientOverlayKey = static_cast<std::size_t>(-1);
    }
    if (!m_RawGradientOverlayTexture) return false;

    std::size_t key = std::hash<int>{}(width);
    const auto mix = [&](auto value) {
        using T = decltype(value);
        key ^= std::hash<T>{}(value) + 0x9e3779b97f4a7c15ull +
            (key << 6) + (key >> 2);
    };
    mix(height); mix(mask.id); mix(static_cast<int>(mask.shape));
    mix(mask.inverted); mix(mask.centerU); mix(mask.centerV);
    mix(mask.angleRadians); mix(mask.lowBoundary); mix(mask.highBoundary);
    mix(mask.radiusX); mix(mask.radiusY); mix(mask.innerScale);
    for (double value : canvasToLocal) mix(value);
    mix(sourceAspect);
    if (key != m_RawGradientOverlayKey) {
        const Stack::Renderer::GLState::FramebufferState savedFramebuffer(true);
        GLint previousProgram = 0, previousVao = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
        const bool blendEnabled = glIsEnabled(GL_BLEND) == GL_TRUE;
        const bool scissorEnabled = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
        const bool depthEnabled = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
        const bool cullEnabled = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
        const bool stencilEnabled = glIsEnabled(GL_STENCIL_TEST) == GL_TRUE;
        const unsigned int framebuffer = GLHelpers::CreateFBO(m_RawGradientOverlayTexture);
        if (!framebuffer) return false;
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, width, height);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_STENCIL_TEST);
        glUseProgram(m_RawGradientOverlayProgram);
        glUniform3f(glGetUniformLocation(m_RawGradientOverlayProgram,"uCanvasU"),
            float(canvasToLocal[0]),float(canvasToLocal[1]),float(canvasToLocal[2]));
        glUniform3f(glGetUniformLocation(m_RawGradientOverlayProgram,"uCanvasV"),
            float(canvasToLocal[3]),float(canvasToLocal[4]),float(canvasToLocal[5]));
        glUniform1f(glGetUniformLocation(m_RawGradientOverlayProgram, "uAspect"),
            sourceAspect);
        glUniform2f(glGetUniformLocation(m_RawGradientOverlayProgram, "uCenter"),
            mask.centerU, mask.centerV);
        glUniform1f(glGetUniformLocation(m_RawGradientOverlayProgram, "uAngle"),
            mask.angleRadians);
        glUniform2f(glGetUniformLocation(m_RawGradientOverlayProgram, "uLinearBounds"),
            mask.lowBoundary, mask.highBoundary);
        glUniform2f(glGetUniformLocation(m_RawGradientOverlayProgram, "uRadii"),
            mask.radiusX, mask.radiusY);
        glUniform1f(glGetUniformLocation(m_RawGradientOverlayProgram, "uInnerScale"),
            mask.innerScale);
        glUniform1i(glGetUniformLocation(m_RawGradientOverlayProgram, "uShape"),
            mask.shape == Stack::RawRecipe::RawGradientShape::Linear ? 0 : 1);
        glUniform1i(glGetUniformLocation(m_RawGradientOverlayProgram, "uInvert"),
            mask.inverted ? 1 : 0);
        glBindVertexArray(m_RawGradientOverlayVertexArray);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(static_cast<unsigned int>(previousVao));
        glUseProgram(static_cast<unsigned int>(previousProgram));
        if (blendEnabled) glEnable(GL_BLEND);
        if (scissorEnabled) glEnable(GL_SCISSOR_TEST);
        if (depthEnabled) glEnable(GL_DEPTH_TEST);
        if (cullEnabled) glEnable(GL_CULL_FACE);
        if (stencilEnabled) glEnable(GL_STENCIL_TEST);
        savedFramebuffer.Restore(true);
        glDeleteFramebuffers(1, &framebuffer);
        m_RawGradientOverlayKey = key;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(minimum, maximum, true);
    draw->AddImage((ImTextureID)(intptr_t)m_RawGradientOverlayTexture,
        minimum, maximum, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    draw->PopClipRect();
    return true;
}

void EditorModule::RenderRawWorkspaceLabGradientDots(
    const Stack::RawWorkspace::SourceRecord* source,
    const ImVec2& minimum, float width) {
    const bool ev = m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones;
    if (!ev && m_RawWorkspaceLabUi.activeTool != RawLabTool::Tone) return;
    RawWorkspaceEditContext context;
    if (!BeginRawWorkspaceEditContext(source, context) || !context.canEdit) return;
    if (!ev && !context.rawAdjustmentLayerId.empty()) return;
    if (context.resolvedMode == Stack::RawWorkspace::RawProjectMode::ManagedDecomposed)
        return;
    auto& ui = m_RawWorkspaceLabUi;
    const int count = ev ? static_cast<int>(context.recipe.evGradients.size())
        : static_cast<int>(context.recipe.toneGradients.size());
    int& selected = ev ? ui.selectedEvGradient : ui.selectedToneGradient;
    int& hovered = ev ? ui.hoveredEvGradient : ui.hoveredToneGradient;
    hovered = -1;
    const float diameter = 18.0f, gap = 8.0f;
    const float groupWidth = count * diameter + std::max(0, count - 1) * gap;
    const float x0 = minimum.x + std::max(0.0f, (width - groupWidth) * 0.5f);
    const ImVec2 saved = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i + (ev ? 10100 : 10200));
        const ImVec2 center(x0 + diameter * 0.5f + i * (diameter + gap),
            minimum.y + 17.0f);
        ImGui::SetCursorScreenPos({center.x - 11.0f, center.y - 11.0f});
        ImGui::InvisibleButton("##gradientDot", {22.0f, 22.0f});
        if (ImGui::IsItemHovered()) hovered = i;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            selected = i;
            ui.gradientDrawShape = -1;
            ui.selectedZonePoint = -1;
            ui.selectedTonePoint = -1;
            if (ev) { ui.zonesCurveGraph = {}; ui.zonesTargetedView = false; }
            else for (auto& graph : ui.toneCurveGraphs) graph = {};
        }
        auto& mask = ev ? context.recipe.evGradients[static_cast<std::size_t>(i)].mask
            : context.recipe.toneGradients[static_cast<std::size_t>(i)].mask;
        draw->AddCircleFilled(center, 8.0f,
            mask.enabled ? IM_COL32(205, 216, 224, 235) : IM_COL32(93, 100, 108, 180), 24);
        if (selected == i) draw->AddCircle(center, 10.0f,
            IM_COL32(255, 255, 255, 250), 24, 1.8f);
        bool deleted = false;
        if (ImGui::BeginPopupContextItem("##gradientActions")) {
            if (ImGui::MenuItem("Invert")) { mask.inverted = !mask.inverted; changed = true; }
            if (ImGui::MenuItem(mask.enabled ? "Hide adjustment" : "Show adjustment")) {
                mask.enabled = !mask.enabled; changed = true;
            }
            if (ImGui::MenuItem("Duplicate")) {
                if (ev && context.recipe.evGradients.size() < Stack::RawRecipe::kMaxRawGradientAdjustments) {
                    auto copy = context.recipe.evGradients[static_cast<std::size_t>(i)];
                    copy.mask.id = NewGradientId();
                    context.recipe.evGradients.push_back(std::move(copy));
                    selected = static_cast<int>(context.recipe.evGradients.size()) - 1;
                    changed = true;
                } else if (!ev && context.recipe.toneGradients.size() < Stack::RawRecipe::kMaxRawGradientAdjustments) {
                    auto copy = context.recipe.toneGradients[static_cast<std::size_t>(i)];
                    copy.mask.id = NewGradientId();
                    context.recipe.toneGradients.push_back(std::move(copy));
                    selected = static_cast<int>(context.recipe.toneGradients.size()) - 1;
                    changed = true;
                }
            }
            if (ImGui::MenuItem("Delete")) {
                if (ev) context.recipe.evGradients.erase(context.recipe.evGradients.begin() + i);
                else context.recipe.toneGradients.erase(context.recipe.toneGradients.begin() + i);
                selected = -1;
                changed = true;
                deleted = true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
        if (deleted) break;
    }
    ImGui::SetCursorScreenPos(saved);
    if (changed) CommitRawWorkspaceEditContext(context, true, false);
}

void EditorModule::RenderRawWorkspaceLabGradientImage(
    const Stack::RawWorkspace::SourceRecord* source,
    const ImVec2& minimum, const ImVec2& maximum) {
    if (!m_SelectedRawAdjustmentLayer.empty() &&
        (m_RawWorkspaceLabUi.activeTool != RawLabTool::Zones || m_EditingRawLayerMask.has_value())) {
        RawWorkspaceEditContext layerContext;
        if (BeginRawWorkspaceEditContext(source, layerContext) && layerContext.canEdit)
            RenderRawLayerMaskHandles(layerContext, minimum, maximum);
        return;
    }
    const bool ev = m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones;
    if (!ev && m_RawWorkspaceLabUi.activeTool != RawLabTool::Tone) return;
    RawWorkspaceEditContext context;
    if (!BeginRawWorkspaceEditContext(source, context) || !context.canEdit) return;
    if (!ev && !context.rawAdjustmentLayerId.empty()) return;
    if (context.resolvedMode == Stack::RawWorkspace::RawProjectMode::ManagedDecomposed)
        return;
    auto& ui = m_RawWorkspaceLabUi;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ui.gradientDrawShape = -1;
        ui.gradientDragHandle = -1;
        CommitRawWorkspaceEditContext(context, false, false);
    }
    int& selected = ev ? ui.selectedEvGradient : ui.selectedToneGradient;
    const int hovered = ev ? ui.hoveredEvGradient : ui.hoveredToneGradient;
    const int count = ev ? static_cast<int>(context.recipe.evGradients.size())
        : static_cast<int>(context.recipe.toneGradients.size());
    if (selected >= count) selected = -1;
    // Global curves do not have gradient coordinates to resolve. This path also
    // runs after ordinary edits, when no gradient is selected or being created.
    if (selected < 0 && (hovered < 0 || hovered >= count) && ui.gradientDrawShape < 0) return;
    const ImVec2 size(maximum.x - minimum.x, maximum.y - minimum.y);
    if (size.x <= 1.0f || size.y <= 1.0f) return;
    const ImRect imageRect(minimum, maximum);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool inImage = imageRect.Contains(mouse) && ImGui::IsWindowHovered();
    if (!context.rawAdjustmentLayerId.empty() && m_GraphOutputDescriptionRenderRevision != m_RenderRevision)
        BuildGraphSnapshot();
    const auto mappingFor = [&](const RawGradientMask* mask) -> std::optional<CanvasMapping> {
        if (!context.rawAdjustmentLayerId.empty()) {
            const auto* layer = Stack::Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(),context.rawAdjustmentLayerId);
            const auto* operation = SelectedRawOperation();
            if (!layer || !operation) return std::nullopt;
            const auto* generator = mask ? Stack::Project::FindRawCoverageOwner(*layer,operation->id,"gradient:"+mask->id,mask->id) : nullptr;
            if (mask && !generator && ui.gradientDragHandle != 100) return std::nullopt;
            const auto key = "raw/"+layer->id+"/"+EditorNodeGraph::GraphOutputIdentity(
                generator ? generator->id : operation->id,generator ? "maskOut" : "inputImageOut");
            const auto found = m_GraphOutputDescriptions.find(key);
            if (found == m_GraphOutputDescriptions.end() || found->second.descriptor.spatial.state != Stack::NodeMath::KnowledgeState::Known ||
                m_LastGraphOutputSemanticDescriptor.spatial.state != Stack::NodeMath::KnowledgeState::Known) return std::nullopt;
            return CanvasMapping::FromSpatial(found->second.descriptor.spatial.value,
                m_LastGraphOutputSemanticDescriptor.spatial.value,context.recipe.cropRotation);
        }
        Stack::NodeMath::SpatialDescriptor spatial;
        const auto& crop = context.recipe.cropRotation;
        spatial.fullWindow.width = std::max(1,int(std::lround(size.x/(crop.cropEnabled ? crop.cropWidth : 1))));
        spatial.fullWindow.height = std::max(1,int(std::lround(size.y/(crop.cropEnabled ? crop.cropHeight : 1))));
        return CanvasMapping::FromSpatial(spatial,spatial,crop);
    };
    const int mappingIndex = selected >= 0 ? selected : hovered;
    const RawGradientMask* editingMask = mappingIndex >= 0 && mappingIndex < count && ui.gradientDrawShape < 0
        ? (ev ? &context.recipe.evGradients[std::size_t(mappingIndex)].mask : &context.recipe.toneGradients[std::size_t(mappingIndex)].mask) : nullptr;
    const auto resolvedMapping = mappingFor(editingMask);
    if (!resolvedMapping) {
        ImGui::GetWindowDrawList()->AddText({minimum.x+12,minimum.y+12},IM_COL32(230,230,230,255),
            "The selected gradient's image coordinates are unavailable.");
        return;
    }
    const auto mapping = *resolvedMapping;
    const auto localPoint = [&](ImVec2 point) { return mapping.Local({(point.x-minimum.x)/size.x,(point.y-minimum.y)/size.y}); };
    const auto point = localPoint(mouse);
    const float u = point.x, v = point.y;
    const float aspect = mapping.sourceAspect;
    bool changed = false;
    if (ui.gradientDrawShape >= 0 && inImage &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        count < static_cast<int>(Stack::RawRecipe::kMaxRawGradientAdjustments)) {
        RawGradientMask mask;
        mask.id = NewGradientId();
        mask.shape = ui.gradientDrawShape == 0 ? RawGradientShape::Linear : RawGradientShape::Radial;
        mask.centerU = u;
        mask.centerV = v;
        if (ev) {
            Stack::RawRecipe::RawGradientEvAdjustment adjustment;
            adjustment.mask = mask;
            adjustment.curve = Stack::RawRecipe::DefaultLocalRangeRecipe();
            adjustment.curve.enabled = true;
            context.recipe.evGradients.push_back(std::move(adjustment));
            selected = static_cast<int>(context.recipe.evGradients.size()) - 1;
            ui.zonesCurveGraph = {};
            ui.selectedZonePoint = -1;
            ui.zonesTargetedView = false;
        } else {
            Stack::RawRecipe::RawGradientToneAdjustment adjustment;
            adjustment.mask = mask;
            adjustment.curveJson = Stack::RawRecipe::DefaultFinishToneJson();
            context.recipe.toneGradients.push_back(std::move(adjustment));
            selected = static_cast<int>(context.recipe.toneGradients.size()) - 1;
            for (auto& graph : ui.toneCurveGraphs) graph = {};
            ui.selectedTonePoint = -1;
        }
        ui.gradientDragHandle = 100;
        ui.gradientDragStart = mouse;
        ui.gradientDragOriginal = mask;
        changed = true;
    }
    RawGradientMask* selectedMask = selected >= 0 && selected <
        (ev ? static_cast<int>(context.recipe.evGradients.size()) :
            static_cast<int>(context.recipe.toneGradients.size()))
        ? (ev ? &context.recipe.evGradients[static_cast<std::size_t>(selected)].mask
            : &context.recipe.toneGradients[static_cast<std::size_t>(selected)].mask)
        : nullptr;
    if (selectedMask && ui.gradientDrawShape < 0 && inImage &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const int handles = selectedMask->shape == RawGradientShape::Linear ? 4 : 5;
        for (int handle = 0; handle < handles; ++handle) {
            if (Distance(mouse, HandlePoint(*selectedMask, handle, minimum, size, mapping)) < 12.0f) {
                ui.gradientDragHandle = handle;
                ui.gradientDragStart = mouse;
                ui.gradientDragOriginal = *selectedMask;
                break;
            }
        }
    }
    if (selectedMask && ui.gradientDragHandle >= 0 &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const RawGradientMask original = ui.gradientDragOriginal;
        const auto start = localPoint(ui.gradientDragStart);
        const float dx = (u-start.x)*aspect;
        const float dy = v-start.y;
        const float cs = std::cos(original.angleRadians), sn = std::sin(original.angleRadians);
        const float axial = dx * cs + dy * sn;
        switch (ui.gradientDragHandle) {
        case 0:
            selectedMask->centerU = original.centerU + dx / aspect;
            selectedMask->centerV = original.centerV + dy;
            break;
        case 1:
            if (selectedMask->shape == RawGradientShape::Linear)
                selectedMask->lowBoundary = std::min(original.lowBoundary + axial,
                    original.highBoundary - 0.005f);
            else selectedMask->radiusX = std::max(0.005f, original.radiusX + axial);
            break;
        case 2:
            if (selectedMask->shape == RawGradientShape::Linear)
                selectedMask->highBoundary = std::max(original.highBoundary + axial,
                    original.lowBoundary + 0.005f);
            else selectedMask->radiusY = std::max(0.005f,
                original.radiusY - dx * sn + dy * cs);
            break;
        case 3:
            if (selectedMask->shape == RawGradientShape::Linear) {
                selectedMask->angleRadians = std::atan2(v-original.centerV,
                    (u-original.centerU)*aspect) - 1.57079632679f;
            } else {
                selectedMask->innerScale = std::clamp(
                    original.innerScale + axial / std::max(original.radiusX, 0.005f),
                    0.0f, 0.995f);
            }
            break;
        case 4:
            {
                selectedMask->angleRadians = std::atan2(v-original.centerV,
                    (u-original.centerU)*aspect);
            }
            break;
        case 100:
            if (selectedMask->shape == RawGradientShape::Linear) {
                selectedMask->centerU = (u + original.centerU) * 0.5f;
                selectedMask->centerV = (v + original.centerV) * 0.5f;
                selectedMask->angleRadians = std::atan2(dy, dx);
                selectedMask->lowBoundary = -std::max(0.01f, std::hypot(dx, dy) * 0.5f);
                selectedMask->highBoundary = -selectedMask->lowBoundary;
            } else {
                selectedMask->radiusX = std::max(0.01f, std::abs(dx));
                selectedMask->radiusY = std::max(0.01f, std::abs(dy));
            }
            break;
        }
        selectedMask->centerU = std::clamp(selectedMask->centerU, -2.0f, 3.0f);
        selectedMask->centerV = std::clamp(selectedMask->centerV, -2.0f, 3.0f);
        selectedMask->radiusX = std::clamp(selectedMask->radiusX, 0.001f, 4.0f);
        selectedMask->radiusY = std::clamp(selectedMask->radiusY, 0.001f, 4.0f);
        selectedMask->lowBoundary = std::clamp(selectedMask->lowBoundary, -4.0f, 3.99f);
        selectedMask->highBoundary = std::clamp(selectedMask->highBoundary,
            selectedMask->lowBoundary + 0.001f, 4.0f);
        changed = true;
    }
    const bool released = ui.gradientDragHandle >= 0 &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    if (released) {
        ui.gradientDragHandle = -1;
        ui.gradientDrawShape = -1;
    }
    if (changed || released) CommitRawWorkspaceEditContext(context, changed,
        ui.gradientDragHandle >= 0);
    const int displayIndex = hovered >= 0 ? hovered : selected;
    const int finalCount = ev ? static_cast<int>(context.recipe.evGradients.size())
        : static_cast<int>(context.recipe.toneGradients.size());
    if (displayIndex < 0 || displayIndex >= finalCount) return;
    const RawGradientMask& displayMask = ev
        ? context.recipe.evGradients[static_cast<std::size_t>(displayIndex)].mask
        : context.recipe.toneGradients[static_cast<std::size_t>(displayIndex)].mask;
    const auto displayMapping = mappingFor(&displayMask);
    if (!displayMapping) return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (ui.gradientOverlayVisible || hovered >= 0 || ui.gradientDragHandle == 100) {
        if (!DrawRawWorkspaceLabGradientOverlay(
                minimum, maximum, displayMask, displayMapping->canvasToLocal,
                displayMapping->sourceAspect))
            DrawWeightOverlay(draw, minimum, maximum, displayMask, *displayMapping);
    }
    if (ui.gradientGuidesVisible && hovered < 0 && displayIndex == selected)
        DrawGuides(draw, minimum, maximum, displayMask, *displayMapping);
}
