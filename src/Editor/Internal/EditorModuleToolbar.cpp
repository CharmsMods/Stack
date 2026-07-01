#include "Editor/EditorModule.h"

#include "App/settings/AppearanceTheme.h"
#include "App/Resources/EmbeddedTabIcons.h"
#include "Renderer/GLHelpers.h"
#include "ThirdParty/stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

namespace {

unsigned int g_ToneCurveIconTexture = 0;
unsigned int g_ViewTransformIconTexture = 0;
unsigned int g_HdrMergeIconTexture = 0;
unsigned int g_CustomMaskIconTexture = 0;
unsigned int g_LutIconTexture = 0;
unsigned int g_DenoiseIconTexture = 0;

bool HasBlockingModalPopupOpen() {
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (!context) {
        return false;
    }

    for (int i = context->OpenPopupStack.Size - 1; i >= 0; --i) {
        const ImGuiPopupData& popup = context->OpenPopupStack[i];
        if (popup.Window && (popup.Window->Flags & ImGuiWindowFlags_Modal) != 0) {
            return true;
        }
    }
    return false;
}

void CloseNonModalPopupsForToolbarSwitch() {
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (!context || context->OpenPopupStack.empty() || HasBlockingModalPopupOpen()) {
        return;
    }
    ImGui::ClosePopupToLevel(0, true);
}

unsigned int LoadEditorResourceTexture(const unsigned char* data, unsigned int size, const char* debugName) {
    if (!data || size == 0) {
        return 0;
    }
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(0);
    unsigned char* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
    if (!pixels) {
        fprintf(stderr, "[EditorModule] Failed to decode embedded %s icon.\n", debugName);
        return 0;
    }
    const unsigned int texture = GLHelpers::CreateTextureFromPixels(pixels, width, height, 4);
    stbi_image_free(pixels);
    return texture;
}

ImU32 ResolveFloatingToolbarIconTint(const StackAppearance::AppearanceManager* appearance, bool emphasized, bool hovered) {
    if (StackAppearance::UseDarkIconsForCurrentTheme(appearance)) {
        return emphasized
            ? IM_COL32(16, 20, 24, 255)
            : (hovered ? IM_COL32(42, 48, 56, 246) : IM_COL32(82, 88, 96, 226));
    }
    return emphasized
        ? IM_COL32(255, 255, 255, 255)
        : (hovered ? IM_COL32(238, 240, 244, 246) : IM_COL32(204, 208, 214, 218));
}

} // namespace

void EditorModule::SwitchToSubWindow(EditorSubWindow target) {
    if (m_ActiveSubWindow == target && m_TargetSubWindow == target) {
        return;
    }
    if (target != EditorSubWindow::NodeGraph) {
        m_NodeGraphFullscreen = false;
        m_Sidebar.GetNodeGraphUI().CloseTransientDrawers();
    }
    m_TargetSubWindow = target;
    if (target != EditorSubWindow::ComplexNode) {
        m_TargetComplexNodeId = -1;
    }
    m_SubWindowTransitionFadingOut = true;
}

void EditorModule::SwitchToComplexNodeSubWindow(int nodeId) {
    if (m_ActiveSubWindow == EditorSubWindow::ComplexNode && m_ActiveComplexNodeId == nodeId &&
        m_TargetSubWindow == EditorSubWindow::ComplexNode && m_TargetComplexNodeId == nodeId) {
        return;
    }
    if (m_ActiveSubWindow == EditorSubWindow::ComplexNode &&
        m_TargetSubWindow == EditorSubWindow::ComplexNode &&
        !m_SubWindowTransitionFadingOut &&
        m_SubWindowTransitionAlpha >= 0.999f) {
        m_ActiveComplexNodeId = nodeId;
        m_TargetComplexNodeId = nodeId;
        return;
    }
    m_NodeGraphFullscreen = false;
    m_Sidebar.GetNodeGraphUI().CloseTransientDrawers();
    m_TargetSubWindow = EditorSubWindow::ComplexNode;
    m_TargetComplexNodeId = nodeId;
    m_SubWindowTransitionFadingOut = true;
}

