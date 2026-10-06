#include "EditorScopes.h"
#include "Editor/EditorModule.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>

EditorScopes::EditorScopes() = default;

EditorScopes::~EditorScopes() {}

void EditorScopes::Initialize() {}

void EditorScopes::RenderScopeNode(EditorModule* editor, EditorNodeGraph::ScopeKind scopeKind, int sourceNodeId) {
    m_Data.reset();
    if (sourceNodeId <= 0) {
        ImGui::TextDisabled("Connect an image or mask output.");
        return;
    }
    const auto* cached = editor ? editor->GetCachedPreviewPixelsForNode(sourceNodeId) : nullptr;
    if (cached) m_Data = cached->scopeData;
    if (!m_Data) {
        ImGui::TextDisabled("Rendering scope...");
        return;
    }

    switch (scopeKind) {
        case EditorNodeGraph::ScopeKind::Histogram:
            DrawHistogram();
            break;
        case EditorNodeGraph::ScopeKind::Vectorscope:
            DrawVectorscope();
            break;
        case EditorNodeGraph::ScopeKind::RGBParade:
            DrawRGBParade();
            break;
    }
}

void EditorScopes::DrawHistogram() {
    float width = ImGui::GetContentRegionAvail().x;
    float height = ImGui::GetContentRegionAvail().y - 20;

    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1, 0.3f, 0.3f, 0.8f));
    ImGui::PlotLines("##Red", m_Data->HistR.data(), 256, 0, nullptr, 0, 1.0f, ImVec2(width, height / 4));
    ImGui::PopStyleColor();
    
    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.3f, 1, 0.3f, 0.8f));
    ImGui::PlotLines("##Green", m_Data->HistG.data(), 256, 0, nullptr, 0, 1.0f, ImVec2(width, height / 4));
    ImGui::PopStyleColor();
    
    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.3f, 0.3f, 1, 0.8f));
    ImGui::PlotLines("##Blue", m_Data->HistB.data(), 256, 0, nullptr, 0, 1.0f, ImVec2(width, height / 4));
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1, 1, 1, 0.8f));
    ImGui::PlotLines("##Lum", m_Data->HistL.data(), 256, 0, nullptr, 0, 1.0f, ImVec2(width, height / 4));
    ImGui::PopStyleColor();
}

void EditorScopes::DrawVectorscope() {
    ImVec2 size = ImGui::GetContentRegionAvail();
    float side = std::min(size.x, size.y);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Background circle
    ImVec2 center = ImVec2(pos.x + side/2, pos.y + side/2);
    draw->AddCircle(center, side/2, IM_COL32(100, 100, 100, 255), 64);
    draw->AddLine(ImVec2(center.x - side/2, center.y), ImVec2(center.x + side/2, center.y), IM_COL32(50, 50, 50, 255));
    draw->AddLine(ImVec2(center.x, center.y - side/2), ImVec2(center.x, center.y + side/2), IM_COL32(50, 50, 50, 255));

    // Plot points
    for (auto& p : m_Data->VectorPoints) {
        // Map U, V (-0.5 to 0.5 approx) to pixels
        float px = center.x + p.u * (side * 0.8f);
        float py = center.y - p.v * (side * 0.8f);
        draw->AddRectFilled(ImVec2(px, py), ImVec2(px+1.5f, py+1.5f), IM_COL32(255, 255, 255, 40));
    }
    
    ImGui::Dummy(ImVec2(side, side));
}

void EditorScopes::DrawRGBParade() {
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    if (m_Data->ParadeData.empty()) return;

    float colWidth = canvas_size.x / m_Data->ParadeData.size();
    float rowHeight = canvas_size.y / 256.0f;

    for (size_t x = 0; x < m_Data->ParadeData.size(); x++) {
        for (int i = 0; i < 256; i++) {
            float r = m_Data->ParadeData[x].r[i];
            float g = m_Data->ParadeData[x].g[i];
            float b = m_Data->ParadeData[x].b[i];

            if (r > 0) draw->AddRectFilled(ImVec2(pos.x + x * colWidth, pos.y + (255-i) * rowHeight), 
                                        ImVec2(pos.x + (x+1) * colWidth, pos.y + (255-i+1) * rowHeight), 
                                        IM_COL32(255, 0, 0, (int)std::min(255.0f, r * 50)));
            if (g > 0) draw->AddRectFilled(ImVec2(pos.x + x * colWidth, pos.y + (255-i) * rowHeight), 
                                        ImVec2(pos.x + (x+1) * colWidth, pos.y + (255-i+1) * rowHeight), 
                                        IM_COL32(0, 255, 0, (int)std::min(255.0f, g * 50)));
            if (b > 0) draw->AddRectFilled(ImVec2(pos.x + x * colWidth, pos.y + (255-i) * rowHeight), 
                                        ImVec2(pos.x + (x+1) * colWidth, pos.y + (255-i+1) * rowHeight), 
                                        IM_COL32(0, 0, 255, (int)std::min(255.0f, b * 50)));
        }
    }
    
    ImGui::Dummy(canvas_size);
}
