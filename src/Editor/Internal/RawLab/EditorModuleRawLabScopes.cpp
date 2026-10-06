#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

using namespace Stack::Editor::RawLabInternal;

namespace {
void DrawFramelessGrid(ImDrawList* drawList, const ImRect& rect, int columns, int rows) {
    const ImU32 gridColor = ImGui::GetColorU32(ImVec4(
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled).x,
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled).y,
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled).z,
        0.16f));
    for (int column = 1; column < columns; ++column) {
        const float x = rect.Min.x + rect.GetWidth() * (static_cast<float>(column) / columns);
        drawList->AddLine(ImVec2(x, rect.Min.y), ImVec2(x, rect.Max.y), gridColor);
    }
    for (int row = 1; row < rows; ++row) {
        const float y = rect.Min.y + rect.GetHeight() * (static_cast<float>(row) / rows);
        drawList->AddLine(ImVec2(rect.Min.x, y), ImVec2(rect.Max.x, y), gridColor);
    }
}

} // namespace

void EditorModule::ClearRawWorkspaceLabGradingScope() {
    m_RawWorkspaceGradingScopeVisualization.reset();
    m_RawGradingScopeRenderer.Shutdown();
}

void EditorModule::PollRawWorkspaceLabGradingScope() {
    const std::string identity = GetActiveRawWorkspacePreviewIdentity();
    if (!IsRawWorkspaceProjectActive() || identity.empty()) {
        m_Pipeline.ClearRawDevelopmentGradingScopeReadback();
        ClearRawWorkspaceLabGradingScope();
        return;
    }
    // RAW service packets arrive with their render result. Only the legacy
    // synchronous backend polls the local pipeline here.
    if (m_RawRenderClientId != 0) return;
    (void)m_Pipeline.PollRawDevelopmentGradingScopeReadback();
    const auto& readback = m_Pipeline.GetRawDevelopmentGradingScopeReadback();
    const auto expected = m_RawWorkspaceLabUi.gradingScopesShowInput
        ? RawDevelopmentGradingScopeSource::NeutralScene
        : RawDevelopmentGradingScopeSource::DisplayCandidate;
    if (readback.valid && readback.visualization && readback.source == expected &&
        readback.sourceKey == identity) {
        m_RawWorkspaceGradingScopeVisualization = readback.visualization;
    }
}

