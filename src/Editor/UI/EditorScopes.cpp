#include "EditorScopes.h"
#include "Editor/EditorModule.h"
#include <imgui.h>
#include <cmath>
#include <algorithm>

EditorScopes::EditorScopes() {
    m_HistR.assign(256, 0.0f);
    m_HistG.assign(256, 0.0f);
    m_HistB.assign(256, 0.0f);
    m_HistL.assign(256, 0.0f);
}

EditorScopes::~EditorScopes() {}

void EditorScopes::Initialize() {}

void EditorScopes::AnalyzePixels(EditorModule* editor) {
    int w, h;
    auto pixels = editor->GetPipeline().GetScopesPixels(w, h);
    if (pixels.empty()) return;

    // Reset data
    std::fill(m_HistR.begin(), m_HistR.end(), 0.0f);
    std::fill(m_HistG.begin(), m_HistG.end(), 0.0f);
    std::fill(m_HistB.begin(), m_HistB.end(), 0.0f);
    std::fill(m_HistL.begin(), m_HistL.end(), 0.0f);
    m_VectorPoints.clear();
    
    // Parade Setup (1 bin per pixel column if small enough)
    m_ParadeData.assign(w, ParadeColumn{0});

    float maxHist = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = (y * w + x) * 4;
            uint8_t r = pixels[idx];
            uint8_t g = pixels[idx + 1];
            uint8_t b = pixels[idx + 2];
            
            // 1. Histogram
            m_HistR[r]++;
            m_HistG[g]++;
            m_HistB[b]++;
            float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            m_HistL[(int)lum]++;

            // 2. Vectorscope (YUV Chroma)
            // Simplified conversion for plotting
            float rf = r / 255.0f;
            float gf = g / 255.0f;
            float bf = b / 255.0f;
            float u = -0.14713f * rf - 0.28886f * gf + 0.43692f * bf;
            float v =  0.61501f * rf - 0.51499f * gf - 0.10001f * bf;
            
            // Sample only some points for performance if buffer is large
            if ((x + y) % 2 == 0) {
                m_VectorPoints.push_back({u, v});
            }

            // 3. RGB Parade
            m_ParadeData[x].r[r]++;
            m_ParadeData[x].g[g]++;
            m_ParadeData[x].b[b]++;
        }
    }

    // Normalize Histograms
    for (int i = 0; i < 256; i++) {
        maxHist = std::max({maxHist, m_HistR[i], m_HistG[i], m_HistB[i], m_HistL[i]});
    }
    if (maxHist > 0) {
        for (int i = 0; i < 256; i++) {
            m_HistR[i] /= maxHist;
            m_HistG[i] /= maxHist;
            m_HistB[i] /= maxHist;
            m_HistL[i] /= maxHist;
        }
    }
}

void EditorScopes::Render(EditorModule* editor) {
    ImGui::Begin("Scopes Panel");

    // Throttled analysis
    m_UpdateTimer += ImGui::GetIO().DeltaTime;
    if (m_UpdateTimer > m_UpdateInterval && editor->GetPipeline().HasSourceImage()) {
        AnalyzePixels(editor);
        m_UpdateTimer = 0.0f;
    }

    if (!editor->GetPipeline().HasSourceImage()) {
        ImGui::TextDisabled("No image loaded for analysis.");
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("ScopeTabs")) {
        if (ImGui::BeginTabItem("Histogram")) {
            DrawHistogram();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Vectorscope")) {
            DrawVectorscope();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("RGB Parade")) {
            DrawRGBParade();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void EditorScopes::DrawHistogram() {
    float width = ImGui::GetContentRegionAvail().x;
    float height = ImGui::GetContentRegionAvail().y - 20;

    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1, 0.3f, 0.3f, 0.8f));
    ImGui::PlotLines("##Red", m_HistR.data(), 256, 0, nullptr, 0, 1.0f, ImVec2(width, height / 4));
    ImGui::PopStyleColor();
    
    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.3f, 1, 0.3f, 0.8f));
    ImGui::PlotLines("##Green", m_HistG.data(), 256, 0, nullptr, 0, 1.0f, ImVec2(width, height / 4));
    ImGui::PopStyleColor();
    
    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(0.3f, 0.3f, 1, 0.8f));
    ImGui::PlotLines("##Blue", m_HistB.data(), 256, 0, nullptr, 0, 1.0f, ImVec2(width, height / 4));
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1, 1, 1, 0.8f));
    ImGui::PlotLines("##Lum", m_HistL.data(), 256, 0, nullptr, 0, 1.0f, ImVec2(width, height / 4));
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
    for (auto& p : m_VectorPoints) {
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

    if (m_ParadeData.empty()) return;

    float colWidth = canvas_size.x / m_ParadeData.size();
    float rowHeight = canvas_size.y / 256.0f;

    for (size_t x = 0; x < m_ParadeData.size(); x++) {
        for (int i = 0; i < 256; i++) {
            float r = m_ParadeData[x].r[i];
            float g = m_ParadeData[x].g[i];
            float b = m_ParadeData[x].b[i];

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