void EditorModule::TogglePresetsSubWindow() {
    const bool presetsSelected = m_TargetSubWindow == EditorSubWindow::Presets &&
        (m_ActiveSubWindow == EditorSubWindow::Presets || !m_SubWindowTransitionFadingOut);
    if (presetsSelected && m_ActiveSubWindow == EditorSubWindow::Presets) {
        SwitchToSubWindow(EditorSubWindow::NodeGraph);
        return;
    }
    SwitchToSubWindow(EditorSubWindow::Presets);
}

void EditorModule::LoadResourceTextures() {
    if (m_TexturesLoaded) {
        return;
    }
    m_NodeGraphIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::NodeGraph_png_data,
        EmbeddedTabIcons::NodeGraph_png_size,
        "NodeGraph"
    );
    m_PresetsIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::NodePresets_png_data,
        EmbeddedTabIcons::NodePresets_png_size,
        "NodePresets"
    );
    m_ExportIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::Export_png_data,
        EmbeddedTabIcons::Export_png_size,
        "Export"
    );
    m_SettingsIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::Settings_png_data,
        EmbeddedTabIcons::Settings_png_size,
        "Settings"
    );
    m_BackgroundRemoverIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::BackgroundRemover_png_data,
        EmbeddedTabIcons::BackgroundRemover_png_size,
        "BackgroundRemover"
    );
    m_ColorGradeIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::ColorGrade_png_data,
        EmbeddedTabIcons::ColorGrade_png_size,
        "ColorGrade"
    );
    g_ToneCurveIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::ToneCurve_png_data,
        EmbeddedTabIcons::ToneCurve_png_size,
        "ToneCurve"
    );
    g_ViewTransformIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::ViewTransform_png_data,
        EmbeddedTabIcons::ViewTransform_png_size,
        "ViewTransform"
    );
    g_HdrMergeIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::HdrMerge_png_data,
        EmbeddedTabIcons::HdrMerge_png_size,
        "HdrMerge"
    );
    g_CustomMaskIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::CustomMask_png_data,
        EmbeddedTabIcons::CustomMask_png_size,
        "CustomMask"
    );
    g_LutIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::Lut_png_data,
        EmbeddedTabIcons::Lut_png_size,
        "Lut"
    );
    g_DenoiseIconTexture = LoadEditorResourceTexture(
        EmbeddedTabIcons::Denoise_png_data,
        EmbeddedTabIcons::Denoise_png_size,
        "Denoise"
    );
    m_TexturesLoaded = true;
}

