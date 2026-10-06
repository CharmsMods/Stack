#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawDenoiseMapEditor.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"

#include "Restormer/RestormerClient.h"
#include <imgui_internal.h>

#include <algorithm>
#include <string>

using namespace Stack::Editor::RawLabInternal;

namespace {
const char* RgbDenoiseMethodLabel(
    Stack::RawRecipe::RawRgbDenoiseMethod method) {
    switch (method) {
        case Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1:
            return "Adaptive Wavelet Map";
        case Stack::RawRecipe::RawRgbDenoiseMethod::RestormerRealV1:
            return "Restormer Real Photo";
        case Stack::RawRecipe::RawRgbDenoiseMethod::RestormerGaussianBlindV1:
            return "Restormer Gaussian (Blind)";
    }
    return "Classical Multiscale";
}

const char* RgbDenoiseMappingLabel(
    Stack::RawRecipe::RawRgbDenoiseMapping mapping) {
    switch (mapping) {
        case Stack::RawRecipe::RawRgbDenoiseMapping::SceneLinearSafeV1:
            return "Scene-linear Safe";
        case Stack::RawRecipe::RawRgbDenoiseMapping::ProcessedRgbMatchV1:
            return "Processed RGB Match";
    }
    return "Scene-linear Safe";
}


} // namespace

