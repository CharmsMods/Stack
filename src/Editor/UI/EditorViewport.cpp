#include "EditorViewport.h"
#include "Editor/EditorModule.h"
#include "EditorViewportHelpers.h"
#include "Library/LibraryManager.h"
#include "Renderer/GLHelpers.h"
#include "Utils/FileDialogs.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

using namespace EditorViewportHelpers;

EditorViewport::EditorViewport() 
    : m_ZoomLevel(1.0f), m_PanX(0.0f), m_PanY(0.0f), m_IsLocked(false) 
{}

EditorViewport::~EditorViewport() {
    if (m_CheckerTex) glDeleteTextures(1, &m_CheckerTex);
}

void EditorViewport::ResetSinglePreviewState() {
    m_ZoomLevel = 1.0f;
    m_PanX = 0.0f;
    m_PanY = 0.0f;
    m_IsLocked = false;
}

void EditorViewport::Initialize() {
    // Create a 2x2 checkerboard texture for transparency display
    unsigned char checker[2 * 2 * 4] = {
        52, 60, 66, 255,   68, 78, 84, 255,
        68, 78, 84, 255,   52, 60, 66, 255
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
    const bool compositeMode = editor->IsCompositeViewportMode();
    const bool inputBlocked = false;

    if (compositeMode) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 screenPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        constexpr float kCanvasRounding = 18.0f;
        editor->EnsureCompositeSceneState(avail);
        editor->SetLastCompositeCanvasSize(avail);
        auto& exportSettings = editor->GetMutableCompositeExportSettings();

        const float margin = 12.0f;
        const ImVec2 canvasMin(screenPos.x + margin, screenPos.y + margin);
        const ImVec2 canvasMax(screenPos.x + avail.x - margin, screenPos.y + avail.y - margin);
        const ImU32 workspaceFill = ImGui::ColorConvertFloat4ToU32(editor->GetWorkspaceBaseColor());
        drawList->AddRectFilled(canvasMin, canvasMax, workspaceFill, kCanvasRounding);

        const float checkerScale = 24.0f;
        const float canvasW = std::max(1.0f, avail.x - margin * 2.0f);
        const float canvasH = std::max(1.0f, avail.y - margin * 2.0f);
        const float tilesX = std::max(1.0f, canvasW / checkerScale);
        const float tilesY = std::max(1.0f, canvasH / checkerScale);
        drawList->AddImageRounded(
            (ImTextureID)(intptr_t)m_CheckerTex,
            canvasMin,
            canvasMax,
            ImVec2(0.0f, 0.0f),
            ImVec2(tilesX, tilesY),
            IM_COL32(255, 255, 255, 105),
            kCanvasRounding);

        ImGui::InvisibleButton("CompositeCanvasSurface", avail);
        const bool hovered = !inputBlocked && ImGui::IsItemHovered();
        if (hovered && editor->CanConsumeEditorCommandKeys() && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            editor->ToggleCompositeExportBoundsEditMode();
        }
        const ImVec2 canvasCenter((canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f);
        const ImVec2 mousePos = ImGui::GetMousePos();
        const ImVec2 mouseWorld = ScreenToWorld(
            canvasCenter,
            editor->GetCompositeViewZoom(),
            editor->GetCompositeViewPanX(),
            editor->GetCompositeViewPanY(),
            mousePos);
        const bool exportBoundsEditMode = editor->IsCompositeExportBoundsEditMode();
        EditorModule::CompositeFloatRect exportBounds {};
        bool hasExportBounds = false;
        if (exportBoundsEditMode) {
            if (exportSettings.boundsMode == EditorModule::CompositeExportBoundsMode::Custom) {
                exportBounds = {
                    exportSettings.customX,
                    exportSettings.customY,
                    exportSettings.customWidth,
                    exportSettings.customHeight
                };
                hasExportBounds = exportBounds.width > 0.0f && exportBounds.height > 0.0f;
            } else {
                hasExportBounds = editor->TryGetCompositeAutoExportBounds(exportBounds);
            }
        }

        m_CompositeSnapGuides.clear();

        const EditorModule::CompositeSceneItem* hoveredItem = nullptr;
        for (int outputNodeId : editor->GetCompositeZOrder()) {
            const EditorModule::CompositeSceneItem* candidate = editor->FindCompositeSceneItem(outputNodeId);
            if (!candidate || !candidate->visible || candidate->texture == 0) {
                continue;
            }
            if (PointInQuad(ComputeSceneQuadWorld(*candidate), mouseWorld)) {
                hoveredItem = candidate;
                break;
            }
        }
        const EditorModule::CompositeSceneItem* selectedItem = editor->FindCompositeSceneItem(editor->GetCompositeSelectedOutputNodeId());
        if (selectedItem && editor->CanConsumeEditorCommandKeys()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
                editor->SetCompositeResizeMode(EditorModule::CompositeResizeMode::Stretch);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
                if (editor->GetCompositeResizeMode() == EditorModule::CompositeResizeMode::Scale) {
                    editor->ToggleCompositeScaleOriginMode();
                } else {
                    editor->SetCompositeResizeMode(EditorModule::CompositeResizeMode::Scale);
                }
            }
        }

        auto addSnapGuideWorld = [&](const ImVec2& a, const ImVec2& b, const ImU32 color) {
            m_CompositeSnapGuides.push_back({
                WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), a),
                WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), b),
                color
            });
        };

        auto beginSceneHandleInteraction = [&](EditorModule::CompositeSceneItem& item, const SceneHandleType handleType) {
            editor->SetCompositeSelectedOutputNodeId(item.outputNodeId);
            m_ActiveSceneHandle = handleType;
            m_ActiveSceneOutputNodeId = item.outputNodeId;
            m_SceneDragStartMouseWorldX = mouseWorld.x;
            m_SceneDragStartMouseWorldY = mouseWorld.y;
            m_SceneStartX = item.position.x;
            m_SceneStartY = item.position.y;
            m_SceneStartScaleX = item.scale.x;
            m_SceneStartScaleY = item.scale.y;
            m_SceneStartRotation = item.rotation;
            m_SceneStartWidth = std::max(1.0f, (item.isScalable ? 256.0f : static_cast<float>(item.textureWidth)) * std::max(0.0001f, item.scale.x));
            m_SceneStartHeight = std::max(1.0f, (item.isScalable ? 256.0f : static_cast<float>(item.textureHeight)) * std::max(0.0001f, item.scale.y));

            const std::array<ImVec2, 4> worldQuad = ComputeSceneQuadWorld(item);
            const ImVec2 worldTopCenter = Midpoint(worldQuad[0], worldQuad[1]);
            const ImVec2 worldBottomCenter = Midpoint(worldQuad[2], worldQuad[3]);
            const ImVec2 worldLeftCenter = Midpoint(worldQuad[0], worldQuad[3]);
            const ImVec2 worldRightCenter = Midpoint(worldQuad[1], worldQuad[2]);
            switch (handleType) {
                case SceneHandleType::ResizeTopLeft:
                    m_SceneResizeAnchorX = worldQuad[2].x;
                    m_SceneResizeAnchorY = worldQuad[2].y;
                    break;
                case SceneHandleType::ResizeTopRight:
                    m_SceneResizeAnchorX = worldQuad[3].x;
                    m_SceneResizeAnchorY = worldQuad[3].y;
                    break;
                case SceneHandleType::ResizeBottomRight:
                    m_SceneResizeAnchorX = worldQuad[0].x;
                    m_SceneResizeAnchorY = worldQuad[0].y;
                    break;
                case SceneHandleType::ResizeBottomLeft:
                    m_SceneResizeAnchorX = worldQuad[1].x;
                    m_SceneResizeAnchorY = worldQuad[1].y;
                    break;
                case SceneHandleType::ResizeLeft:
                    m_SceneResizeAnchorX = worldRightCenter.x;
                    m_SceneResizeAnchorY = worldRightCenter.y;
                    break;
                case SceneHandleType::ResizeRight:
                    m_SceneResizeAnchorX = worldLeftCenter.x;
                    m_SceneResizeAnchorY = worldLeftCenter.y;
                    break;
                case SceneHandleType::ResizeTop:
                    m_SceneResizeAnchorX = worldBottomCenter.x;
                    m_SceneResizeAnchorY = worldBottomCenter.y;
                    break;
                case SceneHandleType::ResizeBottom:
                    m_SceneResizeAnchorX = worldTopCenter.x;
                    m_SceneResizeAnchorY = worldTopCenter.y;
                    break;
                case SceneHandleType::Rotate: {
                    const ImVec2 center = Midpoint(worldQuad[0], worldQuad[2]);
                    m_SceneStartMouseAngle = std::atan2(mouseWorld.y - center.y, mouseWorld.x - center.x) - item.rotation;
                    break;
                }
                case SceneHandleType::Move:
                case SceneHandleType::None:
                default:
                    break;
            }
        };

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            editor->BeginCompositePan(mousePos);
        }
        if (editor->IsCompositePanActive()) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                editor->UpdateCompositePan(mousePos);
            } else {
                editor->EndCompositePan();
            }
        }

        if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
            const float nextZoom = std::clamp(
                editor->GetCompositeViewZoom() * (1.0f + ImGui::GetIO().MouseWheel * 0.1f),
                0.05f,
                32.0f);
            editor->SetCompositeViewZoom(nextZoom);
        }
        if (!editor->IsCompositePanActive() && m_ActiveSceneHandle == SceneHandleType::None) {
            editor->ClampCompositeViewPanToContent(avail);
        }

        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("CompositeCanvasContext");
        }

        const bool editableCustomBounds =
            exportBoundsEditMode &&
            exportSettings.boundsMode == EditorModule::CompositeExportBoundsMode::Custom &&
            hasExportBounds;
        ImVec2 exportTopLeft {};
        ImVec2 exportTopRight {};
        ImVec2 exportBottomRight {};
        ImVec2 exportBottomLeft {};
        ImVec2 exportTopCenter {};
        ImVec2 exportBottomCenter {};
        ImVec2 exportLeftCenter {};
        ImVec2 exportRightCenter {};
        if (hasExportBounds) {
            exportTopLeft = WorldToScreen(
                canvasCenter,
                editor->GetCompositeViewZoom(),
                editor->GetCompositeViewPanX(),
                editor->GetCompositeViewPanY(),
                ImVec2(exportBounds.x, exportBounds.y));
            exportBottomRight = WorldToScreen(
                canvasCenter,
                editor->GetCompositeViewZoom(),
                editor->GetCompositeViewPanX(),
                editor->GetCompositeViewPanY(),
                ImVec2(exportBounds.x + exportBounds.width, exportBounds.y + exportBounds.height));
            exportTopRight = ImVec2(exportBottomRight.x, exportTopLeft.y);
            exportBottomLeft = ImVec2(exportTopLeft.x, exportBottomRight.y);
            exportTopCenter = ImVec2((exportTopLeft.x + exportBottomRight.x) * 0.5f, exportTopLeft.y);
            exportBottomCenter = ImVec2((exportTopLeft.x + exportBottomRight.x) * 0.5f, exportBottomRight.y);
            exportLeftCenter = ImVec2(exportTopLeft.x, (exportTopLeft.y + exportBottomRight.y) * 0.5f);
            exportRightCenter = ImVec2(exportBottomRight.x, (exportTopLeft.y + exportBottomRight.y) * 0.5f);
        }

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_ActiveExportHandle = ExportHandleType::None;
            m_ActiveSceneHandle = SceneHandleType::None;
            m_ActiveSceneOutputNodeId = -1;
            if (editableCustomBounds) {
                const float threshold = 12.0f;
                const float minX = std::min(exportTopLeft.x, exportBottomRight.x);
                const float maxX = std::max(exportTopLeft.x, exportBottomRight.x);
                const float minY = std::min(exportTopLeft.y, exportBottomRight.y);
                const float maxY = std::max(exportTopLeft.y, exportBottomRight.y);
                const bool insideExportRect =
                    mousePos.x >= minX &&
                    mousePos.x <= maxX &&
                    mousePos.y >= minY &&
                    mousePos.y <= maxY;

                if (DistanceToPoint(mousePos, exportTopLeft) <= threshold) {
                    m_ActiveExportHandle = ExportHandleType::TopLeft;
                } else if (DistanceToPoint(mousePos, exportTopRight) <= threshold) {
                    m_ActiveExportHandle = ExportHandleType::TopRight;
                } else if (DistanceToPoint(mousePos, exportBottomRight) <= threshold) {
                    m_ActiveExportHandle = ExportHandleType::BottomRight;
                } else if (DistanceToPoint(mousePos, exportBottomLeft) <= threshold) {
                    m_ActiveExportHandle = ExportHandleType::BottomLeft;
                } else if (DistanceToPoint(mousePos, exportTopCenter) <= threshold) {
                    m_ActiveExportHandle = ExportHandleType::Top;
                } else if (DistanceToPoint(mousePos, exportRightCenter) <= threshold) {
                    m_ActiveExportHandle = ExportHandleType::Right;
                } else if (DistanceToPoint(mousePos, exportBottomCenter) <= threshold) {
                    m_ActiveExportHandle = ExportHandleType::Bottom;
                } else if (DistanceToPoint(mousePos, exportLeftCenter) <= threshold) {
                    m_ActiveExportHandle = ExportHandleType::Left;
                } else if (insideExportRect) {
                    m_ActiveExportHandle = ExportHandleType::Move;
                }

                if (m_ActiveExportHandle != ExportHandleType::None) {
                    m_ExportDragStartX = exportSettings.customX;
                    m_ExportDragStartY = exportSettings.customY;
                    m_ExportDragStartWidth = exportSettings.customWidth;
                    m_ExportDragStartHeight = exportSettings.customHeight;
                    m_ExportDragStartMouseWorldX = mouseWorld.x;
                    m_ExportDragStartMouseWorldY = mouseWorld.y;
                }
            }

            if (m_ActiveExportHandle != ExportHandleType::None) {
                editor->ClearCompositeSelection();
            } else {
                const EditorModule::CompositeSceneItem* activeSelectedItem = editor->FindCompositeSceneItem(editor->GetCompositeSelectedOutputNodeId());
                bool startedHandle = false;
                if (activeSelectedItem && !activeSelectedItem->locked && activeSelectedItem->textureWidth > 0 && activeSelectedItem->textureHeight > 0) {
                    const std::array<ImVec2, 4> selectedWorldQuad = ComputeSceneQuadWorld(*activeSelectedItem);
                    const std::array<ImVec2, 4> screenQuad = {
                        WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), selectedWorldQuad[0]),
                        WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), selectedWorldQuad[1]),
                        WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), selectedWorldQuad[2]),
                        WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), selectedWorldQuad[3]),
                    };
                    const ImVec2 topCenter = Midpoint(screenQuad[0], screenQuad[1]);
                    const ImVec2 rightCenter = Midpoint(screenQuad[1], screenQuad[2]);
                    const ImVec2 bottomCenter = Midpoint(screenQuad[2], screenQuad[3]);
                    const ImVec2 leftCenter = Midpoint(screenQuad[3], screenQuad[0]);
                    const ImVec2 center = Midpoint(screenQuad[0], screenQuad[2]);
                    const ImVec2 outward = NormalizeOrZero(ImVec2(topCenter.x - center.x, topCenter.y - center.y));
                    const ImVec2 rotateHandle = ImVec2(topCenter.x + outward.x * 30.0f, topCenter.y + outward.y * 30.0f);
                    constexpr float kHandleRadius = 7.0f;
                    constexpr float kRotateRadius = 9.0f;
                    SceneHandleType hitHandle = SceneHandleType::None;
                    if (DistanceToPoint(mousePos, rotateHandle) <= kRotateRadius) {
                        hitHandle = SceneHandleType::Rotate;
                    } else if (DistanceToPoint(mousePos, screenQuad[0]) <= kHandleRadius) {
                        hitHandle = SceneHandleType::ResizeTopLeft;
                    } else if (DistanceToPoint(mousePos, screenQuad[1]) <= kHandleRadius) {
                        hitHandle = SceneHandleType::ResizeTopRight;
                    } else if (DistanceToPoint(mousePos, screenQuad[2]) <= kHandleRadius) {
                        hitHandle = SceneHandleType::ResizeBottomRight;
                    } else if (DistanceToPoint(mousePos, screenQuad[3]) <= kHandleRadius) {
                        hitHandle = SceneHandleType::ResizeBottomLeft;
                    } else if (DistanceToPoint(mousePos, topCenter) <= kHandleRadius) {
                        hitHandle = SceneHandleType::ResizeTop;
                    } else if (DistanceToPoint(mousePos, rightCenter) <= kHandleRadius) {
                        hitHandle = SceneHandleType::ResizeRight;
                    } else if (DistanceToPoint(mousePos, bottomCenter) <= kHandleRadius) {
                        hitHandle = SceneHandleType::ResizeBottom;
                    } else if (DistanceToPoint(mousePos, leftCenter) <= kHandleRadius) {
                        hitHandle = SceneHandleType::ResizeLeft;
                    }
                    if (hitHandle != SceneHandleType::None) {
                        if (EditorModule::CompositeSceneItem* selectedMutable = editor->FindCompositeSceneItem(activeSelectedItem->outputNodeId)) {
                            beginSceneHandleInteraction(*selectedMutable, hitHandle);
                            startedHandle = true;
                        }
                    }
                }

                if (!startedHandle && hoveredItem) {
                    if (EditorModule::CompositeSceneItem* hoveredMutable = editor->FindCompositeSceneItem(hoveredItem->outputNodeId)) {
                        beginSceneHandleInteraction(*hoveredMutable, SceneHandleType::Move);
                    }
                } else if (!startedHandle) {
                    editor->ClearCompositeSelection();
                }
            }
        }
        if (m_ActiveExportHandle != ExportHandleType::None) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (m_ActiveExportHandle != ExportHandleType::Move &&
                    exportSettings.aspectPreset != EditorModule::CompositeExportAspectPreset::Custom) {
                    exportSettings.aspectPreset = EditorModule::CompositeExportAspectPreset::Custom;
                }
                const float deltaX = mouseWorld.x - m_ExportDragStartMouseWorldX;
                const float deltaY = mouseWorld.y - m_ExportDragStartMouseWorldY;
                const bool lockedAspect = exportSettings.aspectPreset != EditorModule::CompositeExportAspectPreset::Custom;
                const float ratio = editor->GetCurrentCompositeExportAspectRatio();
                EditorModule::CompositeFloatRect newBounds {
                    m_ExportDragStartX,
                    m_ExportDragStartY,
                    m_ExportDragStartWidth,
                    m_ExportDragStartHeight
                };

                auto applyCornerWithAspect = [&](const float anchorX, const float anchorY, const bool dragLeft, const bool dragTop) {
                    const float widthByX = std::max(1.0f, std::abs(mouseWorld.x - anchorX));
                    const float heightByX = std::max(1.0f, widthByX / std::max(0.0001f, ratio));
                    const float heightByY = std::max(1.0f, std::abs(mouseWorld.y - anchorY));
                    const float widthByY = std::max(1.0f, heightByY * std::max(0.0001f, ratio));
                    const float xCornerByX = dragLeft ? (anchorX - widthByX) : (anchorX + widthByX);
                    const float yCornerByX = dragTop ? (anchorY - heightByX) : (anchorY + heightByX);
                    const float xCornerByY = dragLeft ? (anchorX - widthByY) : (anchorX + widthByY);
                    const float yCornerByY = dragTop ? (anchorY - heightByY) : (anchorY + heightByY);
                    const float errorByX = std::pow(mouseWorld.x - xCornerByX, 2.0f) + std::pow(mouseWorld.y - yCornerByX, 2.0f);
                    const float errorByY = std::pow(mouseWorld.x - xCornerByY, 2.0f) + std::pow(mouseWorld.y - yCornerByY, 2.0f);
                    const float chosenWidth = (errorByX <= errorByY) ? widthByX : widthByY;
                    const float chosenHeight = std::max(1.0f, chosenWidth / std::max(0.0001f, ratio));
                    const float left = dragLeft ? (anchorX - chosenWidth) : anchorX;
                    const float top = dragTop ? (anchorY - chosenHeight) : anchorY;
                    newBounds = { left, top, chosenWidth, chosenHeight };
                };

                if (m_ActiveExportHandle == ExportHandleType::Move) {
                    newBounds.x = m_ExportDragStartX + deltaX;
                    newBounds.y = m_ExportDragStartY + deltaY;
                } else if (!lockedAspect) {
                    float x1 = m_ExportDragStartX;
                    float y1 = m_ExportDragStartY;
                    float x2 = m_ExportDragStartX + m_ExportDragStartWidth;
                    float y2 = m_ExportDragStartY + m_ExportDragStartHeight;
                    if (m_ActiveExportHandle == ExportHandleType::Left ||
                        m_ActiveExportHandle == ExportHandleType::TopLeft ||
                        m_ActiveExportHandle == ExportHandleType::BottomLeft) {
                        x1 += deltaX;
                    }
                    if (m_ActiveExportHandle == ExportHandleType::Right ||
                        m_ActiveExportHandle == ExportHandleType::TopRight ||
                        m_ActiveExportHandle == ExportHandleType::BottomRight) {
                        x2 += deltaX;
                    }
                    if (m_ActiveExportHandle == ExportHandleType::Top ||
                        m_ActiveExportHandle == ExportHandleType::TopLeft ||
                        m_ActiveExportHandle == ExportHandleType::TopRight) {
                        y1 += deltaY;
                    }
                    if (m_ActiveExportHandle == ExportHandleType::Bottom ||
                        m_ActiveExportHandle == ExportHandleType::BottomLeft ||
                        m_ActiveExportHandle == ExportHandleType::BottomRight) {
                        y2 += deltaY;
                    }
                    newBounds = MakeNormalizedRect(x1, y1, x2, y2);
                } else {
                    const float startRight = m_ExportDragStartX + m_ExportDragStartWidth;
                    const float startBottom = m_ExportDragStartY + m_ExportDragStartHeight;
                    switch (m_ActiveExportHandle) {
                        case ExportHandleType::TopLeft:
                            applyCornerWithAspect(startRight, startBottom, true, true);
                            break;
                        case ExportHandleType::TopRight:
                            applyCornerWithAspect(m_ExportDragStartX, startBottom, false, true);
                            break;
                        case ExportHandleType::BottomRight:
                            applyCornerWithAspect(m_ExportDragStartX, m_ExportDragStartY, false, false);
                            break;
                        case ExportHandleType::BottomLeft:
                            applyCornerWithAspect(startRight, m_ExportDragStartY, true, false);
                            break;
                        default:
                            break;
                    }
                }

                exportSettings.customX = newBounds.x;
                exportSettings.customY = newBounds.y;
                exportSettings.customWidth = std::max(1.0f, newBounds.width);
                exportSettings.customHeight = std::max(1.0f, newBounds.height);
                if (exportSettings.aspectPreset == EditorModule::CompositeExportAspectPreset::Custom) {
                    editor->UpdateCompositeCustomExportAspectFromBounds();
                }
                editor->SyncCompositeExportResolutionFromWidth();
            } else {
                m_ActiveExportHandle = ExportHandleType::None;
            }
        }
        if (m_ActiveSceneHandle != SceneHandleType::None) {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_ActiveSceneHandle = SceneHandleType::None;
                m_ActiveSceneOutputNodeId = -1;
            } else if (EditorModule::CompositeSceneItem* activeItem = editor->FindCompositeSceneItem(m_ActiveSceneOutputNodeId)) {
                const auto& snapSettings = editor->GetCompositeSnapSettings();
                const bool preserveAspect = editor->GetCompositeResizeMode() == EditorModule::CompositeResizeMode::Scale;
                const bool scaleFromCenter =
                    preserveAspect &&
                    editor->GetCompositeScaleOriginMode() == EditorModule::CompositeScaleOriginMode::Center;
                const float threshold = 10.0f / std::max(0.001f, editor->GetCompositeViewZoom());
                auto buildBoundsAt = [&](const float topLeftX, const float topLeftY, const float scaleX, const float scaleY, const float rotation) {
                    EditorModule::CompositeSceneItem temp = *activeItem;
                    temp.position = ImVec2(topLeftX, topLeftY);
                    temp.scale = ImVec2(scaleX, scaleY);
                    temp.rotation = rotation;
                    return QuadBounds(ComputeSceneQuadWorld(temp));
                };

                auto applyMoveSnapping = [&](float& targetX, float& targetY) {
                    if (!snapSettings.enabled) {
                        return;
                    }
                    EditorModule::CompositeFloatRect activeBounds =
                        buildBoundsAt(targetX, targetY, m_SceneStartScaleX, m_SceneStartScaleY, m_SceneStartRotation);
                    const float activeLeft = activeBounds.x;
                    const float activeCenterX = activeBounds.x + activeBounds.width * 0.5f;
                    const float activeRight = activeBounds.x + activeBounds.width;
                    const float activeTop = activeBounds.y;
                    const float activeCenterY = activeBounds.y + activeBounds.height * 0.5f;
                    const float activeBottom = activeBounds.y + activeBounds.height;

                    float bestDeltaX = std::numeric_limits<float>::max();
                    float bestDeltaY = std::numeric_limits<float>::max();
                    bool snappedX = false;
                    bool snappedY = false;
                    ImVec2 bestXGuideA {};
                    ImVec2 bestXGuideB {};
                    ImVec2 bestYGuideA {};
                    ImVec2 bestYGuideB {};

                    auto considerX = [&](const float delta, const float guideX, const float y1, const float y2) {
                        if (std::abs(delta) <= threshold && (!snappedX || std::abs(delta) < std::abs(bestDeltaX))) {
                            snappedX = true;
                            bestDeltaX = delta;
                            bestXGuideA = ImVec2(guideX, y1);
                            bestXGuideB = ImVec2(guideX, y2);
                        }
                    };
                    auto considerY = [&](const float delta, const float guideY, const float x1, const float x2) {
                        if (std::abs(delta) <= threshold && (!snappedY || std::abs(delta) < std::abs(bestDeltaY))) {
                            snappedY = true;
                            bestDeltaY = delta;
                            bestYGuideA = ImVec2(x1, guideY);
                            bestYGuideB = ImVec2(x2, guideY);
                        }
                    };

                    for (const int otherId : editor->GetCompositeZOrder()) {
                        if (otherId == activeItem->outputNodeId) {
                            continue;
                        }
                        const EditorModule::CompositeSceneItem* otherItem = editor->FindCompositeSceneItem(otherId);
                        if (!otherItem || !otherItem->visible || otherItem->textureWidth <= 0 || otherItem->textureHeight <= 0) {
                            continue;
                        }
                        const EditorModule::CompositeFloatRect otherBounds = QuadBounds(ComputeSceneQuadWorld(*otherItem));
                        const float otherLeft = otherBounds.x;
                        const float otherCenterX = otherBounds.x + otherBounds.width * 0.5f;
                        const float otherRight = otherBounds.x + otherBounds.width;
                        const float otherTop = otherBounds.y;
                        const float otherCenterY = otherBounds.y + otherBounds.height * 0.5f;
                        const float otherBottom = otherBounds.y + otherBounds.height;

                        if (snapSettings.snapToObjects) {
                            considerX(otherLeft - activeLeft, otherLeft, std::min(activeBounds.y, otherBounds.y), std::max(activeBottom, otherBottom));
                            considerX(otherRight - activeRight, otherRight, std::min(activeBounds.y, otherBounds.y), std::max(activeBottom, otherBottom));
                            considerY(otherTop - activeTop, otherTop, std::min(activeBounds.x, otherBounds.x), std::max(activeRight, otherRight));
                            considerY(otherBottom - activeBottom, otherBottom, std::min(activeBounds.x, otherBounds.x), std::max(activeRight, otherRight));
                        }
                        if (snapSettings.snapToCenters) {
                            considerX(otherCenterX - activeCenterX, otherCenterX, std::min(activeBounds.y, otherBounds.y), std::max(activeBottom, otherBottom));
                            considerY(otherCenterY - activeCenterY, otherCenterY, std::min(activeBounds.x, otherBounds.x), std::max(activeRight, otherRight));
                        }
                    }

                    if (snapSettings.snapToCanvasCenter) {
                        considerX(-activeCenterX, 0.0f, activeBounds.y, activeBottom);
                        considerY(-activeCenterY, 0.0f, activeBounds.x, activeRight);
                    }

                    if (snapSettings.snapToExportBounds && hasExportBounds) {
                        const float exportLeftW = exportBounds.x;
                        const float exportCenterXW = exportBounds.x + exportBounds.width * 0.5f;
                        const float exportRightW = exportBounds.x + exportBounds.width;
                        const float exportTopW = exportBounds.y;
                        const float exportCenterYW = exportBounds.y + exportBounds.height * 0.5f;
                        const float exportBottomW = exportBounds.y + exportBounds.height;
                        considerX(exportLeftW - activeLeft, exportLeftW, std::min(activeBounds.y, exportBounds.y), std::max(activeBottom, exportBottomW));
                        considerX(exportRightW - activeRight, exportRightW, std::min(activeBounds.y, exportBounds.y), std::max(activeBottom, exportBottomW));
                        considerY(exportTopW - activeTop, exportTopW, std::min(activeBounds.x, exportBounds.x), std::max(activeRight, exportRightW));
                        considerY(exportBottomW - activeBottom, exportBottomW, std::min(activeBounds.x, exportBounds.x), std::max(activeRight, exportRightW));
                        considerX(exportCenterXW - activeCenterX, exportCenterXW, std::min(activeBounds.y, exportBounds.y), std::max(activeBottom, exportBottomW));
                        considerY(exportCenterYW - activeCenterY, exportCenterYW, std::min(activeBounds.x, exportBounds.x), std::max(activeRight, exportRightW));
                    }

                    if (snappedX) {
                        targetX += bestDeltaX;
                        addSnapGuideWorld(bestXGuideA, bestXGuideB, IM_COL32(80, 200, 255, 255));
                    }
                    if (snappedY) {
                        targetY += bestDeltaY;
                        addSnapGuideWorld(bestYGuideA, bestYGuideB, IM_COL32(80, 200, 255, 255));
                    }
                };

                if (m_ActiveSceneHandle == SceneHandleType::Move) {
                    float targetX = m_SceneStartX + (mouseWorld.x - m_SceneDragStartMouseWorldX);
                    float targetY = m_SceneStartY + (mouseWorld.y - m_SceneDragStartMouseWorldY);
                    applyMoveSnapping(targetX, targetY);
                    activeItem->position = ImVec2(targetX, targetY);
                } else if (m_ActiveSceneHandle == SceneHandleType::Rotate) {
                    const std::array<ImVec2, 4> activeQuad = ComputeSceneQuadWorld(*activeItem);
                    const ImVec2 center = Midpoint(activeQuad[0], activeQuad[2]);
                    float targetRotation = std::atan2(mouseWorld.y - center.y, mouseWorld.x - center.x) - m_SceneStartMouseAngle;
                    if (snapSettings.enabled && snapSettings.rotateSnapStep > 0.0f) {
                        float degrees = targetRotation * (180.0f / 3.14159265358979323846f);
                        degrees = std::round(degrees / snapSettings.rotateSnapStep) * snapSettings.rotateSnapStep;
                        targetRotation = degrees * (3.14159265358979323846f / 180.0f);
                    }
                    activeItem->rotation = targetRotation;
                } else {
                    const ImVec2 axisX(std::cos(m_SceneStartRotation), std::sin(m_SceneStartRotation));
                    const ImVec2 axisY(-std::sin(m_SceneStartRotation), std::cos(m_SceneStartRotation));
                    const std::array<ImVec2, 4> activeQuad = ComputeSceneQuadWorld(*activeItem);
                    const ImVec2 currentCenter = Midpoint(activeQuad[0], activeQuad[2]);
                    const ImVec2 anchor = scaleFromCenter ? currentCenter : ImVec2(m_SceneResizeAnchorX, m_SceneResizeAnchorY);
                    const ImVec2 delta(mouseWorld.x - anchor.x, mouseWorld.y - anchor.y);
                    const float alongX = delta.x * axisX.x + delta.y * axisX.y;
                    const float alongY = delta.x * axisY.x + delta.y * axisY.y;
                    const float aspectRatio = std::max(0.0001f, m_SceneStartWidth / std::max(1.0f, m_SceneStartHeight));
                    float newWidth = m_SceneStartWidth;
                    float newHeight = m_SceneStartHeight;
                    float signX = 0.0f;
                    float signY = 0.0f;

                    auto applyCornerWithAspect = [&](const bool dragLeft, const bool dragTop) {
                        signX = dragLeft ? -1.0f : 1.0f;
                        signY = dragTop ? -1.0f : 1.0f;
                        const float widthByX = std::max(1.0f, std::abs(alongX));
                        const float heightByX = std::max(1.0f, widthByX / aspectRatio);
                        const float heightByY = std::max(1.0f, std::abs(alongY));
                        const float widthByY = std::max(1.0f, heightByY * aspectRatio);
                        const float errorByX = std::abs(std::abs(alongY) - heightByX);
                        const float errorByY = std::abs(std::abs(alongX) - widthByY);
                        if (errorByX <= errorByY) {
                            newWidth = widthByX;
                            newHeight = heightByX;
                        } else {
                            newWidth = widthByY;
                            newHeight = heightByY;
                        }
                    };

                    switch (m_ActiveSceneHandle) {
                        case SceneHandleType::ResizeTopLeft:
                            if (preserveAspect) {
                                applyCornerWithAspect(true, true);
                            } else {
                                signX = -1.0f;
                                signY = -1.0f;
                                newWidth = std::max(1.0f, std::abs(alongX));
                                newHeight = std::max(1.0f, std::abs(alongY));
                            }
                            break;
                        case SceneHandleType::ResizeTopRight:
                            if (preserveAspect) {
                                applyCornerWithAspect(false, true);
                            } else {
                                signX = 1.0f;
                                signY = -1.0f;
                                newWidth = std::max(1.0f, std::abs(alongX));
                                newHeight = std::max(1.0f, std::abs(alongY));
                            }
                            break;
                        case SceneHandleType::ResizeBottomRight:
                            if (preserveAspect) {
                                applyCornerWithAspect(false, false);
                            } else {
                                signX = 1.0f;
                                signY = 1.0f;
                                newWidth = std::max(1.0f, std::abs(alongX));
                                newHeight = std::max(1.0f, std::abs(alongY));
                            }
                            break;
                        case SceneHandleType::ResizeBottomLeft:
                            if (preserveAspect) {
                                applyCornerWithAspect(true, false);
                            } else {
                                signX = -1.0f;
                                signY = 1.0f;
                                newWidth = std::max(1.0f, std::abs(alongX));
                                newHeight = std::max(1.0f, std::abs(alongY));
                            }
                            break;
                        case SceneHandleType::ResizeLeft:
                            signX = -1.0f;
                            newWidth = std::max(1.0f, std::abs(alongX));
                            newHeight = preserveAspect ? std::max(1.0f, newWidth / aspectRatio) : m_SceneStartHeight;
                            break;
                        case SceneHandleType::ResizeRight:
                            signX = 1.0f;
                            newWidth = std::max(1.0f, std::abs(alongX));
                            newHeight = preserveAspect ? std::max(1.0f, newWidth / aspectRatio) : m_SceneStartHeight;
                            break;
                        case SceneHandleType::ResizeTop:
                            signY = -1.0f;
                            newHeight = std::max(1.0f, std::abs(alongY));
                            newWidth = preserveAspect ? std::max(1.0f, newHeight * aspectRatio) : m_SceneStartWidth;
                            break;
                        case SceneHandleType::ResizeBottom:
                            signY = 1.0f;
                            newHeight = std::max(1.0f, std::abs(alongY));
                            newWidth = preserveAspect ? std::max(1.0f, newHeight * aspectRatio) : m_SceneStartWidth;
                            break;
                        default:
                            break;
                    }

                    if (snapSettings.enabled && snapSettings.scaleSnapStep > 0.0f) {
                        const float baseWidth = activeItem->isScalable ? 256.0f : std::max(1.0f, static_cast<float>(activeItem->textureWidth));
                        const float baseHeight = activeItem->isScalable ? 256.0f : std::max(1.0f, static_cast<float>(activeItem->textureHeight));
                        newWidth = std::max(1.0f, std::round((newWidth / baseWidth) / snapSettings.scaleSnapStep) * snapSettings.scaleSnapStep * baseWidth);
                        newHeight = std::max(1.0f, std::round((newHeight / baseHeight) / snapSettings.scaleSnapStep) * snapSettings.scaleSnapStep * baseHeight);
                    }

                    const ImVec2 newCenter = scaleFromCenter
                        ? currentCenter
                        : ImVec2(
                            anchor.x + axisX.x * signX * newWidth * 0.5f + axisY.x * signY * newHeight * 0.5f,
                            anchor.y + axisX.y * signX * newWidth * 0.5f + axisY.y * signY * newHeight * 0.5f);
                    activeItem->scale.x = std::max(0.01f, newWidth / (activeItem->isScalable ? 256.0f : std::max(1.0f, static_cast<float>(activeItem->textureWidth))));
                    activeItem->scale.y = std::max(0.01f, newHeight / (activeItem->isScalable ? 256.0f : std::max(1.0f, static_cast<float>(activeItem->textureHeight))));
                    activeItem->position = ImVec2(newCenter.x - newWidth * 0.5f, newCenter.y - newHeight * 0.5f);
                }
            } else {
                m_ActiveSceneHandle = SceneHandleType::None;
                m_ActiveSceneOutputNodeId = -1;
            }
        }

        if (ImGui::BeginPopup("CompositeCanvasContext")) {
            if (ImGui::MenuItem("Add Image")) {
                const std::string path = FileDialogs::OpenImageFileDialog("Add image to composite");
                if (!path.empty()) {
                    editor->AddCompositeImageChainFromFile(path);
                }
            }
            if (ImGui::MenuItem("Add Library Asset")) {
                m_ShowCompositeAssetPicker = true;
                ImGui::OpenPopup("CompositeCanvasAssetPicker");
            }
            if (ImGui::MenuItem("Add Square")) {
                editor->AddCompositeGeneratorChain(EditorNodeGraph::ImageGeneratorKind::Square);
            }
            if (ImGui::MenuItem("Add Circle")) {
                editor->AddCompositeGeneratorChain(EditorNodeGraph::ImageGeneratorKind::Circle);
            }
            if (ImGui::MenuItem("Add Text")) {
                editor->AddCompositeGeneratorChain(EditorNodeGraph::ImageGeneratorKind::Text);
            }
            ImGui::EndPopup();
        }

        if (m_ShowCompositeAssetPicker) {
            ImGui::SetNextWindowSize(ImVec2(560.0f, 420.0f), ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("CompositeCanvasAssetPicker", nullptr, ImGuiWindowFlags_NoCollapse)) {
                const auto& assets = LibraryManager::Get().GetAssets();
                if (assets.empty()) {
                    ImGui::TextDisabled("No library image assets are available.");
                } else {
                    ImGui::TextDisabled("Select a library asset to create a new image chain.");
                    ImGui::Separator();
                    ImGui::BeginChild("CompositeCanvasAssetList", ImVec2(0.0f, -42.0f), false);
                    for (const auto& asset : assets) {
                        if (!asset) {
                            continue;
                        }
                        const std::string label = asset->displayName.empty()
                            ? asset->fileName
                            : (asset->displayName + "##" + asset->fileName);
                        if (ImGui::Selectable(label.c_str(), false)) {
                            if (editor->AddCompositeLibraryAssetChain(asset->fileName)) {
                                m_ShowCompositeAssetPicker = false;
                                ImGui::CloseCurrentPopup();
                            }
                        }
                        if (asset->width > 0 && asset->height > 0) {
                            ImGui::SameLine();
                            ImGui::TextDisabled("%d x %d", asset->width, asset->height);
                        }
                    }
                    ImGui::EndChild();
                }
                if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) {
                    m_ShowCompositeAssetPicker = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            } else {
                m_ShowCompositeAssetPicker = false;
            }
        }

        drawList->PushClipRect(canvasMin, canvasMax, true);
        for (auto it = editor->GetCompositeZOrder().rbegin(); it != editor->GetCompositeZOrder().rend(); ++it) {
            const int outputNodeId = *it;
            const EditorModule::CompositeSceneItem* item = editor->FindCompositeSceneItem(outputNodeId);
            if (!item || !item->visible || item->texture == 0 || item->textureWidth <= 0 || item->textureHeight <= 0) {
                continue;
            }

            const std::array<ImVec2, 4> worldQuad = ComputeSceneQuadWorld(*item);
            const std::array<ImVec2, 4> screenQuad = {
                WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), worldQuad[0]),
                WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), worldQuad[1]),
                WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), worldQuad[2]),
                WorldToScreen(canvasCenter, editor->GetCompositeViewZoom(), editor->GetCompositeViewPanX(), editor->GetCompositeViewPanY(), worldQuad[3]),
            };

            drawList->AddImageQuad(
                (ImTextureID)(intptr_t)item->texture,
                screenQuad[0],
                screenQuad[1],
                screenQuad[2],
                screenQuad[3]);

            if (editor->GetCompositeSelectedOutputNodeId() == item->outputNodeId) {
                drawList->AddQuad(screenQuad[0], screenQuad[1], screenQuad[2], screenQuad[3], IM_COL32(225, 240, 247, 235), 2.0f);
                const ImVec2 topCenter = Midpoint(screenQuad[0], screenQuad[1]);
                const ImVec2 rightCenter = Midpoint(screenQuad[1], screenQuad[2]);
                const ImVec2 bottomCenter = Midpoint(screenQuad[2], screenQuad[3]);
                const ImVec2 leftCenter = Midpoint(screenQuad[3], screenQuad[0]);
                const ImVec2 center = Midpoint(screenQuad[0], screenQuad[2]);
                const ImVec2 outward = NormalizeOrZero(ImVec2(topCenter.x - center.x, topCenter.y - center.y));
                const ImVec2 rotateHandle = ImVec2(topCenter.x + outward.x * 30.0f, topCenter.y + outward.y * 30.0f);
                drawList->AddLine(topCenter, rotateHandle, IM_COL32(225, 240, 247, 200), 1.5f);
                auto drawHandle = [&](const ImVec2& point, const ImU32 fillColor, const float radius) {
                    drawList->AddCircleFilled(point, radius, fillColor, 20);
                    drawList->AddCircle(point, radius, IM_COL32(10, 14, 18, 255), 20, 1.5f);
                };
                const ImU32 resizeFill = IM_COL32(230, 238, 244, 245);
                drawHandle(screenQuad[0], resizeFill, 5.0f);
                drawHandle(screenQuad[1], resizeFill, 5.0f);
                drawHandle(screenQuad[2], resizeFill, 5.0f);
                drawHandle(screenQuad[3], resizeFill, 5.0f);
                drawHandle(topCenter, resizeFill, 4.5f);
                drawHandle(rightCenter, resizeFill, 4.5f);
                drawHandle(bottomCenter, resizeFill, 4.5f);
                drawHandle(leftCenter, resizeFill, 4.5f);
                drawHandle(rotateHandle, IM_COL32(120, 196, 255, 245), 6.0f);
            } else if (hoveredItem && hoveredItem->outputNodeId == item->outputNodeId) {
                drawList->AddQuad(screenQuad[0], screenQuad[1], screenQuad[2], screenQuad[3], IM_COL32(154, 199, 224, 150), 1.0f);
            }
        }
        for (const SnapGuideLine& guide : m_CompositeSnapGuides) {
            drawList->AddLine(guide.a, guide.b, guide.color, 1.4f);
        }

        // Draw an ultra-premium, gap-free, non-overlapping concentric rounded vignette.
        // This completely eliminates overlapping artifacts, double-blending at corners,
        // and hard edges, creating a mathematically perfect and smooth blend for the canvas.
        const float seamFade = 48.0f;
        const float edgeFade = 24.0f;
        const ImVec4 workspaceBg = editor->GetWorkspaceBaseColor();

        constexpr int N = 32;
        for (int i = 0; i < N; ++i) {
            float t = static_cast<float>(i) / N;

            // Inset from canvas edges moving inward
            float leftInset = t * seamFade;
            float rightInset = t * edgeFade;
            float topInset = t * edgeFade;
            float bottomInset = t * edgeFade;

            ImVec2 rectMin(canvasMin.x + leftInset, canvasMin.y + topInset);
            ImVec2 rectMax(canvasMax.x - rightInset, canvasMax.y - bottomInset);

            // Premium cubic falloff for a cinematic, natural-looking smooth transition
            float smoothAlpha = std::pow(1.0f - t, 2.5f);
            ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(workspaceBg.x, workspaceBg.y, workspaceBg.z, smoothAlpha));

            // Adjust corner rounding radius to match the inset rectangle perfectly
            float rounding = std::max(0.0f, kCanvasRounding - (t * edgeFade));

            // Draw concentric rounded rectangle outlines with a thickness of 2.0f.
            // This ensures overlaps are continuous and completely gap-free.
            drawList->AddRect(rectMin, rectMax, color, rounding, ImDrawFlags_None, 2.0f);
        }

        drawList->PopClipRect();

        if (hasExportBounds) {
            const ImU32 boundsColor = exportBoundsEditMode
                ? IM_COL32(96, 192, 255, 255)
                : IM_COL32(96, 192, 255, 110);
            drawList->AddRect(exportTopLeft, exportBottomRight, boundsColor, 0.0f, 0, 2.0f);
            if (editableCustomBounds) {
                const bool freeAspect = true;
                std::array<ImVec2, 8> handlePoints = {
                    exportTopLeft,
                    exportTopCenter,
                    exportTopRight,
                    exportRightCenter,
                    exportBottomRight,
                    exportBottomCenter,
                    exportBottomLeft,
                    exportLeftCenter
                };
                const int handleCount = 8;
                const int handleIndices[] = { 0, 2, 4, 6, 1, 3, 5, 7 };
                for (int handleIndex = 0; handleIndex < handleCount; ++handleIndex) {
                    const ImVec2& point = handlePoints[handleIndices[handleIndex]];
                    drawList->AddRectFilled(
                        ImVec2(point.x - 5.0f, point.y - 5.0f),
                        ImVec2(point.x + 5.0f, point.y + 5.0f),
                        boundsColor,
                        2.0f);
                    drawList->AddRect(
                        ImVec2(point.x - 5.0f, point.y - 5.0f),
                        ImVec2(point.x + 5.0f, point.y + 5.0f),
                        IM_COL32(10, 14, 18, 255),
                        2.0f);
                }
            }
        }

        if (editor->GetCompositeSceneItems().empty()) {
            const char* emptyMessage = "Add separate completed chains to start compositing.";
            const ImVec2 textSize = ImGui::CalcTextSize(emptyMessage);
            drawList->AddText(
                ImVec2(canvasMin.x + std::max(20.0f, (avail.x - textSize.x) * 0.5f),
                       canvasMin.y + std::max(28.0f, (avail.y - textSize.y) * 0.18f)),
                IM_COL32(190, 205, 212, 220),
                emptyMessage);
        }
        return;
    }

    if (!pipeline.HasSourceImage() || !editor->GetNodeGraph().IsOutputConnected() || pipeline.GetOutputTexture() == 0) {
        const char* emptyMessage = editor->IsEditorRenderBusy()
            ? "Rendering editor output..."
            : (editor->GetNodeGraph().GetActiveImageNodeId() > 0
                ? "Connect the graph to the output to preview it."
                : "Drop an image into the graph or press Tab to start.");
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 textSize = ImGui::CalcTextSize(emptyMessage);
        ImGui::SetCursorPos(ImVec2(
            std::max(0.0f, (avail.x - textSize.x) * 0.5f),
            std::max(24.0f, (avail.y - textSize.y) * 0.22f)));
        ImGui::TextDisabled("%s", emptyMessage);
        return;
    }

    // ── Inputs & Zoom Logic ──────────────────────────────────────────────────
    
    // Toggle Lock with 'L' key
    if (!inputBlocked && editor->CanConsumeEditorCommandKeys() && ImGui::IsKeyPressed(ImGuiKey_L, false)) {
        m_IsLocked = !m_IsLocked;
    }

    bool isHovered = !inputBlocked && ImGui::IsWindowHovered();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 mousePos = ImGui::GetMousePos();
    ImVec2 contentScreen = ImGui::GetCursorScreenPos();
    ImVec2 relativeMouse = ImVec2(mousePos.x - contentScreen.x, mousePos.y - contentScreen.y);

    if (isHovered && !m_IsLocked) {
        // Zoom with Scroll
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
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
    float baseScale = std::min(scaleX, scaleY) * 0.84f; // Leave generous room so the image feels staged, not boxed in

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
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const bool showHud = m_IsLocked || editor->IsEditorRenderBusy();
    if (showHud) {
        const ImVec2 hudPos = ImVec2(contentScreen.x + 10.0f, contentScreen.y + 10.0f);
        if (m_IsLocked) {
            drawList->AddText(hudPos, IM_COL32(255, 150, 40, 255), "[ ZOOM LOCKED ] - Press 'L' to unlock");
        }
        if (editor->IsEditorRenderBusy()) {
            const float busyOffsetY = m_IsLocked ? 22.0f : 0.0f;
            drawList->AddText(ImVec2(contentScreen.x + 10.0f, contentScreen.y + 10.0f + busyOffsetY),
                              IM_COL32(190, 195, 205, 230),
                              "Rendering...");
        }
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
                const std::string& statusText = editor->GetCanvasToolStatusText();
                if (!statusText.empty()) {
                    fg->AddText(
                        ImVec2(magPos.x, magPos.y - 22.0f),
                        IM_COL32(230, 236, 242, 230),
                        statusText.c_str());
                }
            }
        }
    }

    // ── Comparison Logic (Hover Fade) ────────────────────────────────────────

    // 1) Draw checkerboard background so transparency is visible
    float checkerSize = 16.0f;
    float tilesX = dispW / checkerSize;
    float tilesY = dispH / checkerSize;
    constexpr float kImageRounding = 18.0f;
    ImGui::Dummy(ImVec2(dispW, dispH));
    const bool imageHovered = ImGui::IsItemHovered();
    const ImVec2 imageMin = ImGui::GetItemRectMin();
    const ImVec2 imageMax = ImGui::GetItemRectMax();
    const ImU32 workspaceFill = ImGui::ColorConvertFloat4ToU32(editor->GetWorkspaceBaseColor());
    drawList->AddRectFilled(imageMin, imageMax, workspaceFill, kImageRounding);
    drawList->AddImageRounded((ImTextureID)(intptr_t)m_CheckerTex, imageMin, imageMax,
                              ImVec2(0, 0), ImVec2(tilesX, tilesY), IM_COL32(255, 255, 255, 170), kImageRounding);

    // Handle Fade Factor (hover to compare with original)
    float hoverTarget = (imageHovered && !m_IsLocked) ? 1.0f : 0.0f;
    float currentFactor = editor->GetHoverFade();
    const float fadeStep = 1.0f - std::exp(-ImGui::GetIO().DeltaTime / 0.2f);
    currentFactor += (hoverTarget - currentFactor) * fadeStep;
    currentFactor = std::max(0.0f, std::min(currentFactor, 1.0f));
    editor->SetHoverFade(currentFactor);

    // 2) Draw processed output fully opaque, then fade the original over it.
    const float uInset = imgW > 1 ? (0.5f / static_cast<float>(imgW)) : 0.0f;
    const float vInset = imgH > 1 ? (0.5f / static_cast<float>(imgH)) : 0.0f;
    drawList->AddImageRounded((ImTextureID)(intptr_t)outputTex, imageMin, imageMax,
                              ImVec2(uInset, 1.0f - vInset), ImVec2(1.0f - uInset, vInset), IM_COL32_WHITE, kImageRounding);

    unsigned int sourceTex = pipeline.GetSourceTexture();
    if (currentFactor > 0.001f) {
        drawList->AddImageRounded((ImTextureID)(intptr_t)sourceTex, imageMin, imageMax,
                                  ImVec2(uInset, 1.0f - vInset), ImVec2(1.0f - uInset, vInset),
                                  IM_COL32(255, 255, 255, static_cast<int>(currentFactor * 255.0f)), kImageRounding);
    }
}
