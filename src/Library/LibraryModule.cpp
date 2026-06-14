#include "LibraryModule.h"
#include "App/settings/AppearanceTheme.h"
#include "LibraryManager.h"
#include "TagManager.h"
#include "Async/TaskSystem.h"
#include "Composite/CompositeModule.h"
#include "Editor/EditorModule.h"
#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"

#include "ProjectData.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLLoader.h"
#include "ThirdParty/stb_image.h"
#include "Persistence/StackBinaryFormat.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <iostream>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace {

unsigned int LoadIconTexture(const std::string& filename) {
    std::vector<std::filesystem::path> searchPaths = {
        std::filesystem::current_path() / "Icons" / filename,
        std::filesystem::current_path() / "../Icons" / filename,
        std::filesystem::current_path() / "../../Icons" / filename,
        std::filesystem::path("Icons") / filename,
        std::filesystem::path("../Icons") / filename,
        std::filesystem::path("../../Icons") / filename
    };

    std::filesystem::path resolvedPath;
    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) {
            resolvedPath = path;
            break;
        }
    }

    if (resolvedPath.empty()) {
        std::cerr << "[LibraryModule] Could not find icon: " << filename << std::endl;
        return 0;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(1);
    unsigned char* pixels = stbi_load(resolvedPath.string().c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        std::cerr << "[LibraryModule] Failed to load icon: " << resolvedPath << std::endl;
        return 0;
    }

    unsigned int tex = GLHelpers::CreateTextureFromPixels(pixels, width, height, 4);
    stbi_image_free(pixels);
    return tex;
}

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
constexpr float kSidebarMotionSpeed = 16.0f;
constexpr float kCardMotionSpeed = 16.0f;
constexpr float kPreviewMotionSpeed = 18.0f;
constexpr float kStatusMotionSpeed = 12.0f;
constexpr float kDialogAppearDuration = 0.12f;

void ComputeCoverUv(
    float sourceWidth,
    float sourceHeight,
    const ImRect& targetRect,
    ImVec2& outUv0,
    ImVec2& outUv1) {
    outUv0 = ImVec2(0.0f, 1.0f);
    outUv1 = ImVec2(1.0f, 0.0f);

    if (sourceWidth <= 0.0f || sourceHeight <= 0.0f) {
        return;
    }

    const float sourceAspect = sourceWidth / std::max(1.0f, sourceHeight);
    const float targetAspect = targetRect.GetWidth() / std::max(1.0f, targetRect.GetHeight());

    if (std::abs(sourceAspect - targetAspect) < 0.001f) {
        return;
    }

    if (sourceAspect > targetAspect) {
        const float visibleWidth = targetAspect / sourceAspect;
        const float inset = std::clamp((1.0f - visibleWidth) * 0.5f, 0.0f, 0.49f);
        outUv0.x = inset;
        outUv1.x = 1.0f - inset;
    } else {
        const float visibleHeight = sourceAspect / targetAspect;
        const float inset = std::clamp((1.0f - visibleHeight) * 0.5f, 0.0f, 0.49f);
        outUv0.y = 1.0f - inset;
        outUv1.y = inset;
    }
}

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

    const float rounding = 14.0f;

    const ImU32 accent = IM_COL32(95, 165, 255, static_cast<int>(32 + (80 * selected)));
    const ImU32 hoverAccent = IM_COL32(120, 180, 255, static_cast<int>(10 + (26 * hover)));
    const ImU32 edge = IM_COL32(255, 255, 255, static_cast<int>(8 + (10 * hover) + (16 * selected)));

    drawList->AddRectFilled(
        ImVec2(rect.Min.x, rect.Min.y),
        ImVec2(rect.Max.x, rect.Max.y),
        ScaleColorAlpha(accent, 0.10f * selected),
        rounding);
    drawList->AddRectFilled(
        ImVec2(rect.Min.x, rect.Min.y),
        ImVec2(rect.Max.x, rect.Max.y),
        ScaleColorAlpha(hoverAccent, 0.6f * hover),
        rounding);
    drawList->AddRect(
        rect.Min,
        rect.Max,
        edge,
        rounding,
        0,
        hovered || selected > 0.01f ? 1.1f : 1.0f);

    // Draw high-visibility selected neon-blue outline
    if (selected > 0.01f) {
        const ImU32 selectedOutlineColor = IM_COL32(95, 165, 255, static_cast<int>(220 * selected));
        drawList->AddRect(
            rect.Min,
            rect.Max,
            selectedOutlineColor,
            rounding,
            0,
            1.8f);
    }
}

void DrawCardOverlayText(
    ImDrawList* drawList,
    const ImRect& rect,
    float alpha,
    const std::string& title,
    const std::string& secondary,
    const std::string& tertiary) {
    if (alpha <= 0.01f) {
        return;
    }

    const float paddingX = 12.0f;
    const float paddingY = 12.0f;
    const ImVec2 titlePos(rect.Min.x + paddingX, rect.Max.y - 58.0f);
    const ImVec2 line2Pos(rect.Min.x + paddingX, rect.Max.y - 36.0f);
    const ImVec2 line3Pos(rect.Min.x + paddingX, rect.Max.y - 18.0f);
    const float maxTextX = rect.Max.x - paddingX;
    const ImU32 titleColor = IM_COL32(255, 255, 255, static_cast<int>(235.0f * alpha));
    const ImU32 bodyColor = IM_COL32(220, 228, 236, static_cast<int>(215.0f * alpha));
    auto fitWithEllipsis = [&](const std::string& source) {
        if (ImGui::CalcTextSize(source.c_str()).x <= (maxTextX - titlePos.x)) {
            return source;
        }

        std::string candidate = source;
        while (candidate.size() > 4) {
            candidate.pop_back();
            std::string withEllipsis = candidate + "...";
            if (ImGui::CalcTextSize(withEllipsis.c_str()).x <= (maxTextX - titlePos.x)) {
                return withEllipsis;
            }
        }
        return std::string("...");
    };
    const std::string titleLine = fitWithEllipsis(title);

    drawList->PushClipRect(rect.Min, rect.Max, true);
    drawList->AddText(titlePos, titleColor, titleLine.c_str());
    drawList->AddText(line2Pos, bodyColor, secondary.c_str());
    drawList->AddText(line3Pos, bodyColor, tertiary.c_str());
    drawList->PopClipRect();
}

