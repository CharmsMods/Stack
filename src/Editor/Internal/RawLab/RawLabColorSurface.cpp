#include "Editor/Internal/RawLab/RawLabColorProjection.h"
#include "Editor/Internal/RawLab/RawLabColorSurface.h"

#include "Editor/Internal/RawLab/RawLabColorCloudRenderer.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include "Renderer/GLHelpers.h"
#include "Utils/ImGuiExtras.h"

#include <GLFW/glfw3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef GL_ARRAY_BUFFER_BINDING
#define GL_ARRAY_BUFFER_BINDING 0x8894
#endif

namespace Stack::Editor::RawLabInternal {

namespace {
void HashRawLabColorCloudValue(std::size_t& seed, std::size_t value) {
    seed ^= value + static_cast<std::size_t>(0x9e3779b9u) +
        (seed << 6u) + (seed >> 2u);
}

int NormalizeRawLabRotationDegrees(int rotationDegrees) {
    int normalized = rotationDegrees % 360;
    if (normalized < 0) {
        normalized += 360;
    }
    return normalized;
}

void DrawRawLabDashedCircle(
    ImDrawList* drawList,
    const ImVec2& center,
    float radius,
    ImU32 color,
    float thickness = 1.0f,
    int segmentCount = 64) {
    if (drawList == nullptr || radius <= 0.5f || segmentCount < 4) {
        return;
    }
    constexpr float twoPi = 6.28318530717958647692f;
    for (int segment = 0; segment < segmentCount; segment += 2) {
        const float angle0 = twoPi * static_cast<float>(segment) /
            static_cast<float>(segmentCount);
        const float angle1 = twoPi * static_cast<float>(segment + 1) /
            static_cast<float>(segmentCount);
        drawList->AddLine(
            ImVec2(
                center.x + std::cos(angle0) * radius,
                center.y + std::sin(angle0) * radius),
            ImVec2(
                center.x + std::cos(angle1) * radius,
                center.y + std::sin(angle1) * radius),
            color,
            thickness);
    }
}

void DrawRawLabArrow(
    ImDrawList* drawList,
    const ImVec2& source,
    const ImVec2& target,
    ImU32 color,
    float thickness) {
    if (drawList == nullptr) {
        return;
    }
    const float dx = target.x - source.x;
    const float dy = target.y - source.y;
    const float length = std::hypot(dx, dy);
    if (length <= 1.0f) {
        return;
    }
    const float ux = dx / length;
    const float uy = dy / length;
    const float arrowSize = std::clamp(length * 0.18f, 5.0f, 9.0f);
    const ImVec2 tip(
        target.x - ux * 6.0f,
        target.y - uy * 6.0f);
    drawList->AddLine(source, tip, color, thickness);
    const ImVec2 normal(-uy, ux);
    drawList->AddTriangleFilled(
        tip,
        ImVec2(
            tip.x - ux * arrowSize + normal.x * arrowSize * 0.45f,
            tip.y - uy * arrowSize + normal.y * arrowSize * 0.45f),
        ImVec2(
            tip.x - ux * arrowSize - normal.x * arrowSize * 0.45f,
            tip.y - uy * arrowSize - normal.y * arrowSize * 0.45f),
        color);
}

using Stack::Editor::RawLabInternal::ProjectColorDisc;
using Stack::Editor::RawLabInternal::UnprojectColorDisc;

struct RawLabColorCanvasTransform {
    ImVec2 minimum;
    float width = 1.0f;
    float height = 1.0f;
    float centerA = 0.0f;
    float centerB = 0.0f;
    float halfWidth = 1.0f;
    float halfHeight = 1.0f;
    float discExtent = 1.0f;

    ImVec2 ToScreen(float a, float b) const {
        const auto projected = ProjectColorDisc(a, b);
        return DisplayToScreen(projected.x, projected.y);
    }
    ImVec2 DisplayToScreen(float a, float b) const {
        return ImVec2(
            minimum.x +
                ((a - centerA) / (2.0f * halfWidth) + 0.5f) * width,
            minimum.y +
                (0.5f - (b - centerB) / (2.0f * halfHeight)) * height);
    }

    ImVec2 FromScreenUnclamped(const ImVec2& point) const {
        const auto display = DisplayFromScreen(point);
        return UnprojectColorDisc(display.x, display.y);
    }
    ImVec2 DisplayFromScreen(const ImVec2& point) const {
        return ImVec2(
            centerA + ((point.x - minimum.x) / width - 0.5f) *
                2.0f * halfWidth,
            centerB + (0.5f - (point.y - minimum.y) / height) *
                2.0f * halfHeight);
    }

    ImVec2 ClampToDisc(const ImVec2& coordinate) const {
        const float radius = std::hypot(coordinate.x, coordinate.y);
        if (radius <= 1.0f || radius <= 0.000001f) {
            return coordinate;
        }
        const float scale = 1.0f / radius;
        return ImVec2(coordinate.x * scale, coordinate.y * scale);
    }

    ImVec2 FromScreen(const ImVec2& point) const {
        return ClampToDisc(FromScreenUnclamped(point));
    }
    ImVec2 MoveByPixels(float a, float b, float dx, float dy) const {
        const auto screen = ToScreen(a, b);
        return FromScreen(ImVec2(screen.x + dx, screen.y + dy));
    }
};

struct RawLabColorPalette {
    Raw::RawWorkingSpace workingSpace;
    float fixedDiscLightness = 0.70f;
    float discExtent = 0.45f;

    ImU32 UiColor(
        const std::array<float, 3>& sceneRgb,
        float alpha) const {
        const auto rgb = Stack::Editor::RawLabInternal::ColorDiscDisplayRgb(sceneRgb, workingSpace);
        const auto display = [](float value) {
            const float compressed = std::max(0.0f, value) /
                (1.0f + std::max(0.0f, value));
            return std::pow(compressed, 1.0f / 2.2f);
        };
        return ImGui::ColorConvertFloat4ToU32(ImVec4(
            display(rgb[0]),
            display(rgb[1]),
            display(rgb[2]),
            alpha));
    }

