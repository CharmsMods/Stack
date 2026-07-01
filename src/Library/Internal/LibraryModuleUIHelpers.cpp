#include "Library/Internal/LibraryModuleUIHelpers.h"

#include "Library/TagManager.h"
#include "Persistence/StackBinaryFormat.h"
#include "Renderer/GLHelpers.h"
#include "ThirdParty/stb_image.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <unordered_map>

namespace Stack::Library::ModuleUI {
namespace {

struct SplitHandleMotionState {
    float hover = 0.0f;
    int lastSeenFrame = 0;
};

void PruneSplitHandleMotionStates(std::unordered_map<ImGuiID, SplitHandleMotionState>& states, int frameCount) {
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

} // namespace

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

void ComputeCoverUv(float sourceWidth, float sourceHeight, const ImRect& targetRect, ImVec2& outUv0, ImVec2& outUv1) {
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

void DrawSplitHandle(ImDrawList* drawList, const ImRect& rect, float split, ImGuiID handleId, bool hovered, bool active, float alpha) {
    static std::unordered_map<ImGuiID, SplitHandleMotionState> s_SplitHandleMotion;
    SplitHandleMotionState& motion = s_SplitHandleMotion[handleId];
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

    if (!filter || !filter[0]) return true;

    std::string needle = filter;
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::string haystack = project.projectName + " " + project.fileName;
    std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return haystack.find(needle) != std::string::npos;
}

bool AssetMatchesFilter(const AssetEntry& asset, const char* filter, const std::unordered_set<std::string>& activeTags, bool noTagOnly) {
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

std::uint64_t HashCombine(std::uint64_t seed, std::uint64_t value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
}

std::uint64_t HashString(const std::string& value) {
    return static_cast<std::uint64_t>(std::hash<std::string>{}(value));
}

std::string BuildTagFilterKey(const std::unordered_set<std::string>& activeTags) {
    std::vector<std::string> tags(activeTags.begin(), activeTags.end());
    std::sort(tags.begin(), tags.end());

    std::string result;
    for (const std::string& tag : tags) {
        result += tag;
        result.push_back('|');
    }
    return result;
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

void DrawComparePreview(
    const ProjectEntry& project,
    const ImVec2& requestedSize,
    float& split,
    float rounding,
    bool showLabels,
    float alpha) {
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

} // namespace Stack::Library::ModuleUI