void EditorModule::RenderRawWorkspaceLabGradingSurface() {
    const ImVec2 available = ImGui::GetContentRegionAvail();
    if (available.x <= 1.0f || available.y <= 1.0f) {
        return;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float headerHeight = 22.0f;
    const float bottomInset = 22.0f;
    const float firstWidth = available.x * 0.30f;
    const float secondWidth = available.x * 0.40f;
    const ImRect vectorCell(
        origin,
        ImVec2(origin.x + firstWidth, origin.y + available.y));
    const ImRect paradeCell(
        ImVec2(vectorCell.Max.x, origin.y),
        ImVec2(vectorCell.Max.x + secondWidth, origin.y + available.y));
    const ImRect histogramCell(
        ImVec2(paradeCell.Max.x, origin.y),
        ImVec2(origin.x + available.x, origin.y + available.y));

    const ImU32 labelColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImVec4 disabled = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const ImU32 gridColor = ImGui::GetColorU32(ImVec4(
        disabled.x, disabled.y, disabled.z, 0.20f));
    drawList->AddText(
        ImVec2(vectorCell.Min.x + 6.0f, vectorCell.Min.y),
        labelColor,
        "Vectorscope");
    drawList->AddText(
        ImVec2(paradeCell.Min.x + 6.0f, paradeCell.Min.y),
        labelColor,
        "RGB Parade");
    drawList->AddText(
        ImVec2(histogramCell.Min.x + 6.0f, histogramCell.Min.y),
        labelColor,
        "Luma Histogram");
    const auto& packet = m_RawWorkspaceGradingScopeVisualization;
    const auto expectedSource = m_RawWorkspaceLabUi.gradingScopesShowInput
        ? RawDevelopmentGradingScopeSource::NeutralScene
        : RawDevelopmentGradingScopeSource::DisplayCandidate;
    const std::string activePreviewIdentity =
        GetActiveRawWorkspacePreviewIdentity();
    const bool visualizationMatchesProject =
        packet && packet->sourceKey == activePreviewIdentity &&
        packet->source == expectedSource;
    const RawDevelopmentGradingScopeSource displayedSource =
        visualizationMatchesProject
            ? packet->source
            : (m_RawWorkspaceLabUi.gradingScopesShowInput
                ? RawDevelopmentGradingScopeSource::NeutralScene
                : RawDevelopmentGradingScopeSource::DisplayCandidate);
    const char* sourceLabel =
        displayedSource == RawDevelopmentGradingScopeSource::NeutralScene
            ? "Input"
            : "Result";
    const ImVec2 sourceLabelSize = ImGui::CalcTextSize(sourceLabel);
    drawList->AddText(
        ImVec2(
            histogramCell.Max.x - sourceLabelSize.x - 4.0f,
            histogramCell.Min.y),
        ImGui::GetColorU32(disabled),
        sourceLabel);

    if (!visualizationMatchesProject) {
        const char* pending = "Scope pending";
        const ImVec2 pendingSize = ImGui::CalcTextSize(pending);
        drawList->AddText(
            ImVec2(
                origin.x + (available.x - pendingSize.x) * 0.5f,
                origin.y + headerHeight +
                    (available.y - headerHeight - pendingSize.y) * 0.5f),
            ImGui::GetColorU32(disabled),
            pending);
        ImGui::Dummy(available);
        return;
    }

    const auto& visualization = *packet;
    const float plotTop = vectorCell.Min.y + headerHeight;
    const float plotBottom = std::max(
        plotTop + 1.0f,
        vectorCell.Max.y - bottomInset);
    const float vectorSide = std::max(
        1.0f,
        std::min(
            vectorCell.GetWidth() - 18.0f,
            plotBottom - plotTop));
    const ImVec2 vectorCenter(
        vectorCell.GetCenter().x,
        plotTop + (plotBottom - plotTop) * 0.5f);
    const float vectorRadius = vectorSide * 0.48f;
    drawList->AddCircle(vectorCenter, vectorRadius, gridColor, 64, 1.0f);
    drawList->AddCircle(vectorCenter, vectorRadius * 0.5f, gridColor, 64, 1.0f);
    drawList->AddLine(
        ImVec2(vectorCenter.x - vectorRadius, vectorCenter.y),
        ImVec2(vectorCenter.x + vectorRadius, vectorCenter.y),
        gridColor);
    drawList->AddLine(
        ImVec2(vectorCenter.x, vectorCenter.y - vectorRadius),
        ImVec2(vectorCenter.x, vectorCenter.y + vectorRadius),
        gridColor);
    m_RawGradingScopeRenderer.QueueDraw(*drawList, packet,
        GradingScopePlot::Vectorscope,
        ImVec2(vectorCenter.x - vectorRadius, vectorCenter.y - vectorRadius),
        ImVec2(vectorCenter.x + vectorRadius, vectorCenter.y + vectorRadius));
    struct VectorTarget {
        const char* label;
        float angleDegrees;
        ImVec4 color;
    };
    const std::array<VectorTarget, 6> targets {{
        { "R", -120.0f, ImVec4(1.0f, 0.25f, 0.20f, 0.72f) },
        { "M",  -60.0f, ImVec4(1.0f, 0.25f, 0.92f, 0.72f) },
        { "B",    0.0f, ImVec4(0.28f, 0.48f, 1.0f, 0.78f) },
        { "C",   60.0f, ImVec4(0.20f, 0.90f, 1.0f, 0.72f) },
        { "G",  120.0f, ImVec4(0.25f, 1.0f, 0.36f, 0.72f) },
        { "Y",  180.0f, ImVec4(1.0f, 0.88f, 0.20f, 0.72f) }
    }};
    const float labelRingRadius = std::max(
        0.0f,
        vectorRadius - ImGui::GetFontSize() * 0.72f);
    for (const VectorTarget& target : targets) {
        const float angle = target.angleDegrees *
            (3.14159265358979323846f / 180.0f);
        const ImVec2 position(
            vectorCenter.x + std::cos(angle) * labelRingRadius,
            vectorCenter.y + std::sin(angle) * labelRingRadius);
        const ImVec2 textSize = ImGui::CalcTextSize(target.label);
        drawList->AddText(
            ImVec2(position.x - textSize.x * 0.5f, position.y - textSize.y * 0.5f),
            ImGui::GetColorU32(target.color),
            target.label);
    }
    drawList->AddCircleFilled(vectorCenter, 1.5f, labelColor);

    const ImRect paradePlot(
        ImVec2(paradeCell.Min.x + 22.0f, paradeCell.Min.y + headerHeight),
        ImVec2(paradeCell.Max.x - 7.0f, paradeCell.Max.y - bottomInset));
    const ImVec2 scaleLabelSize = ImGui::CalcTextSize("100");
    drawList->AddText(
        ImVec2(paradeCell.Min.x + 3.0f, paradePlot.Min.y),
        ImGui::GetColorU32(disabled),
        "100");
    drawList->AddText(
        ImVec2(
            paradeCell.Min.x + 3.0f,
            paradePlot.Max.y - scaleLabelSize.y),
        ImGui::GetColorU32(disabled),
        "0");
    m_RawGradingScopeRenderer.QueueDraw(*drawList, packet,
        GradingScopePlot::Parade, paradePlot.Min, paradePlot.Max);

    const ImRect histogramPlot(
        ImVec2(histogramCell.Min.x + 7.0f, histogramCell.Min.y + headerHeight),
        ImVec2(histogramCell.Max.x - 7.0f, histogramCell.Max.y - bottomInset));
    DrawFramelessGrid(drawList, histogramPlot, 4, 4);
    std::array<ImVec2, RawGradingScopeVisualization::kHistogramBins>
        histogramOutline {};
    const float histogramBinWidth =
        histogramPlot.GetWidth() /
        RawGradingScopeVisualization::kHistogramBins;
    for (int bin = 0;
         bin < RawGradingScopeVisualization::kHistogramBins;
         ++bin) {
        const float value = visualization.histogram[
            static_cast<std::size_t>(bin)];
        const float x = histogramPlot.Min.x + bin * histogramBinWidth;
        const float y = histogramPlot.Max.y - value * histogramPlot.GetHeight();
        drawList->AddRectFilled(
            ImVec2(x, y),
            ImVec2(x + histogramBinWidth + 0.5f, histogramPlot.Max.y),
            ImGui::GetColorU32(ImVec4(0.92f, 0.94f, 0.96f, 0.13f)));
        histogramOutline[static_cast<std::size_t>(bin)] =
            ImVec2(x + histogramBinWidth * 0.5f, y);
    }
    drawList->AddPolyline(
        histogramOutline.data(),
        static_cast<int>(histogramOutline.size()),
        ImGui::GetColorU32(ImVec4(0.92f, 0.94f, 0.96f, 0.78f)),
        ImDrawFlags_None,
        1.0f);
    ImGui::Dummy(available);
}