void DrawSplitHandle(ImDrawList* drawList, const ImRect& rect, float split, ImGuiID handleId, bool hovered, bool active, float alpha = 1.0f) {
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
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    auto scaledAlpha = [alpha](float value) {
        return static_cast<int>(std::clamp(value * alpha, 0.0f, 255.0f));
    };

    drawList->AddLine(
        ImVec2(splitX, rect.Min.y),
        ImVec2(splitX, rect.Max.y),
        IM_COL32(255, 255, 255, scaledAlpha(210.0f + (25.0f * motion.hover))),
        lineThickness);
    drawList->AddCircleFilled(
        center,
        haloRadius,
        IM_COL32(255, 255, 255, scaledAlpha(30.0f + (35.0f * motion.hover))));
    drawList->AddCircleFilled(
        center,
        handleRadius,
        IM_COL32(255, 255, 255, scaledAlpha(220.0f + (25.0f * motion.hover))));
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

ImVec2 ComputeLibraryCardSize(float sourceWidth, float sourceHeight) {
    const float standardWidth = 220.0f;
    const float aspect = (sourceHeight > 0.0f) ? (sourceWidth / sourceHeight) : 1.0f;
    ImVec2 thumbSize(standardWidth, standardWidth / std::max(aspect, 0.1f));

    if (thumbSize.y > 300.0f) {
        thumbSize.y = 300.0f;
        thumbSize.x = 300.0f * aspect;
    }

    return thumbSize;
}

struct LibraryPackedCard {
    size_t index = 0;
    ImVec2 pos = ImVec2(0.0f, 0.0f);
    ImVec2 size = ImVec2(0.0f, 0.0f);
};

bool LibraryPackedRectsOverlap(const LibraryPackedCard& placed, const ImVec2& pos, const ImVec2& size, float gap) {
    return pos.x < placed.pos.x + placed.size.x + gap &&
           pos.x + size.x + gap > placed.pos.x &&
           pos.y < placed.pos.y + placed.size.y + gap &&
           pos.y + size.y + gap > placed.pos.y;
}

float LibraryPackedContactScore(const std::vector<LibraryPackedCard>& placed, const ImVec2& pos, const ImVec2& size, float gap) {
    float score = 0.0f;
    if (std::abs(pos.x) < 0.5f) score += size.y * 0.25f;
    if (std::abs(pos.y) < 0.5f) score += size.x * 0.25f;

    for (const LibraryPackedCard& card : placed) {
        const float verticalOverlap =
            std::max(0.0f, std::min(pos.y + size.y, card.pos.y + card.size.y) - std::max(pos.y, card.pos.y));
        const float horizontalOverlap =
            std::max(0.0f, std::min(pos.x + size.x, card.pos.x + card.size.x) - std::max(pos.x, card.pos.x));

        if (std::abs(pos.x - (card.pos.x + card.size.x + gap)) < 0.5f ||
            std::abs(card.pos.x - (pos.x + size.x + gap)) < 0.5f) {
            score += verticalOverlap;
        }
        if (std::abs(pos.y - (card.pos.y + card.size.y + gap)) < 0.5f ||
            std::abs(card.pos.y - (pos.y + size.y + gap)) < 0.5f) {
            score += horizontalOverlap;
        }
    }

    return score;
}

std::vector<LibraryPackedCard> PackLibraryCards(const std::vector<LibraryPackedCard>& inputCards, float contentWidth, float gap) {
    std::vector<LibraryPackedCard> placed;
    placed.reserve(inputCards.size());
    contentWidth = std::max(contentWidth, 1.0f);

    float packedHeight = 0.0f;
    for (const LibraryPackedCard& input : inputCards) {
        LibraryPackedCard best = input;
        bool found = false;
        float bestScore = 0.0f;

        std::vector<float> candidateXs;
        std::vector<float> candidateYs;
        candidateXs.reserve(placed.size() * 2 + 1);
        candidateYs.reserve(placed.size() * 2 + 1);
        candidateXs.push_back(0.0f);
        candidateYs.push_back(0.0f);

        for (const LibraryPackedCard& card : placed) {
            candidateXs.push_back(card.pos.x);
            candidateXs.push_back(card.pos.x + card.size.x + gap);
            candidateYs.push_back(card.pos.y);
            candidateYs.push_back(card.pos.y + card.size.y + gap);
        }

        for (float y : candidateYs) {
            if (y < 0.0f) continue;
            for (float x : candidateXs) {
                if (x < 0.0f || x + input.size.x > contentWidth + 0.5f) continue;

                bool overlaps = false;
                for (const LibraryPackedCard& card : placed) {
                    if (LibraryPackedRectsOverlap(card, ImVec2(x, y), input.size, gap)) {
                        overlaps = true;
                        break;
                    }
                }
                if (overlaps) continue;

                const float heightAfter = std::max(packedHeight, y + input.size.y);
                const float contact = LibraryPackedContactScore(placed, ImVec2(x, y), input.size, gap);
                const float score = (heightAfter * 100000.0f) + (y * 100.0f) + x - (contact * 2.0f);
                if (!found || score < bestScore) {
                    best = input;
                    best.pos = ImVec2(x, y);
                    bestScore = score;
                    found = true;
                }
            }
        }

        if (!found) {
            best = input;
            best.pos = ImVec2(0.0f, packedHeight > 0.0f ? packedHeight + gap : 0.0f);
        }

        placed.push_back(best);
        packedHeight = std::max(packedHeight, best.pos.y + best.size.y);
    }

    return placed;
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



void DrawComparePreview(const ProjectEntry& project, const ImVec2& requestedSize, float& split, float rounding = 14.0f, bool showLabels = false, float alpha = 1.0f) {
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
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    const int alphaByte = static_cast<int>(255.0f * alpha);
    drawList->AddRectFilled(rect.Min, rect.Max, IM_COL32(14, 16, 20, alphaByte), rounding);

    const ImTextureID beforeTex = (ImTextureID)(intptr_t)project.sourcePreviewTex;
    const ImTextureID afterTex = (ImTextureID)(intptr_t)project.fullPreviewTex;
    const ImVec2 beforeUv0(0, 1);
    const ImVec2 beforeUv1(1, 0);
    const ImVec2 afterUv0(0, 1);
    const ImVec2 afterUv1(1, 0);

    if (beforeTex) {
        drawList->AddImageRounded(beforeTex, rect.Min, rect.Max, beforeUv0, beforeUv1, IM_COL32(255, 255, 255, alphaByte), rounding);
    }

    if (afterTex) {
        const float splitX = rect.Min.x + rect.GetWidth() * split;
        drawList->PushClipRect(rect.Min, ImVec2(splitX, rect.Max.y), true);
        drawList->AddImageRounded(afterTex, rect.Min, rect.Max, afterUv0, afterUv1, IM_COL32(255, 255, 255, alphaByte), rounding);
        drawList->PopClipRect();
        DrawSplitHandle(drawList, rect, split, handleId, hovered, active, alpha);

        if (showLabels) {
            drawList->AddText(ImVec2(rect.Min.x + 10.0f, rect.Min.y + 10.0f), IM_COL32(255, 255, 255, static_cast<int>(220.0f * alpha)), "After");
            drawList->AddText(ImVec2(rect.Max.x - 55.0f, rect.Min.y + 10.0f), IM_COL32(255, 255, 255, static_cast<int>(220.0f * alpha)), "Before");
        }
    }
}

} // namespace

LibraryModule::LibraryModule() {}
LibraryModule::~LibraryModule() {
    if (m_OptionsIconTex) {
        glDeleteTextures(1, &m_OptionsIconTex);
        m_OptionsIconTex = 0;
    }
    if (m_AllProjectsIconTex) {
        glDeleteTextures(1, &m_AllProjectsIconTex);
        m_AllProjectsIconTex = 0;
    }
    if (m_AssetsIconTex) {
        glDeleteTextures(1, &m_AssetsIconTex);
        m_AssetsIconTex = 0;
    }
}

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
        m_ProjectPreviewMenuHover = 0.0f;
        m_ProjectPreviewLaunchRect.valid = false;
        m_AssetPreviewMenuHover = 0.0f;
        m_AssetPreviewLaunchRect.valid = false;
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
        m_ProjectPreviewMenuHover = 0.0f;
        m_ProjectPreviewLaunchRect.valid = false;
        m_AssetPreviewMenuHover = 0.0f;
        m_AssetPreviewLaunchRect.valid = false;
        LibraryManager::Get().CancelProjectPreviewRequests();
        LibraryManager::Get().CancelAssetPreviewRequests();
        LibraryManager::Get().RequestAssetPreview(m_PreviewAsset);
        return;
    }
}

