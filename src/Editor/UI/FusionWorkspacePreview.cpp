#include "Editor/UI/FusionWorkspaceUi.h"
#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Stack::Editor {
namespace {
using Color = std::array<float, 3>;
constexpr std::array<Color, 8> colors {{ {0.20f,0.55f,1}, {1,0.55f,0.18f}, {0.3f,0.85f,0.4f},
    {0.85f,0.35f,0.9f}, {0.95f,0.85f,0.25f}, {0.2f,0.85f,0.85f}, {1,0.35f,0.45f}, {0.7f,0.75f,1} }};
float Display(float value) {
    value = std::max(0.0f, value); value /= 1 + value;
    return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1 / 2.4f) - 0.055f;
}
Raw::Hdr::FusionPixel Evaluate(const FusionWorkspaceUiState& s, const Raw::Hdr::FusionPreview& p,
    std::size_t pixel, std::size_t parity, bool neutral = false) {
    const auto& c = neutral ? Raw::Hdr::FusionControls{} : s.controls;
    const float x = static_cast<float>(((pixel % p.width) * 2 * p.stride + parity % 2 + 0.5) / p.rawWidth);
    const float y = static_cast<float>(((pixel / p.width) * 2 * p.stride + parity / 2 + 0.5) / p.rawHeight);
    return Raw::Hdr::EvaluateFusionPixel(c, p.sources.data(),
        p.observations.data() + (pixel * 4 + parity) * p.sources.size(),
        p.sources.size(), p.anchor, p.analysisEv[pixel], x, y);
}
std::size_t ProbeIndex(const FusionWorkspaceUiState& s, const Raw::Hdr::FusionPreview& p) {
    const auto x = std::min(p.width - 1, static_cast<std::uint32_t>(s.probeX * p.width));
    const auto y = std::min(p.height - 1, static_cast<std::uint32_t>(s.probeY * p.height));
    return static_cast<std::size_t>(y) * p.width + x;
}
}

FusionWorkspaceUiState::~FusionWorkspaceUiState() {
    if (texture) glDeleteTextures(1, &texture);
}

