#include "LibraryModule.h"
#include "LibraryManager.h"
#include "TagManager.h"
#include "Async/TaskSystem.h"
#include "Composite/CompositeModule.h"
#include "Editor/EditorModule.h"
#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "RenderTab/RenderTab.h"
#include "ProjectData.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include "Renderer/GLHelpers.h"
#include "Persistence/StackBinaryFormat.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace {

std::string FormatKeyLabel(const std::string& key) {
    std::string result;
    result.reserve(key.size() + 8);

    bool previousWasUpper = false;
    for (std::size_t i = 0; i < key.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(key[i]);

        if (ch == '_' || ch == '-') {
            result.push_back(' ');
            previousWasUpper = false;
            continue;
        }

        const bool isUpper = std::isupper(ch) != 0;
        const bool needsSpace =
            i > 0 &&
            isUpper &&
            !previousWasUpper &&
            result.back() != ' ';

        if (needsSpace) {
            result.push_back(' ');
        }

        if (result.empty() || result.back() == ' ') {
            result.push_back(static_cast<char>(std::toupper(ch)));
        } else {
            result.push_back(static_cast<char>(ch));
        }

        previousWasUpper = isUpper;
    }

    return result.empty() ? "Value" : result;
}

std::string LayerDisplayName(const std::string& type) {
    const std::string displayName = LayerRegistry::GetLibraryDisplayNameFromTypeId(type);
    if (!displayName.empty()) return displayName;
    return FormatKeyLabel(type);
}

std::string FormatJsonValue(const json& value) {
    if (value.is_boolean()) {
        return value.get<bool>() ? "Yes" : "No";
    }

    if (value.is_number_integer()) {
        return std::to_string(value.get<int>());
    }

    if (value.is_number_float()) {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(3) << value.get<double>();
        return stream.str();
    }

    if (value.is_string()) {
        return value.get<std::string>();
    }

    if (value.is_array()) {
        std::ostringstream stream;
        stream << "[";
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (i > 0) stream << ", ";
            if (value[i].is_number_float()) {
                stream << std::fixed << std::setprecision(3) << value[i].get<double>();
            } else if (value[i].is_number_integer()) {
                stream << value[i].get<int>();
            } else if (value[i].is_string()) {
                stream << value[i].get<std::string>();
            } else if (value[i].is_boolean()) {
                stream << (value[i].get<bool>() ? "Yes" : "No");
            } else {
                stream << value[i].dump();
            }
        }
        stream << "]";
        return stream.str();
    }

    return value.dump();
}

constexpr float kSidebarRailWidth = 40.0f;
constexpr float kSidebarMinWidth = 170.0f;
constexpr float kSidebarMaxWidth = 360.0f;
constexpr float kSidebarMotionSpeed = 20.0f;
constexpr float kCardMotionSpeed = 18.0f;
constexpr float kPreviewMotionSpeed = 18.0f;
constexpr float kStatusMotionSpeed = 16.0f;
constexpr float kDialogAppearDuration = 0.12f;

template <typename TMap>
LibraryCardMotionState& GetCardMotionState(TMap& states, const std::string& key) {
    return states[key];
}

template <typename TMap>
void PruneCardMotionStates(TMap& states, int frameCount) {
    for (auto it = states.begin(); it != states.end(); ) {
        if (frameCount - it->second.lastSeenFrame > 240) {
            it = states.erase(it);
        } else {
            ++it;
        }
    }
}

struct SplitHandleMotionState {
    float hover = 0.0f;
    int lastSeenFrame = 0;
};

template <typename TMap>
void PruneSplitHandleMotionStates(TMap& states, int frameCount) {
    for (auto it = states.begin(); it != states.end(); ) {
        if (frameCount - it->second.lastSeenFrame > 120) {
            it = states.erase(it);
        } else {
            ++it;
        }
    }
}

ImU32 ScaleColorAlpha(ImU32 color, float alphaScale) {
    alphaScale = std::clamp(alphaScale, 0.0f, 1.0f);
    const ImU32 r = color & 0xFFu;
    const ImU32 g = (color >> 8) & 0xFFu;
    const ImU32 b = (color >> 16) & 0xFFu;
    const ImU32 a = static_cast<ImU32>(((color >> 24) & 0xFFu) * alphaScale);
    return (a << 24) | (b << 16) | (g << 8) | r;
}

SplitHandleMotionState& GetSplitHandleMotionState(std::unordered_map<ImGuiID, SplitHandleMotionState>& states, ImGuiID key) {
    return states[key];
}

void DrawCardMotionFrame(ImDrawList* drawList, const ImRect& rect, float hover, float selected, bool hovered) {
    hover = std::clamp(hover, 0.0f, 1.0f);
    selected = std::clamp(selected, 0.0f, 1.0f);

    const float lift = hover * 3.0f;
    const ImVec2 shadowMin(rect.Min.x, rect.Min.y + lift);
    const ImVec2 shadowMax(rect.Max.x, rect.Max.y + lift);
    const float rounding = 10.0f;

    drawList->AddRectFilled(
        ImVec2(shadowMin.x - 2.0f, shadowMin.y - 1.0f),
        ImVec2(shadowMax.x + 2.0f, shadowMax.y + 3.0f),
        IM_COL32(0, 0, 0, static_cast<int>(26 + (52 * hover))),
        rounding + 2.0f);

    const ImU32 accent = IM_COL32(88, 163, 255, static_cast<int>(50 + (120 * selected)));
    const ImU32 hoverAccent = IM_COL32(120, 180, 255, static_cast<int>(20 + (55 * hover)));
    if (selected > 0.01f) {
        drawList->AddRectFilled(
            ImVec2(rect.Min.x - 1.0f, rect.Min.y - 1.0f),
            ImVec2(rect.Max.x + 1.0f, rect.Max.y + 1.0f),
            ScaleColorAlpha(accent, 0.18f * selected),
            rounding + 1.0f);
    }

    if (hovered || hover > 0.01f) {
        drawList->AddRect(
            ImVec2(rect.Min.x - 1.0f, rect.Min.y - 1.0f),
            ImVec2(rect.Max.x + 1.0f, rect.Max.y + 1.0f),
            ScaleColorAlpha(hoverAccent, 0.8f),
            rounding + 1.0f,
            0,
            1.5f);
    }

    if (selected > 0.01f) {
        drawList->AddRect(
            ImVec2(rect.Min.x - 1.0f, rect.Min.y - 1.0f),
            ImVec2(rect.Max.x + 1.0f, rect.Max.y + 1.0f),
            ScaleColorAlpha(accent, 0.85f),
            rounding + 1.0f,
            0,
            2.0f + (selected * 0.25f));
    }
}

void DrawSplitHandle(ImDrawList* drawList, const ImRect& rect, float split, ImGuiID handleId, bool hovered, bool active) {
    static std::unordered_map<ImGuiID, SplitHandleMotionState> s_SplitHandleMotion;
    SplitHandleMotionState& motion = GetSplitHandleMotionState(s_SplitHandleMotion, handleId);
    motion.lastSeenFrame = ImGui::GetFrameCount();
    motion.hover = ImGuiExtras::AnimateTowards(motion.hover, (hovered || active) ? 1.0f : 0.0f, ImGui::GetIO().DeltaTime, 24.0f);
    PruneSplitHandleMotionStates(s_SplitHandleMotion, ImGui::GetFrameCount());

    const float splitX = rect.Min.x + rect.GetWidth() * split;
    const ImVec2 center(splitX, rect.Min.y + rect.GetHeight() * 0.5f);
    const float lineThickness = 1.0f + (0.8f * motion.hover);
    const float handleRadius = 5.0f + (1.1f * motion.hover);
    const float haloRadius = handleRadius + (4.5f * motion.hover);

    drawList->AddLine(
        ImVec2(splitX, rect.Min.y),
        ImVec2(splitX, rect.Max.y),
        IM_COL32(255, 255, 255, static_cast<int>(210 + (25 * motion.hover))),
        lineThickness);
    drawList->AddCircleFilled(
        center,
        haloRadius,
        IM_COL32(255, 255, 255, static_cast<int>(30 + (35 * motion.hover))));
    drawList->AddCircleFilled(
        center,
        handleRadius,
        IM_COL32(255, 255, 255, static_cast<int>(220 + (25 * motion.hover))));
}

bool ProjectMatchesFilter(const ProjectEntry& project, const char* filter, const std::unordered_set<std::string>& activeTags, bool noTagOnly) {
    // 1. Tag filtering
    if (noTagOnly) {
        if (!TagManager::Get().GetTags(project.fileName).empty()) return false;
    } else if (!activeTags.empty()) {
        bool foundAny = false;
        for (const auto& tag : activeTags) {
            if (TagManager::Get().HasTag(project.fileName, tag)) {
                foundAny = true;
                break;
            }
        }
        if (!foundAny) return false;
    }

    // 2. Search filtering
    if (!filter || !filter[0]) return true;

    std::string needle = filter;
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::string haystack = project.projectName + " " + project.fileName;
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return haystack.find(needle) != std::string::npos;
}

bool AssetMatchesFilter(const AssetEntry& asset, const char* filter, const std::unordered_set<std::string>& activeTags, bool noTagOnly) {
    // 1. Tag filtering
    if (noTagOnly) {
        if (!TagManager::Get().GetTags(asset.fileName).empty()) return false;
    } else if (!activeTags.empty()) {
        bool foundAny = false;
        for (const auto& tag : activeTags) {
            if (TagManager::Get().HasTag(asset.fileName, tag)) {
                foundAny = true;
                break;
            }
        }
        if (!foundAny) return false;
    }

    // 2. Search filtering
    if (!filter || !filter[0]) return true;

    std::string needle = filter;
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::string haystack = asset.displayName + " " + asset.fileName + " " + asset.projectName;
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return haystack.find(needle) != std::string::npos;
}

bool IsRenderProject(const ProjectEntry& project) {
    return project.projectKind == StackBinaryFormat::kRenderProjectKind;
}

bool IsCompositeProject(const ProjectEntry& project) {
    return project.projectKind == StackBinaryFormat::kCompositeProjectKind;
}

ImVec2 FitImageToBounds(float imageWidth, float imageHeight, const ImVec2& bounds) {
    if (imageWidth <= 0.0f || imageHeight <= 0.0f) {
        return bounds;
    }

    ImVec2 fitted = bounds;
    const float imageAspect = imageWidth / std::max(imageHeight, 1.0f);
    const float boundsAspect = bounds.x / std::max(bounds.y, 1.0f);
    if (imageAspect > boundsAspect) {
        fitted.y = fitted.x / std::max(imageAspect, 0.1f);
    } else {
        fitted.x = fitted.y * imageAspect;
    }
    return fitted;
}



