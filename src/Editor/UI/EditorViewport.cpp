#include "EditorViewport.h"
#include "Editor/EditorModule.h"
#include <imgui.h>
#include <algorithm>

EditorViewport::EditorViewport() 
    : m_ZoomLevel(1.0f), m_PanX(0.0f), m_PanY(0.0f), m_IsLocked(false) 
{}

EditorViewport::~EditorViewport() {}

void EditorViewport::Initialize() {}

void EditorViewport::Render(EditorModule* editor) {
    auto& pipeline = editor->GetPipeline();

    ImGui::Begin("Canvas Viewport");

    if (!pipeline.HasSourceImage()) {
        ImGui::Text("No image loaded. Go to the 'Canvas' tab to load an image.");
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

    // Overlay lock status
    if (m_IsLocked) {
        ImGui::SetCursorPos(ImVec2(10, 30));
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "[ ZOOM LOCKED ] - Press 'L' to unlock");
    } else {
        ImGui::SetCursorPos(ImVec2(10, 30));
        ImGui::TextDisabled("Scroll to Zoom | 'L' to Lock | Movement Pans");
    }

    // Centering calculations
    float offsetX = (avail.x - dispW) * 0.5f + m_PanX;
    float offsetY = (avail.y - dispH) * 0.5f + m_PanY;

    ImVec2 cursorPos = ImGui::GetCursorPos();
    ImVec2 startCursorPos = ImVec2(cursorPos.x + offsetX, cursorPos.y + offsetY);
    ImGui::SetCursorPos(startCursorPos);
    
    ImVec2 drawScreenPos = ImGui::GetCursorScreenPos();

    // ── Comparison Logic (Hover Fade) ────────────────────────────────────────
    
    // Display Original Image underneath (Base)
    unsigned int sourceTex = pipeline.GetSourceTexture();
    ImGui::Image((ImTextureID)(intptr_t)sourceTex, ImVec2(dispW, dispH), ImVec2(0, 1), ImVec2(1, 0));

    // Handle Fade Factor
    float hoverTarget = (ImGui::IsItemHovered() && !m_IsLocked) ? 1.0f : 0.0f;
    float currentFactor = editor->GetHoverFade();
    currentFactor += (hoverTarget - currentFactor) * 8.0f * ImGui::GetIO().DeltaTime;
    currentFactor = std::max(0.0f, std::min(currentFactor, 1.0f));
    editor->SetHoverFade(currentFactor);

    // Display Processed Image on top (Fade out on hover)
    ImGui::SetCursorScreenPos(drawScreenPos);
    ImGui::Image((ImTextureID)(intptr_t)outputTex, ImVec2(dispW, dispH), 
                 ImVec2(0, 1), ImVec2(1, 0), 
                 ImVec4(1, 1, 1, 1.0f - currentFactor), 
                 ImVec4(0, 0, 0, 0));

    ImGui::End();
}