void EditorModule::RenderFloatingToolbar() {
    LoadResourceTextures();

    const float radius = 20.0f;
    const float margin = 24.0f;
    const float spacing = 12.0f;
    const float buttonDiameter = radius * 2.0f;
    const float rowGap = 8.0f;
    const float rowStep = buttonDiameter + rowGap;

    // Get the position and size of the parent child window (EditorGraphPane)
    const ImVec2 parentPos = ImGui::GetWindowPos();
    const ImVec2 parentSize = ImGui::GetWindowSize();

    const float leftDrawerOffset = std::max(m_LeftPanelWidthAnim, m_NodesPanelWidthAnim);

    const ImVec2 basePos(
        parentPos.x + margin + radius + leftDrawerOffset,
        parentPos.y + parentSize.y - margin - radius
    );

    struct ToolbarButton {
        bool isStandard = true;
        EditorSubWindow subWin = EditorSubWindow::NodeGraph;
        int nodeId = -1;
        EditorNodeGraph::NodeKind nodeKind = EditorNodeGraph::NodeKind::Image;
        std::string typeId;
        std::string label;
    };

    std::vector<ToolbarButton> buttons;
    buttons.push_back({ true, EditorSubWindow::NodeGraph, -1, EditorNodeGraph::NodeKind::Image, "", "Node Graph [1]" });
    buttons.push_back({ true, EditorSubWindow::Presets, -1, EditorNodeGraph::NodeKind::Image, "", "Presets [Ctrl+Tab]" });

    // Append dynamic complex node buttons
    for (const auto& node : m_NodeGraph.GetNodes()) {
        if (!NodeHasDedicatedComplexEditor(node.id)) {
            continue;
        }
        const LayerDescriptor* descriptor = LayerRegistry::FindDescriptorByTypeId(node.typeId);
        const std::string label = !node.title.empty()
            ? node.title
            : (descriptor && descriptor->displayName ? descriptor->displayName : std::string("Advanced Node"));
        buttons.push_back({ false, EditorSubWindow::ComplexNode, node.id, node.kind, node.typeId, label });
    }

    if (GetCompletedChainCount() >= 2) {
        buttons.push_back({ true, EditorSubWindow::ExportSettings, -1, EditorNodeGraph::NodeKind::Image, "", "Export Settings" });
    }

    // Keep the toolbar inside the available editor pane width by wrapping into rows when needed.
    const float availableRowWidth = std::max(
        0.0f,
        parentSize.x - margin - leftDrawerOffset - (radius * 2.0f + 8.0f)
    );
    const size_t buttonsPerRow = std::max<size_t>(
        1,
        static_cast<size_t>((availableRowWidth + spacing) / (buttonDiameter + spacing))
    );
    const size_t rowCount = (buttons.size() + buttonsPerRow - 1) / buttonsPerRow;
    const size_t widestRowButtonCount = std::min(buttonsPerRow, buttons.size());
    const float toolbarContentWidth = widestRowButtonCount * (buttonDiameter + spacing) - spacing;
    const float toolbarWidth = toolbarContentWidth + radius * 2.0f + 8.0f;
    const float toolbarHeight = static_cast<float>(rowCount) * buttonDiameter +
        static_cast<float>(rowCount > 0 ? rowCount - 1 : 0) * rowGap + 8.0f;
    const float topRowOffset = static_cast<float>(rowCount > 0 ? rowCount - 1 : 0) * rowStep;
    ImVec2 toolbarPos(basePos.x - radius - 4.0f, basePos.y - radius - 4.0f - topRowOffset);

    const float minToolbarX = parentPos.x + 4.0f;
    const float maxToolbarX = parentPos.x + parentSize.x - toolbarWidth - 4.0f;
    if (maxToolbarX >= minToolbarX) {
        toolbarPos.x = std::clamp(toolbarPos.x, minToolbarX, maxToolbarX);
    } else {
        toolbarPos.x = minToolbarX;
    }

    const float minToolbarY = parentPos.y + 4.0f;
    const float maxToolbarY = parentPos.y + parentSize.y - toolbarHeight - 4.0f;
    if (maxToolbarY >= minToolbarY) {
        toolbarPos.y = std::clamp(toolbarPos.y, minToolbarY, maxToolbarY);
    } else {
        toolbarPos.y = minToolbarY;
    }

    ImGui::SetNextWindowPos(toolbarPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(toolbarWidth, toolbarHeight), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(10.0f, 10.0f));

    ImGui::Begin("##EditorFloatingToolbar", nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_AlwaysAutoResize);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    auto scaleAlpha = [](ImU32 col, float alpha) -> ImU32 {
        unsigned int r = col & 0xFF;
        unsigned int g = (col >> 8) & 0xFF;
        unsigned int b = (col >> 16) & 0xFF;
        unsigned int a = (col >> 24) & 0xFF;
        unsigned int newA = static_cast<unsigned int>(a * alpha);
        return r | (g << 8) | (b << 16) | (newA << 24);
    };

    for (size_t i = 0; i < buttons.size(); ++i) {
        const auto& btn = buttons[i];
        const bool isActive = btn.isStandard
            ? (m_TargetSubWindow == btn.subWin)
            : (m_TargetSubWindow == EditorSubWindow::ComplexNode && m_TargetComplexNodeId == btn.nodeId);

        const size_t rowIndex = i / buttonsPerRow;
        const size_t columnIndex = i % buttonsPerRow;
        ImVec2 center(
            basePos.x + static_cast<float>(columnIndex) * (buttonDiameter + spacing),
            basePos.y - topRowOffset + static_cast<float>(rowIndex) * rowStep
        );

        float animAlpha = 1.0f;
        if (!btn.isStandard) {
            double curTime = ImGui::GetTime();
            double spawnTime = curTime;
            auto it = m_ToolbarButtonSpawnTimes.find(btn.nodeId);
            if (it == m_ToolbarButtonSpawnTimes.end()) {
                m_ToolbarButtonSpawnTimes[btn.nodeId] = curTime;
            } else {
                spawnTime = it->second;
            }
            double age = curTime - spawnTime;
            float tAnim = std::clamp(static_cast<float>(age / 0.4), 0.0f, 1.0f);
            float easeOut = 1.0f - std::pow(1.0f - tAnim, 3.0f);

            center.x += (easeOut - 1.0f) * 40.0f;
            animAlpha = easeOut;
        }
        ImGui::SetCursorScreenPos(ImVec2(center.x - radius, center.y - radius));
        char btnId[64];
        if (btn.isStandard) {
            snprintf(btnId, sizeof(btnId), "##FloatingSubWindowBtn_%d", static_cast<int>(btn.subWin));
        } else {
            snprintf(btnId, sizeof(btnId), "##FloatingComplexNodeBtn_%d", btn.nodeId);
        }

        ImGui::InvisibleButton(btnId, ImVec2(buttonDiameter, buttonDiameter));
        const bool nonModalPopupOpen =
            ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
            !HasBlockingModalPopupOpen();
        const ImVec2 mousePos = ImGui::GetIO().MousePos;
        const float dx = mousePos.x - center.x;
        const float dy = mousePos.y - center.y;
        const bool manualHover = nonModalPopupOpen && (dx * dx + dy * dy) <= (radius * radius);
        const bool hovered = ImGui::IsItemHovered() || manualHover;
        const bool clicked = ImGui::IsItemClicked() || (manualHover && ImGui::IsMouseClicked(ImGuiMouseButton_Left));

        if (clicked) {
            CloseNonModalPopupsForToolbarSwitch();
            if (btn.isStandard) {
                if (btn.subWin == EditorSubWindow::Presets) {
                    TogglePresetsSubWindow();
                } else {
                    SwitchToSubWindow(btn.subWin);
                }
            } else {
                SwitchToComplexNodeSubWindow(btn.nodeId);
            }
        }

        unsigned int tex = 0;
        if (btn.isStandard) {
            if (btn.subWin == EditorSubWindow::NodeGraph) {
                tex = m_NodeGraphIconTexture;
            } else if (btn.subWin == EditorSubWindow::Presets) {
                tex = m_PresetsIconTexture;
            } else if (btn.subWin == EditorSubWindow::ExportSettings) {
                tex = m_ExportIconTexture;
            }
        } else {
            if (btn.nodeKind == EditorNodeGraph::NodeKind::HdrMerge) {
                tex = g_HdrMergeIconTexture;
            } else if (btn.nodeKind == EditorNodeGraph::NodeKind::CustomMask) {
                tex = g_CustomMaskIconTexture;
            } else if (btn.nodeKind == EditorNodeGraph::NodeKind::Lut) {
                tex = g_LutIconTexture;
            } else if (btn.typeId == "BackgroundPatcher") {
                tex = m_BackgroundRemoverIconTexture;
            } else if (btn.typeId == "ColorGrade") {
                tex = m_ColorGradeIconTexture;
            } else if (btn.typeId == "ToneCurve") {
                tex = g_ToneCurveIconTexture;
            } else if (btn.typeId == "ViewTransform") {
                tex = g_ViewTransformIconTexture;
            } else if (btn.typeId == "ClassicalRgbDenoise" || btn.typeId == "LinearRgbNeuralDenoise") {
                tex = g_DenoiseIconTexture;
            } else {
                tex = m_SettingsIconTexture;
            }
        }

        if (tex) {
            const float iconHalfSize = 12.0f;
            ImVec2 pMin(center.x - iconHalfSize, center.y - iconHalfSize);
            ImVec2 pMax(center.x + iconHalfSize, center.y + iconHalfSize);

            ImU32 iconColor = ResolveFloatingToolbarIconTint(
                GetAppearance(),
                isActive || ImGui::IsItemActive(),
                hovered);
            if (animAlpha < 1.0f) {
                iconColor = scaleAlpha(iconColor, animAlpha);
            }
            drawList->AddImage((ImTextureID)(intptr_t)tex, pMin, pMax, ImVec2(0, 0), ImVec2(1, 1), iconColor);
        }

        if (hovered) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(btn.label.c_str());
            ImGui::EndTooltip();
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(3);

    // Clean up spawn times map
    for (auto it = m_ToolbarButtonSpawnTimes.begin(); it != m_ToolbarButtonSpawnTimes.end();) {
        bool found = false;
        for (const auto& btn : buttons) {
            if (!btn.isStandard && btn.nodeId == it->first) {
                found = true;
                break;
            }
        }
        if (!found) {
            it = m_ToolbarButtonSpawnTimes.erase(it);
        } else {
            ++it;
        }
    }
}