void DrawFusionPreview(FusionWorkspaceUiState& s, const Raw::Hdr::FusionPreview* p, const ImVec2& available) {
    ImGui::TextUnformatted("Fusion preview");
    ImGui::SameLine(); ImGui::TextDisabled("Camera linear, neutral display");
    if (!p || p->sources.empty() || p->width == 0 || p->height == 0) {
        ImGui::TextWrapped("Connect captures through Burst Denoise or HDR Fusion to Output, then Process Output to prepare the live preview. Changed capture or registration settings need a new processing run."); return;
    }
    const char* views[] = {"Result", "Neutral analysis", "Contribution blend", "Dominant source", "Selected source contribution",
        "Selected source headroom RGB", "Noise confidence", "Alignment status", "Effective sample count", "Fallback regions",
        "Estimated uncertainty", "Target exposure", "Local mask", "Motion / disagreement rejection"};
    ImGui::SetNextItemWidth(220); ImGui::Combo("##FusionView", &s.view, views, IM_ARRAYSIZE(views));
    ImGui::SameLine(); ImGui::Checkbox("Lock probe", &s.probeLocked);
    s.source = std::clamp(s.source, 0, static_cast<int>(p->sources.size()) - 1);
    const auto sourceLabel = [&](int i) {
        const auto hit = s.sourceLabels.find(p->sources[i].sourceId);
        return hit == s.sourceLabels.end() ? p->sources[i].sourceId : hit->second;
    };
    if (s.view == 4 || s.view == 5 || s.view == 7 || s.view == 13) {
        ImGui::SetNextItemWidth(260);
        if (ImGui::BeginCombo("##DiagnosticSource", sourceLabel(s.source).c_str())) {
            for (int i = 0; i < static_cast<int>(p->sources.size()); ++i)
                if (ImGui::Selectable(sourceLabel(i).c_str(), s.source == i)) s.source = i;
            ImGui::EndCombo();
        }
    }
    const std::string identity = Raw::Hdr::SerializeFusionControls(s.controls).dump() +
        p->contentIdentity + ":" + std::to_string(s.view) + ":" +
        std::to_string(s.source) + ":" + std::to_string(s.selectedMask);
    if (!s.texture || s.imageIdentity != identity) {
        std::vector<unsigned char> pixels(static_cast<std::size_t>(p->width) * p->height * 4);
        float nominalSupport = 0;
        for (std::size_t source = 0; source < p->sources.size(); ++source) {
            float maximum = 1;
            for (std::size_t site = source; site < p->observations.size(); site += p->sources.size())
                maximum = std::max(maximum, p->observations[site].effectiveSamples);
            nominalSupport += maximum;
        }
        for (std::size_t i = 0; i < p->analysisEv.size(); ++i) {
            Color color {}; std::array<float, 3> channelCount {};
            std::array<float, Raw::Hdr::kFusionMaxSources> contribution {};
            float support = 0, variance = 0, signal = 0, target = 0; bool fallback = false;
            for (std::size_t parity = 0; parity < 4; ++parity) {
                const auto result = Evaluate(s, *p, i, parity, s.view == 1);
                const auto channel = p->channelByParity[parity];
                color[channel] += result.scene; channelCount[channel] += 1;
                support += result.effectiveSamples / 4; variance += result.variance / 4;
                signal += result.scene / 4; target += result.targetEv / 4; fallback |= result.fallback;
                for (std::size_t f = 0; f < p->sources.size(); ++f) contribution[f] += result.contribution[f] / 4;
            }
            for (int c = 0; c < 3; ++c) color[c] /= std::max(1.0f, channelCount[c]);
            if (s.view <= 1) for (int c = 0; c < 3; ++c) color[c] = Display(color[c] * p->whiteBalance[c]);
            else if (s.view == 2 || s.view == 3) {
                color = {};
                const auto dominant = static_cast<std::size_t>(std::max_element(contribution.begin(), contribution.begin() + p->sources.size()) - contribution.begin());
                for (std::size_t f = 0; f < p->sources.size(); ++f) for (int c = 0; c < 3; ++c)
                    color[c] += colors[f % colors.size()][c] * (s.view == 3 ? (f == dominant ? 1.0f : 0) : contribution[f]);
            } else if (s.view == 4) color.fill(contribution[s.source]);
            else if (s.view == 5) {
                color = {};
                for (std::size_t parity = 0; parity < 4; ++parity) color[p->channelByParity[parity]] +=
                    p->observations[(i * 4 + parity) * p->sources.size() + s.source].headroom;
                for (int c = 0; c < 3; ++c) color[c] /= std::max(1.0f, channelCount[c]);
            } else if (s.view == 6) color.fill(signal * signal / std::max(1.0e-12f, signal * signal + variance));
            else if (s.view == 7) color.fill(p->sources[s.source].alignmentConfidence);
            else if (s.view == 8) color.fill(std::min(1.0f, support / std::max(1.0f, nominalSupport)));
            else if (s.view == 9) color = fallback ? Color{1, 0.2f, 0.15f} : Color{0.08f, 0.08f, 0.08f};
            else if (s.view == 10) color.fill(std::clamp(std::sqrt(variance) * 16, 0.0f, 1.0f));
            else if (s.view == 11) color = target >= 0 ? Color{std::min(1.0f, target / 6), 0.1f, 0.1f}
                : Color{0.1f, 0.1f, std::min(1.0f, -target / 6)};
            else if (s.view == 12) {
                const float mask = s.selectedMask >= 0 && s.selectedMask < static_cast<int>(s.controls.masks.size())
                    ? Raw::Hdr::FusionMaskCoverage(s.controls.masks[s.selectedMask],
                        static_cast<float>((i % p->width + 0.5f) / p->width),
                        static_cast<float>((i / p->width + 0.5f) / p->height)) : 0;
                color.fill(mask);
            } else if (s.view == 13) {
                float confidence = 0;
                for (std::size_t parity = 0; parity < 4; ++parity)
                    confidence += p->observations[(i * 4 + parity) * p->sources.size() + s.source].motionConfidence / 4;
                color = {1 - confidence, confidence * 0.5f, 0};
            }
            for (int c = 0; c < 3; ++c) pixels[i * 4 + c] = static_cast<unsigned char>(std::clamp(color[c], 0.0f, 1.0f) * 255 + 0.5f);
            pixels[i * 4 + 3] = 255;
        }
        if (s.texture) glDeleteTextures(1, &s.texture);
        s.texture = GLHelpers::CreateTextureFromPixels(pixels.data(), p->width, p->height, 4);
        s.imageIdentity = identity;
    }
    const auto room = ImGui::GetContentRegionAvail();
    const float scale = std::max(0.01f, std::min(room.x / p->width, std::max(40.0f, room.y - 24) / p->height));
    const ImVec2 size(p->width * scale, p->height * scale);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (room.x - size.x) * 0.5f));
    const auto pos = ImGui::GetCursorScreenPos();
    ImGui::Image(static_cast<ImTextureID>(static_cast<intptr_t>(s.texture)), size);
    if (ImGui::IsItemHovered()) {
        const auto mouse = ImGui::GetIO().MousePos;
        const float x = std::clamp((mouse.x - pos.x) / size.x, 0.0f, 1.0f);
        const float y = std::clamp((mouse.y - pos.y) / size.y, 0.0f, 1.0f);
        if (!s.probeLocked) { s.probeX = x; s.probeY = y; }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && s.placeMask && s.selectedMask >= 0 &&
            s.selectedMask < static_cast<int>(s.controls.masks.size())) {
            auto& mask = s.controls.masks[s.selectedMask]; mask.centerX = x; mask.centerY = y;
            s.dirty = true; s.placeMask = false;
        }
    }
    auto* draw = ImGui::GetWindowDrawList();
    if (s.selectedMask >= 0 && s.selectedMask < static_cast<int>(s.controls.masks.size())) {
        const auto& m = s.controls.masks[s.selectedMask];
        draw->PushClipRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), true);
        for (int i = 0; i < 64; ++i) {
            const float a = i * 6.2831853f / 64, b = (i + 1) * 6.2831853f / 64;
            draw->AddLine(ImVec2(pos.x + (m.centerX + std::cos(a) * m.radiusX) * size.x,
                pos.y + (m.centerY + std::sin(a) * m.radiusY) * size.y),
                ImVec2(pos.x + (m.centerX + std::cos(b) * m.radiusX) * size.x,
                pos.y + (m.centerY + std::sin(b) * m.radiusY) * size.y), IM_COL32(255,255,255,180));
        }
        draw->PopClipRect();
    }
    const ImVec2 probe(pos.x + s.probeX * size.x, pos.y + s.probeY * size.y);
    draw->AddCircle(probe, 4, IM_COL32(255,255,255,220));
    ImGui::TextDisabled("%u x %u sampled preview. Process Output applies edits at full resolution.", p->width, p->height);
    (void)available;
}