void LibraryModule::RenderUI(
    EditorModule* editor,
    CompositeModule* composite,
    StackAppearance::AppearanceManager* appearance,
    int* activeTab,
    std::function<void(const std::string&)> onLoadEditorProject) {
    if (m_OptionsIconTex == 0) {
        m_OptionsIconTex = LoadIconTexture("options.png");
    }
    if (m_AllProjectsIconTex == 0) {
        m_AllProjectsIconTex = LoadIconTexture("all projects.png");
    }
    if (m_AssetsIconTex == 0) {
        m_AssetsIconTex = LoadIconTexture("assets.png");
    }

    m_CachedEditor = editor;
    m_CachedComposite = composite;
    m_CachedActiveTab = activeTab;
    m_OnLoadEditorProject = std::move(onLoadEditorProject);
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

    auto renderStatusLine = [&](const char* text, float& alphaState) {
        if (text == nullptr || text[0] == '\0' || alphaState <= 0.01f) {
            return false;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alphaState);
        ImGui::TextDisabled("%s", text);
        ImGui::PopStyleVar();
        return true;
    };

    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 18));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(255, 255, 255, 24));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(255, 255, 255, 32));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 14));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 26));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 255, 255, 34));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(220, 236, 244, 34));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(80.0f, 32.0f));
    ImGui::BeginChild("LibraryTabContainer", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            if (m_ShowAssets) {
                if (!m_SelectedAssets.empty()) {
                    m_PendingDeleteFileNames.assign(m_SelectedAssets.begin(), m_SelectedAssets.end());
                    m_DeletingAssets = true;
                    m_DeleteConfirmOpen = true;
                }
            } else {
                if (!m_SelectedProjects.empty()) {
                    m_PendingDeleteFileNames.assign(m_SelectedProjects.begin(), m_SelectedProjects.end());
                    m_DeletingAssets = false;
                    m_DeleteConfirmOpen = true;
                }
            }
        }
    }

    float headerHeight = 0.0f;
    if (m_ImportStatusAlpha > 0.01f || m_ExportStatusAlpha > 0.01f || m_SaveStatusAlpha > 0.01f || m_LoadStatusAlpha > 0.01f) {
        headerHeight = 24.0f;
    }

    if (headerHeight > 0.0f) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        if (ImGui::BeginChild("LibraryHeader", ImVec2(0, headerHeight), false, ImGuiWindowFlags_NoScrollbar)) {
            bool hasPreviousStatus = false;
            auto renderInlineStatus = [&](const char* text, float& alphaState) {
                if (text == nullptr || text[0] == '\0' || alphaState <= 0.01f) {
                    return;
                }
                if (hasPreviousStatus) {
                    ImGui::SameLine(0.0f, 18.0f);
                }
                renderStatusLine(text, alphaState);
                hasPreviousStatus = true;
            };

            renderInlineStatus(LibraryManager::Get().GetImportStatusText().c_str(), m_ImportStatusAlpha);
            renderInlineStatus(LibraryManager::Get().GetExportStatusText().c_str(), m_ExportStatusAlpha);
            renderInlineStatus(LibraryManager::Get().GetSaveStatusText().c_str(), m_SaveStatusAlpha);
            renderInlineStatus(LibraryManager::Get().GetProjectLoadStatusText().c_str(), m_LoadStatusAlpha);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(); // Pop LibraryHeader's WindowPadding
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f, 18.0f));
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x + 16.0f, style.ItemSpacing.y + 20.0f));
    ImGui::BeginChild("LibraryGrid", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);

    int renderedCount = 0;
    const ImVec2 layoutStartScreen = ImGui::GetCursorScreenPos();
    const ImVec2 layoutStartLocal = ImGui::GetCursorPos();
    const float layoutWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float packedCardGap = 22.0f;
    float packedHeight = 0.0f;

    if (m_ShowAssets) {
        const auto& assets = LibraryManager::Get().GetAssets();
        std::vector<LibraryPackedCard> cards;
        cards.reserve(assets.size());

        for (size_t idx = 0; idx < assets.size(); ++idx) {
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
        }

        const std::vector<LibraryPackedCard> packedCards = PackLibraryCards(cards, layoutWidth, packedCardGap);
        for (const LibraryPackedCard& card : packedCards) {
            const auto& asset = assets[card.index];
            if (!asset) continue;

            ImGui::SetCursorScreenPos(ImVec2(layoutStartScreen.x + card.pos.x, layoutStartScreen.y + card.pos.y));
            ImGui::PushID(asset->fileName.c_str());
            if (RenderAssetCard(*asset, editor)) ++renderedCount;
            ImGui::PopID();
            packedHeight = std::max(packedHeight, card.pos.y + card.size.y);
        }
    } else {
        const auto& projects = LibraryManager::Get().GetProjects();
        std::vector<LibraryPackedCard> cards;
        cards.reserve(projects.size());

        for (size_t idx = 0; idx < projects.size(); ++idx) {
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
        }

        const std::vector<LibraryPackedCard> packedCards = PackLibraryCards(cards, layoutWidth, packedCardGap);
        for (const LibraryPackedCard& card : packedCards) {
            const auto& project = projects[card.index];
            if (!project) continue;

            ImGui::SetCursorScreenPos(ImVec2(layoutStartScreen.x + card.pos.x, layoutStartScreen.y + card.pos.y));
            ImGui::PushID(project->fileName.c_str());
            if (RenderProjectCard(*project, editor)) ++renderedCount;
            ImGui::PopID();
            packedHeight = std::max(packedHeight, card.pos.y + card.size.y);
        }
    }

    ImGui::SetCursorPos(ImVec2(layoutStartLocal.x, layoutStartLocal.y + packedHeight));
    if (packedHeight > 0.0f) {
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
    }

    m_EmptyStateAlpha = ImGuiExtras::AnimateTowards(m_EmptyStateAlpha, renderedCount == 0 ? 1.0f : 0.0f, dt, kStatusMotionSpeed);
    if (m_EmptyStateAlpha > 0.01f) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_EmptyStateAlpha);
        const char* emptyText =
            m_ShowAssets
                ? (m_SearchFilter[0] ? "No assets match the current search filter." : "No rendered assets have been saved to the library yet.")
                : (m_SearchFilter[0] ? "No projects match the current search filter." : "No projects found in the library.");
        const ImVec2 textSize = ImGui::CalcTextSize(emptyText);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (avail.x - textSize.x) * 0.5f));
        ImGui::Dummy(ImVec2(0.0f, std::max(40.0f, (avail.y * 0.32f) - textSize.y)));
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), (avail.x - textSize.x) * 0.5f));
        ImGui::TextDisabled("%s", emptyText);
        ImGui::PopStyleVar();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(118, 162, 196, 210));
    if (!m_BlockLibraryGridContextMenuThisFrame &&
        ImGui::BeginPopupContextWindow("LibraryGridContextMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        RenderLibraryMenuOptions(importBusy, exportBusy);
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    // Sliding tags panel overlay inside LibraryGrid
    {
        ImVec2 gridPos = ImGui::GetWindowPos();
        ImVec2 gridSize = ImGui::GetWindowSize();
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        const float mainViewportLeft = ImGui::GetMainViewport()->Pos.x;
        
        bool hoveringTagsPanel = false;
        if (!m_FilterPanelExpanded) {
            // Hover trigger along the absolute left program edge up to 15px past LibraryGrid left bounds
            if (mousePos.x >= mainViewportLeft && mousePos.x <= gridPos.x + 15.0f &&
                mousePos.y >= gridPos.y && mousePos.y <= gridPos.y + gridSize.y) {
                hoveringTagsPanel = true;
            }
        } else {
            // Keep open while hovering the drawer (which is m_FilterPanelWidthAnim wide) plus trigger buffer
            if (mousePos.x >= mainViewportLeft && mousePos.x <= gridPos.x + m_FilterPanelWidthAnim + 15.0f &&
                mousePos.y >= gridPos.y && mousePos.y <= gridPos.y + gridSize.y) {
                hoveringTagsPanel = true;
            }
        }
        
        if (ImGui::IsDragDropActive()) {
            hoveringTagsPanel = true;
        }
        
        m_FilterPanelExpanded = hoveringTagsPanel;
        
        float tagsPanelTargetWidth = m_FilterPanelExpanded ? 220.0f : 0.0f;
        m_FilterPanelWidthAnim += (tagsPanelTargetWidth - m_FilterPanelWidthAnim) * dt * 10.0f;
        if (std::abs(m_FilterPanelWidthAnim - tagsPanelTargetWidth) < 0.1f) {
            m_FilterPanelWidthAnim = tagsPanelTargetWidth;
        }
        
        if (m_FilterPanelWidthAnim > 0.1f) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 panelMin = gridPos;
            
            // Feathered blend overlay background
            float gradientWidth = std::min(60.0f, m_FilterPanelWidthAnim);
            float solidWidth = m_FilterPanelWidthAnim - gradientWidth;
            
            ImVec4 workspaceColor = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
            const float luminance = 0.2126f * workspaceColor.x + 0.7152f * workspaceColor.y + 0.0722f * workspaceColor.z;
            const bool isLightBg = luminance >= 0.5f;

            ImVec4 colBgOpaqueVec = workspaceColor;
            colBgOpaqueVec.w = isLightBg ? 0.95f : 0.93f;
            const ImU32 colBgOpaque = ImGui::ColorConvertFloat4ToU32(colBgOpaqueVec);

            ImVec4 colBgTransVec = workspaceColor;
            colBgTransVec.w = 0.0f;
            const ImU32 colBgTrans = ImGui::ColorConvertFloat4ToU32(colBgTransVec);

            const ImU32 colTitleText = isLightBg ? IM_COL32(18, 24, 30, 255) : IM_COL32(255, 255, 255, 255);
            
            // Solid part
            if (solidWidth > 0.0f) {
                drawList->AddRectFilled(panelMin, ImVec2(gridPos.x + solidWidth, gridPos.y + gridSize.y), colBgOpaque);
            }
            // Feathered part
            drawList->AddRectFilledMultiColor(
                ImVec2(gridPos.x + solidWidth, gridPos.y),
                ImVec2(gridPos.x + m_FilterPanelWidthAnim, gridPos.y + gridSize.y),
                colBgOpaque, colBgTrans, colBgTrans, colBgOpaque
            );
            
            // Content
            float contentWidth = m_FilterPanelWidthAnim - 40.0f;
            if (contentWidth > 1.0f) {
                ImGui::SetCursorScreenPos(ImVec2(gridPos.x + 16.0f, gridPos.y + 24.0f));
                ImGui::BeginChild("LibraryTagsDrawer", ImVec2(contentWidth, gridSize.y - 48.0f), false, ImGuiWindowFlags_NoScrollbar);
                
                ImGui::PushStyleColor(ImGuiCol_Text, colTitleText);
                ImGui::TextUnformatted("LIBRARY FILTERS");
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));
                
                // Render the filter options here!
                auto& currentSelection = m_ShowAssets ? m_SelectedAssets : m_SelectedProjects;
                const bool hasSelectionForTagging = !currentSelection.empty();
                auto applyTagToSelection = [&]() {
                    std::string tagText = m_AddTagBuffer;
                    auto firstNonSpace = std::find_if_not(tagText.begin(), tagText.end(), [](unsigned char ch) {
                        return std::isspace(ch) != 0;
                    });
                    auto lastNonSpace = std::find_if_not(tagText.rbegin(), tagText.rend(), [](unsigned char ch) {
                        return std::isspace(ch) != 0;
                    }).base();
                    if (firstNonSpace >= lastNonSpace || currentSelection.empty()) {
                        return false;
                    }

                    tagText = std::string(firstNonSpace, lastNonSpace);
                    for (const auto& fileName : currentSelection) {
                        TagManager::Get().AddTag(fileName, tagText);
                    }
                    m_AddTagBuffer[0] = '\0';
                    return true;
                };
                
                auto allTags = TagManager::Get().GetAllKnownTags();

                bool noTagFilter = m_FilterNoTag;
                if (ImGui::Checkbox("Untagged only", &noTagFilter)) {
                    m_FilterNoTag = noTagFilter;
                    if (m_FilterNoTag) {
                        m_ActiveTagFilters.clear();
                    }
                }

                if (!m_FilterNoTag) {
                    for (const auto& tag : allTags) {
                        bool active = m_ActiveTagFilters.count(tag) > 0;
                        if (ImGui::Checkbox(tag.c_str(), &active)) {
                            if (active) {
                                m_ActiveTagFilters.insert(tag);
                            } else {
                                m_ActiveTagFilters.erase(tag);
                            }
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
                const bool tagReady = hasSelectionForTagging && (m_AddTagBuffer[0] != '\0');
                if (tagReady) {
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(64, 150, 84, 92));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(74, 168, 94, 104));
                    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(82, 184, 102, 116));
                }
                ImGui::SetNextItemWidth(-14.0f);
                const bool submittedTag = ImGui::InputTextWithHint(
                    "##addtag",
                    "New tag...",
                    m_AddTagBuffer,
                    sizeof(m_AddTagBuffer),
                    ImGuiInputTextFlags_EnterReturnsTrue);
                if (tagReady) {
                    ImGui::PopStyleColor(3);
                }
                if (submittedTag) {
                    applyTagToSelection();
                }

                ImGui::Dummy(ImVec2(0.0f, 16.0f));
                ImGui::SeparatorText("Theme");
                if (appearance != nullptr) {
                    const std::string activePresetId = appearance->GetActivePresetId();
                    const StackAppearance::ThemeDefinition* activePreset = appearance->GetActivePreset();
                    const std::string currentPresetName = activePreset ? activePreset->displayName : "Custom";
                    ImGui::SetNextItemWidth(-14.0f);
                    if (ImGui::BeginCombo("##LibraryThemePresetCombo", currentPresetName.c_str())) {
                        // Draw factory presets
                        for (const auto& preset : appearance->GetFactoryThemes()) {
                            const bool selected = activePresetId == preset.id;
                            if (ImGui::Selectable(preset.displayName.c_str(), selected)) {
                                appearance->SelectPresetById(preset.id);
                                appearance->ApplyCurrentTheme(ImGui::GetIO(), ImGui::GetStyle());
                            }
                        }
                        ImGui::EndCombo();
                    }
                }

                ImGui::EndChild();
            }
        }
    }

    // Centered floating search bar and options/mode pills at bottom
    {
        ImVec2 libraryPos = ImGui::GetWindowPos();
        ImVec2 librarySize = ImGui::GetWindowSize();
        
        const float searchW = 340.0f;
        const float gapFromBottom = 10.0f;
        
        // Position bottom-center of the window 10px above the bottom of the grid
        ImVec2 searchPos = ImVec2(
            libraryPos.x + librarySize.x * 0.5f,
            libraryPos.y + librarySize.y - gapFromBottom
        );

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

        // Push tight item spacing for search/icons vertical layout
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f));

        // 1. Render Search Bar
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(24, 28, 34, 235));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(32, 38, 46, 245));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(38, 44, 52, 255));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(110, 186, 255, 64));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 17.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 6.0f));

        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##search", "Search library...", m_SearchFilter, sizeof(m_SearchFilter));

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(4);


        // 2. Render Centered Icons (Options + Mode Pills)
        const float defaultBtnWidth = 110.0f;
        float optionsWidth = (m_OptionsIconTex != 0) ? (18.0f + ImGui::GetStyle().FramePadding.x * 2.0f) : 84.0f;
        float allProjWidth = (m_AllProjectsIconTex != 0) ? (18.0f + ImGui::GetStyle().FramePadding.x * 2.0f) : defaultBtnWidth;
        float assetsWidth = (m_AssetsIconTex != 0) ? (18.0f + ImGui::GetStyle().FramePadding.x * 2.0f) : defaultBtnWidth;
        float iconsRowWidth = optionsWidth + 12.0f + allProjWidth + 6.0f + assetsWidth;

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
            auto renderModePill = [&](const char* label, bool active, bool showAssetsValue, unsigned int iconTex) {
                ImGui::PushStyleColor(ImGuiCol_Button, active ? IM_COL32(110, 186, 255, 52) : IM_COL32(255, 255, 255, 10));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? IM_COL32(110, 186, 255, 68) : IM_COL32(255, 255, 255, 22));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, active ? IM_COL32(110, 186, 255, 82) : IM_COL32(255, 255, 255, 30));
                
                bool clicked = false;
                if (iconTex != 0) {
                    std::string strId = std::string("##ModePill_") + label;
                    clicked = ImGui::ImageButton(strId.c_str(), (ImTextureID)(intptr_t)iconTex, ImVec2(18.0f, 18.0f));
                } else {
                    clicked = ImGui::Button(label, ImVec2(defaultBtnWidth, 28.0f));
                }

                if (clicked) {
                    m_ShowAssets = showAssetsValue;
                    if (m_ShowAssets) {
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

            renderModePill("All Projects", !m_ShowAssets, false, m_AllProjectsIconTex);
            ImGui::SameLine(0.0f, 6.0f);
            renderModePill("Assets", m_ShowAssets, true, m_AssetsIconTex);
        }
        ImGui::EndGroup();

        ImGui::PopStyleVar(); // Pop ItemSpacing
        ImGui::End();
        ImGui::PopStyleVar(3);
    }

    ImGui::EndChild(); // End LibraryGrid
    ImGui::PopStyleVar(2); // Pop LibraryGrid's WindowPadding and ItemSpacing
    ImGui::EndChild(); // End LibraryTabContainer

    ImGui::PopStyleColor(8);
    ImGui::PopStyleVar(3);

    PruneCardMotionStates(m_ProjectCardMotion, ImGui::GetFrameCount());
    PruneCardMotionStates(m_AssetCardMotion, ImGui::GetFrameCount());

    if (m_PreviewProject || m_ProjectPreviewTransition > 0.0f) {
        RenderPreviewPopup(editor, composite, activeTab, onLoadEditorProject);
    } else if (m_PreviewAsset || m_AssetPreviewTransition > 0.0f) {
        RenderAssetPreviewPopup(editor, composite, activeTab, onLoadEditorProject);
    }

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

    ImVec2 thumbSize = ComputeLibraryCardSize(static_cast<float>(project.sourceWidth), static_cast<float>(project.sourceHeight));

    const float cardWidth = thumbSize.x;
    const float totalHeight = thumbSize.y;
    const ImVec2 screenMin = ImGui::GetCursorScreenPos();
    const ImRect cardRect(screenMin, ImVec2(screenMin.x + cardWidth, screenMin.y + totalHeight));

    ImGui::BeginGroup();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float cardAlpha = std::clamp(motion.reveal, 0.0f, 1.0f);
    const float rounding = 14.0f;
    const ImVec4 baseColor(0.10f, 0.115f, 0.135f, 0.94f * cardAlpha);
    drawList->AddRectFilled(cardRect.Min, cardRect.Max, ImGui::ColorConvertFloat4ToU32(baseColor), rounding);

    const float xOffset = (cardWidth - thumbSize.x) * 0.5f;
    const ImRect imageRect(
        ImVec2(cardRect.Min.x + xOffset, cardRect.Min.y),
        ImVec2(cardRect.Min.x + xOffset + thumbSize.x, cardRect.Min.y + thumbSize.y));

    if (project.thumbnailTex) {
        ImVec2 uv0;
        ImVec2 uv1;
        ComputeCoverUv(static_cast<float>(project.sourceWidth), static_cast<float>(project.sourceHeight), imageRect, uv0, uv1);
        drawList->AddImageRounded(
            (ImTextureID)(intptr_t)project.thumbnailTex,
            imageRect.Min,
            imageRect.Max,
            uv0,
            uv1,
            IM_COL32(255, 255, 255, static_cast<int>(255.0f * cardAlpha)),
            rounding);
    } else {
        drawList->AddRectFilled(imageRect.Min, imageRect.Max, IM_COL32(40, 46, 52, static_cast<int>(240.0f * cardAlpha)), rounding);
        const char* noPreview = "No Preview";
        const ImVec2 noPreviewSize = ImGui::CalcTextSize(noPreview);
        drawList->AddText(
            ImVec2(imageRect.Min.x + (imageRect.GetWidth() - noPreviewSize.x) * 0.5f, imageRect.Min.y + (imageRect.GetHeight() - noPreviewSize.y) * 0.5f),
            IM_COL32(220, 228, 236, static_cast<int>(210.0f * cardAlpha)),
            noPreview);
    }

    ImGui::InvisibleButton("##hitbox", ImVec2(cardWidth, totalHeight));
    const bool isHovered = ImGui::IsItemHovered();
    const bool isLeftClicked = matchesFilter && ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool isRightClicked = matchesFilter && ImGui::IsItemClicked(ImGuiMouseButton_Right);
    const bool isDoubleClicked = matchesFilter && isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    motion.hover = ImGuiExtras::AnimateTowards(motion.hover, (matchesFilter && isHovered) ? 1.0f : 0.0f, dt, 24.0f);

    const ImRect motionRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const float overlayAlpha = ImGuiExtras::EaseOutCubic(motion.hover);
    drawList->AddRectFilled(
        motionRect.Min,
        motionRect.Max,
        IM_COL32(8, 11, 14, static_cast<int>(124.0f * overlayAlpha)),
        rounding);
    DrawCardOverlayText(
        drawList,
        motionRect,
        overlayAlpha,
        project.projectName,
        std::to_string(project.sourceWidth) + "x" + std::to_string(project.sourceHeight),
        project.timestamp);
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
                m_ProjectPreviewMenuHover = 0.0f;
                m_ProjectPreviewLaunchRect.minX = motionRect.Min.x;
                m_ProjectPreviewLaunchRect.minY = motionRect.Min.y;
                m_ProjectPreviewLaunchRect.maxX = motionRect.Max.x;
                m_ProjectPreviewLaunchRect.maxY = motionRect.Max.y;
                m_ProjectPreviewLaunchRect.valid = true;
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(118, 162, 196, 210));
    if (ImGui::BeginPopup(popupId.c_str())) {
        const bool multiple = m_SelectedProjects.size() > 1;
        if (!multiple) {
            if (project.projectKind == "editor" || project.projectKind.empty()) {
                if (ImGui::MenuItem("Open in Editor")) {
                    if (m_OnLoadEditorProject) {
                        m_OnLoadEditorProject(project.fileName);
                    } else {
                        LibraryManager::Get().RequestLoadProject(project.fileName, m_CachedEditor);
                        if (m_CachedActiveTab) *m_CachedActiveTab = 1;
                    }
                }
            }
            if (project.projectKind == "composite") {
                ImGui::BeginDisabled();
                ImGui::MenuItem("Legacy Composite Project (Unsupported)", nullptr, false, false);
                ImGui::EndDisabled();
            }
        }

        ImGui::BeginDisabled();
        ImGui::MenuItem("Load Selected into Composite", nullptr, false, false);
        ImGui::EndDisabled();

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
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    ImGui::EndGroup();
    return true;
}

