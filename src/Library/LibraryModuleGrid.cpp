#include "LibraryModule.h"

#include "App/settings/AppearanceTheme.h"
#include "Library/Internal/LibraryModuleUIHelpers.h"
#include "LibraryManager.h"
#include "Utils/ImGuiExtras.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace Stack::Library::ModuleUI;

namespace {

ImVec4 BlendColor(const ImVec4& from, const ImVec4& to, float t) {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    return ImVec4(
        from.x + (to.x - from.x) * clamped,
        from.y + (to.y - from.y) * clamped,
        from.z + (to.z - from.z) * clamped,
        from.w + (to.w - from.w) * clamped);
}

} // namespace

void LibraryModule::RenderLibraryGrid(
    EditorModule* editor,
    StackAppearance::AppearanceManager* appearance,
    bool wallpaperSurfaces,
    const StackAppearance::RuntimeSurfacePalette& surfacePalette,
    const LibraryRefreshSnapshot& refreshSnapshot,
    bool refreshBusy,
    bool importBusy,
    bool exportBusy,
    float dt) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 18.0f));
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x + 16.0f, style.ItemSpacing.y + 20.0f));
    ImGui::BeginChild("LibraryGrid", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (m_ScrollTargetY < 0.0f) {
        m_ScrollTargetY = ImGui::GetScrollY();
        m_ScrollCurrentY = m_ScrollTargetY;
    }

    const float currentScrollY = ImGui::GetScrollY();
    if (std::abs(currentScrollY - m_ScrollCurrentY) > 2.0f && m_ScrollCurrentY >= 0.0f) {
        m_ScrollTargetY = currentScrollY;
        m_ScrollCurrentY = currentScrollY;
    }

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            m_ScrollTargetY -= wheel * 90.0f;
        }
    }

    int renderedCount = 0;
    const ImVec2 layoutStartScreen = ImGui::GetCursorScreenPos();
    const ImVec2 layoutStartLocal = ImGui::GetCursorPos();
    const float layoutWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float packedCardGap = 22.0f;
    const auto layoutStarted = std::chrono::steady_clock::now();
    const auto& assets = LibraryManager::Get().GetAssets();
    const auto& projects = LibraryManager::Get().GetProjects();
    std::vector<LibraryPackedCard> cards;
    std::uint64_t contentHash = 1469598103934665603ull;

    if (m_ShowAssets) {
        cards.reserve(assets.size());
        for (std::size_t idx = 0; idx < assets.size(); ++idx) {
            const auto& asset = assets[idx];
            if (!asset) continue;

            const auto motionIt = m_AssetCardMotion.find(asset->fileName);
            const bool revealing = motionIt != m_AssetCardMotion.end() && motionIt->second.reveal > 0.01f;
            const bool matchesFilter = AssetMatchesFilter(*asset, m_SearchFilter, m_ActiveTagFilters, m_FilterNoTag);
            if (!matchesFilter && !revealing) continue;

            LibraryPackedCard card;
            card.index = idx;
            card.size = ComputeLibraryCardSize(static_cast<float>(asset->width), static_cast<float>(asset->height));
            cards.push_back(card);
            contentHash = HashCombine(contentHash, HashString(asset->fileName));
            contentHash = HashCombine(contentHash, static_cast<std::uint64_t>(std::max(0, asset->width)));
            contentHash = HashCombine(contentHash, static_cast<std::uint64_t>(std::max(0, asset->height)));
        }
    } else {
        cards.reserve(projects.size());
        for (std::size_t idx = 0; idx < projects.size(); ++idx) {
            const auto& project = projects[idx];
            if (!project) continue;

            const auto motionIt = m_ProjectCardMotion.find(project->fileName);
            const bool revealing = motionIt != m_ProjectCardMotion.end() && motionIt->second.reveal > 0.01f;
            const bool matchesFilter = ProjectMatchesFilter(*project, m_SearchFilter, m_ActiveTagFilters, m_FilterNoTag);
            if (!matchesFilter && !revealing) continue;

            LibraryPackedCard card;
            card.index = idx;
            card.size = ComputeLibraryCardSize(static_cast<float>(project->sourceWidth), static_cast<float>(project->sourceHeight));
            cards.push_back(card);
            contentHash = HashCombine(contentHash, HashString(project->fileName));
            contentHash = HashCombine(contentHash, static_cast<std::uint64_t>(std::max(0, project->sourceWidth)));
            contentHash = HashCombine(contentHash, static_cast<std::uint64_t>(std::max(0, project->sourceHeight)));
        }
    }

    std::ostringstream layoutKey;
    layoutKey << (m_ShowAssets ? 'A' : 'P')
              << '|' << static_cast<int>(std::round(layoutWidth))
              << '|' << refreshSnapshot.generation
              << '|' << m_FilterNoTag
              << '|' << m_SearchFilter
              << '|' << BuildTagFilterKey(m_ActiveTagFilters)
              << '|' << cards.size()
              << '|' << contentHash;

    const std::string layoutKeyText = layoutKey.str();
    m_LastRenderStats.layoutCacheHit = layoutKeyText == m_CachedLayoutKey;
    if (!m_LastRenderStats.layoutCacheHit) {
        const std::vector<LibraryPackedCard> packedCards = PackLibraryCards(cards, layoutWidth, packedCardGap);
        m_CachedPackedCards.clear();
        m_CachedPackedCards.reserve(packedCards.size());
        m_CachedPackedHeight = 0.0f;
        for (const LibraryPackedCard& card : packedCards) {
            LibraryCachedPackedCard cached;
            cached.index = card.index;
            cached.x = card.pos.x;
            cached.y = card.pos.y;
            cached.width = card.size.x;
            cached.height = card.size.y;
            m_CachedPackedCards.push_back(cached);
            m_CachedPackedHeight = std::max(m_CachedPackedHeight, cached.y + cached.height);
        }
        m_CachedLayoutKey = layoutKeyText;
    }

    const double now = ImGui::GetTime();
    const int frameCount = ImGui::GetFrameCount();
    std::vector<std::pair<std::string, const LibraryCachedPackedCard*>> entranceOrder;
    entranceOrder.reserve(m_CachedPackedCards.size());
    for (const LibraryCachedPackedCard& card : m_CachedPackedCards) {
        if (m_ShowAssets) {
            if (card.index >= assets.size() || !assets[card.index]) continue;
            entranceOrder.emplace_back(assets[card.index]->fileName, &card);
        } else {
            if (card.index >= projects.size() || !projects[card.index]) continue;
            entranceOrder.emplace_back(projects[card.index]->fileName, &card);
        }
    }
    std::sort(entranceOrder.begin(), entranceOrder.end(), [](const auto& lhs, const auto& rhs) {
        if (std::abs(lhs.second->y - rhs.second->y) > 0.5f) {
            return lhs.second->y < rhs.second->y;
        }
        return lhs.second->x < rhs.second->x;
    });
    for (std::size_t orderIndex = 0; orderIndex < entranceOrder.size(); ++orderIndex) {
        const std::string& key = entranceOrder[orderIndex].first;
        LibraryCardMotionState& motion = m_ShowAssets
            ? GetCardMotionState(m_AssetCardMotion, key)
            : GetCardMotionState(m_ProjectCardMotion, key);
        std::unordered_set<std::string>& introduced = m_ShowAssets ? m_IntroducedAssetCards : m_IntroducedProjectCards;
        const bool firstIntroduction = introduced.insert(key).second;
        motion.lastSeenFrame = frameCount;
        if (!motion.entranceInitialized) {
            motion.entranceInitialized = true;
            if (firstIntroduction) {
                const int staggerRank = std::min(static_cast<int>(orderIndex), kCardEntranceStaggerMaxRank);
                motion.entrance = 0.0f;
                motion.entranceStartTime = now + static_cast<double>(staggerRank) * kCardEntranceStaggerSeconds;
            } else {
                motion.entrance = 1.0f;
                motion.entranceStartTime = now - kCardEntranceDurationSeconds;
            }
        }
    }

    m_LastRenderStats.totalCards = m_ShowAssets ? static_cast<int>(assets.size()) : static_cast<int>(projects.size());
    m_LastRenderStats.packedCards = static_cast<int>(m_CachedPackedCards.size());
    m_LastRenderStats.layoutMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - layoutStarted).count();

    const float visibleMinY = ImGui::GetScrollY() - 360.0f;
    const float visibleMaxY = ImGui::GetScrollY() + ImGui::GetWindowHeight() + 360.0f;
    std::vector<std::string> priorityProjects;
    std::vector<std::string> priorityAssets;
    priorityProjects.reserve(48);
    priorityAssets.reserve(48);

    const auto cardRenderStarted = std::chrono::steady_clock::now();
    for (const LibraryCachedPackedCard& card : m_CachedPackedCards) {
        const float cardMinY = layoutStartLocal.y + card.y;
        const float cardMaxY = cardMinY + card.height;
        if (cardMaxY < visibleMinY || cardMinY > visibleMaxY) {
            continue;
        }

        if (m_ShowAssets) {
            if (card.index >= assets.size() || !assets[card.index]) continue;
            const auto& asset = assets[card.index];
            float entranceOffsetY = 0.0f;
            if (auto motionIt = m_AssetCardMotion.find(asset->fileName); motionIt != m_AssetCardMotion.end()) {
                entranceOffsetY = (1.0f - ResolveCardEntranceProgress(motionIt->second, now)) * kCardEntranceOffsetY;
            }
            ImGui::SetCursorScreenPos(ImVec2(layoutStartScreen.x + card.x, layoutStartScreen.y + card.y + entranceOffsetY));
            ImGui::PushID(asset->fileName.c_str());
            if (RenderAssetCard(*asset, editor)) ++renderedCount;
            ImGui::PopID();
            priorityAssets.push_back(asset->fileName);
        } else {
            if (card.index >= projects.size() || !projects[card.index]) continue;
            const auto& project = projects[card.index];
            float entranceOffsetY = 0.0f;
            if (auto motionIt = m_ProjectCardMotion.find(project->fileName); motionIt != m_ProjectCardMotion.end()) {
                entranceOffsetY = (1.0f - ResolveCardEntranceProgress(motionIt->second, now)) * kCardEntranceOffsetY;
            }
            ImGui::SetCursorScreenPos(ImVec2(layoutStartScreen.x + card.x, layoutStartScreen.y + card.y + entranceOffsetY));
            ImGui::PushID(project->fileName.c_str());
            if (RenderProjectCard(*project, editor)) ++renderedCount;
            ImGui::PopID();
            priorityProjects.push_back(project->fileName);
        }
    }
    m_LastRenderStats.cardRenderMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cardRenderStarted).count();
    m_LastRenderStats.visibleCards = renderedCount;
    LibraryManager::Get().SetThumbnailWarmupPriority(std::move(priorityProjects), std::move(priorityAssets));

    const float packedHeight = m_CachedPackedHeight;

    ImGui::SetCursorPos(ImVec2(layoutStartLocal.x, layoutStartLocal.y + packedHeight));
    if (packedHeight > 0.0f) {
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
    }

    const bool noPackedCards = m_LastRenderStats.packedCards == 0;
    const bool showLoadingState = noPackedCards && refreshBusy;
    const bool showEmptyState = noPackedCards && !refreshBusy;
    m_LibraryLoadingStateAlpha = ImGuiExtras::AnimateTowards(m_LibraryLoadingStateAlpha, showLoadingState ? 1.0f : 0.0f, dt, kStatusMotionSpeed);
    m_EmptyStateAlpha = ImGuiExtras::AnimateTowards(m_EmptyStateAlpha, showEmptyState ? 1.0f : 0.0f, dt, kStatusMotionSpeed);

    auto renderCenteredGridStatus = [&](const char* text, float alpha, bool spinner) {
        if (text == nullptr || text[0] == '\0' || alpha <= 0.01f) {
            return;
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 windowPos = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const ImVec2 textPos(
            windowPos.x + (windowSize.x - textSize.x) * 0.5f,
            windowPos.y + windowSize.y * 0.42f);
        ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        textColor.w *= alpha;
        drawList->AddText(textPos, ImGui::ColorConvertFloat4ToU32(textColor), text);

        if (spinner) {
            const float radius = 7.0f;
            const ImVec2 center(textPos.x - 18.0f, textPos.y + textSize.y * 0.5f);
            const float start = static_cast<float>(now * 5.0);
            ImVec4 spinnerColor = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
            spinnerColor.w *= alpha;
            drawList->PathClear();
            drawList->PathArcTo(center, radius, start, start + IM_PI * 1.55f, 20);
            drawList->PathStroke(ImGui::ColorConvertFloat4ToU32(spinnerColor), false, 2.0f);
        }
    };

    renderCenteredGridStatus("Scanning library...", m_LibraryLoadingStateAlpha, true);
    const char* emptyText = m_ShowAssets
        ? (m_SearchFilter[0] ? "No assets match the current search filter." : "No rendered assets have been saved to the library yet.")
        : (m_SearchFilter[0] ? "No projects match the current search filter." : "No projects found in the library.");
    renderCenteredGridStatus(emptyText, m_EmptyStateAlpha, false);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, wallpaperSurfaces ? surfacePalette.border : ImVec4(118.0f / 255.0f, 162.0f / 255.0f, 196.0f / 255.0f, 210.0f / 255.0f));
    if (!m_BlockLibraryGridContextMenuThisFrame &&
        ImGui::BeginPopupContextWindow("LibraryGridContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        RenderLibraryMenuOptions(importBusy, exportBusy);
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    RenderTagsDrawer(appearance, wallpaperSurfaces, surfacePalette, dt);

    {
        ImVec2 libraryPos = ImGui::GetWindowPos();
        ImVec2 librarySize = ImGui::GetWindowSize();

        const float searchW = 340.0f;
        const float gapFromBottom = 10.0f;
        ImVec2 searchPos = ImVec2(
            libraryPos.x + librarySize.x * 0.5f,
            libraryPos.y + librarySize.y - gapFromBottom);

        ImGui::SetNextWindowPos(searchPos, ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowSize(ImVec2(searchW, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(10.0f, 10.0f));
        ImGui::Begin("##LibraryFloatingSearch", nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f));

        if (wallpaperSurfaces) {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, surfacePalette.controlSurface);
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, surfacePalette.controlSurfaceHovered);
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, surfacePalette.controlSurfaceActive);
            ImGui::PushStyleColor(ImGuiCol_Border, surfacePalette.border);
        } else {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(24, 28, 34, 235));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(32, 38, 46, 245));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(38, 44, 52, 255));
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(110, 186, 255, 64));
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 17.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 6.0f));

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##search", "Search library...", m_SearchFilter, sizeof(m_SearchFilter));

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(4);

        const float defaultBtnWidth = 110.0f;
        const float optionsWidth = (m_OptionsIconTex != 0) ? (18.0f + ImGui::GetStyle().FramePadding.x * 2.0f) : 84.0f;
        const float allProjWidth = (m_AllProjectsIconTex != 0) ? (18.0f + ImGui::GetStyle().FramePadding.x * 2.0f) : defaultBtnWidth;
        const float assetsWidth = (m_AssetsIconTex != 0) ? (18.0f + ImGui::GetStyle().FramePadding.x * 2.0f) : defaultBtnWidth;
        const float rawWorkspaceWidth = 134.0f;
        const float iconsRowWidth =
            optionsWidth + 12.0f + allProjWidth + 6.0f + assetsWidth + 6.0f + rawWorkspaceWidth;

        ImGui::SetCursorPosX((searchW - iconsRowWidth) * 0.5f);

        bool openOptions = false;
        if (m_OptionsIconTex != 0) {
            if (ImGui::ImageButton("##OptionsIconBtn", (ImTextureID)(intptr_t)m_OptionsIconTex, ImVec2(18.0f, 18.0f))) {
                openOptions = true;
            }
        } else {
            if (ImGui::Button("Options", ImVec2(84.0f, 28.0f))) {
                openOptions = true;
            }
        }
        if (openOptions) {
            ImGui::OpenPopup("LibraryOptionsPopup");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Options");
        }

        if (ImGui::BeginPopup("LibraryOptionsPopup")) {
            RenderLibraryMenuOptions(importBusy, exportBusy);
            ImGui::EndPopup();
        }

        ImGui::SameLine(0.0f, 12.0f);
        ImGui::BeginGroup();
        {
            auto renderModePill = [&](const char* label, bool active, bool showAssetsValue, bool showRawWorkspaceValue, unsigned int iconTex, float textWidth) {
                if (wallpaperSurfaces) {
                    const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
                    const ImVec4 button = active
                        ? BlendColor(surfacePalette.controlSurface, accent, 0.28f)
                        : surfacePalette.controlSurface;
                    const ImVec4 buttonHovered = active
                        ? BlendColor(surfacePalette.controlSurfaceHovered, accent, 0.34f)
                        : surfacePalette.controlSurfaceHovered;
                    const ImVec4 buttonActive = active
                        ? BlendColor(surfacePalette.controlSurfaceActive, accent, 0.40f)
                        : surfacePalette.controlSurfaceActive;
                    ImGui::PushStyleColor(ImGuiCol_Button, button);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonHovered);
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, buttonActive);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, active ? IM_COL32(110, 186, 255, 52) : IM_COL32(255, 255, 255, 10));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? IM_COL32(110, 186, 255, 68) : IM_COL32(255, 255, 255, 22));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active ? IM_COL32(110, 186, 255, 82) : IM_COL32(255, 255, 255, 30));
                }

                bool clicked = false;
                if (iconTex != 0) {
                    const std::string strId = std::string("##ModePill_") + label;
                    clicked = ImGui::ImageButton(strId.c_str(), (ImTextureID)(intptr_t)iconTex, ImVec2(18.0f, 18.0f));
                } else {
                    clicked = ImGui::Button(label, ImVec2(textWidth, 28.0f));
                }

                if (clicked) {
                    m_ShowRawWorkspace = showRawWorkspaceValue;
                    m_ShowAssets = showAssetsValue;
                    if (m_ShowRawWorkspace) {
                        m_SelectedProjects.clear();
                        m_SelectedAssets.clear();
                        m_PreviewProject = nullptr;
                        m_PreviewAsset = nullptr;
                    } else if (m_ShowAssets) {
                        m_SelectedProjects.clear();
                    } else {
                        m_SelectedAssets.clear();
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", label);
                }
                ImGui::PopStyleColor(3);
            };

            renderModePill("All Projects", !m_ShowRawWorkspace && !m_ShowAssets, false, false, m_AllProjectsIconTex, defaultBtnWidth);
            ImGui::SameLine(0.0f, 6.0f);
            renderModePill("Assets", !m_ShowRawWorkspace && m_ShowAssets, true, false, m_AssetsIconTex, defaultBtnWidth);
            ImGui::SameLine(0.0f, 6.0f);
            renderModePill("RAW Workspace", m_ShowRawWorkspace, false, true, 0, rawWorkspaceWidth);
        }
        ImGui::EndGroup();

        ImGui::PopStyleVar();
        ImGui::End();
        ImGui::PopStyleVar(3);
    }

    const float maxScrollY = ImGui::GetScrollMaxY();
    m_ScrollTargetY = std::clamp(m_ScrollTargetY, 0.0f, maxScrollY);

    if (std::abs(m_ScrollCurrentY - m_ScrollTargetY) > 0.05f) {
        const float scrollT = 1.0f - std::exp(-dt * 14.0f);
        m_ScrollCurrentY += (m_ScrollTargetY - m_ScrollCurrentY) * scrollT;
        m_ScrollCurrentY = std::clamp(m_ScrollCurrentY, 0.0f, maxScrollY);
        ImGui::SetScrollY(m_ScrollCurrentY);
    } else {
        m_ScrollCurrentY = m_ScrollTargetY;
        ImGui::SetScrollY(m_ScrollCurrentY);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    PruneCardMotionStates(m_ProjectCardMotion, ImGui::GetFrameCount());
    PruneCardMotionStates(m_AssetCardMotion, ImGui::GetFrameCount());
}