    ImU32 CoordinateColor(float a, float b, float alpha) const {
        RawRecipe::RawColorWarpCoordinate coordinate;
        coordinate.lightness = fixedDiscLightness;
        coordinate.a = a;
        coordinate.b = b;
        std::array<float, 3> rgb =
            RawRecipe::ColorWarpCoordinateToWorkingRgb(
                coordinate,
                workingSpace);
        const float minimum = std::min({ rgb[0], rgb[1], rgb[2] });
        if (minimum < 0.0f) {
            for (float& channel : rgb) {
                channel -= minimum;
            }
        }
        const float peak = std::max({ rgb[0], rgb[1], rgb[2], 1.0f });
        if (peak > 1.0f) {
            for (float& channel : rgb) {
                channel /= peak;
            }
        }
        return UiColor(rgb, alpha);
    }


};

void RefreshRawLabColorCloud(
    RawLabColorSurfaceArgs& args,
    float coordinateLimit) {
    const std::string& previewIdentity = args.previewIdentity;
    const RawDevelopmentGraphScopeReadback& scope = args.scope;
    const std::size_t scopePixelCount = scope.valid &&
            scope.stage == RawDevelopmentGraphScopeStage::ColorWarpInput &&
            scope.width > 0 && scope.height > 0
        ? static_cast<std::size_t>(scope.width) *
            static_cast<std::size_t>(scope.height)
        : 0u;
    const int cloudWorkingSpace =
        static_cast<int>(args.workingSpace) * 10 + args.colorWarp.version;
    const std::size_t cloudInputFingerprint = args.cloudInputFingerprint;
    auto& colorCloud = args.ui.colorWarpCloud;
    const bool rebuildColorCloud =
        args.ui.colorWarpCloudSourceKey != previewIdentity ||
        args.ui.colorWarpCloudInputFingerprint !=
            cloudInputFingerprint ||
        args.ui.colorWarpCloudWorkingSpace != cloudWorkingSpace ||
        (colorCloud.empty() && scopePixelCount > 0u);
    if (rebuildColorCloud) {
        colorCloud.clear();
        args.ui.colorWarpCloudSourceKey = previewIdentity;
        args.ui.colorWarpCloudInputFingerprint =
            cloudInputFingerprint;
        args.ui.colorWarpCloudWorkingSpace = cloudWorkingSpace;
    }
    if (rebuildColorCloud && scopePixelCount > 0u &&
        scope.pixels.size() >= scopePixelCount * 3u) {
        // Keep a stable, density-aware packet rather than retaining every
        // scope pixel.  The UI and future GPU point-sprite path can consume
        // the same bounded packet without allocating or rebuilding a screen
        // density map on every frame.
        constexpr std::size_t packetBinColumns = 48u;
        constexpr std::size_t packetBinRows = 48u;
        constexpr std::size_t packetBinCount =
            packetBinColumns * packetBinRows;
        constexpr std::size_t maximumPacketSamples = 4096u;
        std::array<std::size_t, packetBinCount> packetCounts {};
        const auto buildCloudPoint = [&](std::size_t index,
                                         Stack::EditorModuleTypes::RawWorkspaceColorCloudPoint& point) {
            const std::size_t offset = index * 3u;
            point.rgb = {
                scope.pixels[offset + 0u],
                scope.pixels[offset + 1u],
                scope.pixels[offset + 2u]
            };
            if (!std::isfinite(point.rgb[0]) ||
                !std::isfinite(point.rgb[1]) ||
                !std::isfinite(point.rgb[2])) {
                return false;
            }
            const RawRecipe::RawColorWarpCoordinate coordinate =
                Stack::RawRecipe::WorkingRgbToColorWarpCoordinate(
                    point.rgb,
                    args.workingSpace);
            if (!std::isfinite(coordinate.a) ||
                !std::isfinite(coordinate.b)) {
                return false;
            }
            point.a = coordinate.a;
            point.b = coordinate.b;
            point.sceneEv = coordinate.sceneEv;
            return std::isfinite(point.sceneEv);
        };
        const auto packetBinForPoint = [&](const auto& point) {
            const float normalizedA = std::clamp(
                (point.a + coordinateLimit) / (2.0f * coordinateLimit),
                0.0f,
                0.999999f);
            const float normalizedB = std::clamp(
                (point.b + coordinateLimit) / (2.0f * coordinateLimit),
                0.0f,
                0.999999f);
            const std::size_t binX = static_cast<std::size_t>(
                normalizedA * static_cast<float>(packetBinColumns));
            const std::size_t binY = static_cast<std::size_t>(
                normalizedB * static_cast<float>(packetBinRows));
            return binY * packetBinColumns + binX;
        };

        Stack::EditorModuleTypes::RawWorkspaceColorCloudPoint candidate;
        for (std::size_t index = 0; index < scopePixelCount; ++index) {
            if (buildCloudPoint(index, candidate)) {
                ++packetCounts[packetBinForPoint(candidate)];
            }
        }

        std::array<std::size_t, packetBinCount> packetQuotas {};
        std::size_t totalQuota = 0u;
        for (std::size_t bin = 0; bin < packetBinCount; ++bin) {
            if (packetCounts[bin] == 0u) {
                continue;
            }
            packetQuotas[bin] = std::min(
                packetCounts[bin],
                std::max<std::size_t>(
                    1u,
                    static_cast<std::size_t>(std::ceil(
                        std::sqrt(static_cast<float>(packetCounts[bin])) *
                        3.0f))));
            totalQuota += packetQuotas[bin];
        }
        if (totalQuota > maximumPacketSamples) {
            const float scale = static_cast<float>(maximumPacketSamples) /
                static_cast<float>(totalQuota);
            totalQuota = 0u;
            for (std::size_t bin = 0; bin < packetBinCount; ++bin) {
                if (packetQuotas[bin] == 0u) {
                    continue;
                }
                packetQuotas[bin] = std::max<std::size_t>(
                    1u,
                    static_cast<std::size_t>(std::floor(
                        static_cast<float>(packetQuotas[bin]) * scale)));
                totalQuota += packetQuotas[bin];
            }
        }

        colorCloud.reserve(std::min(totalQuota, maximumPacketSamples));
        std::array<std::size_t, packetBinCount> packetSeen {};
        std::array<std::size_t, packetBinCount> packetSelected {};
        for (std::size_t index = 0;
             index < scopePixelCount &&
             colorCloud.size() < maximumPacketSamples;
             ++index) {
            if (!buildCloudPoint(index, candidate)) {
                continue;
            }
            const std::size_t bin = packetBinForPoint(candidate);
            const std::size_t seen = ++packetSeen[bin];
            const std::size_t desired =
                (seen * packetQuotas[bin]) / packetCounts[bin];
            if (desired <= packetSelected[bin]) {
                continue;
            }
            ++packetSelected[bin];
            colorCloud.push_back(candidate);
        }
    }
}

float RawLabColorWarpEvForSample(std::size_t index) {
    return RawRecipe::kRawColorWarpEvMinimum +
        static_cast<float>(index) /
            static_cast<float>(RawRecipe::kRawColorWarpEvCurveSampleCount - 1u) *
            (RawRecipe::kRawColorWarpEvMaximum -
             RawRecipe::kRawColorWarpEvMinimum);
}

std::size_t RawLabColorWarpEvSampleIndex(float ev) {
    const float normalized = std::clamp(
        (ev - RawRecipe::kRawColorWarpEvMinimum) /
            (RawRecipe::kRawColorWarpEvMaximum -
             RawRecipe::kRawColorWarpEvMinimum),
        0.0f,
        1.0f);
    return std::min(
        RawRecipe::kRawColorWarpEvCurveSampleCount - 1u,
        static_cast<std::size_t>(std::round(
            normalized * static_cast<float>(
                RawRecipe::kRawColorWarpEvCurveSampleCount - 1u))));
}

float RawLabColorWarpSampleValues(
    const std::vector<float>& values,
    float ev,
    float fallback = 0.0f) {
    if (values.empty()) return fallback;
    const float normalized = std::clamp(
        (ev - RawRecipe::kRawColorWarpEvMinimum) /
            (RawRecipe::kRawColorWarpEvMaximum -
             RawRecipe::kRawColorWarpEvMinimum),
        0.0f,
        1.0f);
    const float position = normalized * static_cast<float>(values.size() - 1u);
    const std::size_t lower = std::min(
        values.size() - 1u,
        static_cast<std::size_t>(std::floor(position)));
    const std::size_t upper = std::min(values.size() - 1u, lower + 1u);
    const float fraction = position - static_cast<float>(lower);
    return values[lower] + (values[upper] - values[lower]) * fraction;
}

float RawLabColorWarpCurveSlope(
    const RawRecipe::RawColorWarpEvCurve& curve,
    float ev) {
    constexpr float step = 0.125f;
    return (RawRecipe::EvaluateColorWarpEvCurve(curve, ev + step) -
            RawRecipe::EvaluateColorWarpEvCurve(curve, ev - step)) /
        (2.0f * step);
}

void RawLabColorWarpBuildSmartComponents(
    const RawRecipe::RawColorWarpEvCurve& curve,
    float leftEv,
    float rightEv,
    float centerEv,
    Stack::EditorModuleTypes::RawWorkspaceColorWarpSmartSelectionState& selection) {
    leftEv = std::clamp(
        leftEv,
        RawRecipe::kRawColorWarpEvMinimum,
        RawRecipe::kRawColorWarpEvMaximum - 0.30f);
    rightEv = std::clamp(
        rightEv,
        leftEv + 0.30f,
        RawRecipe::kRawColorWarpEvMaximum);
    selection.active = true;
    selection.leftEv = leftEv;
    selection.rightEv = rightEv;
    selection.centerEv = std::clamp(centerEv, leftEv, rightEv);
    selection.background.assign(
        RawRecipe::kRawColorWarpEvCurveSampleCount, 0.0f);
    selection.residual.assign(
        RawRecipe::kRawColorWarpEvCurveSampleCount, 0.0f);
    selection.profile.assign(
        RawRecipe::kRawColorWarpEvCurveSampleCount, 0.0f);

    const float leftValue = RawRecipe::EvaluateColorWarpEvCurve(curve, leftEv);
    const float rightValue = RawRecipe::EvaluateColorWarpEvCurve(curve, rightEv);
    const float width = std::max(0.30f, rightEv - leftEv);
    const float secant = (rightValue - leftValue) / width;
    float leftSlope = RawLabColorWarpCurveSlope(curve, leftEv);
    float rightSlope = RawLabColorWarpCurveSlope(curve, rightEv);
    if (std::abs(secant) <= 0.000001f) {
        leftSlope = 0.0f;
        rightSlope = 0.0f;
    } else {
        if (leftSlope * secant <= 0.0f) leftSlope = 0.0f;
        if (rightSlope * secant <= 0.0f) rightSlope = 0.0f;
        const float slopeLimit = 3.0f * std::abs(secant);
        leftSlope = std::clamp(leftSlope, -slopeLimit, slopeLimit);
        rightSlope = std::clamp(rightSlope, -slopeLimit, slopeLimit);
    }
    for (std::size_t index = 0;
         index < RawRecipe::kRawColorWarpEvCurveSampleCount;
         ++index) {
        const float ev = RawLabColorWarpEvForSample(index);
        const float original = RawRecipe::EvaluateColorWarpEvCurve(curve, ev);
        float background = original;
        if (ev >= leftEv && ev <= rightEv) {
            const float t = std::clamp((ev - leftEv) / width, 0.0f, 1.0f);
            const float t2 = t * t;
            const float t3 = t2 * t;
            background =
                (2.0f * t3 - 3.0f * t2 + 1.0f) * leftValue +
                (t3 - 2.0f * t2 + t) * width * leftSlope +
                (-2.0f * t3 + 3.0f * t2) * rightValue +
                (t3 - t2) * width * rightSlope;
        }
        selection.background[index] = background;
        selection.residual[index] = ev >= leftEv && ev <= rightEv
            ? original - background
            : 0.0f;
    }
    selection.amplitude = RawLabColorWarpSampleValues(
        selection.residual, selection.centerEv, 0.0f);
    if (std::abs(selection.amplitude) > 0.0001f) {
        for (std::size_t index = 0; index < selection.profile.size(); ++index) {
            selection.profile[index] =
                selection.residual[index] / selection.amplitude;
        }
    } else {
        selection.amplitude = 0.0f;
        for (std::size_t index = 0; index < selection.profile.size(); ++index) {
            const float ev = RawLabColorWarpEvForSample(index);
            if (ev < leftEv || ev > rightEv) continue;
            const float t = (ev - leftEv) / width;
            const float sine = std::sin(
                3.14159265358979323846f * std::clamp(t, 0.0f, 1.0f));
            selection.profile[index] = sine * sine;
        }
    }
}

Stack::EditorModuleTypes::RawWorkspaceColorWarpSmartSelectionState
RawLabColorWarpDetectSmartSelection(
    const RawRecipe::RawColorWarpEvCurve& curve,
    float clickedEv,
    const std::string& pinId) {
    using Selection =
        Stack::EditorModuleTypes::RawWorkspaceColorWarpSmartSelectionState;
    Selection selection;
    selection.pinId = pinId;
    const std::size_t clicked = RawLabColorWarpEvSampleIndex(clickedEv);
    constexpr std::size_t searchRadius = 32u;
    const std::size_t searchLeft = clicked > searchRadius
        ? clicked - searchRadius
        : 1u;
    const std::size_t searchRight = std::min(
        RawRecipe::kRawColorWarpEvCurveSampleCount - 2u,
        clicked + searchRadius);
    std::size_t center = clicked;
    std::size_t nearestDistance = RawRecipe::kRawColorWarpEvCurveSampleCount;
    bool centerIsPeak = true;
    for (std::size_t index = searchLeft; index <= searchRight; ++index) {
        const float before = curve.samples[index] - curve.samples[index - 1u];
        const float after = curve.samples[index + 1u] - curve.samples[index];
        const bool peak = before >= 0.0f && after <= 0.0f &&
            std::abs(before - after) > 0.0005f;
        const bool valley = before <= 0.0f && after >= 0.0f &&
            std::abs(before - after) > 0.0005f;
        if (!peak && !valley) continue;
        const std::size_t distance = index > clicked
            ? index - clicked
            : clicked - index;
        if (distance < nearestDistance) {
            nearestDistance = distance;
            center = index;
            centerIsPeak = peak;
        }
    }

    constexpr std::size_t fallbackHalfWidth = 16u;
    std::size_t left = center > fallbackHalfWidth
        ? center - fallbackHalfWidth
        : 0u;
    std::size_t right = std::min(
        RawRecipe::kRawColorWarpEvCurveSampleCount - 1u,
        center + fallbackHalfWidth);
    if (nearestDistance != RawRecipe::kRawColorWarpEvCurveSampleCount) {
        for (std::size_t index = center; index > 1u; --index) {
            const float before = curve.samples[index - 1u] - curve.samples[index - 2u];
            const float after = curve.samples[index] - curve.samples[index - 1u];
            const bool opposite = centerIsPeak
                ? before <= 0.0f && after >= 0.0f
                : before >= 0.0f && after <= 0.0f;
            if (opposite) {
                left = index - 1u;
                break;
            }
            if (center - index >= searchRadius) break;
        }
        for (std::size_t index = center + 1u;
             index + 1u < curve.samples.size();
             ++index) {
            const float before = curve.samples[index] - curve.samples[index - 1u];
            const float after = curve.samples[index + 1u] - curve.samples[index];
            const bool opposite = centerIsPeak
                ? before <= 0.0f && after >= 0.0f
                : before >= 0.0f && after <= 0.0f;
            if (opposite) {
                right = index;
                break;
            }
            if (index - center >= searchRadius) break;
        }
    }
    if (right <= left + 2u) {
        left = center > fallbackHalfWidth ? center - fallbackHalfWidth : 0u;
        right = std::min(
            RawRecipe::kRawColorWarpEvCurveSampleCount - 1u,
            center + fallbackHalfWidth);
    }
    RawLabColorWarpBuildSmartComponents(
        curve,
        RawLabColorWarpEvForSample(left),
        RawLabColorWarpEvForSample(right),
        RawLabColorWarpEvForSample(center),
        selection);
    return selection;
}

float RawLabColorWarpLegalAmplitude(
    const std::vector<float>& background,
    const std::vector<float>& profile,
    float requested) {
    float minimum = -std::numeric_limits<float>::infinity();
    float maximum = std::numeric_limits<float>::infinity();
    const std::size_t count = std::min(background.size(), profile.size());
    for (std::size_t index = 0; index < count; ++index) {
        const float p = profile[index];
        if (std::abs(p) <= 0.000001f) continue;
        const float lower = -background[index] / p;
        const float upper = (1.0f - background[index]) / p;
        minimum = std::max(minimum, std::min(lower, upper));
        maximum = std::min(maximum, std::max(lower, upper));
    }
    if (minimum > maximum) return 0.0f;
    return std::clamp(requested, minimum, maximum);
}

void RawLabColorWarpApplySmartAmplitude(
    const RawRecipe::RawColorWarpEvCurve& startCurve,
    const Stack::EditorModuleTypes::RawWorkspaceColorWarpSmartSelectionState& start,
    float requestedAmplitude,
    RawRecipe::RawColorWarpEvCurve& outputCurve,
    Stack::EditorModuleTypes::RawWorkspaceColorWarpSmartSelectionState& output) {
    outputCurve = startCurve;
    const float amplitude = RawLabColorWarpLegalAmplitude(
        start.background, start.profile, requestedAmplitude);
    for (std::size_t index = 0; index < outputCurve.samples.size(); ++index) {
        const float ev = RawLabColorWarpEvForSample(index);
        if (ev < start.leftEv || ev > start.rightEv) continue;
        outputCurve.samples[index] = start.background[index] +
            start.profile[index] * amplitude;
    }
    output = start;
    output.amplitude = amplitude;
    for (std::size_t index = 0; index < output.residual.size(); ++index) {
        output.residual[index] = output.profile[index] * amplitude;
    }
}

void RawLabColorWarpApplySmartRemap(
    const RawRecipe::RawColorWarpEvCurve& startCurve,
    const Stack::EditorModuleTypes::RawWorkspaceColorWarpSmartSelectionState& start,
    float newLeft,
    float newRight,
    float newCenter,
    int oneSided,
    RawRecipe::RawColorWarpEvCurve& outputCurve,
    Stack::EditorModuleTypes::RawWorkspaceColorWarpSmartSelectionState& output) {
    outputCurve = startCurve;
    for (std::size_t index = 0; index < outputCurve.samples.size(); ++index) {
        const float ev = RawLabColorWarpEvForSample(index);
        if (ev >= start.leftEv && ev <= start.rightEv) {
            outputCurve.samples[index] = start.background[index];
        }
    }
    Stack::EditorModuleTypes::RawWorkspaceColorWarpSmartSelectionState newBase;
    newBase.active = true;
    newBase.pinId = start.pinId;
    newBase.leftEv = newLeft;
    newBase.rightEv = newRight;
    newBase.centerEv = newCenter;
    newBase.background = outputCurve.samples;
    newBase.profile.assign(
        RawRecipe::kRawColorWarpEvCurveSampleCount, 0.0f);
    newBase.residual.assign(
        RawRecipe::kRawColorWarpEvCurveSampleCount, 0.0f);
    for (std::size_t index = 0; index < newBase.profile.size(); ++index) {
        const float ev = RawLabColorWarpEvForSample(index);
        if (ev < newLeft || ev > newRight) continue;
        float sourceEv = ev;
        if (oneSided < 0) {
            if (ev <= newCenter) {
                const float t = (ev - newLeft) /
                    std::max(0.0001f, newCenter - newLeft);
                sourceEv = start.leftEv + t *
                    (start.centerEv - start.leftEv);
            }
        } else if (oneSided > 0) {
            if (ev >= newCenter) {
                const float t = (ev - newCenter) /
                    std::max(0.0001f, newRight - newCenter);
                sourceEv = start.centerEv + t *
                    (start.rightEv - start.centerEv);
            }
        } else {
            const float t = (ev - newLeft) /
                std::max(0.0001f, newRight - newLeft);
            sourceEv = start.leftEv + t *
                (start.rightEv - start.leftEv);
        }
        newBase.profile[index] = RawLabColorWarpSampleValues(
            start.profile, sourceEv, 0.0f);
    }
    const float amplitude = RawLabColorWarpLegalAmplitude(
        newBase.background, newBase.profile, start.amplitude);
    for (std::size_t index = 0; index < outputCurve.samples.size(); ++index) {
        const float ev = RawLabColorWarpEvForSample(index);
        if (ev < newLeft || ev > newRight) continue;
        outputCurve.samples[index] = newBase.background[index] +
            newBase.profile[index] * amplitude;
    }
    newBase.amplitude = amplitude;
    for (std::size_t index = 0; index < newBase.residual.size(); ++index) {
        newBase.residual[index] = newBase.profile[index] * amplitude;
    }
    output = std::move(newBase);
}

void RawLabColorWarpPaintCurve(
    RawRecipe::RawColorWarpEvCurve& curve,
    float centerEv,
    float target,
    float brushWidthEv) {
    const float radius = std::max(0.20f, brushWidthEv);
    target = std::clamp(target, 0.0f, 1.0f);
    for (std::size_t index = 0; index < curve.samples.size(); ++index) {
        const float distance = std::abs(
            RawLabColorWarpEvForSample(index) - centerEv);
        if (distance >= radius) continue;
        const float normalized = distance / radius;
        const float influence = 1.0f -
            normalized * normalized * (3.0f - 2.0f * normalized);
        curve.samples[index] +=
            (target - curve.samples[index]) * influence;
        curve.samples[index] = std::clamp(curve.samples[index], 0.0f, 1.0f);
    }
}

} // namespace

bool RenderRawLabColorSurface(RawLabColorSurfaceArgs& args) {
    using Stack::RawRecipe::RawColorWarpCoordinate;
    using Stack::RawRecipe::RawColorWarpPin;

    bool changed = false;
    constexpr int kColorWarpGestureNone = 0;
    constexpr int kColorWarpGestureTarget = 1;
    constexpr int kColorWarpGestureSource = 2;
    constexpr int kColorWarpGestureQualifier = 3;
    constexpr int kColorWarpGestureProvisional = 4;
    constexpr int kColorWarpGesturePan = 5;
    constexpr int kColorWarpGestureGroup = 6;
    args.ui.colorWarpInteractionActive = false;
    const bool provisionalCreatedFromPhoto =
        args.ui.colorWarpProvisionalCreatedFromPhoto;
    args.ui.colorWarpProvisionalCreatedFromPhoto = false;
    args.ui.colorWarpLightnessRangePreviewActive = false;
    args.colorWarp = Stack::RawRecipe::SanitizeColorWarpRecipe(
        std::move(args.colorWarp));
    auto& colorWarp = args.colorWarp;
    if ((!std::isfinite(args.ui.colorWarpRegionReachWheelDelta) ||
         std::abs(args.ui.colorWarpRegionReachWheelDelta) > 0.0001f) ||
        (!std::isfinite(args.ui.colorWarpRegionFeatherWheelDelta) ||
         std::abs(args.ui.colorWarpRegionFeatherWheelDelta) > 0.0001f)) {
        const auto inspectedRegion = std::find_if(
            colorWarp.regions.begin(), colorWarp.regions.end(),
            [&](const auto& region) {
                return region.id == args.ui.colorWarpInspectedRegionId;
            });
        if (inspectedRegion != colorWarp.regions.end()) {
            if (std::isfinite(args.ui.colorWarpRegionReachWheelDelta)) {
                inspectedRegion->reachPixels = std::clamp(
                    inspectedRegion->reachPixels +
                        args.ui.colorWarpRegionReachWheelDelta,
                    0.0f,
                    512.0f);
            }
            if (std::isfinite(args.ui.colorWarpRegionFeatherWheelDelta)) {
                inspectedRegion->featherPixels = std::clamp(
                    inspectedRegion->featherPixels +
                        args.ui.colorWarpRegionFeatherWheelDelta,
                    0.0f,
                    512.0f);
            }
            changed = true;
        }
        args.ui.colorWarpRegionReachWheelDelta = 0.0f;
        args.ui.colorWarpRegionFeatherWheelDelta = 0.0f;
        args.ui.colorWarpDiagnosticTargetLongEdge = 192;
    }
    if (args.ui.colorWarpPendingCircleCommitRequested &&
        args.ui.colorWarpPendingCircleActive &&
        args.ui.colorWarpPendingCircleAppend) {
        const auto region = std::find_if(
            colorWarp.regions.begin(), colorWarp.regions.end(),
            [&](const auto& candidate) {
                return candidate.id == args.ui.colorWarpPendingReplaceRegionId;
            });
        if (region != colorWarp.regions.end() &&
            region->circles.size() < Stack::RawRecipe::kMaxRawColorWarpSampleCircles) {
            auto circle = args.ui.colorWarpPendingCircle;
            circle.id = "sample-" + std::to_string(region->circles.size() + 1u);
            region->circles.push_back(std::move(circle));
            region->spatialMode =
                Stack::RawRecipe::RawColorWarpSpatialMode::AssistedRegion;
            args.ui.colorWarpInspectionActive = true;
            args.ui.colorWarpInspectedRegionId = region->id;
            changed = true;
        }
        args.ui.colorWarpPendingCircleActive = false;
        args.ui.colorWarpPendingCircleCommitRequested = false;
        args.ui.colorWarpPendingCircleAppend = false;
        args.ui.colorWarpPendingReplaceRegionId.clear();
        args.ui.colorWarpPendingReplaceCircleId.clear();
        args.ui.colorWarpAreaAnalysisResult = {};
    }
    if (args.ui.colorWarpPendingCircleCommitRequested &&
        args.ui.colorWarpPendingCircleActive &&
        !args.ui.colorWarpPendingCircleAppend &&
        !args.ui.colorWarpPendingCircleRefining &&
        !args.ui.colorWarpAreaAnalysisResult.proposals.empty()) {
        const std::string replaceRegionId =
            args.ui.colorWarpPendingReplaceRegionId;
        auto existingRegion = std::find_if(
            colorWarp.regions.begin(), colorWarp.regions.end(),
            [&](const auto& candidate) { return candidate.id == replaceRegionId; });
        const bool replacing = existingRegion != colorWarp.regions.end();
        std::string regionId;
        if (replacing) {
            regionId = existingRegion->id;
            const auto existingCircle = std::find_if(
                existingRegion->circles.begin(), existingRegion->circles.end(),
                [&](const auto& circle) {
                    return circle.id == args.ui.colorWarpPendingReplaceCircleId;
                });
            if (existingCircle != existingRegion->circles.end()) {
                const std::string retainedId = existingCircle->id;
                *existingCircle = args.ui.colorWarpPendingCircle;
                existingCircle->id = retainedId;
            } else {
                existingRegion->circles = { args.ui.colorWarpPendingCircle };
                existingRegion->circles.front().id = "sample-1";
            }
        } else {
            std::unordered_set<std::string> regionIds;
            for (const auto& existing : colorWarp.regions) regionIds.insert(existing.id);
            Stack::RawRecipe::RawColorWarpRegion region;
            region.id = "region-" + std::to_string(colorWarp.regions.size() + 1u);
            while (regionIds.find(region.id) != regionIds.end()) region.id += "-copy";
            region.spatialMode = Stack::RawRecipe::RawColorWarpSpatialMode::Cohesive;
            region.circles.push_back(args.ui.colorWarpPendingCircle);
            region.circles.front().id = "sample-1";
            regionId = region.id;
            colorWarp.regions.push_back(std::move(region));
        }

        std::unordered_set<std::string> replacedPinIds;
        if (replacing) {
            for (const auto& pin : colorWarp.pins) {
                if (pin.regionId == regionId) replacedPinIds.insert(pin.id);
            }
            colorWarp.pins.erase(
                std::remove_if(
                    colorWarp.pins.begin(), colorWarp.pins.end(),
                    [&](const auto& pin) { return pin.regionId == regionId; }),
                colorWarp.pins.end());
            for (auto& group : colorWarp.linkGroups) {
                group.pinIds.erase(
                    std::remove_if(
                        group.pinIds.begin(), group.pinIds.end(),
                        [&](const std::string& pinId) {
                            return replacedPinIds.find(pinId) != replacedPinIds.end();
                        }),
                    group.pinIds.end());
            }
            colorWarp.linkGroups.erase(
                std::remove_if(
                    colorWarp.linkGroups.begin(), colorWarp.linkGroups.end(),
                    [](const auto& group) { return group.pinIds.empty(); }),
                colorWarp.linkGroups.end());
        }

        std::unordered_set<std::string> pinIds;
        for (const auto& existing : colorWarp.pins) pinIds.insert(existing.id);
        std::vector<std::string> createdPinIds;
        for (const auto& proposal : args.ui.colorWarpAreaAnalysisResult.proposals) {
            if (colorWarp.pins.size() >= Stack::RawRecipe::kMaxRawColorWarpPins) break;
            RawColorWarpPin pin;
            pin.id = "color-" + std::to_string(colorWarp.pins.size() + 1u);
            while (pinIds.find(pin.id) != pinIds.end()) pin.id += "-copy";
            pinIds.insert(pin.id);
            pin.name = "Sampled Color " + std::to_string(colorWarp.pins.size() + 1u);
            pin.sourceA = proposal.sourceA;
            pin.sourceB = proposal.sourceB;
            pin.targetA = proposal.sourceA;
            pin.targetB = proposal.sourceB;
            pin.radius = 0.055f;
            pin.regionId = regionId;
            pin.evCurve = proposal.evCurve;
            createdPinIds.push_back(pin.id);
            colorWarp.pins.push_back(std::move(pin));
        }
        if (createdPinIds.size() > 1u) {
            Stack::RawRecipe::RawColorWarpLinkGroup group;
            group.id = "group-" + std::to_string(colorWarp.linkGroups.size() + 1u);
            group.name = "Sampled Color Group";
            group.pinIds = createdPinIds;
            colorWarp.linkGroups.push_back(std::move(group));
        }
        if (!createdPinIds.empty()) {
            args.ui.selectedColorWarpPin = static_cast<int>(colorWarp.pins.size() - createdPinIds.size());
            args.ui.colorWarpInspectionActive = true;
            args.ui.colorWarpInspectedRegionId = regionId;
            changed = true;
        } else if (!replacing) {
            colorWarp.regions.pop_back();
        }
        args.ui.colorWarpPendingCircleActive = false;
        args.ui.colorWarpPendingCircleCommitRequested = false;
        args.ui.colorWarpPendingReplaceRegionId.clear();
        args.ui.colorWarpPendingReplaceCircleId.clear();
        args.ui.colorWarpAreaAnalysisResult = {};
    }
    constexpr float fullWheelExtent = 0.45f;
    constexpr float coordinateLimit = 1.0f;
    constexpr float sceneEvMinimum = -16.0f;
    constexpr float sceneEvMaximum = 16.0f;
    constexpr float fixedDiscLightness = 0.70f;
    if (!args.ui.colorWarpViewAnimationInitialized) {
        args.ui.colorWarpViewTargetCenterA =
            args.ui.colorWarpViewCenterA;
        args.ui.colorWarpViewTargetCenterB =
            args.ui.colorWarpViewCenterB;
        args.ui.colorWarpViewTargetExtent =
            args.ui.colorWarpViewExtent;
        args.ui.colorWarpViewAnimationInitialized = true;
    }

    const std::string& previewIdentity = args.previewIdentity;
    RefreshRawLabColorCloud(args, coordinateLimit);
    auto& colorCloud = args.ui.colorWarpCloud;

    const auto fitColorCloud = [&](float canvasAspect) {
        if (colorCloud.empty()) {
            args.ui.colorWarpViewCenterA = 0.0f;
            args.ui.colorWarpViewCenterB = 0.0f;
            args.ui.colorWarpViewExtent = fullWheelExtent;
            args.ui.colorWarpViewTargetCenterA = 0.0f;
            args.ui.colorWarpViewTargetCenterB = 0.0f;
            args.ui.colorWarpViewTargetExtent = fullWheelExtent;
            return;
        }
        float maximumAbsA = 0.0f;
        float maximumAbsB = 0.0f;
        bool foundPoint = false;
        for (const auto& point : colorCloud) {
            foundPoint = true;
            const auto projected = ProjectColorDisc(point.a, point.b);
            maximumAbsA = std::max(maximumAbsA, std::abs(projected.x));
            maximumAbsB = std::max(maximumAbsB, std::abs(projected.y));
        }
        if (!foundPoint) {
            args.ui.colorWarpViewCenterA = 0.0f;
            args.ui.colorWarpViewCenterB = 0.0f;
            args.ui.colorWarpViewExtent = fullWheelExtent;
            args.ui.colorWarpViewTargetCenterA = 0.0f;
            args.ui.colorWarpViewTargetCenterB = 0.0f;
            args.ui.colorWarpViewTargetExtent = fullWheelExtent;
            return;
        }
        // Keep neutral OKLab at the visual center. The rectangular viewport
        // expands its horizontal coordinate range with its aspect ratio.
        args.ui.colorWarpViewCenterA = 0.0f;
        args.ui.colorWarpViewCenterB = 0.0f;
        const float safeAspect = std::max(0.1f, canvasAspect);
        const float requiredHalfHeight = std::max(
            maximumAbsB,
            maximumAbsA / safeAspect);
        args.ui.colorWarpViewExtent = std::clamp(
            requiredHalfHeight * 1.12f + 0.015f,
            0.055f,
            coordinateLimit);
        args.ui.colorWarpViewTargetCenterA =
            args.ui.colorWarpViewCenterA;
        args.ui.colorWarpViewTargetCenterB =
            args.ui.colorWarpViewCenterB;
        args.ui.colorWarpViewTargetExtent =
            args.ui.colorWarpViewExtent;
    };

    if (args.ui.colorWarpViewSourceKey != previewIdentity) {
        args.ui.colorWarpViewSourceKey = previewIdentity;
        args.ui.colorWarpViewNeedsFit = true;
        args.ui.colorWarpViewCenterA = 0.0f;
        args.ui.colorWarpViewCenterB = 0.0f;
        args.ui.colorWarpViewExtent = fullWheelExtent;
        args.ui.colorWarpViewTargetCenterA = 0.0f;
        args.ui.colorWarpViewTargetCenterB = 0.0f;
        args.ui.colorWarpViewTargetExtent = fullWheelExtent;
        args.ui.colorWarpProvisionalPinValid = false;
        args.ui.colorWarpProvisionalSceneEvValid = false;
        args.ui.colorWarpDraggingProvisional = false;
        args.ui.colorWarpPhotoHoverValid = false;
        args.ui.colorWarpPendingCircleActive = false;
        args.ui.colorWarpPendingCircleDrawing = false;
        args.ui.colorWarpPendingCircleRefining = false;
        args.ui.colorWarpPendingCircleCommitRequested = false;
        args.ui.colorWarpPendingCircleAppend = false;
        args.ui.colorWarpPendingReplaceRegionId.clear();
        args.ui.colorWarpPendingReplaceCircleId.clear();
        args.ui.colorWarpRegionReachWheelDelta = 0.0f;
        args.ui.colorWarpRegionFeatherWheelDelta = 0.0f;
        ++args.ui.colorWarpAreaAnalysisGeneration;
        args.ui.colorWarpAreaAnalysisState.reset();
        ++args.ui.colorWarpDiagnosticGeneration;
        args.ui.colorWarpDiagnosticState.reset();
        args.ui.colorWarpInspectionActive = false;
        args.ui.colorWarpInspectedRegionId.clear();
        args.ui.colorWarpEvGesture = 0;
        args.ui.colorWarpEvGesturePinId.clear();
        args.ui.colorWarpSmartSelection = {};
        args.ui.colorWarpGestureOwner =
            kColorWarpGestureNone;
    }
    args.ui.colorWarpViewExtent = std::clamp(
        args.ui.colorWarpViewExtent,
        0.035f,
        coordinateLimit);
    args.ui.colorWarpViewTargetExtent = std::clamp(
        args.ui.colorWarpViewTargetExtent,
        0.035f,
        coordinateLimit);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) &&
        !args.ui.colorWarpPendingCircleActive) {
        args.ui.colorWarpInspectionActive = false;
        args.ui.colorWarpInspectedRegionId.clear();
        if (!args.ui.colorWarpDiagnosticLocked) {
            args.ui.colorWarpDiagnosticRequestedFingerprint = 0;
        }
    }
    if (!colorWarp.pins.empty() &&
        !args.ui.colorWarpProvisionalPinValid) {
        args.ui.selectedColorWarpPin = std::clamp(
            args.ui.selectedColorWarpPin,
            0,
            static_cast<int>(colorWarp.pins.size() - 1u));
    } else if (colorWarp.pins.empty()) {
        args.ui.selectedColorWarpPin = -1;
    }

    const auto applySampledLuminanceRange = [&](RawColorWarpPin& pin,
                                                float sampledSceneEv) {
        // A photograph-picked pin begins as a local scene-luminance
        // qualifier. The fully weighted core stays narrow, while the
        // feather avoids hard luminance seams in the rendered grade.
        constexpr float sampledCoreHalfWidthEv = 0.75f;
        constexpr float sampledFeatherEv = 0.75f;
        const float center = std::clamp(
            sampledSceneEv,
            sceneEvMinimum,
            sceneEvMaximum);
        pin.evCurve = Stack::RawRecipe::MakeColorWarpEvCurveHump(
            center,
            sampledCoreHalfWidthEv,
            sampledFeatherEv);
    };
    const ImVec2 availableCanvasRegion = ImGui::GetContentRegionAvail();
    const float availableWidth = std::max(180.0f, availableCanvasRegion.x);
    const float bottomControlReserve =
        colorWarp.pins.empty() ||
            args.ui.colorWarpProvisionalPinValid
        ? 42.0f
        : 178.0f;
    const float canvasWidth = availableWidth;
    const float canvasHeight = std::max(
        220.0f,
        availableCanvasRegion.y - bottomControlReserve);
    const float canvasAspect = canvasWidth / std::max(1.0f, canvasHeight);
    if (args.ui.colorWarpViewNeedsFit) {
        fitColorCloud(canvasAspect);
        args.ui.colorWarpViewNeedsFit = false;
    }
    const float zoomAnimationAmount = 1.0f - std::exp(
        -14.0f * std::max(0.0f, ImGui::GetIO().DeltaTime));
    args.ui.colorWarpViewExtent +=
        (args.ui.colorWarpViewTargetExtent -
         args.ui.colorWarpViewExtent) * zoomAnimationAmount;
    args.ui.colorWarpViewCenterA +=
        (args.ui.colorWarpViewTargetCenterA -
         args.ui.colorWarpViewCenterA) * zoomAnimationAmount;
    args.ui.colorWarpViewCenterB +=
        (args.ui.colorWarpViewTargetCenterB -
         args.ui.colorWarpViewCenterB) * zoomAnimationAmount;
    const float viewHalfHeight =
        args.ui.colorWarpViewExtent;
    const float viewHalfWidth = viewHalfHeight * canvasAspect;
    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasMax(
        canvasMin.x + canvasWidth,
        canvasMin.y + canvasHeight);
    const RawLabColorCanvasTransform canvasTransform {
        canvasMin,
        canvasWidth,
        canvasHeight,
        args.ui.colorWarpViewCenterA,
        args.ui.colorWarpViewCenterB,
        viewHalfWidth,
        viewHalfHeight,
        fullWheelExtent
    };
    const RawLabColorPalette palette {
        args.workingSpace,
        fixedDiscLightness,
        fullWheelExtent
    };

    ImGui::InvisibleButton(
        "##RawLabColorWarpCanvas",
        ImVec2(canvasWidth, canvasHeight),
        ImGuiButtonFlags_MouseButtonLeft |
            ImGuiButtonFlags_MouseButtonRight |
            ImGuiButtonFlags_MouseButtonMiddle);
    const bool canvasHovered = ImGui::IsItemHovered();
    if (canvasHovered) {
        // The wheel owns vertical scrolling while hovered. Besides preventing
        // the containing pane from moving, this routes the wheel to the zoom
        // and pin-adjustment behavior below.
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    }
    LabTooltip(
        "Color surface: click empty color to stage a pin; click a pin to "
        "select it; drag the hollow source or filled destination to edit it. "
        "Wheel zooms around the pointer; middle-drag pans. With a selected "
        "pin, Ctrl+wheel changes qualifier shape, "
        "and Shift+wheel changes color feather. Right-click a sampled pin to "
        "recall its region; double-click a pin to delete it.");
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        args.ui.colorWarpGestureOwner =
            kColorWarpGestureNone;
    }
    if (canvasHovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Middle) &&
        args.ui.colorWarpGestureOwner ==
            kColorWarpGestureNone) {
        args.ui.colorWarpGestureOwner =
            kColorWarpGesturePan;
        args.ui.colorWarpPanAnchorScreenPos = ImGui::GetMousePos();
        args.ui.colorWarpPanRestoreScreenPos = ImGui::GetMousePos();
    }
    const ImVec2 mouseCoordinate = canvasTransform.FromScreenUnclamped(mouse);
    const bool mouseInsideColorDisc =
        std::hypot(canvasTransform.DisplayFromScreen(mouse).x,
            canvasTransform.DisplayFromScreen(mouse).y) < fullWheelExtent;

    const auto qualifierHandlePositions = [&](const RawColorWarpPin& pin,
                                              ImVec2& orientationHandle,
                                              ImVec2& apertureHandlePositive,
                                              ImVec2& apertureHandleNegative) {
        const float directionA = std::cos(pin.qualifierOrientationRadians);
        const float directionB = std::sin(pin.qualifierOrientationRadians);
        orientationHandle = canvasTransform.ToScreen(
            pin.sourceA + directionA * pin.radius,
            pin.sourceB + directionB * pin.radius);
        const float sideA = -directionB * pin.radius * pin.qualifierAperture;
        const float sideB = directionA * pin.radius * pin.qualifierAperture;
        apertureHandlePositive = canvasTransform.ToScreen(
            pin.sourceA + sideA,
            pin.sourceB + sideB);
        apertureHandleNegative = canvasTransform.ToScreen(
            pin.sourceA - sideA,
            pin.sourceB - sideB);
    };
    const auto hitSelectedQualifierHandle = [&](const ImVec2& point) {
        if (args.ui.selectedColorWarpPin < 0 ||
            args.ui.selectedColorWarpPin >=
                static_cast<int>(colorWarp.pins.size())) {
            return 0;
        }
        ImVec2 orientationHandle;
        ImVec2 apertureHandlePositive;
        ImVec2 apertureHandleNegative;
        qualifierHandlePositions(
            colorWarp.pins[static_cast<std::size_t>(
                args.ui.selectedColorWarpPin)],
            orientationHandle,
            apertureHandlePositive,
            apertureHandleNegative);
        if (std::hypot(
                point.x - orientationHandle.x,
                point.y - orientationHandle.y) <= 11.0f) {
            return 1;
        }
        if (std::min(
                std::hypot(
                    point.x - apertureHandlePositive.x,
                    point.y - apertureHandlePositive.y),
                std::hypot(
                    point.x - apertureHandleNegative.x,
                    point.y - apertureHandleNegative.y)) <= 11.0f) {
            return 2;
        }
        return 0;
    };

    const auto findNearestPin = [&](const ImVec2& point, bool& sourceHandle) {
        int nearest = -1;
        sourceHandle = false;
        float nearestDistance = 14.0f;
        for (std::size_t index = 0; index < colorWarp.pins.size(); ++index) {
            const RawColorWarpPin& pin = colorWarp.pins[index];
            const ImVec2 source = canvasTransform.ToScreen(pin.sourceA, pin.sourceB);
            const ImVec2 target = canvasTransform.ToScreen(pin.targetA, pin.targetB);
            const float sourceDistance = std::hypot(
                point.x - source.x,
                point.y - source.y);
            const float targetDistance = std::hypot(
                point.x - target.x,
                point.y - target.y);
            // Destination owns an overlap. Shift explicitly captures the
            // hollow source ring instead.
            const bool sourceHit = sourceDistance < 14.0f;
            const bool targetHit = targetDistance < 14.0f;
            const bool chooseSource = io.KeyShift
                ? sourceHit
                : (!targetHit && sourceHit);
            const float distance = chooseSource ? sourceDistance : targetDistance;
            if ((!chooseSource && !targetHit) || (chooseSource && !sourceHit)) continue;
            if (distance < nearestDistance) {
                nearest = static_cast<int>(index);
                sourceHandle = chooseSource;
                nearestDistance = distance;
            }
        }
        return nearest;
    };
    const auto findNearestGroup = [&](const ImVec2& point) {
        int nearest = -1;
        float nearestDistance = 13.0f;
        for (std::size_t groupIndex = 0; groupIndex < colorWarp.linkGroups.size(); ++groupIndex) {
            const auto& group = colorWarp.linkGroups[groupIndex];
            ImVec2 centroid(0.0f, 0.0f);
            int count = 0;
            for (const auto& pin : colorWarp.pins) {
                if (std::find(group.pinIds.begin(), group.pinIds.end(), pin.id) ==
                    group.pinIds.end()) continue;
                centroid.x += pin.targetA;
                centroid.y += pin.targetB;
                ++count;
            }
            if (count < 2) continue;
            centroid.x /= count;
            centroid.y /= count;
            const ImVec2 screen = canvasTransform.ToScreen(centroid.x, centroid.y);
            const float distance = std::hypot(point.x - screen.x, point.y - screen.y);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = static_cast<int>(groupIndex);
            }
        }
        return nearest;
    };
    const auto addCommittedPin = [&](const ImVec2& sourceCoordinate,
                                     const ImVec2& targetCoordinate) {
        RawColorWarpPin pin;
        pin.id = "color-" + std::to_string(colorWarp.pins.size() + 1u);
        std::unordered_set<std::string> ids;
        for (const RawColorWarpPin& existing : colorWarp.pins) {
            ids.insert(existing.id);
        }
        while (ids.find(pin.id) != ids.end()) {
            pin.id += "-copy";
        }
        pin.name = "Color " + std::to_string(colorWarp.pins.size() + 1u);
        pin.sourceA = sourceCoordinate.x;
        pin.sourceB = sourceCoordinate.y;
        pin.targetA = targetCoordinate.x;
        pin.targetB = targetCoordinate.y;
        const float initialDirectionA = targetCoordinate.x - sourceCoordinate.x;
        const float initialDirectionB = targetCoordinate.y - sourceCoordinate.y;
        if (std::hypot(initialDirectionA, initialDirectionB) > 0.000001f) {
            pin.qualifierOrientationRadians = std::atan2(
                initialDirectionB,
                initialDirectionA);
        }
        pin.radius = 0.055f;
        pin.evCurve = Stack::RawRecipe::MakeUniformColorWarpEvCurve();
        if (args.ui.colorWarpProvisionalSceneEvValid) {
            applySampledLuminanceRange(
                pin,
                args.ui.colorWarpProvisionalSceneEv);
        }
        colorWarp.pins.push_back(std::move(pin));
        args.ui.selectedColorWarpPin =
            static_cast<int>(colorWarp.pins.size() - 1u);
    };

    bool deleteSelected = false;
    bool duplicateSelected = false;
    bool pointDragActive = false;
    if (canvasHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        bool sourceHandle = false;
        const int nearest = findNearestPin(mouse, sourceHandle);
        if (nearest >= 0) {
            args.ui.selectedColorWarpPin = nearest;
            args.ui.colorWarpProvisionalPinValid = false;
            args.ui.colorWarpProvisionalSceneEvValid = false;
            args.ui.colorWarpDraggingProvisional = false;
            deleteSelected = true;
        }
    } else if (canvasHovered &&
               ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
               args.ui.colorWarpGestureOwner ==
                   kColorWarpGestureNone) {
        const int qualifierHandle = hitSelectedQualifierHandle(mouse);
        const int nearestGroup = findNearestGroup(mouse);
        bool sourceHandle = false;
        const int nearest = findNearestPin(mouse, sourceHandle);
        if (qualifierHandle != 0) {
            args.ui.colorWarpQualifierDragHandle =
                qualifierHandle;
            args.ui.colorWarpDraggingSource = false;
            args.ui.colorWarpDraggingProvisional = false;
            args.ui.colorWarpProvisionalPinValid = false;
            args.ui.colorWarpProvisionalSceneEvValid = false;
            args.ui.colorWarpGestureOwner =
                kColorWarpGestureQualifier;
        } else if (nearestGroup >= 0) {
            args.ui.selectedColorWarpGroupId =
                colorWarp.linkGroups[static_cast<std::size_t>(nearestGroup)].id;
            args.ui.colorWarpGestureOwner = kColorWarpGestureGroup;
            args.ui.colorWarpDraggingSource = false;
        } else if (nearest >= 0) {
            if (args.ui.selectedColorWarpPin != nearest &&
                !args.ui.colorWarpDiagnosticLocked) {
                args.ui.colorWarpInspectionActive = false;
                args.ui.colorWarpInspectedRegionId.clear();
            }
            args.ui.selectedColorWarpPin = nearest;
            args.ui.selectedColorWarpGroupId.clear();
            args.ui.colorWarpDraggingSource = sourceHandle;
            args.ui.colorWarpQualifierDragHandle = 0;
            args.ui.colorWarpProvisionalPinValid = false;
            args.ui.colorWarpProvisionalSceneEvValid = false;
            args.ui.colorWarpDraggingProvisional = false;
            args.ui.colorWarpGestureOwner = sourceHandle
                ? kColorWarpGestureSource
                : kColorWarpGestureTarget;
        } else if (colorWarp.pins.size() <
                   Stack::RawRecipe::kMaxRawColorWarpPins) {
            if (mouseInsideColorDisc) {
                const ImVec2 coordinate =
                    canvasTransform.ClampToDisc(mouseCoordinate);
                const ImVec2 provisional = canvasTransform.ToScreen(
                    args.ui.colorWarpProvisionalA,
                    args.ui.colorWarpProvisionalB);
                const bool clickedProvisional =
                    args.ui.colorWarpProvisionalPinValid &&
                    std::hypot(
                        mouse.x - provisional.x,
                        mouse.y - provisional.y) <= 16.0f;
                if (!clickedProvisional) {
                    args.ui.colorWarpProvisionalA = coordinate.x;
                    args.ui.colorWarpProvisionalB = coordinate.y;
                    args.ui.colorWarpProvisionalPinValid = true;
                    // The 2D disc has no unique luminance coordinate. Pins
                    // placed directly here therefore use Working Range.
                    args.ui.colorWarpProvisionalSceneEvValid = false;
                }
                args.ui.selectedColorWarpPin = -1;
                args.ui.colorWarpDraggingSource = false;
                args.ui.colorWarpDraggingProvisional = true;
                args.ui.colorWarpGestureOwner =
                    kColorWarpGestureProvisional;
            } else {
                args.ui.colorWarpProvisionalPinValid = false;
                args.ui.colorWarpProvisionalSceneEvValid = false;
                args.ui.colorWarpDraggingProvisional = false;
            }
        }
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !canvasHovered &&
        !provisionalCreatedFromPhoto &&
        args.ui.colorWarpProvisionalPinValid) {
        args.ui.colorWarpProvisionalPinValid = false;
        args.ui.colorWarpProvisionalSceneEvValid = false;
        args.ui.colorWarpDraggingProvisional = false;
    }
    if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        bool sourceHandle = false;
        const int nearest = findNearestPin(mouse, sourceHandle);
        if (nearest >= 0) {
            args.ui.selectedColorWarpPin = nearest;
            args.ui.colorWarpInspectionActive = true;
            args.ui.colorWarpInspectedRegionId =
                colorWarp.pins[static_cast<std::size_t>(nearest)].regionId;
            ImGui::OpenPopup("RawLabColorWarpPinMenu");
        }
    }

    if (ImGui::IsItemActive() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.5f) &&
        args.ui.colorWarpGestureOwner ==
            kColorWarpGestureQualifier &&
        args.ui.colorWarpQualifierDragHandle != 0 &&
        args.ui.selectedColorWarpPin >= 0 &&
        args.ui.selectedColorWarpPin <
            static_cast<int>(colorWarp.pins.size())) {
        RawColorWarpPin& pin = colorWarp.pins[static_cast<std::size_t>(
            args.ui.selectedColorWarpPin)];
        const ImVec2 delta(
            mouseCoordinate.x - pin.sourceA,
            mouseCoordinate.y - pin.sourceB);
        if (args.ui.colorWarpQualifierDragHandle == 1) {
            if (std::hypot(delta.x, delta.y) > 0.000001f) {
                pin.qualifierOrientationRadians = std::atan2(delta.y, delta.x);
            }
        } else {
            const float directionA = std::cos(pin.qualifierOrientationRadians);
            const float directionB = std::sin(pin.qualifierOrientationRadians);
            const float sideProjection =
                std::abs(delta.x * -directionB + delta.y * directionA);
            pin.qualifierAperture = std::clamp(
                sideProjection / std::max(0.005f, pin.radius),
                0.10f,
                1.0f);
        }
        pin.qualifierDirectionality = std::max(
            pin.qualifierDirectionality,
            0.5f);
        changed = true;
        args.ui.colorWarpInteractionActive = true;
        pointDragActive = true;
    } else if (ImGui::IsItemActive() &&
               ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.5f) &&
               args.ui.colorWarpGestureOwner == kColorWarpGestureGroup &&
               !args.ui.selectedColorWarpGroupId.empty()) {
        const auto group = std::find_if(
            colorWarp.linkGroups.begin(), colorWarp.linkGroups.end(),
            [&](const auto& candidate) {
                return candidate.id == args.ui.selectedColorWarpGroupId;
            });
        if (group != colorWarp.linkGroups.end()) {
            std::vector<RawColorWarpPin*> members;
            ImVec2 centroid(0.0f, 0.0f);
            for (auto& pin : colorWarp.pins) {
                if (std::find(group->pinIds.begin(), group->pinIds.end(), pin.id) ==
                    group->pinIds.end()) continue;
                members.push_back(&pin);
                centroid.x += pin.targetA;
                centroid.y += pin.targetB;
            }
            if (!members.empty()) {
                centroid.x /= static_cast<float>(members.size());
                centroid.y /= static_cast<float>(members.size());
                const auto movedCenter = canvasTransform.MoveByPixels(
                    centroid.x, centroid.y, io.MouseDelta.x * 0.5f, io.MouseDelta.y * 0.5f);
                const ImVec2 delta(movedCenter.x - centroid.x, movedCenter.y - centroid.y);
                const float angle = io.MouseDelta.x * 0.012f;
                const float scale = std::exp(io.MouseDelta.x * 0.012f);
                const float pull = std::clamp(io.MouseDelta.x * 0.012f, -0.25f, 0.25f);
                const RawColorWarpPin* chosen = args.ui.selectedColorWarpPin >= 0 &&
                        args.ui.selectedColorWarpPin < static_cast<int>(colorWarp.pins.size())
                    ? &colorWarp.pins[static_cast<std::size_t>(args.ui.selectedColorWarpPin)]
                    : members.front();
                float sharedAmount = 1.0f;
                std::vector<ImVec2> candidates(members.size());
                for (int attempt = 0; attempt < 12; ++attempt) {
                    bool fits = true;
                    for (std::size_t index = 0; index < members.size(); ++index) {
                        const auto& pin = *members[index];
                        ImVec2 next(pin.targetA, pin.targetB);
                        if (args.ui.colorWarpGroupOperation == 1) {
                            const float appliedAngle = angle * sharedAmount;
                            const float c = std::cos(appliedAngle);
                            const float s = std::sin(appliedAngle);
                            const float x = pin.targetA - centroid.x;
                            const float y = pin.targetB - centroid.y;
                            next = ImVec2(
                                centroid.x + x * c - y * s,
                                centroid.y + x * s + y * c);
                        } else if (args.ui.colorWarpGroupOperation == 2) {
                            const float appliedScale = 1.0f + (scale - 1.0f) * sharedAmount;
                            next = ImVec2(
                                centroid.x + (pin.targetA - centroid.x) * appliedScale,
                                centroid.y + (pin.targetB - centroid.y) * appliedScale);
                        } else if (args.ui.colorWarpGroupOperation == 3) {
                            next = ImVec2(
                                pin.targetA + (chosen->targetA - pin.targetA) * pull * sharedAmount,
                                pin.targetB + (chosen->targetB - pin.targetB) * pull * sharedAmount);
                        } else if (args.ui.colorWarpGroupOperation == 4) {
                            const float appliedScale = 1.0f + (scale - 1.0f) * sharedAmount;
                            next = ImVec2(pin.targetA * appliedScale, pin.targetB * appliedScale);
                        } else {
                            next = ImVec2(
                                pin.targetA + delta.x * sharedAmount,
                                pin.targetB + delta.y * sharedAmount);
                        }
                        candidates[index] = next;
                        if (std::hypot(next.x, next.y) > coordinateLimit) fits = false;
                        if (io.KeyShift &&
                            std::hypot(
                                pin.sourceA + next.x - pin.targetA,
                                pin.sourceB + next.y - pin.targetB) >
                                coordinateLimit) {
                            fits = false;
                        }
                    }
                    if (fits) break;
                    sharedAmount *= 0.5f;
                }
                for (std::size_t index = 0; index < members.size(); ++index) {
                    auto& pin = *members[index];
                    const float moveA = candidates[index].x - pin.targetA;
                    const float moveB = candidates[index].y - pin.targetB;
                    pin.targetA = candidates[index].x;
                    pin.targetB = candidates[index].y;
                    if (io.KeyShift) {
                        pin.sourceA += moveA;
                        pin.sourceB += moveB;
                    }
                }
                changed = true;
                args.ui.colorWarpInteractionActive = true;
                pointDragActive = true;
            }
        }
    } else if (ImGui::IsItemActive() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.5f) &&
        args.ui.colorWarpGestureOwner ==
            kColorWarpGestureProvisional &&
        args.ui.colorWarpDraggingProvisional &&
        args.ui.colorWarpProvisionalPinValid &&
        colorWarp.pins.size() < Stack::RawRecipe::kMaxRawColorWarpPins) {
        const ImVec2 source(
            args.ui.colorWarpProvisionalA,
            args.ui.colorWarpProvisionalB);
        constexpr float pointDragSensitivity = 0.5f;
        const auto sourceScreen = canvasTransform.ToScreen(source.x, source.y);
        const ImVec2 slowedTarget = canvasTransform.FromScreen(ImVec2(
            sourceScreen.x + (mouse.x - sourceScreen.x) * pointDragSensitivity,
            sourceScreen.y + (mouse.y - sourceScreen.y) * pointDragSensitivity));
        addCommittedPin(source, slowedTarget);
        args.ui.colorWarpProvisionalPinValid = false;
        args.ui.colorWarpProvisionalSceneEvValid = false;
        args.ui.colorWarpDraggingProvisional = false;
        args.ui.colorWarpGestureOwner =
            kColorWarpGestureTarget;
        changed = true;
        args.ui.colorWarpInteractionActive = true;
        pointDragActive = true;
    } else if (ImGui::IsItemActive() &&
               ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.5f) &&
               (args.ui.colorWarpGestureOwner ==
                    kColorWarpGestureTarget ||
                args.ui.colorWarpGestureOwner ==
                    kColorWarpGestureSource) &&
               args.ui.selectedColorWarpPin >= 0 &&
               args.ui.selectedColorWarpPin <
                   static_cast<int>(colorWarp.pins.size())) {
        RawColorWarpPin& pin = colorWarp.pins[static_cast<std::size_t>(
            args.ui.selectedColorWarpPin)];
        constexpr float pointDragSensitivity = 0.5f;

        if (args.ui.colorWarpDraggingSource) {
            const ImVec2 nextSource = canvasTransform.MoveByPixels(pin.sourceA, pin.sourceB,
                io.MouseDelta.x * pointDragSensitivity, io.MouseDelta.y * pointDragSensitivity);
            pin.sourceA = nextSource.x;
            pin.sourceB = nextSource.y;
        } else if (!pin.protectColor) {
            const ImVec2 nextTarget = canvasTransform.MoveByPixels(pin.targetA, pin.targetB,
                io.MouseDelta.x * pointDragSensitivity, io.MouseDelta.y * pointDragSensitivity);
            pin.targetA = nextTarget.x;
            pin.targetB = nextTarget.y;
        }
        changed = true;
        args.ui.colorWarpInteractionActive = true;
        pointDragActive = true;
    }
    if (pointDragActive) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        args.ui.colorWarpDraggingProvisional = false;
        args.ui.colorWarpQualifierDragHandle = 0;
    }

    if (ImGui::IsItemActive() &&
        args.ui.colorWarpGestureOwner ==
            kColorWarpGesturePan &&
        ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        ImGuiExtras::SubmitCursorCaptureRequest(
            ImGuiExtras::CursorCaptureRequest {
                ImGuiExtras::CursorCaptureMode::LockedPan,
                args.ui.colorWarpPanAnchorScreenPos,
                args.ui.colorWarpPanRestoreScreenPos
            });
        const float coordinatePerPixel =
            2.0f * viewHalfHeight / std::max(1.0f, canvasHeight);
        args.ui.colorWarpViewCenterA = std::clamp(
            args.ui.colorWarpViewCenterA -
                io.MouseDelta.x * coordinatePerPixel,
            -coordinateLimit,
            coordinateLimit);
        args.ui.colorWarpViewCenterB = std::clamp(
            args.ui.colorWarpViewCenterB +
                io.MouseDelta.y * coordinatePerPixel,
            -coordinateLimit,
            coordinateLimit);
        args.ui.colorWarpViewTargetCenterA =
            args.ui.colorWarpViewCenterA;
        args.ui.colorWarpViewTargetCenterB =
            args.ui.colorWarpViewCenterB;
        args.ui.colorWarpViewTargetExtent =
            args.ui.colorWarpViewExtent;
        args.ui.colorWarpViewNeedsFit = false;
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }

    if ((canvasHovered || pointDragActive) &&
        !io.KeyAlt &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Middle) &&
        std::abs(io.MouseWheel) > 0.0001f) {
        bool adjustedPin = false;
        const bool selectedPinValid =
            args.ui.selectedColorWarpPin >= 0 &&
            args.ui.selectedColorWarpPin <
                static_cast<int>(colorWarp.pins.size());
        const bool modifierPinAdjustment =
            canvasHovered && selectedPinValid &&
            (io.KeyCtrl || io.KeyShift);
        const bool capturedPinGesture =
            args.ui.colorWarpGestureOwner ==
                kColorWarpGestureTarget ||
            args.ui.colorWarpGestureOwner ==
                kColorWarpGestureSource ||
            args.ui.colorWarpGestureOwner ==
                kColorWarpGestureQualifier ||
            args.ui.colorWarpGestureOwner ==
                kColorWarpGestureProvisional;
        if ((capturedPinGesture || pointDragActive ||
             modifierPinAdjustment) && selectedPinValid) {
            RawColorWarpPin& pin = colorWarp.pins[static_cast<std::size_t>(
                args.ui.selectedColorWarpPin)];
            if (io.KeyCtrl) {
                pin.qualifierDirectionality = std::clamp(
                    pin.qualifierDirectionality + io.MouseWheel * 0.06f,
                    0.0f,
                    1.0f);
            } else if (io.KeyShift) {
                pin.softness = std::clamp(
                    pin.softness + io.MouseWheel * 0.04f,
                    0.0f,
                    1.0f);
            } else {
                pin.radius = std::clamp(
                    pin.radius * std::pow(1.12f, io.MouseWheel),
                    0.01f,
                    0.50f);
            }
            changed = true;
            adjustedPin = true;
            args.ui.colorWarpInteractionActive = true;
        }
        if (!adjustedPin &&
            args.ui.colorWarpGestureOwner ==
                kColorWarpGestureNone) {
            const ImVec2 anchorCoordinate = canvasTransform.DisplayFromScreen(mouse);
            const float nextHalfHeight = std::clamp(
                args.ui.colorWarpViewTargetExtent *
                    std::pow(0.86f, io.MouseWheel),
                0.035f,
                coordinateLimit);
            const float nextHalfWidth = nextHalfHeight * canvasAspect;
            const float horizontalFraction =
                (mouse.x - canvasMin.x) / canvasWidth - 0.5f;
            const float verticalFraction =
                0.5f - (mouse.y - canvasMin.y) / canvasHeight;
            args.ui.colorWarpViewTargetExtent = nextHalfHeight;
            args.ui.colorWarpViewTargetCenterA = std::clamp(
                anchorCoordinate.x - horizontalFraction * 2.0f * nextHalfWidth,
                -coordinateLimit,
                coordinateLimit);
            args.ui.colorWarpViewTargetCenterB = std::clamp(
                anchorCoordinate.y - verticalFraction * 2.0f * nextHalfHeight,
                -coordinateLimit,
                coordinateLimit);
            args.ui.colorWarpViewNeedsFit = false;
        }
    }

    if (canvasHovered &&
        args.ui.selectedColorWarpPin >= 0 &&
        args.ui.selectedColorWarpPin <
            static_cast<int>(colorWarp.pins.size()) &&
        !io.WantTextInput && !io.KeyAlt) {
        RawColorWarpPin& pin = colorWarp.pins[static_cast<std::size_t>(
            args.ui.selectedColorWarpPin)];
        const float nudge = 2.0f * viewHalfHeight /
            std::max(1.0f, canvasHeight);
        float deltaA = 0.0f;
        float deltaB = 0.0f;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) deltaA -= nudge;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) deltaA += nudge;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) deltaB -= nudge;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) deltaB += nudge;
        bool keyboardChanged = false;
        if (io.KeyCtrl && std::abs(deltaB) > 0.0f) {
            pin.qualifierDirectionality = std::clamp(
                pin.qualifierDirectionality + deltaB * 2.0f,
                0.0f,
                1.0f);
            keyboardChanged = true;
        } else if (std::abs(deltaA) > 0.0f || std::abs(deltaB) > 0.0f) {
            if (io.KeyShift) {
                const ImVec2 next = canvasTransform.MoveByPixels(pin.sourceA, pin.sourceB,
                    deltaA / nudge, -deltaB / nudge);
                pin.sourceA = next.x;
                pin.sourceB = next.y;
            } else if (!pin.protectColor) {
                const ImVec2 next = canvasTransform.MoveByPixels(pin.targetA, pin.targetB,
                    deltaA / nudge, -deltaB / nudge);
                pin.targetA = next.x;
                pin.targetB = next.y;
            }
            keyboardChanged = true;
        }
        if (keyboardChanged) {
            changed = true;
            args.ui.colorWarpInteractionActive = true;
        }
    }

    ImGuiWindow* surfaceWindow = ImGui::GetCurrentWindow();
    ImGuiWindow* rawWorkspaceWindow = nullptr;
    for (ImGuiWindow* ancestor = surfaceWindow;
         ancestor != nullptr;
         ancestor = ancestor->ParentWindow) {
        if (ancestor->Name != nullptr &&
            std::strstr(ancestor->Name, "RawWorkspaceLabRoot") != nullptr) {
            rawWorkspaceWindow = ancestor;
        }
    }
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImDrawList* backdropDrawList = rawWorkspaceWindow != nullptr
        ? rawWorkspaceWindow->DrawList
        : drawList;
    const ImVec2 backdropClipMinimum = rawWorkspaceWindow != nullptr
        ? rawWorkspaceWindow->Pos
        : canvasMin;
    const ImVec2 backdropClipMaximum = rawWorkspaceWindow != nullptr
        ? ImVec2(
              rawWorkspaceWindow->Pos.x + rawWorkspaceWindow->Size.x,
              rawWorkspaceWindow->Pos.y + rawWorkspaceWindow->Size.y)
        : canvasMax;
    backdropDrawList->PushClipRect(
        backdropClipMinimum,
        backdropClipMaximum,
        true);
    const float coordinatePixels =
        canvasHeight / (2.0f * args.ui.colorWarpViewExtent);
    const ImVec2 neutral = canvasTransform.ToScreen(0.0f, 0.0f);
    const float discScreenRadius = fullWheelExtent * coordinatePixels;
    constexpr float pi = 3.14159265358979323846f;
    args.ui.colorWarpWheelRenderer.QueueDraw(
        *backdropDrawList,
        canvasTransform.DisplayToScreen(-fullWheelExtent, fullWheelExtent),
        canvasTransform.DisplayToScreen(fullWheelExtent, -fullWheelExtent),
        args.workingSpace, fixedDiscLightness, fullWheelExtent);

    const ImU32 gridColor = IM_COL32(20, 24, 27, 22);
    for (float radius = 0.1f;
         radius < fullWheelExtent - 0.001f;
         radius += 0.1f) {
        backdropDrawList->AddCircle(
            neutral,
            ProjectColorDisc(radius, 0.0f).x * coordinatePixels,
            gridColor,
            96,
            0.8f);
    }
    for (int spoke = 0; spoke < 12; ++spoke) {
        const float angle = static_cast<float>(spoke) * pi / 6.0f;
        const float directionA = std::cos(angle);
        const float directionB = std::sin(angle);
        backdropDrawList->AddLine(
            neutral,
            canvasTransform.DisplayToScreen(
                directionA * fullWheelExtent,
                directionB * fullWheelExtent),
            gridColor,
            0.75f);
    }
    backdropDrawList->AddCircle(
        neutral,
        fullWheelExtent * coordinatePixels,
        IM_COL32(18, 21, 24, 86),
        160,
        1.15f);

    // A deliberately soft, non-interactive echo remains visible outside the
    // editing rectangle. The sharp manipulation drawing below still owns the
    // canvas itself. Layered wide strokes approximate a strong UI blur while
    // keeping the backdrop independent from hit testing.
    for (const RawColorWarpPin& pin : colorWarp.pins) {
        const ImVec2 source =
            canvasTransform.ToScreen(pin.sourceA, pin.sourceB);
        const ImVec2 target =
            canvasTransform.ToScreen(pin.targetA, pin.targetB);
        const auto blurredColor = [&](float alpha) {
            return palette.CoordinateColor(
                pin.targetA,
                pin.targetB,
                alpha);
        };
        backdropDrawList->AddLine(
            source, target, blurredColor(0.025f), 22.0f);
        backdropDrawList->AddLine(
            source, target, blurredColor(0.045f), 12.0f);
        backdropDrawList->AddLine(
            source, target, blurredColor(0.075f), 5.0f);
        for (const ImVec2& position : { source, target }) {
            backdropDrawList->AddCircleFilled(
                position, 23.0f, blurredColor(0.020f), 32);
            backdropDrawList->AddCircleFilled(
                position, 14.0f, blurredColor(0.040f), 28);
            backdropDrawList->AddCircleFilled(
                position, 7.0f, blurredColor(0.075f), 20);
        }
        if ((&pin - colorWarp.pins.data()) ==
            args.ui.selectedColorWarpPin) {
            backdropDrawList->AddCircle(
                source,
                std::max(3.0f, pin.radius * coordinatePixels),
                blurredColor(0.035f),
                96,
                14.0f);
        }
    }
    backdropDrawList->PopClipRect();

    // The wheel is a panel backdrop, but manipulation UI remains confined to
    // the explicit editing region so handles and labels cannot overlap the
    // controls surrounding it.
    drawList->PushClipRect(canvasMin, canvasMax, true);

    const int inspectedColorWarpPin =
        args.ui.colorWarpHoveredSelection >= 0 &&
        args.ui.colorWarpHoveredSelection <
            static_cast<int>(colorWarp.pins.size())
        ? args.ui.colorWarpHoveredSelection
        : args.ui.selectedColorWarpPin;
    const bool showSelectedCatch = ImGui::IsKeyDown(ImGuiKey_H) &&
        inspectedColorWarpPin >= 0 &&
        inspectedColorWarpPin <
            static_cast<int>(colorWarp.pins.size());
    const float cloudDotScale = std::clamp(
        discScreenRadius / 230.0f,
        0.55f,
        1.80f);
    std::size_t cloudGpuFingerprint = static_cast<std::size_t>(0x8f3d5b79u);
    HashRawLabColorCloudValue(
        cloudGpuFingerprint,
        std::hash<std::string>{}(args.ui.colorWarpCloudSourceKey));
    HashRawLabColorCloudValue(
        cloudGpuFingerprint,
        args.ui.colorWarpCloudInputFingerprint);
    HashRawLabColorCloudValue(
        cloudGpuFingerprint,
        static_cast<std::size_t>(args.ui.colorWarpCloudWorkingSpace + 1));
    HashRawLabColorCloudValue(cloudGpuFingerprint, colorCloud.size());
    HashRawLabColorCloudValue(
        cloudGpuFingerprint,
        args.ui.colorWarpLiveCloud ? 1u : 0u);
    HashRawLabColorCloudValue(
        cloudGpuFingerprint,
        showSelectedCatch ?
            static_cast<std::size_t>(
                inspectedColorWarpPin + 2) :
            0u);
    const auto hashColorWarpFloat = [&](float value) {
        HashRawLabColorCloudValue(
            cloudGpuFingerprint,
            std::hash<float>{}(value));
    };
    if (args.ui.colorWarpLiveCloud) {
        HashRawLabColorCloudValue(
            cloudGpuFingerprint,
            colorWarp.enabled ? 1u : 0u);
        hashColorWarpFloat(colorWarp.strength);
        HashRawLabColorCloudValue(cloudGpuFingerprint, colorWarp.pins.size());
        for (const RawColorWarpPin& pin : colorWarp.pins) {
            HashRawLabColorCloudValue(cloudGpuFingerprint, pin.enabled ? 1u : 0u);
            HashRawLabColorCloudValue(cloudGpuFingerprint, pin.protectColor ? 1u : 0u);
            hashColorWarpFloat(pin.sourceA);
            hashColorWarpFloat(pin.sourceB);
            hashColorWarpFloat(pin.targetA);
            hashColorWarpFloat(pin.targetB);
            hashColorWarpFloat(pin.radius);
            hashColorWarpFloat(pin.softness);
            hashColorWarpFloat(pin.qualifierDirectionality);
            hashColorWarpFloat(pin.qualifierOrientationRadians);
            hashColorWarpFloat(pin.qualifierAperture);
            hashColorWarpFloat(pin.strength);
            HashRawLabColorCloudValue(
                cloudGpuFingerprint, pin.evCurve.samples.size());
            for (const float sample : pin.evCurve.samples) {
                hashColorWarpFloat(sample);
            }
            hashColorWarpFloat(pin.lightnessDeltaEv);
        }
    } else if (showSelectedCatch) {
        const RawColorWarpPin& selected = colorWarp.pins[static_cast<std::size_t>(
            inspectedColorWarpPin)];
        hashColorWarpFloat(selected.sourceA);
        hashColorWarpFloat(selected.sourceB);
        hashColorWarpFloat(selected.radius);
        hashColorWarpFloat(selected.softness);
        hashColorWarpFloat(selected.qualifierDirectionality);
        hashColorWarpFloat(selected.qualifierOrientationRadians);
        hashColorWarpFloat(selected.qualifierAperture);
        for (const float sample : selected.evCurve.samples) {
            hashColorWarpFloat(sample);
        }
    }

    const bool cloudRendererReady = EnsureRawLabColorCloudRenderer(
        args.ui);
    if (cloudRendererReady &&
        args.ui.colorWarpCloudGpuFingerprint !=
            cloudGpuFingerprint) {
        std::vector<RawLabColorCloudGpuVertex> gpuVertices;
        gpuVertices.reserve(colorCloud.size());
        for (const auto& point : colorCloud) {
            std::array<float, 3> rgb = point.rgb;
            RawColorWarpCoordinate coordinate;
            coordinate.a = point.a;
            coordinate.b = point.b;
            coordinate.sceneEv = point.sceneEv;
            if (args.ui.colorWarpLiveCloud) {
                rgb = Stack::RawRecipe::ApplyPreparedColorWarp(
                    colorWarp,
                    point.rgb,
                    args.workingSpace);
                coordinate =
                    Stack::RawRecipe::WorkingRgbToColorWarpCoordinate(
                        rgb,
                        args.workingSpace);
            }
            if (!std::isfinite(coordinate.a) ||
                !std::isfinite(coordinate.b) ||
                !std::isfinite(coordinate.sceneEv) ||
                !std::isfinite(rgb[0]) ||
                !std::isfinite(rgb[1]) ||
                !std::isfinite(rgb[2])) {
                continue;
            }

            float selectedCatch = 0.0f;
            if (showSelectedCatch) {
                const RawColorWarpPin& selected =
                    colorWarp.pins[static_cast<std::size_t>(
                        inspectedColorWarpPin)];
                const float shapeWeight =
                    Stack::RawRecipe::EvaluateColorWarpPinShapeWeight(
                        selected,
                        coordinate.a,
                        coordinate.b);
                const float lightnessWeight =
                    Stack::RawRecipe::EvaluateColorWarpPinLightnessWeight(
                        selected,
                        coordinate.sceneEv);
                selectedCatch = shapeWeight > 0.0f && lightnessWeight > 0.0f
                    ? 1.0f : 0.0f;
            }

            const auto projected = ProjectColorDisc(coordinate.a, coordinate.b);
            rgb = Stack::Editor::RawLabInternal::ColorDiscDisplayRgb(rgb, args.workingSpace);
            gpuVertices.push_back({
                projected.x,
                projected.y,
                rgb[0],
                rgb[1],
                rgb[2],
                coordinate.sceneEv,
                selectedCatch
            });
        }

        GLint previousArrayBuffer = 0;
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
        glBindBuffer(
            GL_ARRAY_BUFFER,
            args.ui.colorWarpCloudVertexBuffer);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(
                gpuVertices.size() * sizeof(RawLabColorCloudGpuVertex)),
            gpuVertices.empty() ? nullptr : gpuVertices.data(),
            GL_DYNAMIC_DRAW);
        glBindBuffer(
            GL_ARRAY_BUFFER,
            static_cast<unsigned int>(previousArrayBuffer));
        args.ui.colorWarpCloudGpuPointCount =
            static_cast<int>(gpuVertices.size());
        args.ui.colorWarpCloudGpuFingerprint =
            cloudGpuFingerprint;
    }

    const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
    const float cloudPhysicalScale = std::max(
        0.5f,
        std::min(framebufferScale.x, framebufferScale.y));
    if (cloudRendererReady &&
        args.ui.colorWarpCloudGpuPointCount > 0) {
        const ImGuiViewport* windowViewport = ImGui::GetWindowViewport();
        const ImVec2 displayPos = windowViewport
            ? windowViewport->Pos
            : ImVec2(0.0f, 0.0f);
        const ImVec2 displaySize = windowViewport
            ? windowViewport->Size
            : ImGui::GetIO().DisplaySize;
        args.ui.colorWarpCloudCanvasMinX = canvasMin.x;
        args.ui.colorWarpCloudCanvasMinY = canvasMin.y;
        args.ui.colorWarpCloudCanvasWidth = canvasWidth;
        args.ui.colorWarpCloudCanvasHeight = canvasHeight;
        args.ui.colorWarpCloudDisplayPosX = displayPos.x;
        args.ui.colorWarpCloudDisplayPosY = displayPos.y;
        args.ui.colorWarpCloudDisplayWidth = displaySize.x;
        args.ui.colorWarpCloudDisplayHeight = displaySize.y;
        args.ui.colorWarpCloudFramebufferScaleX =
            std::max(0.5f, framebufferScale.x);
        args.ui.colorWarpCloudFramebufferScaleY =
            std::max(0.5f, framebufferScale.y);
        args.ui.colorWarpCloudViewCenterA =
            args.ui.colorWarpViewCenterA;
        args.ui.colorWarpCloudViewCenterB =
            args.ui.colorWarpViewCenterB;
        args.ui.colorWarpCloudViewHalfWidth = viewHalfWidth;
        args.ui.colorWarpCloudViewHalfHeight = viewHalfHeight;
        args.ui.colorWarpCloudPointScale =
            cloudDotScale * cloudPhysicalScale;
        args.ui.colorWarpCloudWorkingRange =
            0;
        args.ui.colorWarpCloudShowSelectedCatch =
            showSelectedCatch;
        drawList->AddCallback(
            DrawRawLabColorCloudCallback,
            &args.ui);
        if (ImGui::GetPlatformIO().DrawCallback_ResetRenderState) {
            drawList->AddCallback(
                ImGui::GetPlatformIO().DrawCallback_ResetRenderState,
                nullptr);
        }
    }
    if (colorCloud.empty()) {
        drawList->AddText(
            ImVec2(canvasMin.x + 10.0f, canvasMax.y - 24.0f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            "Using the last settled color map when available.");
    }

    if (args.ui.colorWarpPhotoHoverValid) {
        const ImVec2 hover = canvasTransform.ToScreen(
            args.ui.colorWarpPhotoHoverA,
            args.ui.colorWarpPhotoHoverB);
        if (hover.x >= canvasMin.x && hover.x <= canvasMax.x &&
            hover.y >= canvasMin.y && hover.y <= canvasMax.y) {
            const ImU32 hoverColor = IM_COL32(8, 11, 14, 245);
            const ImU32 hoverShadow = IM_COL32(255, 255, 255, 105);
            drawList->AddLine(
                ImVec2(hover.x - 9.0f, hover.y),
                ImVec2(hover.x + 9.0f, hover.y),
                hoverShadow,
                3.2f);
            drawList->AddLine(
                ImVec2(hover.x, hover.y - 9.0f),
                ImVec2(hover.x, hover.y + 9.0f),
                hoverShadow,
                3.2f);
            drawList->AddLine(
                ImVec2(hover.x - 8.0f, hover.y),
                ImVec2(hover.x + 8.0f, hover.y),
                hoverColor,
                1.2f);
            drawList->AddLine(
                ImVec2(hover.x, hover.y - 8.0f),
                ImVec2(hover.x, hover.y + 8.0f),
                hoverColor,
                1.2f);
            drawList->AddCircle(hover, 4.0f, hoverShadow, 16, 3.0f);
            drawList->AddCircle(hover, 3.0f, hoverColor, 16, 1.0f);
        }
    }

    if (args.ui.colorWarpProvisionalPinValid) {
        const ImVec2 provisional = canvasTransform.ToScreen(
            args.ui.colorWarpProvisionalA,
            args.ui.colorWarpProvisionalB);
        if (provisional.x >= canvasMin.x - 8.0f &&
            provisional.x <= canvasMax.x + 8.0f &&
            provisional.y >= canvasMin.y - 8.0f &&
            provisional.y <= canvasMax.y + 8.0f) {
            drawList->AddCircleFilled(
                provisional,
                7.0f,
                palette.CoordinateColor(
                    args.ui.colorWarpProvisionalA,
                    args.ui.colorWarpProvisionalB,
                    0.95f),
                20);
            drawList->AddCircle(
                provisional,
                9.0f,
                IM_COL32(8, 11, 14, 245),
                24,
                1.8f);
        }
    }

    for (std::size_t index = 0; index < colorWarp.pins.size(); ++index) {
        const RawColorWarpPin& pin = colorWarp.pins[index];
        const bool selected =
            static_cast<int>(index) == inspectedColorWarpPin;
        const ImVec2 source = canvasTransform.ToScreen(pin.sourceA, pin.sourceB);
        const ImVec2 target = canvasTransform.ToScreen(pin.targetA, pin.targetB);
        const ImU32 pinColor = pin.protectColor
            ? IM_COL32(240, 196, 86, 235)
            : (selected
                ? IM_COL32(8, 11, 14, 245)
                : IM_COL32(120, 226, 224, 185));
        if (!pin.protectColor) {
            DrawRawLabArrow(
                drawList,
                source,
                target,
                pinColor,
                selected ? 1.8f : 1.0f);
        }
        if (selected) {
            constexpr int qualifierSegments = 96;
            std::array<ImVec2, qualifierSegments + 1> outerContour {};
            std::array<ImVec2, qualifierSegments + 1> innerContour {};
            const float coreScale = std::max(0.0f, 1.0f - pin.softness);
            for (int segment = 0; segment <= qualifierSegments; ++segment) {
                const float angle = 2.0f * pi *
                    static_cast<float>(segment) /
                    static_cast<float>(qualifierSegments);
                const float rayA = std::cos(angle);
                const float rayB = std::sin(angle);
                const float shapeDistance = std::max(
                    0.000001f,
                    Stack::RawRecipe::EvaluateColorWarpPinShapeDistance(
                        pin,
                        pin.sourceA + rayA * pin.radius,
                        pin.sourceB + rayB * pin.radius));
                const float boundaryRadius = pin.radius / shapeDistance;
                outerContour[static_cast<std::size_t>(segment)] = canvasTransform.ToScreen(
                    pin.sourceA + rayA * boundaryRadius,
                    pin.sourceB + rayB * boundaryRadius);
                innerContour[static_cast<std::size_t>(segment)] = canvasTransform.ToScreen(
                    pin.sourceA + rayA * boundaryRadius * coreScale,
                    pin.sourceB + rayB * boundaryRadius * coreScale);
            }
            if (coreScale > 0.001f) {
                drawList->AddPolyline(
                    innerContour.data(),
                    static_cast<int>(innerContour.size()),
                    IM_COL32(8, 11, 14, 190),
                    0,
                    1.2f);
            }
            for (int segment = 0; segment < qualifierSegments; segment += 2) {
                drawList->AddLine(
                    outerContour[static_cast<std::size_t>(segment)],
                    outerContour[static_cast<std::size_t>(segment + 1)],
                    IM_COL32(8, 11, 14, 215),
                    1.15f);
            }
            ImVec2 orientationHandle;
            ImVec2 apertureHandlePositive;
            ImVec2 apertureHandleNegative;
            qualifierHandlePositions(
                pin,
                orientationHandle,
                apertureHandlePositive,
                apertureHandleNegative);
            drawList->AddLine(
                source,
                orientationHandle,
                IM_COL32(8, 11, 14, 150),
                1.0f);
            drawList->AddCircleFilled(
                orientationHandle,
                4.5f,
                IM_COL32(8, 11, 14, 230),
                12);
            for (const ImVec2 handle : {
                     apertureHandlePositive,
                     apertureHandleNegative }) {
                drawList->AddRectFilled(
                    ImVec2(handle.x - 3.5f, handle.y - 3.5f),
                    ImVec2(handle.x + 3.5f, handle.y + 3.5f),
                    IM_COL32(8, 11, 14, 220),
                    1.5f);
            }
        }
        DrawRawLabDashedCircle(
            drawList,
            source,
            selected ? 7.5f : 5.5f,
            pinColor,
            selected ? 1.7f : 1.2f,
            24);
        const ImU32 targetFill = palette.CoordinateColor(
            pin.targetA,
            pin.targetB,
            0.95f);
        drawList->AddCircleFilled(
            target,
            selected ? 6.0f : 4.5f,
            targetFill,
            20);
        drawList->AddCircle(
            target,
            selected ? 6.5f : 5.0f,
            pinColor,
            20,
            selected ? 2.0f : 1.3f);
        if (selected && !pin.protectColor) {
            const float sourceHue = std::atan2(pin.sourceB, pin.sourceA);
            const float targetHue = std::atan2(pin.targetB, pin.targetA);
            float hueDelta = (targetHue - sourceHue) * 180.0f / pi;
            while (hueDelta > 180.0f) hueDelta -= 360.0f;
            while (hueDelta < -180.0f) hueDelta += 360.0f;
            const float chromaDelta =
                std::hypot(pin.targetA, pin.targetB) -
                std::hypot(pin.sourceA, pin.sourceB);
            char deltaText[64] {};
            std::snprintf(
                deltaText,
                sizeof(deltaText),
                "%+.0f deg  %+.3f",
                hueDelta,
                chromaDelta);
            const ImVec2 midpoint(
                (source.x + target.x) * 0.5f + 7.0f,
                (source.y + target.y) * 0.5f - 14.0f);
            drawList->AddText(
                midpoint,
                IM_COL32(8, 11, 14, 230),
                deltaText);
        }
        if (pin.protectColor && selected) {
            drawList->AddCircle(
                source,
                11.0f,
                IM_COL32(240, 196, 86, 220),
                24,
                1.5f);
        }
        if (!pin.enabled) {
            drawList->AddLine(
                ImVec2(source.x - 5.0f, source.y - 5.0f),
                ImVec2(source.x + 5.0f, source.y + 5.0f),
                IM_COL32(160, 160, 160, 220),
                1.5f);
        }
    }
    for (const auto& group : colorWarp.linkGroups) {
        ImVec2 centroid(0.0f, 0.0f);
        int memberCount = 0;
        for (const auto& pin : colorWarp.pins) {
            if (std::find(group.pinIds.begin(), group.pinIds.end(), pin.id) ==
                group.pinIds.end()) continue;
            centroid.x += pin.targetA;
            centroid.y += pin.targetB;
            ++memberCount;
        }
        if (memberCount < 2) continue;
        centroid.x /= memberCount;
        centroid.y /= memberCount;
        const ImVec2 center = canvasTransform.ToScreen(centroid.x, centroid.y);
        const bool selectedGroup = group.id == args.ui.selectedColorWarpGroupId;
        drawList->AddCircleFilled(
            center, selectedGroup ? 6.0f : 4.5f,
            IM_COL32(238, 160, 62, selectedGroup ? 245 : 195), 16);
        drawList->AddCircle(
            center, selectedGroup ? 10.0f : 7.5f,
            IM_COL32(18, 21, 24, 225), 20,
            selectedGroup ? 2.0f : 1.2f);
    }
    drawList->PopClipRect();
    if (ImGui::BeginPopup("RawLabColorWarpPinMenu")) {
        if (args.ui.selectedColorWarpPin >= 0 &&
            args.ui.selectedColorWarpPin <
                static_cast<int>(colorWarp.pins.size())) {
            RawColorWarpPin& selected = colorWarp.pins[static_cast<std::size_t>(
                args.ui.selectedColorWarpPin)];
            if (ImGui::MenuItem("Enabled", nullptr, selected.enabled)) {
                selected.enabled = !selected.enabled;
                changed = true;
            }
            LabTooltip(
                "Enable or bypass this pin without removing its stored edit.");
            if (ImGui::MenuItem("Protect color", nullptr, selected.protectColor)) {
                selected.protectColor = !selected.protectColor;
                if (selected.protectColor) {
                    selected.targetA = selected.sourceA;
                    selected.targetB = selected.sourceB;
                    selected.lightnessDeltaEv = 0.0f;
                }
                changed = true;
            }
            LabTooltip(
                "Return this pin's destination to its source and prevent hue, "
                "chroma, or lightness displacement for the protected color.");
            const auto sampledRegion = std::find_if(
                colorWarp.regions.begin(), colorWarp.regions.end(),
                [&](const auto& region) { return region.id == selected.regionId; });
            if (sampledRegion != colorWarp.regions.end() &&
                !sampledRegion->circles.empty()) {
                if (ImGui::MenuItem("Edit sampled area")) {
                    args.ui.colorWarpPendingCircle = sampledRegion->circles.front();
                    args.ui.colorWarpPendingCircleActive = true;
                    args.ui.colorWarpPendingCircleDrawing = false;
                    args.ui.colorWarpPendingCircleRefining = false;
                    args.ui.colorWarpPendingReplaceRegionId = sampledRegion->id;
                    args.ui.colorWarpPendingReplaceCircleId =
                        sampledRegion->circles.front().id;
                    args.ui.colorWarpAreaAnalysisResult = {};
                    args.ui.colorWarpAreaStatus = "Adjust and release to refine";
                }
                LabTooltip(
                    "Recall the first saved sample circle as a pending copy. "
                    "Committing replaces the saved region atomically; Escape cancels it.");
            }
            ImGui::Separator();
            const bool qualifierShapeMenuOpen = ImGui::BeginMenu("Qualifier shape");
            LabTooltip(
                "Choose how the selected pin admits nearby colors around its "
                "source: equally in all directions or biased along an orientation.");
            if (qualifierShapeMenuOpen) {
                if (ImGui::MenuItem(
                        "Circle",
                        nullptr,
                        selected.qualifierDirectionality <= 0.001f)) {
                    selected.qualifierDirectionality = 0.0f;
                    selected.qualifierAperture = 0.45f;
                    changed = true;
                }
                LabTooltip(
                    "Use a round OKLab qualifier with equal reach in every hue/chroma direction.");
                if (ImGui::MenuItem(
                        "Directional lobe",
                        nullptr,
                        selected.qualifierDirectionality > 0.001f &&
                            selected.qualifierDirectionality < 0.999f)) {
                    selected.qualifierDirectionality = 0.5f;
                    selected.qualifierAperture = 0.60f;
                    changed = true;
                }
                LabTooltip(
                    "Use a rounded directional qualifier that favors colors "
                    "along the selected orientation but retains side and rear reach.");
                if (ImGui::MenuItem(
                        "Cone",
                        nullptr,
                        selected.qualifierDirectionality >= 0.999f)) {
                    selected.qualifierDirectionality = 1.0f;
                    selected.qualifierAperture = 0.35f;
                    changed = true;
                }
                LabTooltip(
                    "Use the strongest directional qualifier with reduced side and rear admission.");
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate pin")) {
                duplicateSelected = true;
            }
            LabTooltip(
                "Create another pin with the selected pin's current qualifier and edit settings.");
            if (ImGui::MenuItem("Delete pin")) {
                deleteSelected = true;
            }
            LabTooltip("Remove the selected pin from the Color Warp recipe.");
        }
        ImGui::EndPopup();
    }

    if (args.ui.selectedColorWarpPin >= 0 &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
         ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        deleteSelected = true;
    }

    if (duplicateSelected &&
        args.ui.selectedColorWarpPin >= 0 &&
        args.ui.selectedColorWarpPin <
            static_cast<int>(colorWarp.pins.size()) &&
        colorWarp.pins.size() < Stack::RawRecipe::kMaxRawColorWarpPins) {
        RawColorWarpPin duplicate = colorWarp.pins[static_cast<std::size_t>(
            args.ui.selectedColorWarpPin)];
        duplicate.id = "color-" + std::to_string(colorWarp.pins.size() + 1u);
        std::unordered_set<std::string> ids;
        for (const RawColorWarpPin& existing : colorWarp.pins) {
            ids.insert(existing.id);
        }
        while (ids.find(duplicate.id) != ids.end()) {
            duplicate.id += "-copy";
        }
        duplicate.name = "Color " + std::to_string(colorWarp.pins.size() + 1u);
        colorWarp.pins.push_back(std::move(duplicate));
        args.ui.selectedColorWarpPin =
            static_cast<int>(colorWarp.pins.size() - 1u);
        changed = true;
    }
    if (deleteSelected &&
        args.ui.selectedColorWarpPin >= 0 &&
        args.ui.selectedColorWarpPin <
            static_cast<int>(colorWarp.pins.size())) {
        colorWarp.pins.erase(
            colorWarp.pins.begin() +
                args.ui.selectedColorWarpPin);
        args.ui.selectedColorWarpPin = colorWarp.pins.empty()
            ? -1
            : std::min(
                  args.ui.selectedColorWarpPin,
                  static_cast<int>(colorWarp.pins.size() - 1u));
        changed = true;
    }

    ImGui::Spacing();
    if (args.ui.colorWarpProvisionalPinValid) {
        ImGui::TextWrapped(
            "Provisional pin: drag it to retain the edit, or click away to discard it.");
        return changed;
    }
    if (colorWarp.pins.empty()) {
        ImGui::TextWrapped(
            "Click the photograph or the OKLab disc to place the first pin.");
        return changed;
    }

    args.ui.selectedColorWarpPin = std::clamp(
        args.ui.selectedColorWarpPin,
        0,
        static_cast<int>(colorWarp.pins.size() - 1u));
    const bool presentingHoveredPin =
        args.ui.colorWarpHoveredSelection >= 0 &&
        args.ui.colorWarpHoveredSelection <
            static_cast<int>(colorWarp.pins.size()) &&
        args.ui.colorWarpHoveredSelection != args.ui.selectedColorWarpPin;
    const int presentedPinIndex = presentingHoveredPin
        ? args.ui.colorWarpHoveredSelection
        : args.ui.selectedColorWarpPin;
    RawColorWarpPin& selected = colorWarp.pins[static_cast<std::size_t>(
        presentedPinIndex)];
    if (!presentingHoveredPin &&
        args.ui.colorWarpSmartSelection.active &&
        args.ui.colorWarpSmartSelection.pinId != selected.id) {
        args.ui.colorWarpSmartSelection = {};
    }
    if (!presentingHoveredPin &&
        args.ui.colorWarpEvGesture == 0 &&
        args.ui.colorWarpSmartSelection.active &&
        args.ui.colorWarpSmartSelection.pinId == selected.id &&
        args.ui.colorWarpSmartSelection.background.size() ==
            selected.evCurve.samples.size() &&
        args.ui.colorWarpSmartSelection.residual.size() ==
            selected.evCurve.samples.size()) {
        bool curveChangedOutsideSmartState = false;
        for (std::size_t index = 0; index < selected.evCurve.samples.size(); ++index) {
            const float expected =
                args.ui.colorWarpSmartSelection.background[index] +
                args.ui.colorWarpSmartSelection.residual[index];
            if (std::abs(expected - selected.evCurve.samples[index]) > 0.0002f) {
                curveChangedOutsideSmartState = true;
                break;
            }
        }
        if (curveChangedOutsideSmartState) {
            auto refreshed = args.ui.colorWarpSmartSelection;
            RawLabColorWarpBuildSmartComponents(
                selected.evCurve,
                refreshed.leftEv,
                refreshed.rightEv,
                refreshed.centerEv,
                refreshed);
            refreshed.pinId = selected.id;
            args.ui.colorWarpSmartSelection = std::move(refreshed);
        }
    }
    ImGui::BeginDisabled(presentingHoveredPin);

    constexpr std::size_t lightnessBinCount = 64;
    std::array<float, lightnessBinCount> lightnessBins {};
    float maximumLightnessBin = 0.0f;
    for (const auto& point : colorCloud) {
        const float weight =
            Stack::RawRecipe::EvaluateColorWarpPinShapeWeight(
                selected,
                point.a,
                point.b);
        if (weight <= 0.0f) {
            continue;
        }
        const float normalized = std::clamp(
            (point.sceneEv - sceneEvMinimum) /
                (sceneEvMaximum - sceneEvMinimum),
            0.0f,
            0.999999f);
        const std::size_t bin = std::min(
            lightnessBinCount - 1u,
            static_cast<std::size_t>(
                normalized * static_cast<float>(lightnessBinCount)));
        lightnessBins[bin] += weight;
        maximumLightnessBin = std::max(maximumLightnessBin, lightnessBins[bin]);
    }

    ImGui::TextDisabled(
        "%zu pins · Pin %d",
        colorWarp.pins.size(),
        presentedPinIndex + 1);
    ImGui::SameLine();
    ImGui::TextDisabled(
        "Reach %.3f · Feather %.0f%%",
        selected.radius,
        selected.softness * 100.0f);

    const ImVec2 stripMin = ImGui::GetCursorScreenPos();
    const float stripWidth = std::max(180.0f, ImGui::GetContentRegionAvail().x);
    constexpr float stripHeight = 126.0f;
    ImGui::InvisibleButton(
        "##RawLabColorWarpEvCurve",
        ImVec2(stripWidth, stripHeight),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const ImVec2 stripMax(stripMin.x + stripWidth, stripMin.y + stripHeight);
    if (ImGui::IsItemHovered() && args.ui.colorWarpEvEditMode == 0)
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    const auto evToX = [&](float ev) {
        return stripMin.x +
            std::clamp(
                (ev - sceneEvMinimum) /
                    (sceneEvMaximum - sceneEvMinimum),
                0.0f,
                1.0f) * stripWidth;
    };
    const auto xToEv = [&](float x) {
        return sceneEvMinimum +
            std::clamp((x - stripMin.x) / stripWidth, 0.0f, 1.0f) *
                (sceneEvMaximum - sceneEvMinimum);
    };
    const auto qualificationToY = [&](float qualification) {
        return stripMin.y + 8.0f +
            (1.0f - std::clamp(qualification, 0.0f, 1.0f)) *
                (stripHeight - 24.0f);
    };
    const auto yToQualification = [&](float y) {
        return 1.0f - std::clamp(
            (y - stripMin.y - 8.0f) / (stripHeight - 24.0f),
            0.0f,
            1.0f);
    };

    if (ImGui::IsItemHovered() &&
        args.ui.colorWarpEvEditMode == 1 &&
        args.ui.colorWarpEvGesture == 0 &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        args.ui.colorWarpSmartHoverSelection =
            RawLabColorWarpDetectSmartSelection(
                selected.evCurve,
                xToEv(io.MousePos.x),
                selected.id);
    } else if (!ImGui::IsItemHovered()) {
        args.ui.colorWarpSmartHoverSelection = {};
    }

    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f &&
        args.ui.colorWarpEvEditMode == 0) {
        args.ui.colorWarpEvBrushWidth = std::clamp(
            args.ui.colorWarpEvBrushWidth + io.MouseWheel * 0.20f,
            0.40f,
            8.0f);
    }
    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
        args.ui.colorWarpEvGesture == 0 &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        args.ui.colorWarpEvEditMode = 1 - args.ui.colorWarpEvEditMode;
        if (args.ui.colorWarpEvEditMode == 0) {
            args.ui.colorWarpSmartSelection = {};
        }
        args.ui.colorWarpEvGesture = 0;
    }
    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        args.ui.colorWarpEvGesturePinId = selected.id;
        args.ui.colorWarpEvGesturePress = io.MousePos;
        args.ui.colorWarpEvGestureLast = io.MousePos;
        args.ui.colorWarpEvGestureStartCurve = selected.evCurve;
        if (args.ui.colorWarpEvEditMode == 0) {
            args.ui.colorWarpEvGesture = 1;
            RawLabColorWarpPaintCurve(
                selected.evCurve,
                xToEv(io.MousePos.x),
                yToQualification(io.MousePos.y),
                args.ui.colorWarpEvBrushWidth);
            args.ui.colorWarpSmartSelection.active = false;
            changed = true;
        } else {
            const float clickedEv = xToEv(io.MousePos.x);
            const bool insideExisting =
                args.ui.colorWarpSmartSelection.active &&
                args.ui.colorWarpSmartSelection.pinId == selected.id &&
                clickedEv >= args.ui.colorWarpSmartSelection.leftEv &&
                clickedEv <= args.ui.colorWarpSmartSelection.rightEv;
            if (!insideExisting) {
                args.ui.colorWarpSmartSelection =
                    RawLabColorWarpDetectSmartSelection(
                        selected.evCurve, clickedEv, selected.id);
            }
            args.ui.colorWarpSmartGestureStart =
                args.ui.colorWarpSmartSelection;
            const float shoulderPixels = std::max(
                8.0f,
                (evToX(args.ui.colorWarpSmartSelection.rightEv) -
                 evToX(args.ui.colorWarpSmartSelection.leftEv)) * 0.22f);
            if (std::abs(io.MousePos.x - evToX(
                    args.ui.colorWarpSmartSelection.leftEv)) <= shoulderPixels) {
                args.ui.colorWarpEvGesture = 7;
            } else if (std::abs(io.MousePos.x - evToX(
                           args.ui.colorWarpSmartSelection.rightEv)) <=
                       shoulderPixels) {
                args.ui.colorWarpEvGesture = 8;
            } else {
                args.ui.colorWarpEvGesture = 2;
            }
        }
    }
    if (ImGui::IsItemActive() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) &&
        args.ui.colorWarpEvGesturePinId == selected.id) {
        args.ui.colorWarpLightnessRangePreviewActive = true;
        const float dx = io.MousePos.x - args.ui.colorWarpEvGesturePress.x;
        const float dy = io.MousePos.y - args.ui.colorWarpEvGesturePress.y;
        const float absDx = std::abs(dx);
        const float absDy = std::abs(dy);
        if ((args.ui.colorWarpEvGesture == 2 ||
             args.ui.colorWarpEvGesture == 7 ||
             args.ui.colorWarpEvGesture == 8) &&
            std::max(absDx, absDy) >= 8.0f) {
            const int pendingZone = args.ui.colorWarpEvGesture;
            if (absDy > absDx * 1.15f) {
                args.ui.colorWarpEvGesture = 3;
            } else if (pendingZone == 7) {
                args.ui.colorWarpEvGesture = 5;
            } else if (pendingZone == 8) {
                args.ui.colorWarpEvGesture = 6;
            } else {
                args.ui.colorWarpEvGesture = 4;
            }
        }

        if (args.ui.colorWarpEvGesture == 1) {
            const float startEv = xToEv(args.ui.colorWarpEvGestureLast.x);
            const float endEv = xToEv(io.MousePos.x);
            const float startQ = yToQualification(
                args.ui.colorWarpEvGestureLast.y);
            const float endQ = yToQualification(io.MousePos.y);
            const int steps = std::max(
                1,
                static_cast<int>(std::ceil(
                    std::abs(endEv - startEv) / 0.0625f)));
            for (int step = 1; step <= steps; ++step) {
                const float t = static_cast<float>(step) /
                    static_cast<float>(steps);
                RawLabColorWarpPaintCurve(
                    selected.evCurve,
                    startEv + (endEv - startEv) * t,
                    startQ + (endQ - startQ) * t,
                    args.ui.colorWarpEvBrushWidth);
            }
            args.ui.colorWarpEvGestureLast = io.MousePos;
            changed = true;
        } else if (args.ui.colorWarpEvGesture == 3) {
            const float requestedAmplitude =
                args.ui.colorWarpSmartGestureStart.amplitude -
                dy / (stripHeight - 24.0f);
            RawLabColorWarpApplySmartAmplitude(
                args.ui.colorWarpEvGestureStartCurve,
                args.ui.colorWarpSmartGestureStart,
                requestedAmplitude,
                selected.evCurve,
                args.ui.colorWarpSmartSelection);
            changed = true;
        } else if (args.ui.colorWarpEvGesture == 4) {
            const auto& start = args.ui.colorWarpSmartGestureStart;
            const float evDelta = dx / stripWidth *
                (sceneEvMaximum - sceneEvMinimum);
            const float width = start.rightEv - start.leftEv;
            const float newLeft = std::clamp(
                start.leftEv + evDelta,
                sceneEvMinimum,
                sceneEvMaximum - width);
            RawLabColorWarpApplySmartRemap(
                args.ui.colorWarpEvGestureStartCurve,
                start,
                newLeft,
                newLeft + width,
                start.centerEv + (newLeft - start.leftEv),
                0,
                selected.evCurve,
                args.ui.colorWarpSmartSelection);
            changed = true;
        } else if (args.ui.colorWarpEvGesture == 5) {
            const auto& start = args.ui.colorWarpSmartGestureStart;
            const float newLeft = std::clamp(
                xToEv(io.MousePos.x),
                sceneEvMinimum,
                start.centerEv - 0.30f);
            RawLabColorWarpApplySmartRemap(
                args.ui.colorWarpEvGestureStartCurve,
                start,
                newLeft,
                start.rightEv,
                start.centerEv,
                -1,
                selected.evCurve,
                args.ui.colorWarpSmartSelection);
            changed = true;
        } else if (args.ui.colorWarpEvGesture == 6) {
            const auto& start = args.ui.colorWarpSmartGestureStart;
            const float newRight = std::clamp(
                xToEv(io.MousePos.x),
                start.centerEv + 0.30f,
                sceneEvMaximum);
            RawLabColorWarpApplySmartRemap(
                args.ui.colorWarpEvGestureStartCurve,
                start,
                start.leftEv,
                newRight,
                start.centerEv,
                1,
                selected.evCurve,
                args.ui.colorWarpSmartSelection);
            changed = true;
        }
        if (changed) args.ui.colorWarpInteractionActive = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        args.ui.colorWarpEvGesture = 0;
        args.ui.colorWarpEvGesturePinId.clear();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
        args.ui.colorWarpEvGesture != 0) {
        selected.evCurve = args.ui.colorWarpEvGestureStartCurve;
        if (args.ui.colorWarpEvGesture != 1) {
            args.ui.colorWarpSmartSelection =
                args.ui.colorWarpSmartGestureStart;
        }
        args.ui.colorWarpEvGesture = 0;
        args.ui.colorWarpEvGesturePinId.clear();
        args.ui.colorWarpInteractionActive = true;
        changed = true;
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
               args.ui.colorWarpSmartSelection.active) {
        args.ui.colorWarpSmartSelection = {};
    }

    ImDrawList* stripDrawList = ImGui::GetWindowDrawList();
    stripDrawList->AddRectFilled(
        stripMin,
        stripMax,
        ImGui::GetColorU32(ImGuiCol_FrameBg),
        5.0f);
    const float graphTop = stripMin.y + 8.0f;
    const float graphBottom = stripMax.y - 16.0f;
    if (maximumLightnessBin > 0.0f) {
        for (std::size_t bin = 0; bin < lightnessBinCount; ++bin) {
            const float x0 = stripMin.x +
                stripWidth * static_cast<float>(bin) /
                    static_cast<float>(lightnessBinCount);
            const float x1 = stripMin.x +
                stripWidth * static_cast<float>(bin + 1u) /
                    static_cast<float>(lightnessBinCount);
            const float height = (graphBottom - graphTop) * 0.82f *
                lightnessBins[bin] / maximumLightnessBin;
            stripDrawList->AddRectFilled(
                ImVec2(x0, graphBottom - height),
                ImVec2(std::max(x0 + 1.0f, x1 - 0.5f), graphBottom),
                ImGui::GetColorU32(ImGuiCol_Text, 0.12f));
        }
    }
    stripDrawList->AddLine(
        ImVec2(stripMin.x, qualificationToY(1.0f)),
        ImVec2(stripMax.x, qualificationToY(1.0f)),
        ImGui::GetColorU32(ImGuiCol_Border, 0.50f));
    stripDrawList->AddLine(
        ImVec2(stripMin.x, qualificationToY(0.0f)),
        ImVec2(stripMax.x, qualificationToY(0.0f)),
        ImGui::GetColorU32(ImGuiCol_Border, 0.50f));
    stripDrawList->AddLine(
        ImVec2(evToX(0.0f), graphTop),
        ImVec2(evToX(0.0f), graphBottom),
        ImGui::GetColorU32(ImGuiCol_Border, 0.28f));

    if (args.ui.colorWarpEvEditMode == 1 &&
        args.ui.colorWarpSmartHoverSelection.active &&
        args.ui.colorWarpSmartHoverSelection.pinId == selected.id) {
        const float hoverLeft = evToX(
            args.ui.colorWarpSmartHoverSelection.leftEv);
        const float hoverRight = evToX(
            args.ui.colorWarpSmartHoverSelection.rightEv);
        stripDrawList->AddRect(
            ImVec2(hoverLeft, graphTop),
            ImVec2(hoverRight, graphBottom),
            ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.42f),
            2.0f,
            0,
            1.0f);
    }

    if (args.ui.colorWarpSmartSelection.active &&
        args.ui.colorWarpSmartSelection.pinId == selected.id) {
        const float leftX = evToX(args.ui.colorWarpSmartSelection.leftEv);
        const float rightX = evToX(args.ui.colorWarpSmartSelection.rightEv);
        stripDrawList->AddRectFilled(
            ImVec2(leftX, graphTop),
            ImVec2(rightX, graphBottom),
            palette.CoordinateColor(selected.sourceA, selected.sourceB, 0.10f));
        stripDrawList->AddLine(
            ImVec2(leftX, graphTop),
            ImVec2(leftX, graphBottom),
            IM_COL32(245, 245, 245, 190), 2.0f);
        stripDrawList->AddLine(
            ImVec2(rightX, graphTop),
            ImVec2(rightX, graphBottom),
            IM_COL32(245, 245, 245, 190), 2.0f);
    }

    const ImU32 curveFill = palette.CoordinateColor(
        selected.sourceA, selected.sourceB, 0.22f);
    const ImU32 curveLine = palette.CoordinateColor(
        selected.sourceA, selected.sourceB, 0.92f);
    for (std::size_t index = 1; index < selected.evCurve.samples.size(); ++index) {
        const float previousEv = RawLabColorWarpEvForSample(index - 1u);
        const float ev = RawLabColorWarpEvForSample(index);
        const ImVec2 previous(
            evToX(previousEv),
            qualificationToY(selected.evCurve.samples[index - 1u]));
        const ImVec2 current(
            evToX(ev),
            qualificationToY(selected.evCurve.samples[index]));
        stripDrawList->AddQuadFilled(
            previous,
            current,
            ImVec2(current.x, graphBottom),
            ImVec2(previous.x, graphBottom),
            curveFill);
        stripDrawList->AddLine(previous, current, curveLine, 2.0f);
    }
    stripDrawList->AddRect(
        stripMin,
        stripMax,
        ImGui::GetColorU32(ImGuiCol_Border, 0.65f),
        5.0f);
    stripDrawList->AddText(
        ImVec2(stripMin.x + 4.0f, stripMax.y - 14.0f),
        ImGui::GetColorU32(ImGuiCol_TextDisabled),
        "-16 EV");
    const char* rightLabel = "+16 EV";
    stripDrawList->AddText(
        ImVec2(stripMax.x - ImGui::CalcTextSize(rightLabel).x - 4.0f,
               stripMax.y - 14.0f),
        ImGui::GetColorU32(ImGuiCol_TextDisabled),
        rightLabel);
    const bool evGraphHovered = ImGui::IsItemHovered();
    if (evGraphHovered) {
        ImGui::SetTooltip(
            args.ui.colorWarpEvEditMode == 0
                ? "Draw qualification directly. The wheel changes the %.1f EV brush radius. Right-click switches to Smart."
                : "Click a peak or valley to select it. Drag vertically to flatten, strengthen, or invert; drag the body or shoulders horizontally. Right-click switches to Draw.",
            args.ui.colorWarpEvBrushWidth);
    }
    ImGui::EndDisabled();
    return changed;
}


} // namespace Stack::Editor::RawLabInternal