bool LibraryModule::RenderAssetCard(const AssetEntry& asset, EditorModule* editor) {
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

    ImVec2 thumbSize = ComputeLibraryCardSize(static_cast<float>(asset.width), static_cast<float>(asset.height));

    const float cardWidth = thumbSize.x;
    const float totalHeight = thumbSize.y;
    const ImVec2 screenMin = ImGui::GetCursorScreenPos();
    const ImRect cardRect(screenMin, ImVec2(screenMin.x + cardWidth, screenMin.y + totalHeight));

    ImGui::BeginGroup();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float cardAlpha = std::clamp(motion.reveal, 0.0f, 1.0f);
    const float rounding = 14.0f;
    const ImVec4 baseColor(0.10f, 0.115f, 0.135f, 0.94f * cardAlpha);
    drawList->AddRectFilled(cardRect.Min, cardRect.Max, ImGui::ColorConvertFloat4ToU32(baseColor), rounding);

    const float xOffset = (cardWidth - thumbSize.x) * 0.5f;
    const ImRect imageRect(
        ImVec2(cardRect.Min.x + xOffset, cardRect.Min.y),
        ImVec2(cardRect.Min.x + xOffset + thumbSize.x, cardRect.Min.y + thumbSize.y));

    if (asset.thumbnailTex) {
        ImVec2 uv0;
        ImVec2 uv1;
        ComputeCoverUv(static_cast<float>(asset.width), static_cast<float>(asset.height), imageRect, uv0, uv1);
        drawList->AddImageRounded(
            (ImTextureID)(intptr_t)asset.thumbnailTex,
            imageRect.Min,
            imageRect.Max,
            uv0,
            uv1,
            IM_COL32(255, 255, 255, static_cast<int>(255.0f * cardAlpha)),
            rounding);
    } else {
        drawList->AddRectFilled(imageRect.Min, imageRect.Max, IM_COL32(40, 46, 52, static_cast<int>(240.0f * cardAlpha)), rounding);
        const char* noPreview = "No Asset Preview";
        const ImVec2 noPreviewSize = ImGui::CalcTextSize(noPreview);
        drawList->AddText(
            ImVec2(imageRect.Min.x + (imageRect.GetWidth() - noPreviewSize.x) * 0.5f, imageRect.Min.y + (imageRect.GetHeight() - noPreviewSize.y) * 0.5f),
            IM_COL32(220, 228, 236, static_cast<int>(210.0f * cardAlpha)),
            noPreview);
    }

    ImGui::InvisibleButton("##hitbox", ImVec2(cardWidth, totalHeight));
    const bool isHovered = ImGui::IsItemHovered();
    const bool isLeftClicked = matchesFilter && ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool isRightClicked = matchesFilter && ImGui::IsItemClicked(ImGuiMouseButton_Right);
    const bool isDoubleClicked = matchesFilter && isHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    motion.hover = ImGuiExtras::AnimateTowards(motion.hover, (matchesFilter && isHovered) ? 1.0f : 0.0f, dt, 24.0f);
    const ImRect motionRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const float overlayAlpha = ImGuiExtras::EaseOutCubic(motion.hover);
    drawList->AddRectFilled(
        motionRect.Min,
        motionRect.Max,
        IM_COL32(8, 11, 14, static_cast<int>(124.0f * overlayAlpha)),
        rounding);
    DrawCardOverlayText(
        drawList,
        motionRect,
        overlayAlpha,
        asset.displayName,
        std::to_string(asset.width) + "x" + std::to_string(asset.height),
        !asset.projectName.empty() ? ("From: " + asset.projectName) : asset.fileName);
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
        const auto& assets = LibraryManager::Get().GetAssets();
        for (const auto& candidate : assets) {
            if (candidate && candidate->fileName == asset.fileName) {
                m_ShowAssets = true;
                m_PreviewAsset = candidate;
                m_PreviewProject = nullptr;
                m_AssetPreviewTransition = 0.0f;
                m_AssetPreviewClosing = false;
                m_ProjectPreviewTransition = 0.0f;
                m_ProjectPreviewClosing = false;
                m_ProjectPreviewRefreshAfterClose = false;
                m_ProjectPreviewMenuHover = 0.0f;
                m_ProjectPreviewLaunchRect.valid = false;
                m_AssetPreviewMenuHover = 0.0f;
                m_AssetPreviewLaunchRect.minX = motionRect.Min.x;
                m_AssetPreviewLaunchRect.minY = motionRect.Min.y;
                m_AssetPreviewLaunchRect.maxX = motionRect.Max.x;
                m_AssetPreviewLaunchRect.maxY = motionRect.Max.y;
                m_AssetPreviewLaunchRect.valid = true;
                LibraryManager::Get().CancelProjectPreviewRequests();
                LibraryManager::Get().CancelAssetPreviewRequests();
                LibraryManager::Get().RequestAssetPreview(m_PreviewAsset);
                break;
            }
        }
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
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(118, 162, 196, 210));
    if (ImGui::BeginPopup(popupId.c_str())) {
        if (ImGui::MenuItem("Add Selected To Editor Composite", nullptr, false, !m_SelectedAssets.empty() && editor != nullptr)) {
            for (const auto& fn : m_SelectedAssets) {
                editor->AddCompositeLibraryAssetChain(fn);
            }
            if (m_CachedActiveTab) *m_CachedActiveTab = 1;
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
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);

    ImGui::EndGroup();
    return true;
}

