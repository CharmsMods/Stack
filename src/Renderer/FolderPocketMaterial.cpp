#include "FolderPocketMaterial.h"
#include "FolderPocketMaterialShaders.h"
#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Stack::Renderer {
namespace {
struct DrawParameters {
    unsigned int program;
    ImVec4 displayRect;
    ImVec2 sampleStep;
    ImVec4 topTint, bottomTint;
    float transmission;
    ImVec2 gradientUv;
};
bool Finite(ImVec2 value) { return std::isfinite(value.x) && std::isfinite(value.y); }
bool Finite(ImVec4 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z) && std::isfinite(value.w);
}
}

bool FolderPocketMaterial::EnsureProgram() {
    if (m_Program) return true;
    if (m_Failed) return false;
    m_Program = GLHelpers::CreateShaderProgram(FolderPocketMaterialShaders::Vertex,
        FolderPocketMaterialShaders::Fragment, &m_Error);
    m_Failed = m_Program == 0;
    return !m_Failed;
}

bool FolderPocketMaterial::Begin(ImDrawList* draw, ImVec2 uvStep, ImVec4 topTint,
    ImVec4 bottomTint, float transmission, ImVec2 gradientUv) {
    if (!draw || !Finite(uvStep) || !Finite(topTint) || !Finite(bottomTint) ||
        !Finite(gradientUv) || !std::isfinite(transmission)) return false;
    const auto* viewport = ImGui::GetWindowViewport();
    if (!viewport || viewport->Size.x <= 0 || viewport->Size.y <= 0 || !EnsureProgram()) return false;
    DrawParameters parameters{m_Program,
        ImVec4(viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y),
        ImVec2(std::abs(uvStep.x), std::abs(uvStep.y)), topTint, bottomTint,
        std::clamp(transmission, 0.f, .3f), gradientUv};
    // ImGui copies this small payload into its own frame draw storage. No owner
    // pointer, source texture cache, or callback lifetime survives the frame.
    draw->AddCallback(Bind, &parameters, sizeof(parameters));
    return true;
}

void FolderPocketMaterial::Bind(const ImDrawList*, const ImDrawCmd* command) {
    // Other callbacks may have odd-sized payloads in ImGui's byte buffer.
    // Copy rather than assuming that this payload begins at aligned storage.
    DrawParameters p;
    std::memcpy(&p, command->UserCallbackData, sizeof(p));
    glUseProgram(p.program);
    glUniform4f(1, p.displayRect.x, p.displayRect.y, p.displayRect.z, p.displayRect.w);
    glUniform2f(2, p.sampleStep.x, p.sampleStep.y);
    glUniform4f(3, p.topTint.x, p.topTint.y, p.topTint.z, p.topTint.w);
    glUniform4f(4, p.bottomTint.x, p.bottomTint.y, p.bottomTint.z, p.bottomTint.w);
    glUniform1f(5, p.transmission);
    glUniform2f(6, p.gradientUv.x, p.gradientUv.y);
    // Respect the borrowed texture's actual filter/completeness. ImGui's
    // ordinary linear sampler would disable its existing mip-chain filtering.
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);
}

void FolderPocketMaterial::End(ImDrawList* draw) {
    if (draw) draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

void FolderPocketMaterial::Shutdown() {
    if (m_Program) glDeleteProgram(m_Program);
    m_Program = 0;
    m_Failed = false;
    m_Error.clear();
}

} // namespace Stack::Renderer
