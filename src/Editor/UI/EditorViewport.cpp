#include "EditorViewport.h"
#include "Editor/EditorModule.h"
#include "Renderer/GLHelpers.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

EditorViewport::EditorViewport() 
    : m_ZoomLevel(1.0f), m_PanX(0.0f), m_PanY(0.0f), m_IsLocked(false) 
{}

EditorViewport::~EditorViewport() {
    if (m_CheckerTex) glDeleteTextures(1, &m_CheckerTex);
}

void EditorViewport::Initialize() {
    // Create a 2x2 checkerboard texture for transparency display
    unsigned char checker[2 * 2 * 4] = {
        204, 204, 204, 255,   153, 153, 153, 255,
        153, 153, 153, 255,   204, 204, 204, 255
    };
    m_CheckerTex = GLHelpers::CreateTextureFromPixels(checker, 2, 2, 4);
    glBindTexture(GL_TEXTURE_2D, m_CheckerTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void EditorViewport::Render(EditorModule* editor) {
    auto& pipeline = editor->GetPipeline();

    ImGui::Begin("Canvas Viewport");

    if (!pipeline.HasSourceImage() || !editor->GetNodeGraph().IsOutputConnected() || pipeline.GetOutputTexture() == 0) {
        ImGui::Text(editor->IsEditorRenderBusy()
            ? "Rendering editor output..."
            : (editor->GetNodeGraph().GetActiveImageNodeId() > 0
                ? "No graph output is connected."
                : "Drop an image onto the node graph, then connect it to the output chain."));
        ImGui::End();
        return;
    }

    // ── Inputs & Zoom Logic ──────────────────────────────────────────────────
    
    // Toggle Lock with 'L' key
    if (ImGui::IsKeyPressed(ImGuiKey_L)) {
        m_IsLocked = !m_IsLocked;
    }

    bool isHovered = ImGui::IsWindowHovered();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 mousePos = ImGui::GetMousePos();
    ImVec2 windowPos = ImGui::GetWindowPos();
    ImVec2 relativeMouse = ImVec2(mousePos.x - windowPos.x, mousePos.y - windowPos.y);

    if (isHovered && !m_IsLocked) {
        // Zoom with Scroll
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            float prevZoom = m_ZoomLevel;
            m_ZoomLevel += wheel * m_ZoomLevel * 0.1f;
            m_ZoomLevel = std::max(1.0f, std::min(m_ZoomLevel, 100.0f)); // Min 1.0 (Fit), Max 100x
        }
    }

    // ── Rendering Logic ──────────────────────────────────────────────────────
    
    unsigned int outputTex = pipeline.GetOutputTexture();
    int imgW = pipeline.GetCanvasWidth();
    int imgH = pipeline.GetCanvasHeight();

    // Base scale to fit screen
    float scaleX = avail.x / (float)imgW;
    float scaleY = avail.y / (float)imgH;
    float baseScale = std::min(scaleX, scaleY) * 0.95f; // Leave a bit of margin

    float finalScale = baseScale * m_ZoomLevel;
    float dispW = (float)imgW * finalScale;
    float dispH = (float)imgH * finalScale;

    // Panning (Follow mouse if zoomed in and not locked)
    if (!m_IsLocked && finalScale > baseScale) {
        // Map mouse position [0, avail] to pan offset
        // Normalizing relativeMouse to [0, 1] across the available region
        float mouseNormX = (relativeMouse.x / avail.x) - 0.5f;
        float mouseNormY = (relativeMouse.y / avail.y) - 0.5f;

        // The overflow is how much larger the image is than the viewport
        float overflowX = std::max(0.0f, dispW - avail.x);
        float overflowY = std::max(0.0f, dispH - avail.y);

        m_PanX = -mouseNormX * overflowX;
        m_PanY = -mouseNormY * overflowY;
    }

    const ImVec2 contentCursor = ImGui::GetCursorPos();
    const ImVec2 contentScreen = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddText(ImVec2(contentScreen.x + 10.0f, contentScreen.y + 10.0f),
                      m_IsLocked ? IM_COL32(255, 150, 40, 255) : IM_COL32(170, 170, 178, 220),
                      m_IsLocked ? "[ ZOOM LOCKED ] - Press 'L' to unlock" : "Scroll to Zoom | 'L' to Lock | Movement Pans");
    if (editor->IsEditorRenderBusy()) {
        drawList->AddText(ImVec2(contentScreen.x + 10.0f, contentScreen.y + 32.0f),
                          IM_COL32(190, 195, 205, 230),
                          "Rendering...");
    }

    // Centering calculations
    float offsetX = (avail.x - dispW) * 0.5f + m_PanX;
    float offsetY = (avail.y - dispH) * 0.5f + m_PanY;

    ImVec2 startCursorPos = ImVec2(contentCursor.x + offsetX, contentCursor.y + offsetY);
    ImGui::SetCursorPos(startCursorPos);
    
    ImVec2 drawScreenPos = ImGui::GetCursorScreenPos();

    // ── Handle Color Picking ─────────────────────────────────────────────────
    if (editor->IsPickingColor() && isHovered && !m_IsLocked) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        // Calculate pixel under cursor using screen-space image origin
        float imgMouseX = mousePos.x - drawScreenPos.x;
        float imgMouseY = mousePos.y - drawScreenPos.y;
        bool overImage = (imgMouseX >= 0 && imgMouseX <= dispW && imgMouseY >= 0 && imgMouseY <= dispH);

        if (overImage) {
            float u = imgMouseX / dispW;
            float v = imgMouseY / dispH;
            int px = std::clamp((int)(u * imgW), 0, imgW - 1);
            int py = std::clamp((int)(v * imgH), 0, imgH - 1);

            const auto& sourcePixels = pipeline.GetSourcePixelsRaw();
            int ch = pipeline.GetSourceChannels();

            // Click to pick
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                int flippedY = imgH - 1 - py;
                int idx = (flippedY * imgW + px) * ch;
                if (!sourcePixels.empty() && idx >= 0 && (size_t)(idx + 2) < sourcePixels.size()) {
                    float r = sourcePixels[idx] / 255.0f;
                    float g = sourcePixels[idx + 1] / 255.0f;
                    float b = sourcePixels[idx + 2] / 255.0f;
                    editor->OnColorPicked(r, g, b);
                }
            }

            // Draw magnifier tooltip
            if (!sourcePixels.empty()) {
                const int magRadius = 5; // 11x11 grid
                const float cellSize = 12.0f;
                const int gridSize = magRadius * 2 + 1;
                const float magSize = gridSize * cellSize;

                ImVec2 magPos = ImVec2(mousePos.x + 20, mousePos.y + 20);

                // Keep magnifier on screen
                ImVec2 displaySize = ImGui::GetIO().DisplaySize;
                if (magPos.x + magSize + 4 > displaySize.x) magPos.x = mousePos.x - magSize - 20;
                if (magPos.y + magSize + 4 > displaySize.y) magPos.y = mousePos.y - magSize - 20;

                ImDrawList* fg = ImGui::GetForegroundDrawList();
                fg->AddRectFilled(ImVec2(magPos.x - 2, magPos.y - 2),
                                  ImVec2(magPos.x + magSize + 2, magPos.y + magSize + 2),
                                  IM_COL32(30, 30, 30, 230), 4.0f);

                for (int gy = -magRadius; gy <= magRadius; gy++) {
                    for (int gx = -magRadius; gx <= magRadius; gx++) {
                        int sx = std::clamp(px + gx, 0, imgW - 1);
                        int sy = std::clamp(py + gy, 0, imgH - 1);
                        int flippedSY = imgH - 1 - sy;
                        int sIdx = (flippedSY * imgW + sx) * ch;

                        ImU32 col = IM_COL32(0, 0, 0, 255);
                        if (sIdx >= 0 && (size_t)(sIdx + 2) < sourcePixels.size()) {
                            col = IM_COL32(sourcePixels[sIdx], sourcePixels[sIdx + 1], sourcePixels[sIdx + 2], 255);
                        }

                        float cx = magPos.x + (gx + magRadius) * cellSize;
                        float cy = magPos.y + (gy + magRadius) * cellSize;
                        fg->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + cellSize, cy + cellSize), col);
                    }
                }

                // Draw crosshair on center pixel
                float centerX = magPos.x + magRadius * cellSize;
                float centerY = magPos.y + magRadius * cellSize;
                fg->AddRect(ImVec2(centerX, centerY),
                            ImVec2(centerX + cellSize, centerY + cellSize),
                            IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);
                fg->AddRect(ImVec2(centerX + 1, centerY + 1),
                            ImVec2(centerX + cellSize - 1, centerY + cellSize - 1),
                            IM_COL32(0, 0, 0, 255), 0.0f, 0, 1.0f);
            }
        }
    }

    // ── Comparison Logic (Hover Fade) ────────────────────────────────────────

    // 1) Draw checkerboard background so transparency is visible
    float checkerSize = 16.0f;
    float tilesX = dispW / checkerSize;
    float tilesY = dispH / checkerSize;
    ImGui::Image((ImTextureID)(intptr_t)m_CheckerTex, ImVec2(dispW, dispH),
                 ImVec2(0, 0), ImVec2(tilesX, tilesY));
    const bool imageHovered = ImGui::IsItemHovered();
    const ImVec2 imageMin = ImGui::GetItemRectMin();
    const ImVec2 imageMax = ImGui::GetItemRectMax();

    // Handle Fade Factor (hover to compare with original)
    float hoverTarget = (imageHovered && !m_IsLocked) ? 1.0f : 0.0f;
    float currentFactor = editor->GetHoverFade();
    const float fadeStep = 1.0f - std::exp(-ImGui::GetIO().DeltaTime / 0.5f);
    currentFactor += (hoverTarget - currentFactor) * fadeStep;
    currentFactor = std::max(0.0f, std::min(currentFactor, 1.0f));
    editor->SetHoverFade(currentFactor);

    // 2) Draw processed output fully opaque, then fade the original over it.
    drawList->AddImage((ImTextureID)(intptr_t)outputTex, imageMin, imageMax,
                       ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);

    unsigned int sourceTex = pipeline.GetSourceTexture();
    if (currentFactor > 0.001f) {
        drawList->AddImage((ImTextureID)(intptr_t)sourceTex, imageMin, imageMax,
                           ImVec2(0, 0), ImVec2(1, 1),
                           IM_COL32(255, 255, 255, static_cast<int>(currentFactor * 255.0f)));
    }

    ImGui::End();
}