void LibraryModule::RenderPreviewPopup(
    EditorModule* editor,
    CompositeModule* composite,
    int* activeTab,
    const std::function<void(const std::string&)>& onLoadEditorProject) {
    if (!m_PreviewProject && m_ProjectPreviewTransition <= 0.0f) {
        return;
    }

    const float previewProgressTarget = (m_PreviewProject && !m_ProjectPreviewClosing) ? 1.0f : 0.0f;
    m_ProjectPreviewTransition = ImGuiExtras::AnimateTowards(m_ProjectPreviewTransition, previewProgressTarget, ImGui::GetIO().DeltaTime, kPreviewMotionSpeed);
    const float previewAlpha = ImGuiExtras::EaseOutCubic(m_ProjectPreviewTransition);
    const float closeProgress = std::clamp(1.0f - m_ProjectPreviewTransition, 0.0f, 1.0f);
    const float delayedContentClose = std::clamp((closeProgress - 0.34f) / 0.66f, 0.0f, 1.0f);
    const float closeContentAmount = ImGuiExtras::EaseOutCubic(delayedContentClose);

    if ((m_ProjectPreviewClosing || !m_PreviewProject) && m_ProjectPreviewTransition <= 0.01f) {
        LibraryManager::Get().CancelProjectPreviewRequests();
        m_PreviewProject = nullptr;
        m_PreviewAsset = nullptr;
        m_RenameTargetFileName.clear();
        m_ProjectPreviewClosing = false;
        m_ProjectPreviewMenuHover = 0.0f;
        m_ProjectPreviewLaunchRect.valid = false;
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
    ImGui::Begin("Library Project Preview", nullptr, flags);

    const bool previewReady = (m_PreviewProject->sourcePreviewTex != 0 && m_PreviewProject->fullPreviewTex != 0);
    const bool previewBusy = Async::IsBusy(m_PreviewProject->previewTaskState);
    const bool renderProject = IsRenderProject(*m_PreviewProject);
    const bool compositeProject = IsCompositeProject(*m_PreviewProject);

    const float contentProgress = ImGuiExtras::EaseOutCubic(previewAlpha);
    const float contentVisibility = m_ProjectPreviewClosing
        ? std::clamp(1.0f - closeContentAmount, 0.0f, 1.0f)
        : std::clamp((contentProgress - 0.08f) / 0.92f, 0.0f, 1.0f);
    const float contentScale = m_ProjectPreviewClosing
        ? (1.0f - (0.045f * closeContentAmount))
        : (0.972f + (0.028f * contentProgress));
    const ImVec2 baseContentSize(
        std::min(viewport->Size.x * 0.76f, 1260.0f),
        std::min(viewport->Size.y * 0.70f, 820.0f));
    const ImVec2 contentSize(baseContentSize.x * contentScale, baseContentSize.y * contentScale);
    const float detailsCollapsedWidth = 52.0f;
    const float detailsExpandedWidth = 348.0f;
    const float detailsWidth =
        detailsCollapsedWidth + ((detailsExpandedWidth - detailsCollapsedWidth) * m_ProjectPreviewMenuHover);
    const float gap = 18.0f;
    const ImVec2 contentMin(
        viewport->Pos.x + (viewport->Size.x - contentSize.x) * 0.5f,
        viewport->Pos.y + (viewport->Size.y - contentSize.y) * 0.5f + ((1.0f - contentProgress) * 14.0f) + (closeContentAmount * 8.0f));
    const ImVec2 previewArea(std::max(360.0f, contentSize.x - detailsWidth - gap), contentSize.y);
    const ImVec2 sideArea(detailsWidth, contentSize.y);

    const ImVec2 fittedImageSize = FitImageToBounds(
        static_cast<float>(m_PreviewProject->sourceWidth),
        static_cast<float>(m_PreviewProject->sourceHeight),
        previewArea);
    const ImRect finalImageRect(
        ImVec2(
            contentMin.x + std::max(0.0f, (previewArea.x - fittedImageSize.x) * 0.5f),
            contentMin.y + std::max(0.0f, (previewArea.y - fittedImageSize.y) * 0.5f)),
        ImVec2(
            contentMin.x + std::max(0.0f, (previewArea.x - fittedImageSize.x) * 0.5f) + fittedImageSize.x,
            contentMin.y + std::max(0.0f, (previewArea.y - fittedImageSize.y) * 0.5f) + fittedImageSize.y));
    const ImRect detailsRect(
        ImVec2(contentMin.x + previewArea.x + gap, contentMin.y),
        ImVec2(contentMin.x + previewArea.x + gap + sideArea.x, contentMin.y + sideArea.y));

    const bool canCloseFromBackdrop = contentProgress >= 0.92f;
    const bool clickedBackdrop =
        canCloseFromBackdrop &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !m_ProjectPreviewClosing &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
        !finalImageRect.Contains(ImGui::GetIO().MousePos) &&
        !detailsRect.Contains(ImGui::GetIO().MousePos);
    if (clickedBackdrop) {
        m_ProjectPreviewClosing = true;
    }

    if (!m_ProjectPreviewClosing && m_ProjectPreviewLaunchRect.valid) {
        const float launchBlend = std::clamp(1.0f - contentProgress, 0.0f, 1.0f);
        if (launchBlend > 0.01f) {
            const ImRect launchRect(
                ImVec2(m_ProjectPreviewLaunchRect.minX, m_ProjectPreviewLaunchRect.minY),
                ImVec2(m_ProjectPreviewLaunchRect.maxX, m_ProjectPreviewLaunchRect.maxY));
            const auto lerp = [](float a, float b, float t) { return a + ((b - a) * t); };
            const ImRect animatedRect(
                ImVec2(lerp(launchRect.Min.x, finalImageRect.Min.x, contentProgress), lerp(launchRect.Min.y, finalImageRect.Min.y, contentProgress)),
                ImVec2(lerp(launchRect.Max.x, finalImageRect.Max.x, contentProgress), lerp(launchRect.Max.y, finalImageRect.Max.y, contentProgress)));
            const ImTextureID animTex = previewReady
                ? (ImTextureID)(intptr_t)m_PreviewProject->fullPreviewTex
                : (ImTextureID)(intptr_t)m_PreviewProject->thumbnailTex;
            if (animTex) {
                ImGui::GetWindowDrawList()->AddImageRounded(
                    animTex,
                    animatedRect.Min,
                    animatedRect.Max,
                    ImVec2(0, 1),
                    ImVec2(1, 0),
                    IM_COL32(255, 255, 255, static_cast<int>(255.0f * launchBlend)),
                    18.0f);
            }
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, contentVisibility);
    if (previewReady) {
        ImGui::SetCursorPos(ImVec2(
            finalImageRect.Min.x - viewport->Pos.x,
            finalImageRect.Min.y - viewport->Pos.y));
        if (renderProject || compositeProject) {
            ImGui::InvisibleButton("##PreviewImageHit", fittedImageSize);
            const ImRect imageRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImGui::GetWindowDrawList()->AddImageRounded(
                (ImTextureID)(intptr_t)m_PreviewProject->fullPreviewTex,
                imageRect.Min,
                imageRect.Max,
                ImVec2(0, 1),
                ImVec2(1, 0),
                IM_COL32(255, 255, 255, static_cast<int>(255.0f * contentVisibility)),
                18.0f);
        } else {
            DrawComparePreview(*m_PreviewProject, fittedImageSize, m_CompareSplit, 18.0f, false, contentVisibility);
        }
    } else {
        const ImVec2 spinnerPos(
            contentMin.x + std::max(0.0f, (previewArea.x - 140.0f) * 0.5f),
            contentMin.y + std::max(0.0f, (previewArea.y - 70.0f) * 0.44f));
        ImGui::SetCursorPos(ImVec2(spinnerPos.x - viewport->Pos.x, spinnerPos.y - viewport->Pos.y));
        const char* spinnerLabel = m_PreviewProject->previewStatusText.empty()
            ? "Rendering preview..."
            : m_PreviewProject->previewStatusText.c_str();
        ImGuiExtras::DrawSpinner(spinnerLabel, 18.0f, 4, IM_COL32(220, 220, 220, 240));
    }
    ImGui::SetCursorPos(ImVec2(
        contentMin.x + previewArea.x + gap - viewport->Pos.x,
        contentMin.y - viewport->Pos.y));
    ImGui::BeginChild("LibraryPreviewDetails", sideArea, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetWindowPos(),
        ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
        IM_COL32(255, 255, 255, 10),
        18.0f);
    const bool anyPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const bool detailsHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || anyPopupOpen;
    m_ProjectPreviewMenuHover = ImGuiExtras::AnimateTowards(
        m_ProjectPreviewMenuHover,
        detailsHovered ? 1.0f : 0.0f,
        ImGui::GetIO().DeltaTime,
        11.0f);
    const bool detailsExpanded = m_ProjectPreviewMenuHover > 0.30f;

    if (!detailsExpanded) {
        const ImVec2 panelPos = ImGui::GetWindowPos();
        const ImVec2 panelSize = ImGui::GetWindowSize();
        const ImVec2 center(panelPos.x + panelSize.x * 0.5f, panelPos.y + panelSize.y * 0.5f);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(center.x - 5.0f, center.y - 12.0f),
            IM_COL32(235, 239, 245, 210),
            "<");
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::End();

        if (m_ProjectPreviewClosing && m_ProjectPreviewTransition <= 0.01f) {
            LibraryManager::Get().CancelProjectPreviewRequests();
            m_PreviewProject = nullptr;
            m_PreviewAsset = nullptr;
            m_RenameTargetFileName.clear();
            m_ProjectPreviewClosing = false;
            m_ProjectPreviewMenuHover = 0.0f;
            m_ProjectPreviewLaunchRect.valid = false;
            if (m_ProjectPreviewRefreshAfterClose) {
                LibraryManager::Get().RefreshLibrary();
                m_ProjectPreviewRefreshAfterClose = false;
            }
        }
        return;
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::Indent(18.0f);
    const float panelWrapX = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - 34.0f;
    const float contentWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x - 28.0f);

    ImGui::PushTextWrapPos(panelWrapX);
    ImGui::TextWrapped("%s", m_PreviewProject->projectName.c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("CREATED");
    ImGui::SameLine(0.0f, 20.0f);
    ImGui::TextDisabled("%s", m_PreviewProject->timestamp.c_str());
    ImGui::TextDisabled("SIZE");
    ImGui::SameLine(0.0f, 38.0f);
    ImGui::TextDisabled("%dx%d", m_PreviewProject->sourceWidth, m_PreviewProject->sourceHeight);
    ImGui::TextDisabled("FILE");
    ImGui::Spacing();
    ImGui::TextWrapped("%s", m_PreviewProject->fileName.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushItemWidth(contentWidth);
    ImGui::InputText("##renameproject", m_RenameBuffer, sizeof(m_RenameBuffer));
    ImGui::PopItemWidth();
    ImGui::Spacing();

    const bool projectLoadBusy = Async::IsBusy(LibraryManager::Get().GetProjectLoadTaskState());
    const float actionWidth = std::max(108.0f, (contentWidth - 12.0f) * 0.5f);
    const ImVec2 actionSize(actionWidth, 34.0f);

    if (ImGui::Button("Save Name", actionSize)) {
        if (LibraryManager::Get().RenameProject(m_PreviewProject->fileName, m_RenameBuffer)) {
            m_RenameTargetFileName.clear();
            SyncRenameBuffer();
        }
    }
    ImGui::SameLine(0.0f, 10.0f);
    if (ImGui::Button("Close Preview", actionSize)) {
        m_ProjectPreviewClosing = true;
    }

    ImGui::BeginDisabled(
        m_ProjectPreviewClosing || projectLoadBusy || previewBusy
        || renderProject
        || compositeProject);
    if (ImGui::Button(
            projectLoadBusy
                ? "Loading Project..."
                : ((renderProject || compositeProject) ? "Load Unsupported" : "Load Into Editor"),
            actionSize)) {
        const std::string projectFileName = m_PreviewProject->fileName;
        if (!renderProject && !compositeProject) {
            if (editor != nullptr && editor->IsDirty()) {
                m_PendingLoadProjectFileName = projectFileName;
                m_PendingLoadTarget = PendingLoadTarget::Editor;
                m_ConfirmLoadOpen = true;
            } else {
                m_ProjectPreviewClosing = true;
                m_ProjectPreviewRefreshAfterClose = false;
                if (onLoadEditorProject) {
                    onLoadEditorProject(projectFileName);
                } else {
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
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, 10.0f);
    if (ImGui::Button("Delete Project...", actionSize)) {
        ImGui::OpenPopup("Confirm Delete Project");
    }

    ImGui::Spacing();
    if (ImGui::Button("Export Project...", ImVec2(contentWidth, 34.0f))) {
        std::string exportPath = FileDialogs::SaveProjectFileDialog("Export Project File", m_PreviewProject->fileName.c_str());
        if (!exportPath.empty()) {
            LibraryManager::Get().ExportProject(m_PreviewProject->fileName, exportPath);
        }
    }

    if (compositeProject) {
        ImGui::Spacing();
        ImGui::TextDisabled("Legacy standalone composite projects are no longer supported.");
    }

    if (!LibraryManager::Get().GetProjectLoadStatusText().empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", LibraryManager::Get().GetProjectLoadStatusText().c_str());
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

    ImGui::Unindent(12.0f);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();

    if (m_ProjectPreviewClosing && m_ProjectPreviewTransition <= 0.01f) {
        LibraryManager::Get().CancelProjectPreviewRequests();
        m_PreviewProject = nullptr;
        m_PreviewAsset = nullptr;
        m_RenameTargetFileName.clear();
        m_ProjectPreviewClosing = false;
        m_ProjectPreviewMenuHover = 0.0f;
        m_ProjectPreviewLaunchRect.valid = false;
        if (m_ProjectPreviewRefreshAfterClose) {
            LibraryManager::Get().RefreshLibrary();
            m_ProjectPreviewRefreshAfterClose = false;
        }
    }
}

void LibraryModule::RenderAssetPreviewPopup(
    EditorModule* editor,
    CompositeModule* composite,
    int* activeTab,
    const std::function<void(const std::string&)>& onLoadEditorProject) {
    if (!m_PreviewAsset && m_AssetPreviewTransition <= 0.0f) {
        return;
    }

    const float previewProgressTarget = (m_PreviewAsset && !m_AssetPreviewClosing) ? 1.0f : 0.0f;
    m_AssetPreviewTransition = ImGuiExtras::AnimateTowards(m_AssetPreviewTransition, previewProgressTarget, ImGui::GetIO().DeltaTime, kPreviewMotionSpeed);
    const float previewAlpha = ImGuiExtras::EaseOutCubic(m_AssetPreviewTransition);
    const float closeProgress = std::clamp(1.0f - m_AssetPreviewTransition, 0.0f, 1.0f);
    const float delayedContentClose = std::clamp((closeProgress - 0.34f) / 0.66f, 0.0f, 1.0f);
    const float closeContentAmount = ImGuiExtras::EaseOutCubic(delayedContentClose);

    if ((m_AssetPreviewClosing || !m_PreviewAsset) && m_AssetPreviewTransition <= 0.01f) {
        LibraryManager::Get().CancelAssetPreviewRequests();
        m_PreviewAsset = nullptr;
        m_PreviewProject = nullptr;
        m_AssetPreviewClosing = false;
        m_AssetPreviewMenuHover = 0.0f;
        m_AssetPreviewLaunchRect.valid = false;
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
    ImGui::Begin("Library Asset Preview", nullptr, flags);

    const bool previewReady = (m_PreviewAsset->fullPreviewTex != 0);
    const bool previewBusy = Async::IsBusy(m_PreviewAsset->previewTaskState);
    const bool projectLoadBusy = Async::IsBusy(LibraryManager::Get().GetProjectLoadTaskState());
    const bool renderLinkedProject = m_PreviewAsset->projectKind == StackBinaryFormat::kRenderProjectKind;
    const bool compositeLinkedProject = m_PreviewAsset->projectKind == StackBinaryFormat::kCompositeProjectKind;
    const float contentProgress = ImGuiExtras::EaseOutCubic(previewAlpha);
    const float contentVisibility = m_AssetPreviewClosing
        ? std::clamp(1.0f - closeContentAmount, 0.0f, 1.0f)
        : std::clamp((contentProgress - 0.08f) / 0.92f, 0.0f, 1.0f);
    const float contentScale = m_AssetPreviewClosing
        ? (1.0f - (0.045f * closeContentAmount))
        : (0.972f + (0.028f * contentProgress));
    const ImVec2 baseContentSize(
        std::min(viewport->Size.x * 0.76f, 1260.0f),
        std::min(viewport->Size.y * 0.70f, 820.0f));
    const ImVec2 contentSize(baseContentSize.x * contentScale, baseContentSize.y * contentScale);
    const float detailsCollapsedWidth = 52.0f;
    const float detailsExpandedWidth = 348.0f;
    const float detailsWidth =
        detailsCollapsedWidth + ((detailsExpandedWidth - detailsCollapsedWidth) * m_AssetPreviewMenuHover);
    const float gap = 18.0f;
    const ImVec2 contentMin(
        viewport->Pos.x + (viewport->Size.x - contentSize.x) * 0.5f,
        viewport->Pos.y + (viewport->Size.y - contentSize.y) * 0.5f + ((1.0f - contentProgress) * 14.0f) + (closeContentAmount * 8.0f));
    const ImVec2 previewArea(std::max(360.0f, contentSize.x - detailsWidth - gap), contentSize.y);
    const ImVec2 sideArea(detailsWidth, contentSize.y);

    const ImVec2 fittedImageSize = FitImageToBounds(
        static_cast<float>(m_PreviewAsset->width),
        static_cast<float>(m_PreviewAsset->height),
        previewArea);
    const ImRect finalImageRect(
        ImVec2(
            contentMin.x + std::max(0.0f, (previewArea.x - fittedImageSize.x) * 0.5f),
            contentMin.y + std::max(0.0f, (previewArea.y - fittedImageSize.y) * 0.5f)),
        ImVec2(
            contentMin.x + std::max(0.0f, (previewArea.x - fittedImageSize.x) * 0.5f) + fittedImageSize.x,
            contentMin.y + std::max(0.0f, (previewArea.y - fittedImageSize.y) * 0.5f) + fittedImageSize.y));
    const ImRect detailsRect(
        ImVec2(contentMin.x + previewArea.x + gap, contentMin.y),
        ImVec2(contentMin.x + previewArea.x + gap + sideArea.x, contentMin.y + sideArea.y));

    const bool canCloseFromBackdrop = contentProgress >= 0.92f;
    const bool clickedBackdrop =
        canCloseFromBackdrop &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !m_AssetPreviewClosing &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
        !finalImageRect.Contains(ImGui::GetIO().MousePos) &&
        !detailsRect.Contains(ImGui::GetIO().MousePos);
    if (clickedBackdrop) {
        m_AssetPreviewClosing = true;
    }

    if (!m_AssetPreviewClosing && m_AssetPreviewLaunchRect.valid) {
        const float launchBlend = std::clamp(1.0f - contentProgress, 0.0f, 1.0f);
        if (launchBlend > 0.01f) {
            const ImRect launchRect(
                ImVec2(m_AssetPreviewLaunchRect.minX, m_AssetPreviewLaunchRect.minY),
                ImVec2(m_AssetPreviewLaunchRect.maxX, m_AssetPreviewLaunchRect.maxY));
            const auto lerp = [](float a, float b, float t) { return a + ((b - a) * t); };
            const ImRect animatedRect(
                ImVec2(lerp(launchRect.Min.x, finalImageRect.Min.x, contentProgress), lerp(launchRect.Min.y, finalImageRect.Min.y, contentProgress)),
                ImVec2(lerp(launchRect.Max.x, finalImageRect.Max.x, contentProgress), lerp(launchRect.Max.y, finalImageRect.Max.y, contentProgress)));
            const ImTextureID animTex = previewReady
                ? (ImTextureID)(intptr_t)m_PreviewAsset->fullPreviewTex
                : (ImTextureID)(intptr_t)m_PreviewAsset->thumbnailTex;
            if (animTex) {
                ImGui::GetWindowDrawList()->AddImageRounded(
                    animTex,
                    animatedRect.Min,
                    animatedRect.Max,
                    ImVec2(0, 1),
                    ImVec2(1, 0),
                    IM_COL32(255, 255, 255, static_cast<int>(255.0f * launchBlend)),
                    18.0f);
            }
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, contentVisibility);
    if (previewReady) {
        ImGui::SetCursorPos(ImVec2(
            finalImageRect.Min.x - viewport->Pos.x,
            finalImageRect.Min.y - viewport->Pos.y));
        ImGui::InvisibleButton("##AssetPreviewImageHit", fittedImageSize);
        const ImRect imageRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::GetWindowDrawList()->AddImageRounded(
            (ImTextureID)(intptr_t)m_PreviewAsset->fullPreviewTex,
            imageRect.Min,
            imageRect.Max,
            ImVec2(0, 1),
            ImVec2(1, 0),
            IM_COL32(255, 255, 255, static_cast<int>(255.0f * contentVisibility)),
            18.0f);
    } else {
        const ImVec2 spinnerPos(
            contentMin.x + std::max(0.0f, (previewArea.x - 140.0f) * 0.5f),
            contentMin.y + std::max(0.0f, (previewArea.y - 70.0f) * 0.44f));
        ImGui::SetCursorPos(ImVec2(spinnerPos.x - viewport->Pos.x, spinnerPos.y - viewport->Pos.y));
        const char* spinnerLabel = m_PreviewAsset->previewStatusText.empty()
            ? "Loading full-quality asset..."
            : m_PreviewAsset->previewStatusText.c_str();
        ImGuiExtras::DrawSpinner(spinnerLabel, 18.0f, 4, IM_COL32(220, 220, 220, 240));
    }

    ImGui::SetCursorPos(ImVec2(
        contentMin.x + previewArea.x + gap - viewport->Pos.x,
        contentMin.y - viewport->Pos.y));
    ImGui::BeginChild("LibraryAssetDetails", sideArea, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImGui::GetWindowPos(),
        ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y),
        IM_COL32(255, 255, 255, 10),
        18.0f);
    const bool anyPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const bool detailsHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || anyPopupOpen;
    m_AssetPreviewMenuHover = ImGuiExtras::AnimateTowards(
        m_AssetPreviewMenuHover,
        detailsHovered ? 1.0f : 0.0f,
        ImGui::GetIO().DeltaTime,
        11.0f);
    const bool detailsExpanded = m_AssetPreviewMenuHover > 0.30f;

    if (!detailsExpanded) {
        const ImVec2 panelPos = ImGui::GetWindowPos();
        const ImVec2 panelSize = ImGui::GetWindowSize();
        const ImVec2 center(panelPos.x + panelSize.x * 0.5f, panelPos.y + panelSize.y * 0.5f);
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(center.x - 5.0f, center.y - 12.0f),
            IM_COL32(235, 239, 245, 210),
            "<");
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::End();

        if (m_AssetPreviewClosing && m_AssetPreviewTransition <= 0.01f) {
            LibraryManager::Get().CancelAssetPreviewRequests();
            m_PreviewAsset = nullptr;
            m_PreviewProject = nullptr;
            m_AssetPreviewClosing = false;
            m_AssetPreviewMenuHover = 0.0f;
            m_AssetPreviewLaunchRect.valid = false;
        }
        return;
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::Indent(18.0f);
    const float panelWrapX = ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - 34.0f;
    const float contentWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x - 28.0f);

    ImGui::PushTextWrapPos(panelWrapX);
    ImGui::TextWrapped("%s", m_PreviewAsset->displayName.c_str());
    ImGui::Spacing();
    ImGui::TextDisabled("CREATED");
    ImGui::SameLine(0.0f, 20.0f);
    ImGui::TextDisabled("%s", m_PreviewAsset->timestamp.c_str());
    ImGui::TextDisabled("SIZE");
    ImGui::SameLine(0.0f, 38.0f);
    ImGui::TextDisabled("%dx%d", m_PreviewAsset->width, m_PreviewAsset->height);
    ImGui::TextDisabled("FILE");
    ImGui::Spacing();
    ImGui::TextWrapped("%s", m_PreviewAsset->fileName.c_str());
    if (!m_PreviewAsset->projectName.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("LINKED");
        ImGui::Spacing();
        ImGui::TextWrapped("%s", m_PreviewAsset->projectName.c_str());
    } else if (!m_PreviewAsset->projectFileName.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("LINKED FILE");
        ImGui::Spacing();
        ImGui::TextWrapped("%s", m_PreviewAsset->projectFileName.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const float actionWidth = std::max(108.0f, (contentWidth - 12.0f) * 0.5f);
    const ImVec2 actionSize(actionWidth, 34.0f);
    if (ImGui::Button("Download Asset", actionSize)) {
        std::string exportPath = FileDialogs::SavePngFileDialog("Download Library Asset", m_PreviewAsset->fileName.c_str());
        if (!exportPath.empty()) {
            LibraryManager::Get().ExportAsset(m_PreviewAsset->fileName, exportPath);
        }
    }
    ImGui::SameLine(0.0f, 10.0f);
    if (ImGui::Button("Close Preview", actionSize)) {
        m_AssetPreviewClosing = true;
    }

    ImGui::BeginDisabled(
        m_AssetPreviewClosing || projectLoadBusy || previewBusy || m_PreviewAsset->projectFileName.empty()
        || renderLinkedProject
        || compositeLinkedProject);
    if (ImGui::Button(
            projectLoadBusy
                ? "Loading Project..."
                : ((renderLinkedProject || compositeLinkedProject) ? "Load Unsupported" : "Load Into Editor"),
            ImVec2(contentWidth, 34.0f))) {
        const std::string projectFileName = m_PreviewAsset->projectFileName;
        if (!renderLinkedProject && !compositeLinkedProject) {
            if (editor != nullptr && editor->IsDirty()) {
                m_PendingLoadProjectFileName = projectFileName;
                m_PendingLoadTarget = PendingLoadTarget::Editor;
                m_ConfirmLoadOpen = true;
            } else {
                m_AssetPreviewClosing = true;
                if (onLoadEditorProject) {
                    onLoadEditorProject(projectFileName);
                } else {
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
    }
    ImGui::EndDisabled();
    if (compositeLinkedProject) {
        ImGui::Spacing();
        ImGui::TextDisabled("Linked legacy composite projects are no longer supported.");
    } else if (m_PreviewAsset->projectFileName.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No linked project metadata was found for this asset.");
    }

    if (!LibraryManager::Get().GetProjectLoadStatusText().empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", LibraryManager::Get().GetProjectLoadStatusText().c_str());
    }

    ImGui::Unindent(18.0f);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::End();

    if (m_AssetPreviewClosing && m_AssetPreviewTransition <= 0.01f) {
        LibraryManager::Get().CancelAssetPreviewRequests();
        m_PreviewAsset = nullptr;
        m_PreviewProject = nullptr;
        m_AssetPreviewClosing = false;
        m_AssetPreviewMenuHover = 0.0f;
        m_AssetPreviewLaunchRect.valid = false;
    }
}



void LibraryModule::RenderGlobalPopups() {
    RenderConfirmLoadPopup(m_OnLoadEditorProject);
    RenderFolderImportPopup();
    RenderImportConflictPopup();
    RenderAssetConflictPopup();
    RenderDeleteConfirmPopup();
}

void LibraryModule::DismissPreviewsForProjectLoad() {
    LibraryManager::Get().CancelProjectPreviewRequests();
    LibraryManager::Get().CancelAssetPreviewRequests();
    m_PreviewProject = nullptr;
    m_PreviewAsset = nullptr;
    m_ProjectPreviewTransition = 0.0f;
    m_ProjectPreviewClosing = false;
    m_ProjectPreviewRefreshAfterClose = false;
    m_AssetPreviewTransition = 0.0f;
    m_AssetPreviewClosing = false;
    m_ProjectPreviewMenuHover = 0.0f;
    m_AssetPreviewMenuHover = 0.0f;
    m_ProjectPreviewLaunchRect.valid = false;
    m_AssetPreviewLaunchRect.valid = false;
    m_RenameTargetFileName.clear();
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

void LibraryModule::RenderConfirmLoadPopup(const std::function<void(const std::string&)>& onLoadEditorProject) {
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
                        m_CachedEditor->GetCurrentProjectFileName(),
                        [this](bool success) {
                            if (!success || !m_CachedEditor) {
                                return;
                            }
                            if (m_PreviewProject) {
                                m_ProjectPreviewClosing = true;
                                m_ProjectPreviewRefreshAfterClose = false;
                            }
                            if (m_PreviewAsset) {
                                m_AssetPreviewClosing = true;
                            }
                            if (m_OnLoadEditorProject) {
                                m_OnLoadEditorProject(m_PendingLoadProjectFileName);
                            } else {
                                LibraryManager::Get().RequestLoadProject(m_PendingLoadProjectFileName, m_CachedEditor, [this](bool loadSuccess) {
                                    if (loadSuccess && m_CachedActiveTab) *m_CachedActiveTab = 1;
                                });
                            }
                        });
                }
            } else if (m_PendingLoadTarget == PendingLoadTarget::Composite && m_CachedComposite) {
                if (m_CachedComposite->GetCurrentProjectFileName().empty()) {
                    needsName = true;
                } else {
                    LibraryManager::Get().RequestSaveCompositeProject(
                        m_CachedComposite->GetCurrentProjectName(),
                        m_CachedComposite,
                        m_CachedComposite->GetCurrentProjectFileName(),
                        [this](bool success) {
                            if (!success || !m_CachedComposite) {
                                return;
                            }
                            if (m_PreviewProject) {
                                m_ProjectPreviewClosing = true;
                                m_ProjectPreviewRefreshAfterClose = false;
                            }
                            if (m_PreviewAsset) {
                                m_AssetPreviewClosing = true;
                            }
                            LibraryManager::Get().RequestLoadCompositeProject(m_PendingLoadProjectFileName, m_CachedComposite, [this](bool loadSuccess) {
                                if (loadSuccess && m_CachedActiveTab) *m_CachedActiveTab = 2;
                            });
                        });
                }
            }

            if (needsName) {
                m_SaveNamePromptOpen = true;
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
                if (onLoadEditorProject) {
                    onLoadEditorProject(m_PendingLoadProjectFileName);
                } else {
                    LibraryManager::Get().RequestLoadProject(m_PendingLoadProjectFileName, m_CachedEditor, [this](bool success) {
                        if (success && m_CachedActiveTab) *m_CachedActiveTab = 1;
                    });
                }
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
                LibraryManager::Get().RequestSaveProject(newName, m_CachedEditor, "", [this](bool success) {
                    if (!success || !m_CachedEditor) {
                        return;
                    }
                    if (m_PreviewProject) {
                        m_ProjectPreviewClosing = true;
                        m_ProjectPreviewRefreshAfterClose = false;
                    }
                    if (m_PreviewAsset) {
                        m_AssetPreviewClosing = true;
                    }
                    if (m_OnLoadEditorProject) {
                        m_OnLoadEditorProject(m_PendingLoadProjectFileName);
                    } else {
                        LibraryManager::Get().RequestLoadProject(m_PendingLoadProjectFileName, m_CachedEditor, [this](bool loadSuccess) {
                            if (loadSuccess && m_CachedActiveTab) *m_CachedActiveTab = 1;
                        });
                    }
                });
            } else if (m_PendingLoadTarget == PendingLoadTarget::Composite && m_CachedComposite) {
                LibraryManager::Get().RequestSaveCompositeProject(newName, m_CachedComposite, "", [this](bool success) {
                    if (!success || !m_CachedComposite) {
                        return;
                    }
                    if (m_PreviewProject) {
                        m_ProjectPreviewClosing = true;
                        m_ProjectPreviewRefreshAfterClose = false;
                    }
                    if (m_PreviewAsset) {
                        m_AssetPreviewClosing = true;
                    }
                    LibraryManager::Get().RequestLoadCompositeProject(m_PendingLoadProjectFileName, m_CachedComposite, [this](bool loadSuccess) {
                        if (loadSuccess && m_CachedActiveTab) *m_CachedActiveTab = 2;
                    });
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