void DrawComparePreview(const ProjectEntry& project, const ImVec2& requestedSize, float& split) {
    ImGui::InvisibleButton("##LibraryComparePreview", requestedSize);
    const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImGuiID handleId = ImGui::GetItemID();

    if (hovered) {
        split = (ImGui::GetIO().MousePos.x - rect.Min.x) / std::max(1.0f, rect.GetWidth());
        split = std::clamp(split, 0.0f, 1.0f);
    } else {
        split = std::clamp(split, 0.0f, 1.0f);
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(rect.Min, rect.Max, IM_COL32(14, 16, 20, 255), 10.0f);

    const ImTextureID beforeTex = (ImTextureID)(intptr_t)project.sourcePreviewTex;
    const ImTextureID afterTex = (ImTextureID)(intptr_t)project.fullPreviewTex;
    const ImVec2 beforeUv0(0, 1);
    const ImVec2 beforeUv1(1, 0);
    const ImVec2 afterUv0(0, 1);
    const ImVec2 afterUv1(1, 0);

    if (beforeTex) {
        drawList->AddImage(beforeTex, rect.Min, rect.Max, beforeUv0, beforeUv1);
    }

    if (afterTex) {
        const float splitX = rect.Min.x + rect.GetWidth() * split;
        drawList->PushClipRect(rect.Min, ImVec2(splitX, rect.Max.y), true);
        drawList->AddImage(afterTex, rect.Min, rect.Max, afterUv0, afterUv1);
        drawList->PopClipRect();
        DrawSplitHandle(drawList, rect, split, handleId, hovered, active);

        drawList->AddText(ImVec2(rect.Min.x + 10.0f, rect.Min.y + 10.0f), IM_COL32(255, 255, 255, 220), "After");
        drawList->AddText(ImVec2(rect.Max.x - 55.0f, rect.Min.y + 10.0f), IM_COL32(255, 255, 255, 220), "Before");
    }
}

} // namespace

LibraryModule::LibraryModule() {}
LibraryModule::~LibraryModule() {}

void LibraryModule::Initialize() {
    LibraryManager::Get().RefreshLibrary();
}

void LibraryModule::SyncRenameBuffer() {
    if (!m_PreviewProject) {
        m_RenameBuffer[0] = '\0';
        m_RenameTargetFileName.clear();
        return;
    }

    if (m_RenameTargetFileName == m_PreviewProject->fileName) {
        return;
    }

    std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", m_PreviewProject->projectName.c_str());
    m_RenameTargetFileName = m_PreviewProject->fileName;
}

void LibraryModule::OpenProjectPreviewByFileName(const std::string& fileName) {
    if (fileName.empty()) {
        return;
    }

    const auto& projects = LibraryManager::Get().GetProjects();
    for (const auto& project : projects) {
        if (!project || project->fileName != fileName) {
            continue;
        }

        m_ShowAssets = false;
        m_PreviewProject = project;
        m_PreviewAsset = nullptr;
        m_ProjectPreviewTransition = 0.0f;
        m_ProjectPreviewClosing = false;
        m_ProjectPreviewRefreshAfterClose = false;
        m_AssetPreviewTransition = 0.0f;
        m_AssetPreviewClosing = false;
        m_CompareSplit = 0.5f;
        LibraryManager::Get().CancelAssetPreviewRequests();
        LibraryManager::Get().CancelProjectPreviewRequests();
        LibraryManager::Get().RequestProjectPreview(m_PreviewProject);
        SyncRenameBuffer();
        return;
    }
}

void LibraryModule::OpenAssetPreviewByFileName(const std::string& fileName) {
    if (fileName.empty()) {
        return;
    }

    const auto& assets = LibraryManager::Get().GetAssets();
    for (const auto& asset : assets) {
        if (!asset || asset->fileName != fileName) {
            continue;
        }

        m_ShowAssets = true;
        m_PreviewAsset = asset;
        m_PreviewProject = nullptr;
        m_AssetPreviewTransition = 0.0f;
        m_AssetPreviewClosing = false;
        m_ProjectPreviewTransition = 0.0f;
        m_ProjectPreviewClosing = false;
        m_ProjectPreviewRefreshAfterClose = false;
        LibraryManager::Get().CancelProjectPreviewRequests();
        LibraryManager::Get().CancelAssetPreviewRequests();
        LibraryManager::Get().RequestAssetPreview(m_PreviewAsset);
        return;
    }
}

void LibraryModule::RenderUI(EditorModule* editor, RenderTab* renderTab, CompositeModule* composite, int* activeTab) {
    m_CachedEditor = editor;
    m_CachedComposite = composite;
    m_CachedActiveTab = activeTab;
    m_BlockLibraryGridContextMenuThisFrame = false;
    const float dt = ImGui::GetIO().DeltaTime;

    if (!m_PreviewProject && !m_PreviewAsset) {
        LibraryManager::Get().TickAutoRefresh();
    }

    const bool importBusy = Async::IsBusy(LibraryManager::Get().GetImportTaskState());
    const bool exportBusy = Async::IsBusy(LibraryManager::Get().GetExportTaskState());
    const bool saveBusy = Async::IsBusy(LibraryManager::Get().GetSaveTaskState());
    const bool loadBusy = Async::IsBusy(LibraryManager::Get().GetProjectLoadTaskState());

    m_ImportStatusAlpha = ImGuiExtras::AnimateTowards(
        m_ImportStatusAlpha,
        !LibraryManager::Get().GetImportStatusText().empty() ? 1.0f : 0.0f,
        dt,
        kStatusMotionSpeed);
    m_ExportStatusAlpha = ImGuiExtras::AnimateTowards(
        m_ExportStatusAlpha,
        !LibraryManager::Get().GetExportStatusText().empty() ? 1.0f : 0.0f,
        dt,
        kStatusMotionSpeed);
    m_SaveStatusAlpha = ImGuiExtras::AnimateTowards(
        m_SaveStatusAlpha,
        (saveBusy || !LibraryManager::Get().GetSaveStatusText().empty()) ? 1.0f : 0.0f,
        dt,
        kStatusMotionSpeed);
    m_LoadStatusAlpha = ImGuiExtras::AnimateTowards(
        m_LoadStatusAlpha,
        (loadBusy || !LibraryManager::Get().GetProjectLoadStatusText().empty()) ? 1.0f : 0.0f,
        dt,
        kStatusMotionSpeed);

    ImGui::BeginChild("LibraryHeader", ImVec2(0, 50), true);
    if (ImGui::Button("Options")) {
        ImGui::OpenPopup("LibraryOptionsPopup");
    }

    if (ImGui::BeginPopup("LibraryOptionsPopup")) {
        RenderLibraryMenuOptions(importBusy, exportBusy);
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##search", "Search library...", m_SearchFilter, sizeof(m_SearchFilter));
    ImGui::EndChild();

    auto renderStatusLine = [&](const char* text, float& alphaState) {
        if (text == nullptr || text[0] == '\0' || alphaState <= 0.01f) {
            return;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alphaState);
        ImGui::TextDisabled("%s", text);
        ImGui::PopStyleVar();
    };

    renderStatusLine(LibraryManager::Get().GetImportStatusText().c_str(), m_ImportStatusAlpha);
    renderStatusLine(LibraryManager::Get().GetExportStatusText().c_str(), m_ExportStatusAlpha);
    renderStatusLine(LibraryManager::Get().GetSaveStatusText().c_str(), m_SaveStatusAlpha);
    renderStatusLine(LibraryManager::Get().GetProjectLoadStatusText().c_str(), m_LoadStatusAlpha);

    const float splitterWidth = 6.0f;
    const float bodyHeight = ImGui::GetContentRegionAvail().y;
    m_FilterPanelExpandedWidth = std::clamp(m_FilterPanelExpandedWidth, kSidebarMinWidth, kSidebarMaxWidth);
    const float sidebarTargetWidth = m_FilterPanelCollapsed ? kSidebarRailWidth : m_FilterPanelExpandedWidth;
    m_FilterPanelWidth = ImGuiExtras::AnimateTowards(m_FilterPanelWidth, sidebarTargetWidth, dt, kSidebarMotionSpeed);
    m_FilterPanelWidth = std::clamp(m_FilterPanelWidth, kSidebarRailWidth, kSidebarMaxWidth);

    const float sidebarDenominator = std::max(1.0f, m_FilterPanelExpandedWidth - kSidebarRailWidth);
    const float sidebarProgress = std::clamp((m_FilterPanelWidth - kSidebarRailWidth) / sidebarDenominator, 0.0f, 1.0f);
    const float sidebarContentAlpha = ImGuiExtras::EaseOutCubic(sidebarProgress);
    const bool sidebarExpanded = sidebarProgress > 0.06f;

    ImGui::BeginChild("LibrarySidebar", ImVec2(m_FilterPanelWidth, 0), true);
    if (ImGui::Button(m_FilterPanelCollapsed ? ">" : "<", ImVec2(-1.0f, 24.0f))) {
        m_FilterPanelCollapsed = !m_FilterPanelCollapsed;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(m_FilterPanelCollapsed ? "Expand filters" : "Collapse filters");
    }

    if (sidebarExpanded) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, sidebarContentAlpha);
        ImGui::Text("FILTERS");
        ImGui::Separator();
        if (ImGui::Selectable("All Projects", !m_ShowAssets)) { m_ShowAssets = false; m_SelectedAssets.clear(); }
        if (ImGui::Selectable("Assets", m_ShowAssets)) { m_ShowAssets = true; m_SelectedProjects.clear(); }
        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Tags", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto allTags = TagManager::Get().GetAllKnownTags();

            bool noTagFilter = m_FilterNoTag;
            if (ImGui::Checkbox("Untagged only", &noTagFilter)) {
                m_FilterNoTag = noTagFilter;
                if (m_FilterNoTag) m_ActiveTagFilters.clear();
            }

            if (!m_FilterNoTag) {
                for (const auto& tag : allTags) {
                    bool active = m_ActiveTagFilters.count(tag) > 0;
                    if (ImGui::Checkbox(tag.c_str(), &active)) {
                        if (active) m_ActiveTagFilters.insert(tag);
                        else m_ActiveTagFilters.erase(tag);
                    }
                }
            }

            if (!m_ActiveTagFilters.empty() || m_FilterNoTag) {
                if (ImGui::SmallButton("Clear Filters")) {
                    m_ActiveTagFilters.clear();
                    m_FilterNoTag = false;
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("ADD TAG TO SELECTED");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##addtag", "New tag...", m_AddTagBuffer, sizeof(m_AddTagBuffer));
            if (ImGui::SmallButton("Apply Tag") && m_AddTagBuffer[0] != '\0') {
                auto& sel = m_ShowAssets ? m_SelectedAssets : m_SelectedProjects;
                for (const auto& fn : sel) {
                    TagManager::Get().AddTag(fn, m_AddTagBuffer);
                }
                m_AddTagBuffer[0] = '\0';
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove Tag") && m_AddTagBuffer[0] != '\0') {
                auto& sel = m_ShowAssets ? m_SelectedAssets : m_SelectedProjects;
                for (const auto& fn : sel) {
                    TagManager::Get().RemoveTag(fn, m_AddTagBuffer);
                }
                m_AddTagBuffer[0] = '\0';
            }
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Library refresh is automatic.");
        ImGui::PopStyleVar();
    } else {
        ImGui::Spacing();
        ImGui::TextDisabled("Filters");
        ImGui::TextDisabled("collapsed");
    }
    ImGui::EndChild();

    if (!m_FilterPanelCollapsed) {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::InvisibleButton("LibrarySidebarSplitter", ImVec2(splitterWidth, bodyHeight));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (ImGui::IsItemActive()) {
            m_FilterPanelExpandedWidth = std::clamp(
                m_FilterPanelExpandedWidth + ImGui::GetIO().MouseDelta.x,
                kSidebarMinWidth,
                kSidebarMaxWidth);
            m_FilterPanelWidth = m_FilterPanelExpandedWidth;
        }

        ImDrawList* splitterDrawList = ImGui::GetWindowDrawList();
        splitterDrawList->AddRectFilled(
            ImGui::GetItemRectMin(),
            ImGui::GetItemRectMax(),
            ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_SeparatorActive : ImGuiCol_SeparatorHovered),
            2.0f);

        ImGui::SameLine(0.0f, 0.0f);
    } else {
        ImGui::SameLine(0.0f, 0.0f);
    }

    ImGui::BeginChild("LibraryGrid", ImVec2(0, 0), false);

    int renderedCount = 0;
    if (m_ShowAssets) {
        float windowVisibleX2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        ImGuiStyle& style = ImGui::GetStyle();

        const auto& assets = LibraryManager::Get().GetAssets();
        for (const auto& asset : assets) {
            if (!asset) continue;

            ImGui::PushID(asset->fileName.c_str());
            const bool rendered = RenderAssetCard(*asset, editor);
            if (rendered) {
                ++renderedCount;
                const float lastButtonX2 = ImGui::GetItemRectMax().x;
                const float nextButtonX2 = lastButtonX2 + style.ItemSpacing.x + 220.0f;
                if (nextButtonX2 < windowVisibleX2) {
                    ImGui::SameLine();
                }
            }
            ImGui::PopID();
        }
    } else {
        float windowVisibleX2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        ImGuiStyle& style = ImGui::GetStyle();

        const auto& projects = LibraryManager::Get().GetProjects();
        for (const auto& project : projects) {
            if (!project) continue;

            ImGui::PushID(project->fileName.c_str());
            const bool rendered = RenderProjectCard(*project, editor);
            if (rendered) {
                ++renderedCount;
                const float lastButtonX2 = ImGui::GetItemRectMax().x;
                const float nextButtonX2 = lastButtonX2 + style.ItemSpacing.x + 220.0f;
                if (nextButtonX2 < windowVisibleX2) {
                    ImGui::SameLine();
                }
            }
            ImGui::PopID();
        }
    }

    m_EmptyStateAlpha = ImGuiExtras::AnimateTowards(m_EmptyStateAlpha, renderedCount == 0 ? 1.0f : 0.0f, dt, kStatusMotionSpeed);
    if (m_EmptyStateAlpha > 0.01f) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_EmptyStateAlpha);
        ImGui::TextDisabled(
            m_ShowAssets
                ? (m_SearchFilter[0] ? "No assets match the current search filter." : "No rendered assets have been saved to the library yet.")
                : (m_SearchFilter[0] ? "No projects match the current search filter." : "No projects found in the library."));
        ImGui::PopStyleVar();
    }

    if (!m_BlockLibraryGridContextMenuThisFrame &&
        ImGui::BeginPopupContextWindow("LibraryGridContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        RenderLibraryMenuOptions(importBusy, exportBusy);
        ImGui::EndPopup();
    }

    ImGui::EndChild();

    PruneCardMotionStates(m_ProjectCardMotion, ImGui::GetFrameCount());
    PruneCardMotionStates(m_AssetCardMotion, ImGui::GetFrameCount());

    if (m_PreviewProject || m_ProjectPreviewTransition > 0.0f) {
        RenderPreviewPopup(editor, renderTab, composite, activeTab);
    } else if (m_PreviewAsset || m_AssetPreviewTransition > 0.0f) {
        RenderAssetPreviewPopup(editor, renderTab, composite, activeTab);
    }

    RenderConfirmRenderLoadPopup(renderTab, activeTab);

    if (importBusy) {
        ImGuiExtras::RenderBusyOverlay(LibraryManager::Get().GetImportStatusText().c_str());
    } else if (exportBusy) {
        ImGuiExtras::RenderBusyOverlay(LibraryManager::Get().GetExportStatusText().c_str());
    } else if (saveBusy) {
        ImGuiExtras::RenderBusyOverlay(LibraryManager::Get().GetSaveStatusText().c_str());
    } else if (loadBusy) {
        ImGuiExtras::RenderBusyOverlay(LibraryManager::Get().GetProjectLoadStatusText().c_str());
    }
}