bool EditorModule::RenderRawWorkspaceLabRgbDenoiseSurface(
    RawWorkspaceEditContext& context) {
    bool changed = false;
    Stack::RawRecipe::RawRgbDenoiseRecipe& denoise = context.recipe.rgbDenoise;

    if (BypassEyeButton(
            "##RawLabRgbDenoiseBypass",
            denoise.enabled,
            "RGB Denoise is included when its authored amounts have an effect. Click to bypass it without losing any settings.",
            "RGB Denoise is bypassed. Click to include the preserved settings again.")) {
        denoise.enabled = !denoise.enabled;
        changed = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Post-demosaic scene-linear frequency denoise");
    ImGui::TextDisabled("Zero amounts leave this pipeline stage unrendered.");
    LabTooltip(
        "Denoises scene-linear luma and chroma after demosaic and white balance, "
        "before RAW Exposure and tone compression. The stage is skipped until "
        "a luma or chroma amount is added.");

    ImGui::Spacing();
    ImGui::TextDisabled("Method");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    if (ImGui::BeginCombo(
            "##RawLabRgbDenoiseMethod",
            RgbDenoiseMethodLabel(denoise.method))) {
        constexpr Stack::RawRecipe::RawRgbDenoiseMethod methods[] = {
            Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1,
            Stack::RawRecipe::RawRgbDenoiseMethod::RestormerRealV1,
            Stack::RawRecipe::RawRgbDenoiseMethod::RestormerGaussianBlindV1
        };
        for (const auto method : methods) {
            const bool selected = denoise.method == method;
            if (ImGui::Selectable(
                    RgbDenoiseMethodLabel(method), selected) && !selected) {
                denoise.method = method;
                if (method ==
                    Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1) {
                    denoise.packageVersion.clear();
                    denoise.modelSha256.clear();
                    m_RawWorkspaceRestormerPackageStatusText.clear();
                } else {
                    denoise.packageId =
                        Stack::RawRecipe::kRestormerDenoisePackageId;
                    denoise.adapterVersion =
                        Stack::RawRecipe::kRestormerDenoiseAdapterVersion;
                    const Stack::Restormer::ValidationResult package =
                        Stack::Restormer::Client::Instance().Validate(
                            denoise, true);
                    if (package.ok) {
                        denoise.packageVersion =
                            package.manifest.packageVersion;
                        denoise.modelSha256 =
                            package.selectedModel.sha256;
                        m_RawWorkspaceRestormerPackageStatusText.clear();
                    } else {
                        denoise.packageVersion.clear();
                        denoise.modelSha256.clear();
                        m_RawWorkspaceRestormerPackageStatusText =
                            package.error;
                    }
                }
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar();
    LabTooltip(
        "Real Photo is intended for genuine camera noise. Gaussian (Blind) "
        "is the synthetic-noise comparison model.");

    const bool aiMethod =
        denoise.method !=
        Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1;
    if (aiMethod) {
        ImGui::Spacing();
        ImGui::TextDisabled("Model Mapping");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        if (ImGui::BeginCombo(
                "##RawLabRgbDenoiseMapping",
                RgbDenoiseMappingLabel(denoise.mapping))) {
            constexpr Stack::RawRecipe::RawRgbDenoiseMapping mappings[] = {
                Stack::RawRecipe::RawRgbDenoiseMapping::SceneLinearSafeV1,
                Stack::RawRecipe::RawRgbDenoiseMapping::ProcessedRgbMatchV1
            };
            for (const auto mapping : mappings) {
                const bool selected = denoise.mapping == mapping;
                if (ImGui::Selectable(
                        RgbDenoiseMappingLabel(mapping), selected) &&
                    !selected) {
                    denoise.mapping = mapping;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar();
        LabTooltip(
            "Scene-linear Safe removes model drift and bounds the residual. "
            "Processed RGB Match keeps more of the model's native response.");
        if (denoise.packageVersion.empty() || denoise.modelSha256.empty()) {
            ImGui::TextWrapped(
                "The optional Restormer package has not been pinned yet. "
                "Rendering and export remain blocked until an approved local "
                "package is installed, or you select Adaptive Wavelet Map.");
            if (BareTextButton("Check Package")) {
                const Stack::Restormer::ValidationResult package =
                    Stack::Restormer::Client::Instance().Validate(
                        denoise, true);
                if (package.ok) {
                    denoise.packageVersion =
                        package.manifest.packageVersion;
                    denoise.modelSha256 =
                        package.selectedModel.sha256;
                    denoise.adapterVersion =
                        package.manifest.adapterVersion;
                    m_RawWorkspaceRestormerPackageStatusText.clear();
                    changed = true;
                } else {
                    m_RawWorkspaceRestormerPackageStatusText =
                        package.error;
                }
            }
            LabTooltip(
                "Validates every package artifact and pins this recipe to the "
                "installed package version and selected model hash.");
            if (!m_RawWorkspaceRestormerPackageStatusText.empty()) {
                ImGui::TextWrapped(
                    "%s",
                    m_RawWorkspaceRestormerPackageStatusText.c_str());
            }
        } else {
            ImGui::TextDisabled(
                "Package %s | model %.12s...",
                denoise.packageVersion.c_str(),
                denoise.modelSha256.c_str());
            if (BareTextButton("Recheck Package")) {
                const Stack::Restormer::ValidationResult package =
                    Stack::Restormer::Client::Instance().Validate(
                        denoise, false);
                m_RawWorkspaceRestormerPackageStatusText =
                    package.ok
                        ? "Package check passed."
                        : package.error;
            }
            LabTooltip(
                "Revalidates the pinned package manifest, model hash, helper, "
                "runtime, licenses, and notices.");
            if (!m_RawWorkspaceRestormerPackageStatusText.empty()) {
                ImGui::TextWrapped(
                    "%s",
                    m_RawWorkspaceRestormerPackageStatusText.c_str());
            }
        }
        const std::string& denoiseStatus =
            m_Pipeline.GetRawRgbDenoiseStatus();
        if (!denoiseStatus.empty()) {
            ImGui::TextDisabled("%s", denoiseStatus.c_str());
        }
        if (!m_RawWorkspaceStaleRenderStatusText.empty()) {
            ImGui::TextWrapped(
                "Preview stale: %s",
                m_RawWorkspaceStaleRenderStatusText.c_str());
        }
    }

    ImGui::Spacing();
    if (!aiMethod) {
        const RawDenoiseMapEditorResult mapResult =
            RenderRawDenoiseMapEditor(
                denoise,
                m_RawWorkspaceLabUi.denoiseMap);
        changed |= mapResult.recipeChanged;
        if (mapResult.viewChanged) {
            MarkRenderRefreshDirty();
        }
        LabTooltip(
            "Map values add denoising from zero. Zero preserves coefficients "
            "and one uses Stack's automatic threshold.");
    } else {
        float colorNoise = denoise.colorNoise;
        if (BareSliderFloat(
                "Color Noise",
                "RawLabRgbDenoiseColor",
                &colorNoise,
                0.0f,
                1.0f,
                "%.2f",
                std::max(150.0f, ImGui::GetContentRegionAvail().x - 120.0f))) {
            denoise.colorNoise = colorNoise;
            changed = true;
        }
        LabTooltip("Scales the model residual in opponent-color channels.");

        float luminanceNoise = denoise.luminanceNoise;
        if (BareSliderFloat(
                "Luminance Noise",
                "RawLabRgbDenoiseLuminance",
                &luminanceNoise,
                0.0f,
                1.0f,
                "%.2f",
                std::max(140.0f, ImGui::GetContentRegionAvail().x - 145.0f))) {
            denoise.luminanceNoise = luminanceNoise;
            changed = true;
        }
        LabTooltip("Scales the model residual along the luminance direction.");
    }
    ImGui::Spacing();
    if (aiMethod) {
        ImGui::TextDisabled(
            "AI runs outside Stack.exe. Judge the settled result at 100%% zoom.");
    } else {
        ImGui::TextDisabled(
            "Frequency coordinates stay tied to source pixels. Judge fine detail at 100%% zoom.");
    }
    return changed;
}

bool EditorModule::RenderRawWorkspaceLabRgbDenoiseSecondaryControls(
    RawWorkspaceEditContext& context) {
    bool changed = false;
    Stack::RawRecipe::RawRgbDenoiseRecipe& denoise = context.recipe.rgbDenoise;
    float detailProtection = denoise.detailProtection;
    if (BareSliderFloat(
            "Detail Protection",
            "RawLabRgbDenoiseDetail",
            &detailProtection,
            0.0f,
            1.0f,
            "%.2f")) {
        denoise.detailProtection = detailProtection;
        changed = true;
    }
    if (denoise.method ==
        Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1) {
        float edgeSensitivity = denoise.edgeSensitivity;
        if (BareSliderFloat(
                "Edge Sensitivity",
                "RawLabRgbDenoiseEdgeSensitivity",
                &edgeSensitivity,
                0.0f,
                1.0f,
                "%.2f")) {
            denoise.edgeSensitivity = edgeSensitivity;
            changed = true;
        }
        float maximumStructure = denoise.maximumStructureSize;
        if (BareSliderFloat(
                "Largest Structure",
                "RawLabRgbDenoiseLargestStructure",
                &maximumStructure,
                8.0f,
                2048.0f,
                "%.0f source px")) {
            denoise.maximumStructureSize = maximumStructure;
            changed = true;
        }
        ImGui::TextWrapped(
            "Stack chooses the band count from source resolution and the largest "
            "structure. Edge sensitivity changes the decomposition, not the map.");
    }
    ImGui::Spacing();
    ImGui::TextDisabled(
        "Pipeline: demosaic and white balance, RGB denoise, RAW Exposure.");
    return changed;
}

void EditorModule::ApplyRawWorkspaceDenoiseViewOverride(
    Stack::RawRecipe::RawDevelopmentRecipe& recipe) const {
    if (m_RawWorkspaceLabUi.activeTool != RawLabTool::RgbDenoise ||
        recipe.rgbDenoise.method !=
            Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1) {
        return;
    }
    const auto& state = m_RawWorkspaceLabUi.denoiseMap;
    recipe.rgbDenoise.diagnosticMode =
        state.pointDragDiagnosticActive
        ? Stack::RawRecipe::RawDenoiseDiagnosticMode::SelectedControlInfluence
        : static_cast<Stack::RawRecipe::RawDenoiseDiagnosticMode>(
            std::clamp(state.diagnosticMode, 0, 6));
    recipe.rgbDenoise.diagnosticLayer = state.activeLayer == 1
        ? Stack::RawRecipe::RawDenoiseMapLayer::Chroma
        : Stack::RawRecipe::RawDenoiseMapLayer::Luma;
    recipe.rgbDenoise.diagnosticPointId = state.selectedPointId;
}
