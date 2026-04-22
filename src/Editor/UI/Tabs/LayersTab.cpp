#include "LayersTab.h"
#include "Editor/EditorModule.h"
#include <imgui.h>

void LayersTab::Initialize() {}

void LayersTab::Render(EditorModule* editor) {
    ImGui::Text("Available Modules");
    ImGui::TextDisabled("Click to add to the end of the pipeline");
    ImGui::Separator();
    ImGui::Spacing();

    // Plain listing of layers as buttons/selectables, grouped by category but always visible
    
    auto LayerButton = [&](const char* label, LayerType type) {
        if (ImGui::Button(label, ImVec2(-FLT_MIN, 0))) {
            editor->AddLayer(type);
        }
    };

    ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "BASE");
    LayerButton("Crop / Rotate / Flip", LayerType::CropTransform);
    LayerButton("Expander (Canvas Pad)", LayerType::Expander);
    LayerButton("Background Patcher", LayerType::BackgroundPatcher);
    LayerButton("Alpha Handling / Protect", LayerType::AlphaHandling);
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "COLOR");
    LayerButton("Adjustments (Color/Contrast)", LayerType::Adjustments);
    LayerButton("3-Way Color Grade", LayerType::ColorGrade);
    LayerButton("HDR Emulation", LayerType::HDR);
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.7f, 1.0f), "TEXTURE & BLUR");
    LayerButton("Blur (Box/Gaussian)", LayerType::Blur);
    LayerButton("Denoising", LayerType::Denoising);
    LayerButton("Bilateral Filter", LayerType::BilateralFilter);
    LayerButton("Noise / Film Grain",   LayerType::Noise);
    LayerButton("Tilt-Shift Blur", LayerType::TiltShiftBlur);
    LayerButton("Hankel (Optical) Blur", LayerType::HankelBlur);
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.55f, 1.0f), "STYLIZE");
    LayerButton("Dithering", LayerType::Dither);
    LayerButton("Halftoning (Dots)", LayerType::Halftoning);
    LayerButton("Cell Shading", LayerType::CellShading);
    LayerButton("Palette Reconstructor", LayerType::PaletteReconstructor);
    LayerButton("Edge Effects", LayerType::EdgeEffects);
    LayerButton("Text Overlay", LayerType::TextOverlay);
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.55f, 1.0f), "DAMAGE & GLITCH");
    LayerButton("Compression", LayerType::Compression);
    LayerButton("Corruption (Digital)", LayerType::Corruption);
    LayerButton("Image Breaks", LayerType::ImageBreaks);
    LayerButton("Analog (VHS/CRT)", LayerType::AnalogVideo);
    ImGui::Spacing();

    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.7f, 1.0f), "OPTICS");
    LayerButton("Vignette", LayerType::Vignette);
    LayerButton("Glare Rays", LayerType::GlareRays);
    LayerButton("Chromatic Aberration", LayerType::ChromaticAberration);
    LayerButton("Lens Distortion", LayerType::LensDistortion);
    LayerButton("Heatwave & Ripples", LayerType::Heatwave);
    LayerButton("Airy Bloom", LayerType::AiryBloom);
    ImGui::Spacing();
}