bool LibraryModule::RenderProjectCard(const ProjectEntry& project, EditorModule* editor) {
    (void)editor;

    const bool isSelected = m_SelectedProjects.count(project.fileName) > 0;
    const bool matchesFilter = ProjectMatchesFilter(project, m_SearchFilter, m_ActiveTagFilters, m_FilterNoTag);
    const float dt = ImGui::GetIO().DeltaTime;
    const int frameCount = ImGui::GetFrameCount();

    LibraryCardMotionState& motion = GetCardMotionState(m_ProjectCardMotion, project.fileName);
    motion.lastSeenFrame = frameCount;
    motion.reveal = ImGuiExtras::AnimateTowards(motion.reveal, matchesFilter ? 1.0f : 0.0f, dt, kCardMotionSpeed);
    motion.selected = ImGuiExtras::AnimateTowards(motion.selected, isSelected ? 1.0f : 0.0f, dt, kCardMotionSpeed);

    if (!matchesFilter && motion.reveal <= 0.01f) {
        return false;
    }

    const float cardWidth = 220.0f;
    const float aspect = (project.sourceHeight > 0) ? (static_cast<float>(project.sourceWidth) / static_cast<float>(project.sourceHeight)) : 1.0f;
    ImVec2 thumbSize(cardWidth, cardWidth / std::max(aspect, 0.1f));

    if (thumbSize.y > 300.0f) {
        thumbSize.y = 300.0f;
        thumbSize.x = 300.0f * aspect;
    }

    const float totalHeight = thumbSize.y + 60.0f + ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 screenMin = ImGui::GetCursorScreenPos();
    const ImRect cardRect(screenMin, ImVec2(screenMin.x + cardWidth, screenMin.y + totalHeight));
    const ImVec2 startPos = ImGui::GetCursorPos();

    ImGui::BeginGroup();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float cardAlpha = std::clamp(motion.reveal, 0.0f, 1.0f);
    const ImVec4 baseColor(0.11f, 0.12f, 0.14f, 0.92f * cardAlpha);
    drawList->AddRectFilled(cardRect.Min, cardRect.Max, ImGui::ColorConvertFloat4ToU32(baseColor), 10.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, cardAlpha);

    const float xOffset = (cardWidth - thumbSize.x) * 0.5f;
    if (xOffset > 0.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + xOffset);
    }

    if (project.thumbnailTex) {
        ImGui::Image((ImTextureID)(intptr_t)project.thumbnailTex, thumbSize, ImVec2(0, 1), ImVec2(1, 0));
    } else {
        ImGui::Button("No Preview", thumbSize);
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_FrameBg));
    ImGui::BeginChild("CardInfo", ImVec2(cardWidth, 60), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
    ImGui::Text("%s", project.projectName.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::Text("%dx%d", project.sourceWidth, project.sourceHeight);
    ImGui::Text("%s", project.timestamp.c_str());
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SetCursorPos(startPos);
    ImGui::InvisibleButton("##hitbox", ImVec2(cardWidth, totalHeight));
    const bool isHovered = ImGui::IsItemHovered();
    const bool isLeftClicked = matchesFilter && ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool isRightClicked = matchesFilter && ImGui::IsItemClicked(ImGuiMouseButton_Right);
    const bool isDoubleClicked = matchesFilter && isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    motion.hover = ImGuiExtras::AnimateTowards(motion.hover, (matchesFilter && isHovered) ? 1.0f : 0.0f, dt, 24.0f);

    const ImRect motionRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    DrawCardMotionFrame(drawList, motionRect, motion.hover, motion.selected, isHovered);

    if (isLeftClicked) {
        if (ImGui::GetIO().KeyCtrl) {
            if (isSelected) m_SelectedProjects.erase(project.fileName);
            else m_SelectedProjects.insert(project.fileName);
        } else {
            m_SelectedProjects.clear();
            m_SelectedProjects.insert(project.fileName);
        }
        m_LastClickedProject = project.fileName;
    }

    if (isDoubleClicked) {
        const auto& projects = LibraryManager::Get().GetProjects();
        for (const auto& candidate : projects) {
            if (candidate && candidate->fileName == project.fileName) {
                m_PreviewProject = candidate;
                m_PreviewAsset = nullptr;
                m_ProjectPreviewTransition = 0.0f;
                m_ProjectPreviewClosing = false;
                m_AssetPreviewTransition = 0.0f;
                m_AssetPreviewClosing = false;
                m_CompareSplit = 0.5f;
                LibraryManager::Get().CancelAssetPreviewRequests();
                LibraryManager::Get().CancelProjectPreviewRequests();
                LibraryManager::Get().RequestProjectPreview(m_PreviewProject);
                SyncRenameBuffer();
                break;
            }
        }
    }

    if (isRightClicked) {
        if (!isSelected) {
            m_SelectedProjects.clear();
            m_SelectedProjects.insert(project.fileName);
        }
        m_BlockLibraryGridContextMenuThisFrame = true;
        const std::string popupId = std::string("ProjectCardContextMenu##") + project.fileName;
        ImGui::OpenPopup(popupId.c_str());
    }

    const std::string popupId = std::string("ProjectCardContextMenu##") + project.fileName;
    if (ImGui::BeginPopup(popupId.c_str())) {
        const bool multiple = m_SelectedProjects.size() > 1;
        if (!multiple) {
            if (project.projectKind == "editor" || project.projectKind.empty()) {
                if (ImGui::MenuItem("Open in Editor")) {
                    LibraryManager::Get().RequestLoadProject(project.fileName, m_CachedEditor);
                    if (m_CachedActiveTab) *m_CachedActiveTab = 0;
                }
            }
            if (project.projectKind == "composite") {
                if (ImGui::MenuItem("Open in Composite")) {
                    LibraryManager::Get().RequestLoadCompositeProject(project.fileName, m_CachedComposite);
                    if (m_CachedActiveTab) *m_CachedActiveTab = 2;
                }
            }
        }

        if (ImGui::MenuItem("Load Selected into Composite", nullptr, false, !m_SelectedProjects.empty())) {
            for (const auto& fn : m_SelectedProjects) {
                auto fullPath = LibraryManager::Get().GetLibraryPath() / fn;
                m_CachedComposite->AddProjectLayerFromFile(fullPath.string());
            }
            if (m_CachedActiveTab) *m_CachedActiveTab = 2;
        }

        ImGui::Separator();

        char deleteLabel[64];
        snprintf(deleteLabel, sizeof(deleteLabel), "Delete Selected (%d)", (int)m_SelectedProjects.size());
        if (ImGui::MenuItem(deleteLabel)) {
            m_PendingDeleteFileNames.assign(m_SelectedProjects.begin(), m_SelectedProjects.end());
            m_DeletingAssets = false;
            m_DeleteConfirmOpen = true;
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar();
    ImGui::EndGroup();
    return true;
}

bool LibraryModule::RenderAssetCard(const AssetEntry& asset, EditorModule* editor) {
    (void)editor;

    const bool isSelected = m_SelectedAssets.count(asset.fileName) > 0;
    const bool matchesFilter = AssetMatchesFilter(asset, m_SearchFilter, m_ActiveTagFilters, m_FilterNoTag);
    const float dt = ImGui::GetIO().DeltaTime;
    const int frameCount = ImGui::GetFrameCount();

    LibraryCardMotionState& motion = GetCardMotionState(m_AssetCardMotion, asset.fileName);
    motion.lastSeenFrame = frameCount;
    motion.reveal = ImGuiExtras::AnimateTowards(motion.reveal, matchesFilter ? 1.0f : 0.0f, dt, kCardMotionSpeed);
    motion.selected = ImGuiExtras::AnimateTowards(motion.selected, isSelected ? 1.0f : 0.0f, dt, kCardMotionSpeed);

    if (!matchesFilter && motion.reveal <= 0.01f) {
        return false;
    }

    const float cardWidth = 220.0f;
    const float aspect = (asset.height > 0) ? (static_cast<float>(asset.width) / static_cast<float>(asset.height)) : 1.0f;
    ImVec2 thumbSize(cardWidth, cardWidth / std::max(aspect, 0.1f));

    if (thumbSize.y > 300.0f) {
        thumbSize.y = 300.0f;
        thumbSize.x = 300.0f * aspect;
    }

    const float totalHeight = thumbSize.y + 72.0f + ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 screenMin = ImGui::GetCursorScreenPos();
    const ImRect cardRect(screenMin, ImVec2(screenMin.x + cardWidth, screenMin.y + totalHeight));
    const ImVec2 startPos = ImGui::GetCursorPos();

    ImGui::BeginGroup();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float cardAlpha = std::clamp(motion.reveal, 0.0f, 1.0f);
    const ImVec4 baseColor(0.11f, 0.12f, 0.14f, 0.92f * cardAlpha);
    drawList->AddRectFilled(cardRect.Min, cardRect.Max, ImGui::ColorConvertFloat4ToU32(baseColor), 10.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, cardAlpha);

    const float xOffset = (cardWidth - thumbSize.x) * 0.5f;
    if (xOffset > 0.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + xOffset);
    }

    if (asset.thumbnailTex) {
        ImGui::Image((ImTextureID)(intptr_t)asset.thumbnailTex, thumbSize, ImVec2(0, 1), ImVec2(1, 0));
    } else {
        ImGui::Button("No Asset Preview", thumbSize);
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(ImGuiCol_FrameBg));
    ImGui::BeginChild("AssetCardInfo", ImVec2(cardWidth, 72), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
    ImGui::Text("%s", asset.displayName.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::Text("%dx%d", asset.width, asset.height);
    if (!asset.projectName.empty()) {
        ImGui::Text("From: %s", asset.projectName.c_str());
    } else {
        ImGui::Text("%s", asset.fileName.c_str());
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SetCursorPos(startPos);
    ImGui::InvisibleButton("##hitbox", ImVec2(cardWidth, totalHeight));
    const bool isHovered = ImGui::IsItemHovered();
    const bool isLeftClicked = matchesFilter && ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool isRightClicked = matchesFilter && ImGui::IsItemClicked(ImGuiMouseButton_Right);
    const bool isDoubleClicked = matchesFilter && isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    motion.hover = ImGuiExtras::AnimateTowards(motion.hover, (matchesFilter && isHovered) ? 1.0f : 0.0f, dt, 24.0f);
    const ImRect motionRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    DrawCardMotionFrame(drawList, motionRect, motion.hover, motion.selected, isHovered);

    if (isLeftClicked) {
        if (ImGui::GetIO().KeyCtrl) {
            if (isSelected) m_SelectedAssets.erase(asset.fileName);
            else m_SelectedAssets.insert(asset.fileName);
        } else {
            m_SelectedAssets.clear();
            m_SelectedAssets.insert(asset.fileName);
        }
        m_LastClickedAsset = asset.fileName;
    }

    if (isDoubleClicked) {
        OpenAssetPreviewByFileName(asset.fileName);
    }

    if (isRightClicked) {
        if (!isSelected) {
            m_SelectedAssets.clear();
            m_SelectedAssets.insert(asset.fileName);
        }
        m_BlockLibraryGridContextMenuThisFrame = true;
        const std::string popupId = std::string("AssetCardContextMenu##") + asset.fileName;
        ImGui::OpenPopup(popupId.c_str());
    }

    const std::string popupId = std::string("AssetCardContextMenu##") + asset.fileName;
    if (ImGui::BeginPopup(popupId.c_str())) {
        if (ImGui::MenuItem("Load Selected into Composite", nullptr, false, !m_SelectedAssets.empty())) {
            for (const auto& fn : m_SelectedAssets) {
                auto fullPath = LibraryManager::Get().GetAssetsPath() / fn;
                m_CachedComposite->AddImageLayerFromFile(fullPath.string());
            }
            if (m_CachedActiveTab) *m_CachedActiveTab = 2;
        }

        ImGui::Separator();

        char deleteLabel[64];
        snprintf(deleteLabel, sizeof(deleteLabel), "Delete Selected (%d)", (int)m_SelectedAssets.size());
        if (ImGui::MenuItem(deleteLabel)) {
            m_PendingDeleteFileNames.assign(m_SelectedAssets.begin(), m_SelectedAssets.end());
            m_DeletingAssets = true;
            m_DeleteConfirmOpen = true;
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar();
    ImGui::EndGroup();
    return true;
}

void LibraryModule::RenderPreviewPopup(EditorModule* editor, RenderTab* renderTab, CompositeModule* composite, int* activeTab) {
    if (!m_PreviewProject && m_ProjectPreviewTransition <= 0.0f) {
        return;
    }

    const float previewProgressTarget = (m_PreviewProject && !m_ProjectPreviewClosing) ? 1.0f : 0.0f;
    m_ProjectPreviewTransition = ImGuiExtras::AnimateTowards(m_ProjectPreviewTransition, previewProgressTarget, ImGui::GetIO().DeltaTime, kPreviewMotionSpeed);
    const float previewAlpha = ImGuiExtras::EaseOutCubic(m_ProjectPreviewTransition);

    if ((m_ProjectPreviewClosing || !m_PreviewProject) && m_ProjectPreviewTransition <= 0.01f) {
        LibraryManager::Get().CancelProjectPreviewRequests();
        m_PreviewProject = nullptr;
        m_PreviewAsset = nullptr;
        m_RenameTargetFileName.clear();
        m_ProjectPreviewClosing = false;
        if (m_ProjectPreviewRefreshAfterClose) {
            LibraryManager::Get().RefreshLibrary();
            m_ProjectPreviewRefreshAfterClose = false;
        }
        return;
    }

    if (!m_PreviewProject) {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::SetNextWindowBgAlpha(0.04f + (0.93f * previewAlpha));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, previewAlpha);
    ImGui::Begin("Library Project Preview", nullptr, flags);

    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", m_PreviewProject->projectName.c_str());
    ImGui::TextColored(
        ImVec4(0.65f, 0.67f, 0.72f, 1.0f),
        "Last Modified: %s | Dimensions: %dx%d | File: %s",
        m_PreviewProject->timestamp.c_str(),
        m_PreviewProject->sourceWidth,
        m_PreviewProject->sourceHeight,
        m_PreviewProject->fileName.c_str());
    ImGui::Separator();

    const float contentProgress = ImGuiExtras::EaseOutCubic(previewAlpha);
    const float contentScale = 0.985f + (0.015f * contentProgress);
    const float sidePanelWidth = 370.0f * contentScale;
    const float footerHeight = 58.0f;
    const float gap = 16.0f;
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 scaledAvailable(available.x * contentScale, available.y * contentScale);
    const ImVec2 contentOffset(
        (available.x - scaledAvailable.x) * 0.5f,
        (available.y - scaledAvailable.y) * 0.5f + ((1.0f - contentProgress) * 10.0f));
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + contentOffset.x, ImGui::GetCursorPosY() + contentOffset.y));

    const float previewWidth = std::max(320.0f, scaledAvailable.x - sidePanelWidth - gap);
    const ImVec2 previewArea(previewWidth, std::max(260.0f, scaledAvailable.y - footerHeight));
    const ImVec2 sideArea(sidePanelWidth, previewArea.y);

    ImGui::BeginChild("LibraryPreviewViewport", previewArea, true);
    const bool previewReady = (m_PreviewProject->sourcePreviewTex != 0 && m_PreviewProject->fullPreviewTex != 0);
    const bool previewBusy = Async::IsBusy(m_PreviewProject->previewTaskState);

    const bool renderProject = IsRenderProject(*m_PreviewProject);
    const bool compositeProject = IsCompositeProject(*m_PreviewProject);
    if (previewReady) {
        ImVec2 imageSize = FitImageToBounds(
            static_cast<float>(m_PreviewProject->sourceWidth),
            static_cast<float>(m_PreviewProject->sourceHeight),
            previewArea);

        ImGui::SetCursorPosX(std::max(0.0f, (previewArea.x - imageSize.x) * 0.5f));
        ImGui::SetCursorPosY(std::max(0.0f, (previewArea.y - imageSize.y) * 0.5f));
        if (renderProject || compositeProject) {
            ImGui::Image((ImTextureID)(intptr_t)m_PreviewProject->fullPreviewTex, imageSize, ImVec2(0, 1), ImVec2(1, 0));
        } else {
            DrawComparePreview(*m_PreviewProject, imageSize, m_CompareSplit);
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
        ImGui::TextDisabled(renderProject
            ? "Saved beauty preview for the render project."
            : (compositeProject
                    ? "Saved flattened export preview for the composite project."
                    : "Hover across the image to compare the current rendered edit against the original source."));
    } else {
        ImGui::Dummy(ImVec2(0.0f, std::max(0.0f, (previewArea.y - 70.0f) * 0.4f)));
        ImGui::SetCursorPosX(std::max(0.0f, (previewArea.x - 120.0f) * 0.5f));
        const char* spinnerLabel = m_PreviewProject->previewStatusText.empty()
            ? "Rendering full-quality preview..."
            : m_PreviewProject->previewStatusText.c_str();
        ImGuiExtras::DrawSpinner(spinnerLabel, 18.0f, 4, IM_COL32(220, 220, 220, 240));
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("LibraryPreviewDetails", sideArea, true);

    ImGui::Text("Project Actions");
    ImGui::Separator();

    ImGui::InputText("Project Name", m_RenameBuffer, sizeof(m_RenameBuffer));
    if (ImGui::Button("Save Name", ImVec2(-1, 0))) {
        if (LibraryManager::Get().RenameProject(m_PreviewProject->fileName, m_RenameBuffer)) {
            m_RenameTargetFileName.clear();
            SyncRenameBuffer();
        }
    }

    const bool projectLoadBusy = Async::IsBusy(LibraryManager::Get().GetProjectLoadTaskState());
    ImGui::BeginDisabled(
        m_ProjectPreviewClosing || projectLoadBusy || previewBusy
        || (renderProject && renderTab == nullptr)
        || (compositeProject && composite == nullptr));
    if (ImGui::Button(
            projectLoadBusy
                ? "Loading Project..."
                : (renderProject ? "Load Project Into Render"
                                 : (compositeProject ? "Load Project Into Composite" : "Load Project Into Editor")),
            ImVec2(-1, 0))) {
        const std::string projectFileName = m_PreviewProject->fileName;
        if (renderProject) {
            if (renderTab != nullptr && renderTab->HasUnsavedChanges()) {
                m_PendingRenderProjectFileName = projectFileName;
                m_RenderLoadConfirmOpen = true;
                ImGui::OpenPopup("Discard Unsaved Render Project Changes");
            } else {
                m_ProjectPreviewClosing = true;
                m_ProjectPreviewRefreshAfterClose = false;
                LibraryManager::Get().RequestLoadRenderProject(projectFileName, renderTab, [this, activeTab](bool success) {
                    if (!success) {
                        return;
                    }

                    if (activeTab) {
                        *activeTab = 3;
                    }
                });
            }
        } else if (compositeProject && composite) {
            if (composite->HasLayers()) {
                m_PendingLoadProjectFileName = projectFileName;
                m_PendingLoadTarget = PendingLoadTarget::Composite;
                m_ConfirmLoadOpen = true;
            } else {
                m_ProjectPreviewClosing = true;
                m_ProjectPreviewRefreshAfterClose = false;
                LibraryManager::Get().RequestLoadCompositeProject(projectFileName, composite, [this, activeTab](bool success) {
                    if (!success) {
                        return;
                    }

                    if (activeTab) {
                        *activeTab = 2;
                    }
                });
            }
        } else {
            if (editor != nullptr && editor->GetPipeline().HasSourceImage()) {
                m_PendingLoadProjectFileName = projectFileName;
                m_PendingLoadTarget = PendingLoadTarget::Editor;
                m_ConfirmLoadOpen = true;
            } else {
                m_ProjectPreviewClosing = true;
                m_ProjectPreviewRefreshAfterClose = false;
                LibraryManager::Get().RequestLoadProject(projectFileName, editor, [this, activeTab](bool success) {
                    if (!success) {
                        return;
                    }

                    if (activeTab) {
                        *activeTab = 1;
                    }
                });
            }
        }
    }
    ImGui::EndDisabled();

    if (!LibraryManager::Get().GetProjectLoadStatusText().empty()) {
        ImGui::TextDisabled("%s", LibraryManager::Get().GetProjectLoadStatusText().c_str());
    }

    if (ImGui::Button("Delete Project...", ImVec2(-1, 0))) {
        ImGui::OpenPopup("Confirm Delete Project");
    }

    if (ImGui::BeginPopupModal("Confirm Delete Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static double s_deletePopupOpenedAt = 0.0;
        if (ImGui::IsWindowAppearing()) {
            s_deletePopupOpenedAt = ImGui::GetTime();
        }
        const float dialogProgress = ImGuiExtras::EaseOutCubic(std::clamp(
            static_cast<float>((ImGui::GetTime() - s_deletePopupOpenedAt) / kDialogAppearDuration),
            0.0f,
            1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, dialogProgress);
        ImGui::TextWrapped("Delete \"%s\" from the library?", m_PreviewProject ? m_PreviewProject->projectName.c_str() : "this project");
        ImGui::TextDisabled("This removes the saved project file and any linked rendered preview asset from the Library.");
        ImGui::Spacing();

        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            const std::string fileName = m_PreviewProject ? m_PreviewProject->fileName : "";
            if (!fileName.empty()) {
                LibraryManager::Get().DeleteProject(fileName);
                m_ProjectPreviewRefreshAfterClose = true;
                m_ProjectPreviewClosing = true;
            }
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }

    ImGui::Spacing();
    ImGui::Text(renderProject ? "Scene Summary" : (compositeProject ? "Composite Summary" : "Pipeline Summary"));
    ImGui::Separator();

    if (m_PreviewProject->pipelineData.is_null()) {
        const char* loadingText =
            m_PreviewProject->previewTaskState == Async::TaskState::Failed
                ? (renderProject
                        ? "Saved render scene data could not be loaded."
                        : (compositeProject ? "Saved composite data could not be loaded." : "Saved layer settings could not be loaded."))
                : (renderProject
                        ? "Loading saved render scene data..."
                        : (compositeProject ? "Loading saved composite data..." : "Loading saved layer settings..."));
        ImGui::TextDisabled("%s", loadingText);
    } else if (compositeProject) {
        if (m_PreviewProject->pipelineData.is_object()) {
            const json& layers = m_PreviewProject->pipelineData.value("layers", json::array());
            ImGui::TextWrapped("Project Kind: Composite (portable binary)");
            ImGui::TextWrapped("Layer count: %zu", layers.size());
            ImGui::TextWrapped("Format version: %d", m_PreviewProject->pipelineData.value("compositeVersion", 0));
        } else {
            ImGui::TextDisabled("Composite project data is not in the expected object format.");
        }
    } else if (renderProject) {
        const json snapshot =
            m_PreviewProject->pipelineData.is_object()
                ? m_PreviewProject->pipelineData.value("snapshot", json::object())
                : json::object();
        const json settings =
            m_PreviewProject->pipelineData.is_object()
                ? m_PreviewProject->pipelineData.value("settings", json::object())
                : json::object();
        const json finalRender = settings.value("finalRender", json::object());
        ImGui::TextWrapped("Project Kind: Render");
        ImGui::TextWrapped("Label: %s", snapshot.value("label", m_PreviewProject->projectName).c_str());
        ImGui::TextWrapped("Background: %d", snapshot.value("backgroundMode", 0));
        ImGui::TextWrapped("Environment Enabled: %s", snapshot.value("environmentEnabled", true) ? "Yes" : "No");
        ImGui::TextWrapped("Environment Intensity: %.2f", snapshot.value("environmentIntensity", 1.0f));
        ImGui::TextWrapped("Fog Enabled: %s", snapshot.value("fogEnabled", false) ? "Yes" : "No");
        ImGui::TextWrapped("Fog Density: %.3f", snapshot.value("fogDensity", 0.0f));
        ImGui::TextWrapped("Imported Assets: %zu", snapshot.value("importedAssets", json::array()).size());
        ImGui::TextWrapped("Materials: %zu", snapshot.value("materials", json::array()).size());
        ImGui::TextWrapped("Mesh Instances: %zu", snapshot.value("meshInstances", json::array()).size());
        ImGui::TextWrapped("Spheres: %zu", snapshot.value("spheres", json::array()).size());
        ImGui::TextWrapped("Triangles: %zu", snapshot.value("triangles", json::array()).size());
        ImGui::TextWrapped("Lights: %zu", snapshot.value("lights", json::array()).size());
        ImGui::TextWrapped("Final Output: %s", finalRender.value("outputName", std::string("Final Render")).c_str());
        ImGui::TextWrapped(
            "Final Preset: %dx%d | %d samples | %d bounces",
            finalRender.value("resolutionX", 1920),
            finalRender.value("resolutionY", 1080),
            finalRender.value("sampleTarget", 256),
            finalRender.value("maxBounceCount", 8));
        const std::string latestAsset = m_PreviewProject->pipelineData.is_object()
            ? m_PreviewProject->pipelineData.value("latestFinalAssetFileName", std::string())
            : std::string();
        if (!latestAsset.empty()) {
            ImGui::TextWrapped("Latest Saved Asset: %s", latestAsset.c_str());
        }
    } else {
        const json layerArray = EditorNodeGraph::ExtractLayerArray(m_PreviewProject->pipelineData);
        if (!layerArray.is_array() || layerArray.empty()) {
            ImGui::TextDisabled("This project currently has no saved editor layers.");
        } else {
            for (std::size_t i = 0; i < layerArray.size(); ++i) {
                const json& layer = layerArray[i];
                const std::string type = layer.value("type", "Layer");
                const std::string header = std::to_string(i + 1) + ". " + LayerDisplayName(type);

                ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_DefaultOpen;
                if (ImGui::CollapsingHeader(header.c_str(), nodeFlags)) {
                    for (auto it = layer.begin(); it != layer.end(); ++it) {
                        if (it.key() == "type") continue;
                        ImGui::TextWrapped("%s: %s", FormatKeyLabel(it.key()).c_str(), FormatJsonValue(it.value()).c_str());
                    }
                }
            }
        }
    }

    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (ImGui::Button("Back To Library", ImVec2(200, 40))) {
        m_ProjectPreviewClosing = true;
    }

    ImGui::End();
    ImGui::PopStyleVar();

    if (m_ProjectPreviewClosing && m_ProjectPreviewTransition <= 0.01f) {
        LibraryManager::Get().CancelProjectPreviewRequests();
        m_PreviewProject = nullptr;
        m_PreviewAsset = nullptr;
        m_RenameTargetFileName.clear();
        m_ProjectPreviewClosing = false;
        if (m_ProjectPreviewRefreshAfterClose) {
            LibraryManager::Get().RefreshLibrary();
            m_ProjectPreviewRefreshAfterClose = false;
        }
    }
}

void LibraryModule::RenderAssetPreviewPopup(EditorModule* editor, RenderTab* renderTab, CompositeModule* composite, int* activeTab) {
    if (!m_PreviewAsset && m_AssetPreviewTransition <= 0.0f) {
        return;
    }

    const float previewProgressTarget = (m_PreviewAsset && !m_AssetPreviewClosing) ? 1.0f : 0.0f;
    m_AssetPreviewTransition = ImGuiExtras::AnimateTowards(m_AssetPreviewTransition, previewProgressTarget, ImGui::GetIO().DeltaTime, kPreviewMotionSpeed);
    const float previewAlpha = ImGuiExtras::EaseOutCubic(m_AssetPreviewTransition);

    if ((m_AssetPreviewClosing || !m_PreviewAsset) && m_AssetPreviewTransition <= 0.01f) {
        LibraryManager::Get().CancelAssetPreviewRequests();
        m_PreviewAsset = nullptr;
        m_PreviewProject = nullptr;
        m_AssetPreviewClosing = false;
        return;
    }

    if (!m_PreviewAsset) {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::SetNextWindowBgAlpha(0.04f + (0.93f * previewAlpha));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, previewAlpha);
    ImGui::Begin("Library Asset Preview", nullptr, flags);

    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", m_PreviewAsset->displayName.c_str());
    ImGui::TextColored(
        ImVec4(0.65f, 0.67f, 0.72f, 1.0f),
        "Saved: %s | Dimensions: %dx%d | File: %s",
        m_PreviewAsset->timestamp.c_str(),
        m_PreviewAsset->width,
        m_PreviewAsset->height,
        m_PreviewAsset->fileName.c_str());
    ImGui::Separator();

    const float contentProgress = ImGuiExtras::EaseOutCubic(previewAlpha);
    const float contentScale = 0.985f + (0.015f * contentProgress);
    const float sidePanelWidth = 340.0f * contentScale;
    const float footerHeight = 58.0f;
    const float gap = 16.0f;
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 scaledAvailable(available.x * contentScale, available.y * contentScale);
    const ImVec2 contentOffset(
        (available.x - scaledAvailable.x) * 0.5f,
        (available.y - scaledAvailable.y) * 0.5f + ((1.0f - contentProgress) * 10.0f));
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + contentOffset.x, ImGui::GetCursorPosY() + contentOffset.y));

    const float previewWidth = std::max(320.0f, scaledAvailable.x - sidePanelWidth - gap);
    const ImVec2 previewArea(previewWidth, std::max(260.0f, scaledAvailable.y - footerHeight));
    const ImVec2 sideArea(sidePanelWidth, previewArea.y);

    ImGui::BeginChild("LibraryAssetViewport", previewArea, true);
    const bool previewReady = (m_PreviewAsset->fullPreviewTex != 0);
    const bool previewBusy = Async::IsBusy(m_PreviewAsset->previewTaskState);

    if (previewReady) {
        ImVec2 imageSize = FitImageToBounds(
            static_cast<float>(m_PreviewAsset->width),
            static_cast<float>(m_PreviewAsset->height),
            previewArea);

        ImGui::SetCursorPosX(std::max(0.0f, (previewArea.x - imageSize.x) * 0.5f));
        ImGui::SetCursorPosY(std::max(0.0f, (previewArea.y - imageSize.y) * 0.5f));
        ImGui::Image((ImTextureID)(intptr_t)m_PreviewAsset->fullPreviewTex, imageSize, ImVec2(0, 1), ImVec2(1, 0));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
        ImGui::TextDisabled(
            m_PreviewAsset->projectKind == StackBinaryFormat::kRenderProjectKind
                ? "This is the latest rendered output currently saved for the linked render project."
                : "This is the latest rendered output currently saved for the linked editor project.");
    } else {
        ImGui::Dummy(ImVec2(0.0f, std::max(0.0f, (previewArea.y - 70.0f) * 0.4f)));
        ImGui::SetCursorPosX(std::max(0.0f, (previewArea.x - 120.0f) * 0.5f));
        const char* spinnerLabel = m_PreviewAsset->previewStatusText.empty()
            ? "Loading full-quality asset..."
            : m_PreviewAsset->previewStatusText.c_str();
        ImGuiExtras::DrawSpinner(spinnerLabel, 18.0f, 4, IM_COL32(220, 220, 220, 240));
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("LibraryAssetDetails", sideArea, true);
    ImGui::Text("Asset Actions");
    ImGui::Separator();

    if (ImGui::Button("Download Asset...", ImVec2(-1, 0))) {
        std::string exportPath = FileDialogs::SavePngFileDialog("Download Library Asset", m_PreviewAsset->fileName.c_str());
        if (!exportPath.empty()) {
            LibraryManager::Get().ExportAsset(m_PreviewAsset->fileName, exportPath);
        }
    }

    const bool projectLoadBusy = Async::IsBusy(LibraryManager::Get().GetProjectLoadTaskState());
    const bool renderLinkedProject = m_PreviewAsset->projectKind == StackBinaryFormat::kRenderProjectKind;
    const bool compositeLinkedProject = m_PreviewAsset->projectKind == StackBinaryFormat::kCompositeProjectKind;
    ImGui::BeginDisabled(
        m_AssetPreviewClosing || projectLoadBusy || previewBusy || m_PreviewAsset->projectFileName.empty()
        || (renderLinkedProject && renderTab == nullptr)
        || (compositeLinkedProject && composite == nullptr));
    if (ImGui::Button(
            projectLoadBusy
                ? "Loading Project..."
                : (renderLinkedProject
                        ? "Load Linked Project Into Render"
                        : (compositeLinkedProject ? "Load Linked Project Into Composite" : "Load Linked Project Into Editor")),
            ImVec2(-1, 0))) {
        const std::string projectFileName = m_PreviewAsset->projectFileName;
        if (renderLinkedProject) {
            if (renderTab != nullptr && renderTab->HasUnsavedChanges()) {
                m_PendingRenderProjectFileName = projectFileName;
                m_RenderLoadConfirmOpen = true;
                ImGui::OpenPopup("Discard Unsaved Render Project Changes");
            } else {
                m_AssetPreviewClosing = true;
                LibraryManager::Get().RequestLoadRenderProject(projectFileName, renderTab, [this, activeTab](bool success) {
                    if (!success) {
                        return;
                    }

                    if (activeTab) {
                        *activeTab = 3;
                    }
                });
            }
        } else if (compositeLinkedProject && composite) {
            if (composite->HasLayers()) {
                m_PendingLoadProjectFileName = projectFileName;
                m_PendingLoadTarget = PendingLoadTarget::Composite;
                m_ConfirmLoadOpen = true;
            } else {
                m_AssetPreviewClosing = true;
                LibraryManager::Get().RequestLoadCompositeProject(projectFileName, composite, [this, activeTab](bool success) {
                    if (!success) {
                        return;
                    }

                    if (activeTab) {
                        *activeTab = 2;
                    }
                });
            }
        } else {
            if (editor != nullptr && editor->GetPipeline().HasSourceImage()) {
                m_PendingLoadProjectFileName = projectFileName;
                m_PendingLoadTarget = PendingLoadTarget::Editor;
                m_ConfirmLoadOpen = true;
            } else {
                m_AssetPreviewClosing = true;
                LibraryManager::Get().RequestLoadProject(projectFileName, editor, [this, activeTab](bool success) {
                    if (!success) {
                        return;
                    }

                    if (activeTab) {
                        *activeTab = 1;
                    }
                });
            }
        }
    }
    ImGui::EndDisabled();

    if (!LibraryManager::Get().GetProjectLoadStatusText().empty()) {
        ImGui::TextDisabled("%s", LibraryManager::Get().GetProjectLoadStatusText().c_str());
    }

    ImGui::Spacing();
    ImGui::Text("Asset Details");
    ImGui::Separator();
    ImGui::TextWrapped("Display Name: %s", m_PreviewAsset->displayName.c_str());
    ImGui::TextWrapped("Resolution: %d x %d", m_PreviewAsset->width, m_PreviewAsset->height);
    ImGui::TextWrapped("Saved At: %s", m_PreviewAsset->timestamp.c_str());
    ImGui::TextWrapped("Asset File: %s", m_PreviewAsset->fileName.c_str());
    if (!m_PreviewAsset->projectName.empty()) {
        ImGui::TextWrapped("Linked Project: %s", m_PreviewAsset->projectName.c_str());
    } else if (!m_PreviewAsset->projectFileName.empty()) {
        ImGui::TextWrapped("Linked Project File: %s", m_PreviewAsset->projectFileName.c_str());
    } else {
        ImGui::TextDisabled("No linked project metadata was found for this asset.");
    }

    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (ImGui::Button("Back To Library", ImVec2(200, 40))) {
        m_AssetPreviewClosing = true;
    }

    ImGui::End();
    ImGui::PopStyleVar();

    if (m_AssetPreviewClosing && m_AssetPreviewTransition <= 0.01f) {
        LibraryManager::Get().CancelAssetPreviewRequests();
        m_PreviewAsset = nullptr;
        m_PreviewProject = nullptr;
        m_AssetPreviewClosing = false;
    }
}

void LibraryModule::RenderConfirmRenderLoadPopup(RenderTab* renderTab, int* activeTab) {
    if (!m_RenderLoadConfirmOpen) {
        return;
    }

    if (!ImGui::BeginPopupModal("Discard Unsaved Render Project Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    static double s_renderConfirmOpenedAt = 0.0;
    if (ImGui::IsWindowAppearing()) {
        s_renderConfirmOpenedAt = ImGui::GetTime();
    }
    const float popupAlpha = ImGuiExtras::EaseOutCubic(std::clamp(
        static_cast<float>((ImGui::GetTime() - s_renderConfirmOpenedAt) / kDialogAppearDuration),
        0.0f,
        1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, popupAlpha);

    ImGui::TextWrapped("Load another render project and discard the current unsaved render-scene changes?");
    ImGui::TextDisabled("This closes the current in-memory render scene state.");
    ImGui::Spacing();

    if (ImGui::Button("Discard And Load", ImVec2(150.0f, 0.0f))) {
        const std::string pendingFileName = m_PendingRenderProjectFileName;
        m_RenderLoadConfirmOpen = false;
        m_PendingRenderProjectFileName.clear();
        ImGui::CloseCurrentPopup();

        if (renderTab != nullptr && !pendingFileName.empty()) {
            if (m_PreviewProject) {
                m_ProjectPreviewClosing = true;
                m_ProjectPreviewRefreshAfterClose = false;
            }
            if (m_PreviewAsset) {
                m_AssetPreviewClosing = true;
            }
            LibraryManager::Get().RequestLoadRenderProject(pendingFileName, renderTab, [this, activeTab](bool success) {
                if (!success) {
                    return;
                }

                if (activeTab) {
                    *activeTab = 3;
                }
            });
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Keep Editing", ImVec2(150.0f, 0.0f))) {
        m_RenderLoadConfirmOpen = false;
        m_PendingRenderProjectFileName.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::PopStyleVar();
    ImGui::EndPopup();
}

void LibraryModule::RenderGlobalPopups() {
    RenderConfirmLoadPopup();
    RenderFolderImportPopup();
    RenderImportConflictPopup();
    RenderAssetConflictPopup();
    RenderDeleteConfirmPopup();
}

void LibraryModule::RenderFolderImportPopup() {
    if (m_FolderImportPopupOpen) {
        ImGui::OpenPopup("Import Folder Assets");
        m_FolderImportPopupOpen = false;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Import Folder Assets", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Selected Folder:");
        ImGui::TextDisabled("%s", m_PendingFolderImportPath.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Select image formats to import:");
        ImGui::Checkbox(".png", &m_ImportExtPng); ImGui::SameLine();
        ImGui::Checkbox(".jpg / .jpeg", &m_ImportExtJpg); ImGui::SameLine();
        ImGui::Checkbox(".bmp", &m_ImportExtBmp); ImGui::SameLine();
        ImGui::Checkbox(".tga", &m_ImportExtTga);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Import", ImVec2(100.0f, 0.0f))) {
            bool importPng = m_ImportExtPng;
            bool importJpg = m_ImportExtJpg;
            bool importBmp = m_ImportExtBmp;
            bool importTga = m_ImportExtTga;
            std::string path = m_PendingFolderImportPath;

            Async::TaskSystem::Get().Submit([path, importPng, importJpg, importBmp, importTga]() {
                for (const auto& entry : std::filesystem::directory_iterator(path)) {
                    if (!entry.is_regular_file()) continue;

                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                    bool shouldImport = false;
                    if (ext == ".png" && importPng) shouldImport = true;
                    if ((ext == ".jpg" || ext == ".jpeg") && importJpg) shouldImport = true;
                    if (ext == ".bmp" && importBmp) shouldImport = true;
                    if (ext == ".tga" && importTga) shouldImport = true;

                    if (shouldImport) {
                        FILE* f = nullptr;
#ifdef _WIN32
                        _wfopen_s(&f, entry.path().wstring().c_str(), L"rb");
#else
                        f = fopen(entry.path().string().c_str(), "rb");
#endif
                        if (f) {
                            fseek(f, 0, SEEK_END);
                            long size = ftell(f);
                            fseek(f, 0, SEEK_SET);

                            if (size > 0) {
                                std::vector<unsigned char> fileBytes(size);
                                fread(fileBytes.data(), 1, size, f);
                                fclose(f);

                                LibraryManager::Get().QueueLooseAssetSave(
                                    entry.path().stem().string(),
                                    fileBytes,
                                    entry.path().filename().string());
                            } else {
                                fclose(f);
                            }
                        }
                    }
                }
            });

            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void LibraryModule::RenderConfirmLoadPopup() {
    if (m_ConfirmLoadOpen) {
        ImGui::OpenPopup("Project Already Open##Library");
        m_ConfirmLoadOpen = false;
    }

    if (m_SaveNamePromptOpen) {
        ImGui::OpenPopup("Save New Project##Library");
        m_SaveNamePromptOpen = false;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    static double s_projectAlreadyOpenOpenedAt = 0.0;
    static double s_saveNameOpenedAt = 0.0;

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Project Already Open##Library", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            s_projectAlreadyOpenOpenedAt = ImGui::GetTime();
        }
        const float popupAlpha = ImGuiExtras::EaseOutCubic(std::clamp(
            static_cast<float>((ImGui::GetTime() - s_projectAlreadyOpenOpenedAt) / kDialogAppearDuration),
            0.0f,
            1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, popupAlpha);
        ImGui::TextWrapped("There is already a project open in the %s tab. Would you like to save it before loading the new project?", m_PendingLoadTarget == PendingLoadTarget::Editor ? "Editor" : "Composite");
        ImGui::Spacing();

        if (ImGui::Button("Save & Load", ImVec2(140.0f, 0.0f))) {
            bool needsName = false;
            if (m_PendingLoadTarget == PendingLoadTarget::Editor && m_CachedEditor) {
                if (m_CachedEditor->GetCurrentProjectFileName().empty()) {
                    needsName = true;
                } else {
                    LibraryManager::Get().RequestSaveProject(
                        m_CachedEditor->GetCurrentProjectName(),
                        m_CachedEditor,
                        m_CachedEditor->GetCurrentProjectFileName());
                }
            } else if (m_PendingLoadTarget == PendingLoadTarget::Composite && m_CachedComposite) {
                if (m_CachedComposite->GetCurrentProjectFileName().empty()) {
                    needsName = true;
                } else {
                    LibraryManager::Get().RequestSaveCompositeProject(
                        m_CachedComposite->GetCurrentProjectName(),
                        m_CachedComposite,
                        m_CachedComposite->GetCurrentProjectFileName());
                }
            }

            if (needsName) {
                m_SaveNamePromptOpen = true;
            } else {
                if (m_PendingLoadTarget == PendingLoadTarget::Editor && m_CachedEditor) {
                    if (m_PreviewProject) {
                        m_ProjectPreviewClosing = true;
                        m_ProjectPreviewRefreshAfterClose = false;
                    }
                    if (m_PreviewAsset) {
                        m_AssetPreviewClosing = true;
                    }
                    LibraryManager::Get().RequestLoadProject(m_PendingLoadProjectFileName, m_CachedEditor, [this](bool success) {
                        if (success && m_CachedActiveTab) *m_CachedActiveTab = 1;
                    });
                } else if (m_PendingLoadTarget == PendingLoadTarget::Composite && m_CachedComposite) {
                    if (m_PreviewProject) {
                        m_ProjectPreviewClosing = true;
                        m_ProjectPreviewRefreshAfterClose = false;
                    }
                    if (m_PreviewAsset) {
                        m_AssetPreviewClosing = true;
                    }
                    LibraryManager::Get().RequestLoadCompositeProject(m_PendingLoadProjectFileName, m_CachedComposite, [this](bool success) {
                        if (success && m_CachedActiveTab) *m_CachedActiveTab = 2;
                    });
                }
            }
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Discard & Load", ImVec2(140.0f, 0.0f))) {
            if (m_PendingLoadTarget == PendingLoadTarget::Editor && m_CachedEditor) {
                if (m_PreviewProject) {
                    m_ProjectPreviewClosing = true;
                    m_ProjectPreviewRefreshAfterClose = false;
                }
                if (m_PreviewAsset) {
                    m_AssetPreviewClosing = true;
                }
                LibraryManager::Get().RequestLoadProject(m_PendingLoadProjectFileName, m_CachedEditor, [this](bool success) {
                    if (success && m_CachedActiveTab) *m_CachedActiveTab = 1;
                });
            } else if (m_PendingLoadTarget == PendingLoadTarget::Composite && m_CachedComposite) {
                if (m_PreviewProject) {
                    m_ProjectPreviewClosing = true;
                    m_ProjectPreviewRefreshAfterClose = false;
                }
                if (m_PreviewAsset) {
                    m_AssetPreviewClosing = true;
                }
                LibraryManager::Get().RequestLoadCompositeProject(m_PendingLoadProjectFileName, m_CachedComposite, [this](bool success) {
                    if (success && m_CachedActiveTab) *m_CachedActiveTab = 2;
                });
            }
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Save New Project##Library", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            s_saveNameOpenedAt = ImGui::GetTime();
        }
        const float popupAlpha = ImGuiExtras::EaseOutCubic(std::clamp(
            static_cast<float>((ImGui::GetTime() - s_saveNameOpenedAt) / kDialogAppearDuration),
            0.0f,
            1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, popupAlpha);
        ImGui::Text("Enter a name for the current project:");
        ImGui::Spacing();
        ImGui::InputText("##ProjectName", m_SaveNameBuffer, sizeof(m_SaveNameBuffer));
        ImGui::Spacing();

        if (ImGui::Button("Save", ImVec2(100.0f, 0.0f))) {
            std::string newName = m_SaveNameBuffer;
            if (newName.empty()) {
                newName = "Untitled Project";
            }

            if (m_PendingLoadTarget == PendingLoadTarget::Editor && m_CachedEditor) {
                if (m_PreviewProject) {
                    m_ProjectPreviewClosing = true;
                    m_ProjectPreviewRefreshAfterClose = false;
                }
                if (m_PreviewAsset) {
                    m_AssetPreviewClosing = true;
                }
                LibraryManager::Get().RequestSaveProject(newName, m_CachedEditor, "");
                LibraryManager::Get().RequestLoadProject(m_PendingLoadProjectFileName, m_CachedEditor, [this](bool success) {
                    if (success && m_CachedActiveTab) *m_CachedActiveTab = 1;
                });
            } else if (m_PendingLoadTarget == PendingLoadTarget::Composite && m_CachedComposite) {
                if (m_PreviewProject) {
                    m_ProjectPreviewClosing = true;
                    m_ProjectPreviewRefreshAfterClose = false;
                }
                if (m_PreviewAsset) {
                    m_AssetPreviewClosing = true;
                }
                LibraryManager::Get().RequestSaveCompositeProject(newName, m_CachedComposite, "");
                LibraryManager::Get().RequestLoadCompositeProject(m_PendingLoadProjectFileName, m_CachedComposite, [this](bool success) {
                    if (success && m_CachedActiveTab) *m_CachedActiveTab = 2;
                });
            }
            
            m_SaveNameBuffer[0] = '\0';
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
}

void LibraryModule::RenderImportConflictPopup() {
    auto& manager = LibraryManager::Get();
    if (!manager.HasPendingConflicts()) return;

    const char* popupName = "Import Conflict Resolution";
    if (!ImGui::IsPopupOpen(popupName)) {
        ImGui::OpenPopup(popupName);
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x * 0.9f, viewport->Size.y * 0.9f), ImGuiCond_Appearing);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.09f, 0.11f, 0.98f));
    if (ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const auto& conflicts = manager.GetPendingConflicts();
        int currentIndex = 0;
        auto& conflict = const_cast<ImportConflict&>(conflicts[currentIndex]);

        ImGui::BeginChild("ConflictHeader", ImVec2(0, 80), true);
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // TODO: Use a larger font if available
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "IMPORT CONFLICT DETECTED");
        ImGui::PopFont();
        ImGui::Text("Project \"%s\" already exists in your library. Choose how to proceed.", conflict.localName.c_str());
        ImGui::TextDisabled("Conflict 1 of %d remaining", (int)conflicts.size());
        ImGui::EndChild();

        const float detailsHeight = 100.0f;
        const float footerHeight = 60.0f;
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 previewAreaSize(available.x, available.y - detailsHeight - footerHeight - 20.0f);

        // Preview Area with Wipe Slider
        ImGui::BeginChild("ConflictPreview", previewAreaSize, true);
        if (!conflict.previewsReady && !conflict.previewFailed) {
            manager.PrepareConflictPreview(currentIndex);
            ImGuiExtras::DrawSpinner("Generating comparison previews...", 20.0f, 4, IM_COL32(200, 200, 200, 255));
        } else if (conflict.localPreviewTex && conflict.importedPreviewTex) {
            ImVec2 imageSize = FitImageToBounds(
                static_cast<float>(conflict.localWidth),
                static_cast<float>(conflict.localHeight),
                previewAreaSize);

            ImGui::SetCursorPosX((previewAreaSize.x - imageSize.x) * 0.5f);
            ImGui::SetCursorPosY((previewAreaSize.y - imageSize.y) * 0.5f);

            ImGui::InvisibleButton("##ConflictWipe", imageSize);
            const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            const ImGuiID handleId = ImGui::GetItemID();
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();
            if (ImGui::IsItemHovered()) {
                m_ConflictCompareSplit = (ImGui::GetIO().MousePos.x - rect.Min.x) / std::max(1.0f, rect.GetWidth());
                m_ConflictCompareSplit = std::clamp(m_ConflictCompareSplit, 0.0f, 1.0f);
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(rect.Min, rect.Max, IM_COL32(10, 10, 10, 255));
            drawList->AddImage((ImTextureID)(intptr_t)conflict.localPreviewTex, rect.Min, rect.Max, ImVec2(0, 1), ImVec2(1, 0));
            
            float splitX = rect.Min.x + rect.GetWidth() * m_ConflictCompareSplit;
            drawList->PushClipRect(rect.Min, ImVec2(splitX, rect.Max.y), true);
            drawList->AddImage((ImTextureID)(intptr_t)conflict.importedPreviewTex, rect.Min, rect.Max, ImVec2(0, 1), ImVec2(1, 0));
            drawList->PopClipRect();
            DrawSplitHandle(drawList, rect, m_ConflictCompareSplit, handleId, hovered, active);

            // Labels
            drawList->AddText(ImVec2(rect.Min.x + 15, rect.Min.y + 15), IM_COL32(255, 255, 255, 255), "NEW / IMPORTING");
            drawList->AddText(ImVec2(rect.Max.x - 120, rect.Min.y + 15), IM_COL32(255, 255, 255, 255), "CURRENT / LOCAL");
        } else if (conflict.previewFailed) {
            ImGui::TextWrapped("%s", conflict.previewStatusText.empty()
                ? "Failed to generate comparison previews for this conflict."
                : conflict.previewStatusText.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Retry Preview Generation")) {
                manager.ResetConflictPreview(currentIndex);
            }
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Preview generation is still pending.");
        }
        ImGui::EndChild();

        // Comparison Details
        ImGui::BeginChild("ConflictDetails", ImVec2(0, detailsHeight), true);
        ImGui::Columns(2, "DetailsSplit", false);
        ImGui::Text("LOCAL (Existing)");
        ImGui::TextDisabled("Modified: %s", conflict.localTimestamp.c_str());
        ImGui::TextDisabled("Resolution: %d x %d", conflict.localWidth, conflict.localHeight);
        
        ImGui::NextColumn();
        ImGui::Text("IMPORTED (Incoming)");
        ImGui::TextDisabled("Modified: %s", conflict.importedTimestamp.c_str());
        ImGui::TextDisabled("Resolution: %d x %d", conflict.importedWidth, conflict.importedHeight);
        if (conflict.areIdentical) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[ IDENTICAL ]");
        }
        ImGui::Columns(1);
        ImGui::EndChild();

        // Action Buttons
        ImGui::Spacing();
        if (ImGui::Button("Ignore (Skip This)", ImVec2(180, 40))) {
            manager.ResolveConflict(currentIndex, ConflictAction::Ignore);
        }
        ImGui::SameLine();
        if (ImGui::Button("Replace Local Version", ImVec2(220, 40))) {
            manager.ResolveConflict(currentIndex, ConflictAction::Replace);
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep Both (Import as Copy)", ImVec2(220, 40))) {
            manager.ResolveConflict(currentIndex, ConflictAction::KeepBoth);
        }
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 170);
        if (ImGui::Button("Abort Import", ImVec2(150, 40))) {
            manager.ClearConflicts();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
}

void LibraryModule::RenderAssetConflictPopup() {
    auto& manager = LibraryManager::Get();
    if (!manager.HasPendingAssetConflicts()) return;

    const char* popupName = "Library Asset Conflict Resolution";
    if (!ImGui::IsPopupOpen(popupName)) {
        ImGui::OpenPopup(popupName);
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x * 0.88f, viewport->Size.y * 0.84f), ImGuiCond_Appearing);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.09f, 0.11f, 0.98f));
    if (ImGui::BeginPopupModal(popupName, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        const auto& conflicts = manager.GetPendingAssetConflicts();
        int currentIndex = 0;
        auto& conflict = const_cast<AssetImportConflict&>(conflicts[currentIndex]);

        ImGui::BeginChild("AssetConflictHeader", ImVec2(0, 82), true);
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "ASSET CONFLICT DETECTED");
        ImGui::Text("A similar Library asset already exists. Choose how to proceed.");
        ImGui::TextDisabled("Conflict 1 of %d remaining", static_cast<int>(conflicts.size()));
        ImGui::EndChild();

        const float detailsHeight = 112.0f;
        const float footerHeight = 68.0f;
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 previewAreaSize(available.x, available.y - detailsHeight - footerHeight - 20.0f);

        ImGui::BeginChild("AssetConflictPreview", previewAreaSize, true);
        if (!conflict.previewsReady) {
            manager.PrepareAssetConflictPreview(currentIndex);
            ImGuiExtras::DrawSpinner("Generating asset comparison previews...", 20.0f, 4, IM_COL32(200, 200, 200, 255));
        } else if (conflict.localPreviewTex && conflict.importedPreviewTex) {
            ImVec2 imageSize = FitImageToBounds(
                static_cast<float>(std::max(conflict.localWidth, conflict.importedWidth)),
                static_cast<float>(std::max(conflict.localHeight, conflict.importedHeight)),
                previewAreaSize);

            ImGui::SetCursorPosX((previewAreaSize.x - imageSize.x) * 0.5f);
            ImGui::SetCursorPosY((previewAreaSize.y - imageSize.y) * 0.5f);
            ImGui::InvisibleButton("##AssetConflictWipe", imageSize);
            const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            const ImGuiID handleId = ImGui::GetItemID();
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();
            if (ImGui::IsItemHovered()) {
                m_AssetConflictCompareSplit = (ImGui::GetIO().MousePos.x - rect.Min.x) / std::max(1.0f, rect.GetWidth());
                m_AssetConflictCompareSplit = std::clamp(m_AssetConflictCompareSplit, 0.0f, 1.0f);
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(rect.Min, rect.Max, IM_COL32(10, 10, 10, 255));
            drawList->AddImage((ImTextureID)(intptr_t)conflict.localPreviewTex, rect.Min, rect.Max, ImVec2(0, 1), ImVec2(1, 0));

            const float splitX = rect.Min.x + rect.GetWidth() * m_AssetConflictCompareSplit;
            drawList->PushClipRect(rect.Min, ImVec2(splitX, rect.Max.y), true);
            drawList->AddImage((ImTextureID)(intptr_t)conflict.importedPreviewTex, rect.Min, rect.Max, ImVec2(0, 1), ImVec2(1, 0));
            drawList->PopClipRect();
            DrawSplitHandle(drawList, rect, m_AssetConflictCompareSplit, handleId, hovered, active);

            drawList->AddText(ImVec2(rect.Min.x + 15, rect.Min.y + 15), IM_COL32(255, 255, 255, 255), "NEW / IMPORTING");
            drawList->AddText(ImVec2(rect.Max.x - 130, rect.Min.y + 15), IM_COL32(255, 255, 255, 255), "CURRENT / LOCAL");
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Failed to generate previews for this asset conflict.");
        }
        ImGui::EndChild();

        ImGui::BeginChild("AssetConflictDetails", ImVec2(0, detailsHeight), true);
        ImGui::Columns(2, "AssetConflictSplit", false);
        ImGui::Text("LOCAL (Existing)");
        ImGui::TextDisabled("%s", conflict.localDisplayName.c_str());
        ImGui::TextDisabled("Saved: %s", conflict.localTimestamp.c_str());
        ImGui::TextDisabled("Resolution: %d x %d", conflict.localWidth, conflict.localHeight);

        ImGui::NextColumn();
        ImGui::Text("IMPORTED (Incoming)");
        ImGui::TextDisabled("%s", conflict.importedDisplayName.c_str());
        ImGui::TextDisabled("Saved: %s", conflict.importedTimestamp.c_str());
        ImGui::TextDisabled("Resolution: %d x %d", conflict.importedWidth, conflict.importedHeight);
        if (conflict.areIdentical) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "[ IDENTICAL ]");
        }
        ImGui::Columns(1);
        ImGui::EndChild();

        ImGui::Spacing();
        if (ImGui::Button("Use Existing Asset", ImVec2(190, 40))) {
            manager.ResolveAssetConflict(currentIndex, AssetConflictAction::UseExisting);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Replace Existing Asset", ImVec2(210, 40))) {
            manager.ResolveAssetConflict(currentIndex, AssetConflictAction::Replace);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep Both (Import Copy)", ImVec2(220, 40))) {
            manager.ResolveAssetConflict(currentIndex, AssetConflictAction::KeepBoth);
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
}

void LibraryModule::RenderLibraryMenuOptions(bool importBusy, bool exportBusy) {
    ImGui::BeginDisabled(importBusy);
    if (ImGui::MenuItem("Import Library Bundle...")) {
        std::string path = FileDialogs::OpenLibraryBundleFileDialog("Import Library Bundle");
        if (!path.empty()) {
            LibraryManager::Get().RequestImportLibraryBundle(path);
        }
    }
    if (ImGui::MenuItem("Import Web Project...")) {
        std::string path = FileDialogs::OpenWebProjectFileDialog("Import Web Project (.mns.json)");
        if (!path.empty()) {
            LibraryManager::Get().RequestImportWebProject(path);
        }
    }
    if (ImGui::MenuItem("Import Folder Assets...")) {
        std::string path = FileDialogs::OpenFolderDialog("Select Folder for Assets");
        if (!path.empty()) {
            m_PendingFolderImportPath = path;
            m_FolderImportPopupOpen = true;
        }
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    ImGui::BeginDisabled(exportBusy);
    if (ImGui::MenuItem("Export Library Bundle...")) {
        std::string path = FileDialogs::SaveLibraryBundleFileDialog("Export Library Bundle", "modular_studio_library.stacklib");
        if (!path.empty()) {
            LibraryManager::Get().RequestExportLibraryBundle(path);
        }
    }
    ImGui::EndDisabled();

    ImGui::Separator();

    if (ImGui::MenuItem("Refresh Now")) {
        LibraryManager::Get().RefreshLibrary();
    }
}

void LibraryModule::RenderDeleteConfirmPopup() {
    if (m_DeleteConfirmOpen) {
        ImGui::OpenPopup("Confirm Delete");
        m_DeleteConfirmOpen = false;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    static double s_deleteConfirmOpenedAt = 0.0;
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) {
            s_deleteConfirmOpenedAt = ImGui::GetTime();
        }
        const float popupAlpha = ImGuiExtras::EaseOutCubic(std::clamp(
            static_cast<float>((ImGui::GetTime() - s_deleteConfirmOpenedAt) / kDialogAppearDuration),
            0.0f,
            1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, popupAlpha);
        const char* itemType = m_DeletingAssets ? "asset(s)" : "project(s)";
        ImGui::Text("Are you sure you want to permanently delete %d %s?", (int)m_PendingDeleteFileNames.size(), itemType);
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
        float listHeight = std::min((float)m_PendingDeleteFileNames.size() * 20.0f, 200.0f);
        ImGui::BeginChild("DeleteList", ImVec2(400, listHeight), true);
        for (const auto& fn : m_PendingDeleteFileNames) {
            ImGui::BulletText("%s", fn.c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "This action cannot be undone.");
        ImGui::Spacing();

        if (ImGui::Button("Delete", ImVec2(120, 0))) {
            const bool deletingCurrentProjectPreview =
                m_PreviewProject && std::find(m_PendingDeleteFileNames.begin(), m_PendingDeleteFileNames.end(), m_PreviewProject->fileName) != m_PendingDeleteFileNames.end();
            const bool deletingCurrentAssetPreview =
                m_PreviewAsset && std::find(m_PendingDeleteFileNames.begin(), m_PendingDeleteFileNames.end(), m_PreviewAsset->fileName) != m_PendingDeleteFileNames.end();
            for (const auto& fn : m_PendingDeleteFileNames) {
                if (m_DeletingAssets) {
                    LibraryManager::Get().DeleteAsset(fn);
                } else {
                    LibraryManager::Get().DeleteProject(fn);
                }
            }
            LibraryManager::Get().RefreshLibrary();
            m_SelectedProjects.clear();
            m_SelectedAssets.clear();
            m_PendingDeleteFileNames.clear();
            if (deletingCurrentProjectPreview) {
                m_ProjectPreviewClosing = true;
                m_ProjectPreviewRefreshAfterClose = false;
            }
            if (deletingCurrentAssetPreview) {
                m_AssetPreviewClosing = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            m_PendingDeleteFileNames.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
}
