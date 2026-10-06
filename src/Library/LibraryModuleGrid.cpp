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

constexpr float kLibraryViewScaleMin = 0.55f;
constexpr float kLibraryViewScaleMax = 1.80f;

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
        const ImGuiIO& io = ImGui::GetIO();
        const float wheel = io.MouseWheel;
        if (wheel != 0.0f) {
            if (io.KeyCtrl) {
                const float previousScale = m_LibraryViewScale;
                AdjustViewScale(wheel);

                if (std::abs(m_LibraryViewScale - previousScale) > 0.0001f) {
                    const float scaleRatio = m_LibraryViewScale / previousScale;
                    const float cursorOffsetY = std::clamp(
                        io.MousePos.y - ImGui::GetWindowPos().y,
                        0.0f,
                        ImGui::GetWindowHeight());
                    const float scrollAnchor = m_ScrollTargetY >= 0.0f
                        ? m_ScrollTargetY
                        : currentScrollY;
                    const float anchoredScrollY =
                        (scrollAnchor + cursorOffsetY) * scaleRatio - cursorOffsetY;
                    m_ScrollTargetY = std::max(0.0f, anchoredScrollY);
                }

                ImGui::SetTooltip("Library view: %.0f%%", m_LibraryViewScale * 100.0f);
            } else {
                m_ScrollTargetY -= wheel * 90.0f;
            }
        }
    }

    int renderedCount = 0;
    const ImVec2 layoutStartScreen = ImGui::GetCursorScreenPos();
    const ImVec2 layoutStartLocal = ImGui::GetCursorPos();
    const float layoutWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float packedCardGap = 22.0f * m_LibraryViewScale;
    const bool searchLayoutActive = m_SearchFilter[0] != '\0';
    const double now = ImGui::GetTime();
    const std::string currentSearchQuery(m_SearchFilter);
    if (currentSearchQuery != m_LastSearchLayoutQuery) {
        m_LastSearchLayoutQuery = currentSearchQuery;
        m_SearchLayoutQueryChangedAt = now;
        m_ScrollTargetY = 0.0f;
    }
    const bool searchMovementReady =
        !searchLayoutActive || (now - m_SearchLayoutQueryChangedAt) >= 0.10;
    const float searchResultScale = searchLayoutActive ? 1.08f : 1.0f;
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

            const bool matchesFilter = AssetMatchesFilter(*asset, m_SearchFilter, m_ActiveTagFilters, m_FilterNoTag);
            if (!matchesFilter) continue;

            LibraryPackedCard card;
            card.index = idx;
            card.size = ComputeLibraryCardSize(
                static_cast<float>(asset->width),
                static_cast<float>(asset->height),
                m_LibraryViewScale * searchResultScale);
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

            const bool matchesFilter = ProjectMatchesFilter(*project, m_SearchFilter, m_ActiveTagFilters, m_FilterNoTag);
            if (!matchesFilter) continue;

            LibraryPackedCard card;
            card.index = idx;
            card.size = ComputeLibraryCardSize(
                static_cast<float>(project->sourceWidth),
                static_cast<float>(project->sourceHeight),
                m_LibraryViewScale * searchResultScale);
            cards.push_back(card);
            contentHash = HashCombine(contentHash, HashString(project->fileName));
            contentHash = HashCombine(contentHash, static_cast<std::uint64_t>(std::max(0, project->sourceWidth)));
            contentHash = HashCombine(contentHash, static_cast<std::uint64_t>(std::max(0, project->sourceHeight)));
        }
    }

    std::ostringstream layoutKey;
    layoutKey << (m_ShowAssets ? 'A' : 'P')
              << '|' << static_cast<int>(std::round(layoutWidth))
              << '|' << static_cast<int>(std::round(m_LibraryViewScale * 1000.0f))
              << '|' << refreshSnapshot.generation
              << '|' << m_FilterNoTag
              << '|' << m_SearchFilter
              << '|' << BuildTagFilterKey(m_ActiveTagFilters)
              << '|' << cards.size()
              << '|' << contentHash;

    const std::string layoutKeyText = layoutKey.str();
    m_LastRenderStats.layoutCacheHit = layoutKeyText == m_CachedLayoutKey;
    if (!m_LastRenderStats.layoutCacheHit) {
        const float baseTargetScale = std::clamp(
            m_LibraryViewScale * searchResultScale,
            kLibraryViewScaleMin,
            kLibraryViewScaleMax);
        const float adaptiveScaleLimit = std::clamp(
            kLibraryViewScaleMax / std::max(baseTargetScale, 0.001f),
            1.0f,
            1.08f);
        std::vector<LibraryPackedCard> packedCards = PackLibraryCards(
            cards,
            layoutWidth,
            packedCardGap,
            adaptiveScaleLimit);
        float packedWidth = 0.0f;
        float packedHeight = 0.0f;
        for (const LibraryPackedCard& card : packedCards) {
            packedWidth = std::max(packedWidth, card.pos.x + card.size.x);
            packedHeight = std::max(packedHeight, card.pos.y + card.size.y);
        }
        const float centeredX = searchLayoutActive
            ? std::max(0.0f, (layoutWidth - packedWidth) * 0.5f)
            : 0.0f;
        const float availableViewHeight = std::max(1.0f, ImGui::GetWindowHeight() - layoutStartLocal.y - 72.0f);
        const float centeredY = searchLayoutActive && packedHeight < availableViewHeight
            ? std::max(0.0f, (availableViewHeight - packedHeight) * 0.5f)
            : 0.0f;
        if (searchLayoutActive) {
            for (LibraryPackedCard& card : packedCards) {
                card.pos.x += centeredX;
                card.pos.y += centeredY;
            }
        }
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
            cached.scaleMultiplier = card.scaleMultiplier;
            m_CachedPackedCards.push_back(cached);
            m_CachedPackedHeight = std::max(m_CachedPackedHeight, cached.y + cached.height);
        }
        m_CachedLayoutKey = layoutKeyText;
    }

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

    std::vector<LibraryCachedPackedCard> animatedCards;
    animatedCards.reserve(m_CachedPackedCards.size() + 32);
    const auto smoothDamp = [dt](float current, float target, float& velocity, float smoothTime) {
        const float safeTime = std::max(0.05f, smoothTime);
        const float omega = 2.0f / safeTime;
        const float step = omega * std::max(0.0f, dt);
        const float decay = 1.0f /
            (1.0f + step + 0.48f * step * step + 0.235f * step * step * step);
        const float change = current - target;
        const float temporary = (velocity + omega * change) * dt;
        velocity = (velocity - omega * temporary) * decay;
        float result = target + (change + temporary) * decay;
        const bool passedTarget =
            ((target - current) > 0.0f) == (result > target);
        if (passedTarget) {
            result = target;
            velocity = 0.0f;
        }
        if (std::abs(result - target) < 0.01f && std::abs(velocity) < 0.01f) {
            velocity = 0.0f;
            return target;
        }
        return result;
    };

    for (const LibraryCachedPackedCard& targetCard : m_CachedPackedCards) {
        std::string key;
        if (m_ShowAssets) {
            if (targetCard.index >= assets.size() || !assets[targetCard.index]) continue;
            key = assets[targetCard.index]->fileName;
        } else {
            if (targetCard.index >= projects.size() || !projects[targetCard.index]) continue;
            key = projects[targetCard.index]->fileName;
        }

        LibraryCardMotionState& motion = m_ShowAssets
            ? GetCardMotionState(m_AssetCardMotion, key)
            : GetCardMotionState(m_ProjectCardMotion, key);
        motion.lastSeenFrame = frameCount;
        motion.reveal = ImGuiExtras::AnimateTowards(motion.reveal, 1.0f, dt, kCardMotionSpeed);
        motion.layoutTargetX = targetCard.x;
        motion.layoutTargetY = targetCard.y;
        motion.layoutTargetScale = std::clamp(
            m_LibraryViewScale * searchResultScale * targetCard.scaleMultiplier,
            kLibraryViewScaleMin,
            kLibraryViewScaleMax);
        if (!motion.layoutInitialized) {
            motion.layoutX = motion.layoutTargetX;
            motion.layoutY = motion.layoutTargetY;
            motion.layoutScale = motion.layoutTargetScale;
            motion.layoutInitialized = true;
        } else {
            if (searchMovementReady) {
                motion.layoutX = smoothDamp(
                    motion.layoutX, motion.layoutTargetX, motion.layoutVelocityX, 0.30f);
                motion.layoutY = smoothDamp(
                    motion.layoutY, motion.layoutTargetY, motion.layoutVelocityY, 0.30f);
                motion.layoutScale = smoothDamp(
                    motion.layoutScale, motion.layoutTargetScale, motion.layoutScaleVelocity, 0.24f);
            } else {
                const float velocityDecay = std::exp(-dt * 24.0f);
                motion.layoutVelocityX *= velocityDecay;
                motion.layoutVelocityY *= velocityDecay;
                motion.layoutScaleVelocity *= velocityDecay;
            }
        }

        LibraryCachedPackedCard animated = targetCard;
        animated.x = motion.layoutX;
        animated.y = motion.layoutY;
        const ImVec2 animatedSize = ComputeLibraryCardSize(
            static_cast<float>(m_ShowAssets ? assets[targetCard.index]->width : projects[targetCard.index]->sourceWidth),
            static_cast<float>(m_ShowAssets ? assets[targetCard.index]->height : projects[targetCard.index]->sourceHeight),
            motion.layoutScale);
        animated.width = animatedSize.x;
        animated.height = animatedSize.y;
        animatedCards.push_back(animated);
    }

    // Cards rejected by the current query keep their last live rectangle while
    // fading. They are excluded from target packing, so they cannot push the
    // surviving result group around or reserve empty holes in its final layout.
    if (m_ShowAssets) {
        for (std::size_t idx = 0; idx < assets.size(); ++idx) {
            const auto& asset = assets[idx];
            if (!asset || AssetMatchesFilter(*asset, m_SearchFilter, m_ActiveTagFilters, m_FilterNoTag)) continue;
            auto motionIt = m_AssetCardMotion.find(asset->fileName);
            if (motionIt == m_AssetCardMotion.end() || !motionIt->second.layoutInitialized) continue;
            LibraryCardMotionState& motion = motionIt->second;
            motion.lastSeenFrame = frameCount;
            motion.reveal = ImGuiExtras::AnimateTowards(motion.reveal, 0.0f, dt, kCardMotionSpeed);
            if (motion.reveal <= 0.01f) continue;
            const ImVec2 size = ComputeLibraryCardSize(
                static_cast<float>(asset->width), static_cast<float>(asset->height),
                motion.layoutScale);
            animatedCards.push_back({ idx, motion.layoutX, motion.layoutY, size.x, size.y });
        }
    } else {
        for (std::size_t idx = 0; idx < projects.size(); ++idx) {
            const auto& project = projects[idx];
            if (!project || ProjectMatchesFilter(*project, m_SearchFilter, m_ActiveTagFilters, m_FilterNoTag)) continue;
            auto motionIt = m_ProjectCardMotion.find(project->fileName);
            if (motionIt == m_ProjectCardMotion.end() || !motionIt->second.layoutInitialized) continue;
            LibraryCardMotionState& motion = motionIt->second;
            motion.lastSeenFrame = frameCount;
            motion.reveal = ImGuiExtras::AnimateTowards(motion.reveal, 0.0f, dt, kCardMotionSpeed);
            if (motion.reveal <= 0.01f) continue;
            const ImVec2 size = ComputeLibraryCardSize(
                static_cast<float>(project->sourceWidth), static_cast<float>(project->sourceHeight),
                motion.layoutScale);
            animatedCards.push_back({ idx, motion.layoutX, motion.layoutY, size.x, size.y });
        }
    }

    const auto cardRenderStarted = std::chrono::steady_clock::now();
    float animatedContentHeight = m_CachedPackedHeight;
    for (const LibraryCachedPackedCard& card : animatedCards) {
        animatedContentHeight = std::max(animatedContentHeight, card.y + card.height);
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

    const float packedHeight = animatedContentHeight;

    ImGui::SetCursorPos(ImVec2(layoutStartLocal.x, layoutStartLocal.y + packedHeight));
    if (packedHeight > 0.0f) {
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
    }

    const bool noPackedCards = m_LastRenderStats.packedCards == 0;
    const bool transitionCardsGone = animatedCards.empty();
    const bool showLoadingState = noPackedCards && transitionCardsGone && refreshBusy;
    const bool showEmptyState = noPackedCards && transitionCardsGone && !refreshBusy;
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

    ImGui::SetNextWindowSizeConstraints(ImVec2(300.0f, 0.0f), ImVec2(440.0f, ImGui::GetMainViewport()->Size.y * 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 9.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (!m_BlockLibraryGridContextMenuThisFrame || ImGui::IsPopupOpen("LibraryGridContextMenu")) {
        if (ImGui::BeginPopupContextWindow("LibraryGridContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            RenderLibraryMenuOptions(importBusy, exportBusy);
            ImGui::EndPopup();
        }
    }
    ImGui::PopStyleVar(5);

    RenderTagsDrawer(appearance, wallpaperSurfaces, surfacePalette, dt);

    if (!m_SectionPanelHosted) {
        ImVec2 libraryPos = ImGui::GetWindowPos();
        ImVec2 librarySize = ImGui::GetWindowSize();

        const float searchFieldW = 210.0f;
        const float iconHitSize = 28.0f;
        const float iconGap = 8.0f;
        const float stripW = searchFieldW + 14.0f + iconHitSize;
        const float featherX = 74.0f;
        const float featherY = 52.0f;
        const float stripH = 31.0f;
        const float overlayW = stripW + featherX * 2.0f;
        const float overlayH = stripH + featherY * 2.0f;
        const float gapFromBottom = 4.0f;
        ImVec2 searchPos = ImVec2(
            libraryPos.x + librarySize.x * 0.5f,
            libraryPos.y + librarySize.y - gapFromBottom);
        const ImGuiViewport* libraryViewport = ImGui::GetWindowViewport();

        ImGui::SetNextWindowPos(searchPos, ImGuiCond_Always, ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowSize(ImVec2(overlayW, overlayH), ImGuiCond_Always);
        if (libraryViewport != nullptr) {
            ImGui::SetNextWindowViewport(libraryViewport->ID);
        }
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(10.0f, 10.0f));
        ImGui::Begin("##LibraryFloatingSearch", nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse);

        // A broad, low-density feather keeps moving thumbnails legible behind
        // the controls without introducing a panel edge. Each shell contributes
        // only a small amount of opacity; their accumulation becomes gently
        // stronger near the strip and falls to effectively zero at the outside.
        {
            ImDrawList* overlayDrawList = ImGui::GetWindowDrawList();
            const ImVec2 windowPos = ImGui::GetWindowPos();
            const ImVec2 stripMin(windowPos.x + featherX, windowPos.y + featherY);
            const ImVec2 stripMax(stripMin.x + stripW, stripMin.y + stripH);
            ImVec4 backdrop = wallpaperSurfaces
                ? surfacePalette.controlSurface
                : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
            constexpr int kFeatherShells = 30;
            for (int shell = kFeatherShells - 1; shell >= 0; --shell) {
                const float t = static_cast<float>(shell) / static_cast<float>(kFeatherShells - 1);
                const float smooth = t * t * (3.0f - 2.0f * t);
                const float expandX = featherX * t;
                const float expandY = featherY * t;
                ImVec4 shellColor = backdrop;
                shellColor.w = 0.0025f + (1.0f - smooth) * 0.0125f;
                overlayDrawList->AddRectFilled(
                    ImVec2(stripMin.x - expandX, stripMin.y - expandY),
                    ImVec2(stripMax.x + expandX, stripMax.y + expandY),
                    ImGui::GetColorU32(shellColor),
                    17.0f + std::max(expandX, expandY));
            }
        }

        ImGui::SetCursorPos(ImVec2(featherX, featherY));

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(iconGap, 2.0f));

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
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(38.0f, 6.0f));

        ImGui::SetNextItemWidth(searchFieldW);
        ImGui::InputTextWithHint("##search", "Search", m_SearchFilter, sizeof(m_SearchFilter));
        if (m_SearchIconTex != 0) {
            const ImVec2 fieldMin = ImGui::GetItemRectMin();
            const ImVec2 fieldMax = ImGui::GetItemRectMax();
            const float searchIconSize = 15.0f;
            const ImVec2 iconMin(fieldMin.x + 14.0f, fieldMin.y + (fieldMax.y - fieldMin.y - searchIconSize) * 0.5f);
            ImVec4 tint = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                tint = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            }
            ImGui::GetWindowDrawList()->AddImage(
                (ImTextureID)(intptr_t)m_SearchIconTex,
                iconMin, ImVec2(iconMin.x + searchIconSize, iconMin.y + searchIconSize),
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImGui::GetColorU32(tint));
        }

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(4);

        ImGui::SameLine(0.0f, 14.0f);
        const auto renderIconTab = [&](const char* id, const char* tooltip, unsigned int texture, bool active) {
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton(id, ImVec2(iconHitSize, iconHitSize));
            const bool hovered = ImGui::IsItemHovered();
            const bool held = ImGui::IsItemActive();
            ImVec4 tint = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
            if (active || held) {
                tint = accent;
            } else if (hovered) {
                tint = BlendColor(tint, accent, 0.72f);
            } else {
                tint.w *= 0.52f;
            }
            const float visualSize = 18.0f;
            const ImVec2 iconMin(
                cursor.x + (iconHitSize - visualSize) * 0.5f,
                cursor.y + (iconHitSize - visualSize) * 0.5f);
            if (texture != 0) {
                ImGui::GetWindowDrawList()->AddImage(
                    (ImTextureID)(intptr_t)texture,
                    iconMin, ImVec2(iconMin.x + visualSize, iconMin.y + visualSize),
                    ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImGui::GetColorU32(tint));
            }
            if (hovered) ImGui::SetTooltip("%s", tooltip);
            return ImGui::IsItemClicked();
        };

        const bool openOptions = renderIconTab("##OptionsIconBtn", "Options", m_OptionsIconTex, false);
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