void DrawFusionProbe(const FusionWorkspaceUiState& s, const Raw::Hdr::FusionPreview& p) {
    if (p.width == 0 || p.sources.empty()) return;
    const auto index = ProbeIndex(s, p);
    std::array<Raw::Hdr::FusionPixel, 4> samples;
    std::array<float, 3> channelCount {};
    for (std::size_t c = 0; c < 4; ++c) { samples[c] = Evaluate(s, p, index, c); channelCount[p.channelByParity[c]] += 1; }
    ImGui::Text("Probe: %.1f%%, %.1f%%", s.probeX * 100, s.probeY * 100);
    ImGui::Text("Analysis %+.2f EV | target %+.2f EV", p.analysisEv[index], samples[0].targetEv);
    ImGui::Text("Preferred source %+.2f EV", samples[0].preferredEv);
    float total = 0;
    for (std::size_t f = 0; f < p.sources.size(); ++f) {
        float weight = 0; Color rgb {};
        for (int c = 0; c < 4; ++c) { weight += samples[c].contribution[f] / 4; rgb[p.channelByParity[c]] += samples[c].contribution[f]; }
        total += weight;
        const auto label = s.sourceLabels.find(p.sources[f].sourceId);
        ImGui::TextColored(ImVec4(colors[f % 8][0], colors[f % 8][1], colors[f % 8][2], 1),
            "%s", label == s.sourceLabels.end() ? p.sources[f].sourceId.c_str() : label->second.c_str());
        ImGui::Text("%.1f%% | transform %+.2f EV", weight * 100, samples[0].targetEv - p.sources[f].captureEv);
        ImGui::TextDisabled("RGB %.1f%% / %.1f%% / %.1f%%", rgb[0] * 100 / std::max(1.0f, channelCount[0]),
            rgb[1] * 100 / std::max(1.0f, channelCount[1]), rgb[2] * 100 / std::max(1.0f, channelCount[2]));
    }
    ImGui::Text("Contribution total %.2f%%", total * 100);
    ImGui::Text("Effective samples %.2f | sigma %.6f", samples[0].effectiveSamples, std::sqrt(samples[0].variance));
    if (std::any_of(samples.begin(), samples.end(), [](const auto& v) { return v.fallback; }))
        ImGui::TextWrapped("Anchor fallback. One or more channels have no enabled valid measurement.");
}
} // namespace Stack::Editor
