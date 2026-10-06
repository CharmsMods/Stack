#include "Editor/EditorModule.h"
#include "Raw/MultiFrameHdr/Contracts.h"
#include "Renderer/GLHelpers.h"
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

void EditorModule::DrawMultiFrameRawDiagnosticOverlay(
    ImDrawList* drawList, const ImVec2& minimum, const ImVec2& maximum) {
    if (!IsMultiFrameRawProjectActive() || !m_Project->snapshot) return;
    const auto* activeSet = Stack::Project::FindSourceSet(
        *m_Project->snapshot, m_Project->snapshot->activeSourceSetId);
    const bool activeHdr = activeSet && activeSet->operationIntent ==
        Stack::Project::MultiFrameOperationIntent::RawBurstHdr;
    const ImRect imageRect(minimum, maximum);
    if (activeHdr && m_HdrDiagnosticView > 0 &&
        m_HdrAdoptedRawResult && m_HdrAdoptedRawResult->result) {
        const Raw::Hdr::Result& hdr = *m_HdrAdoptedRawResult->result;
        const std::size_t pixelCount = static_cast<std::size_t>(
            hdr.width * hdr.height);
        const bool rebuildOverlay =
            m_HdrDiagnosticOverlayTexture == 0 ||
            m_HdrDiagnosticOverlayView != m_HdrDiagnosticView ||
            m_HdrDiagnosticOverlayCacheKey != hdr.diagnostics.cacheKey ||
            m_HdrDiagnosticOverlayWidth != static_cast<int>(hdr.width) ||
            m_HdrDiagnosticOverlayHeight != static_cast<int>(hdr.height);
        if (rebuildOverlay && pixelCount > 0u &&
            hdr.width <= static_cast<std::uint64_t>(std::numeric_limits<int>::max()) &&
            hdr.height <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            std::vector<unsigned char> overlayPixels(pixelCount * 4u, 0u);
            const auto heatColor = [](float t, float alpha) {
                std::array<unsigned char, 4> color {};
                t = std::clamp(t, 0.0f, 1.0f);
                const float r = std::clamp(1.8f * t - 0.35f, 0.0f, 1.0f);
                const float g = std::clamp(
                    1.7f - std::abs(2.0f * t - 1.0f) * 1.7f,
                    0.0f,
                    1.0f);
                const float b = std::clamp(1.35f - 1.8f * t, 0.0f, 1.0f);
                color[0] = static_cast<unsigned char>(std::round(r * 255.0f));
                color[1] = static_cast<unsigned char>(std::round(g * 255.0f));
                color[2] = static_cast<unsigned char>(std::round(b * 255.0f));
                color[3] = static_cast<unsigned char>(std::round(alpha * 255.0f));
                return color;
            };
            static constexpr std::array<
                std::array<unsigned char, 4>, Raw::Hdr::kMaximumFrameCount>
                ownerPalette {{
                    {{ 51u, 209u, 255u, 173u }},
                    {{ 255u, 122u, 61u, 173u }},
                    {{ 133u, 235u, 92u, 173u }},
                    {{ 214u, 107u, 255u, 173u }},
                    {{ 255u, 209u, 71u, 173u }},
                    {{ 87u, 245u, 199u, 173u }},
                    {{ 255u, 102u, 173u, 173u }},
                    {{ 89u, 142u, 255u, 173u }},
                    {{ 255u, 151u, 122u, 173u }},
                    {{ 185u, 235u, 67u, 173u }},
                    {{ 177u, 136u, 255u, 173u }},
                    {{ 255u, 184u, 51u, 173u }},
                    {{ 102u, 255u, 162u, 173u }},
                    {{ 255u, 92u, 119u, 173u }},
                    {{ 80u, 196u, 255u, 173u }},
                    {{ 255u, 225u, 102u, 173u }},
                    {{ 153u, 102u, 255u, 173u }},
                    {{ 151u, 230u, 51u, 173u }},
                    {{ 61u, 235u, 224u, 173u }},
                    {{ 255u, 132u, 102u, 173u }}
                }};
            const float effectiveSampleDivisor = static_cast<float>(
                std::max<std::size_t>(2u, activeSet->frames.size()));
            for (std::size_t index = 0; index < pixelCount; ++index) {
                std::array<unsigned char, 4> color {};
                if (m_HdrDiagnosticView == 1 &&
                    index < hdr.mergeConfidence.size()) {
                    color = heatColor(hdr.mergeConfidence[index], 0.66f);
                } else if (m_HdrDiagnosticView == 2 &&
                           index < hdr.effectiveSampleCount.size()) {
                    color = heatColor(
                        hdr.effectiveSampleCount[index] /
                            effectiveSampleDivisor,
                        0.66f);
                } else if (m_HdrDiagnosticView == 3 &&
                           index < hdr.ownerFrame.size()) {
                    if (index < hdr.flags.size() &&
                        (hdr.flags[index] &
                         Raw::Hdr::ResultFlagColorCoherentRepair) != 0u) {
                        color = {{ 225u, 255u, 255u, 235u }};
                    } else if (index < hdr.flags.size() &&
                        (hdr.flags[index] &
                         Raw::Hdr::ResultFlagReferenceFallback) != 0u) {
                        color = {{ 255u, 20u, 20u, 199u }};
                    } else if (index < hdr.flags.size() &&
                        (hdr.flags[index] &
                         Raw::Hdr::ResultFlagHighlightSafeHandoff) != 0u) {
                        color = {{ 255u, 245u, 120u, 215u }};
                    } else {
                        color = ownerPalette[
                            hdr.ownerFrame[index] % ownerPalette.size()];
                    }
                } else if (m_HdrDiagnosticView == 4 &&
                           index < hdr.recoveredHeadroomStops.size()) {
                    color = heatColor(
                        hdr.recoveredHeadroomStops[index] / 6.0f,
                        0.70f);
                } else if (m_HdrDiagnosticView == 5 &&
                           index < hdr.varianceProxy.size()) {
                    const float value = std::max(
                        0.0f,
                        hdr.varianceProxy[index]);
                    color = heatColor(
                        std::log10(1.0f + value * 1000000.0f) / 6.0f,
                        0.70f);
                }
                const std::size_t destination = index * 4u;
                std::copy(
                    color.begin(),
                    color.end(),
                    overlayPixels.begin() + destination);
            }
            const unsigned int replacement =
                GLHelpers::CreateTextureFromPixels(
                    overlayPixels.data(),
                    static_cast<int>(hdr.width),
                    static_cast<int>(hdr.height),
                    4);
            if (replacement != 0) {
                GLint previousTexture = 0;
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
                glBindTexture(GL_TEXTURE_2D, replacement);
                const GLint filter = m_HdrDiagnosticView == 3
                    ? GL_NEAREST
                    : GL_LINEAR;
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
                glBindTexture(
                    GL_TEXTURE_2D,
                    static_cast<unsigned int>(previousTexture));
                if (m_HdrDiagnosticOverlayTexture != 0) {
                    glDeleteTextures(1, &m_HdrDiagnosticOverlayTexture);
                }
                m_HdrDiagnosticOverlayTexture = replacement;
                m_HdrDiagnosticOverlayWidth = static_cast<int>(hdr.width);
                m_HdrDiagnosticOverlayHeight = static_cast<int>(hdr.height);
                m_HdrDiagnosticOverlayView = m_HdrDiagnosticView;
                m_HdrDiagnosticOverlayCacheKey = hdr.diagnostics.cacheKey;
            }
        }
        if (m_HdrDiagnosticOverlayTexture != 0 &&
            m_HdrDiagnosticOverlayView == m_HdrDiagnosticView &&
            m_HdrDiagnosticOverlayCacheKey == hdr.diagnostics.cacheKey) {
            // Diagnostic arrays use top-left image coordinates. Keep
            // their UVs unflipped so each RAW pixel lands over the
            // same displayed pixel as the developed result.
            drawList->AddImage(
                (ImTextureID)(intptr_t)m_HdrDiagnosticOverlayTexture,
                imageRect.Min,
                imageRect.Max,
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f));
        }
    }
}
