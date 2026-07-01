#include "Editor/Internal/EditorModuleDevelopSubjectImportance.h"

#include "Editor/EditorModule.h"
#include "Editor/Internal/EditorModuleDevelopDynamicRangeStrategy.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace {

float SaturateFloat(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

} // namespace

namespace Stack::Editor::DevelopSubjectImportance {

float DevelopSubjectMapSmoothStep(float edge0, float edge1, float value) {
    if (edge1 <= edge0) {
        return value >= edge1 ? 1.0f : 0.0f;
    }
    const float t = SaturateFloat((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float DevelopSubjectMapRegionWeight(
    const EditorNodeGraph::DevelopSubjectImportanceRegion& region,
    float x,
    float y) {
    const float radiusX = std::clamp(region.radiusX, 0.005f, 1.0f);
    const float radiusY = std::clamp(region.radiusY, 0.005f, 1.0f);
    const float dx = (x - std::clamp(region.centerX, 0.0f, 1.0f)) / radiusX;
    const float dy = (y - std::clamp(region.centerY, 0.0f, 1.0f)) / radiusY;
    const float distance = std::sqrt(dx * dx + dy * dy);
    const float feather = std::clamp(region.feather, 0.0f, 1.0f);
    const float inner = std::max(0.08f, 1.0f - feather * 0.55f);
    const float outer = 1.0f + feather * 0.35f;
    const float falloff = 1.0f - DevelopSubjectMapSmoothStep(inner, outer, distance);
    return SaturateFloat(region.strength * falloff);
}

float DevelopSubjectMapSegmentDistance(
    float px,
    float py,
    float ax,
    float ay,
    float bx,
    float by) {
    const float vx = bx - ax;
    const float vy = by - ay;
    const float lenSq = vx * vx + vy * vy;
    if (lenSq <= 0.000001f) {
        const float dx = px - ax;
        const float dy = py - ay;
        return std::sqrt(dx * dx + dy * dy);
    }
    const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / lenSq, 0.0f, 1.0f);
    const float sx = ax + vx * t;
    const float sy = ay + vy * t;
    const float dx = px - sx;
    const float dy = py - sy;
    return std::sqrt(dx * dx + dy * dy);
}

float DevelopSubjectMapStrokeWeight(
    const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke,
    float x,
    float y) {
    if (stroke.points.empty()) {
        return 0.0f;
    }

    float minDistance = std::numeric_limits<float>::max();
    if (stroke.points.size() == 1) {
        const float dx = x - std::clamp(stroke.points.front().x, 0.0f, 1.0f);
        const float dy = y - std::clamp(stroke.points.front().y, 0.0f, 1.0f);
        minDistance = std::sqrt(dx * dx + dy * dy);
    } else {
        for (std::size_t index = 1; index < stroke.points.size(); ++index) {
            const auto& a = stroke.points[index - 1];
            const auto& b = stroke.points[index];
            minDistance = std::min(
                minDistance,
                DevelopSubjectMapSegmentDistance(
                    x,
                    y,
                    std::clamp(a.x, 0.0f, 1.0f),
                    std::clamp(a.y, 0.0f, 1.0f),
                    std::clamp(b.x, 0.0f, 1.0f),
                    std::clamp(b.y, 0.0f, 1.0f)));
        }
    }

    const float radius = std::clamp(stroke.radius, 0.002f, 0.50f);
    const float feather = std::clamp(stroke.feather, 0.0f, 1.0f);
    const float inner = radius * std::max(0.20f, 1.0f - feather * 0.65f);
    const float outer = radius * (1.0f + feather * 0.85f);
    const float falloff = 1.0f - DevelopSubjectMapSmoothStep(inner, outer, minDistance);
    return SaturateFloat(stroke.strength * falloff);
}

void AddDevelopSubjectMapCellWeight(
    DevelopSubjectImportanceMapCell& cell,
    EditorNodeGraph::DevelopSubjectImportanceMode mode,
    float weight,
    bool lowPriority) {
    weight = SaturateFloat(weight);
    if (weight <= 0.001f) {
        return;
    }

    if (lowPriority || mode == EditorNodeGraph::DevelopSubjectImportanceMode::Ignore) {
        cell.lowPriority = std::max(cell.lowPriority, weight);
        return;
    }

    cell.importance = std::max(cell.importance, weight);
    switch (mode) {
        case EditorNodeGraph::DevelopSubjectImportanceMode::Reveal:
            cell.reveal = std::max(cell.reveal, weight);
            break;
        case EditorNodeGraph::DevelopSubjectImportanceMode::Protect:
            cell.protect = std::max(cell.protect, weight);
            break;
        case EditorNodeGraph::DevelopSubjectImportanceMode::PreserveMood:
            cell.preserveMood = std::max(cell.preserveMood, weight);
            break;
        case EditorNodeGraph::DevelopSubjectImportanceMode::Important:
        default:
            break;
    }
}

DevelopSubjectImportanceInterpretation InterpretDevelopSubjectImportanceMap(
    const EditorNodeGraph::DevelopSubjectImportanceMap& importance) {
    DevelopSubjectImportanceInterpretation result;
    result.enabled = importance.enabled;
    if (!importance.enabled) {
        return result;
    }

    for (const EditorNodeGraph::DevelopSubjectImportanceRegion& region : importance.regions) {
        if (region.enabled && region.strength > 0.001f) {
            ++result.activeRegionCount;
        }
    }
    for (const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke : importance.strokes) {
        if (stroke.enabled && stroke.strength > 0.001f && !stroke.points.empty()) {
            ++result.activeStrokeCount;
        }
    }

    if (result.activeRegionCount == 0 && result.activeStrokeCount == 0) {
        result.status = "empty";
        result.reason = "Subject importance guidance is enabled, but no active regions or strokes are present.";
        return result;
    }

    result.active = true;
    result.status = "interpretedUserMarks";
    result.reason =
        "A compact solver grid was interpreted from user-marked subject regions and strokes; edge-aware refinement and visual diagnostic maps remain deferred.";

    for (int yIndex = 0; yIndex < kDevelopSubjectImportanceMapGridSize; ++yIndex) {
        for (int xIndex = 0; xIndex < kDevelopSubjectImportanceMapGridSize; ++xIndex) {
            const float x =
                (static_cast<float>(xIndex) + 0.5f) /
                static_cast<float>(kDevelopSubjectImportanceMapGridSize);
            const float y =
                (static_cast<float>(yIndex) + 0.5f) /
                static_cast<float>(kDevelopSubjectImportanceMapGridSize);
            DevelopSubjectImportanceMapCell& cell =
                result.cells[static_cast<std::size_t>(
                    yIndex * kDevelopSubjectImportanceMapGridSize + xIndex)];

            for (const EditorNodeGraph::DevelopSubjectImportanceRegion& region : importance.regions) {
                if (!region.enabled || region.strength <= 0.001f) {
                    continue;
                }
                AddDevelopSubjectMapCellWeight(
                    cell,
                    region.mode,
                    DevelopSubjectMapRegionWeight(region, x, y),
                    false);
            }
            for (const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke : importance.strokes) {
                if (!stroke.enabled || stroke.strength <= 0.001f || stroke.points.empty()) {
                    continue;
                }
                AddDevelopSubjectMapCellWeight(
                    cell,
                    stroke.mode,
                    DevelopSubjectMapStrokeWeight(stroke, x, y),
                    stroke.subtract);
            }

            if (cell.lowPriority > 0.001f) {
                cell.importance = std::max(0.0f, cell.importance * (1.0f - cell.lowPriority * 0.55f));
                cell.reveal = std::max(0.0f, cell.reveal * (1.0f - cell.lowPriority * 0.65f));
                cell.protect = std::max(0.0f, cell.protect * (1.0f - cell.lowPriority * 0.45f));
                cell.preserveMood =
                    std::max(0.0f, cell.preserveMood * (1.0f - cell.lowPriority * 0.35f));
            }
        }
    }

    float positiveCellCount = 0.0f;
    float lowPriorityCellCount = 0.0f;
    float revealCellCount = 0.0f;
    float protectCellCount = 0.0f;
    float moodCellCount = 0.0f;
    float positiveWeightSum = 0.0f;
    float centerWeightSum = 0.0f;
    float edgeWeightSum = 0.0f;
    for (int yIndex = 0; yIndex < kDevelopSubjectImportanceMapGridSize; ++yIndex) {
        for (int xIndex = 0; xIndex < kDevelopSubjectImportanceMapGridSize; ++xIndex) {
            const float x =
                (static_cast<float>(xIndex) + 0.5f) /
                static_cast<float>(kDevelopSubjectImportanceMapGridSize);
            const float y =
                (static_cast<float>(yIndex) + 0.5f) /
                static_cast<float>(kDevelopSubjectImportanceMapGridSize);
            const DevelopSubjectImportanceMapCell& cell =
                result.cells[static_cast<std::size_t>(
                    yIndex * kDevelopSubjectImportanceMapGridSize + xIndex)];
            const float positive = SaturateFloat(
                std::max({ cell.importance, cell.reveal, cell.protect, cell.preserveMood }));
            const float lowPriority = SaturateFloat(cell.lowPriority);
            if (positive > 0.025f) {
                positiveCellCount += 1.0f;
            }
            if (lowPriority > 0.025f) {
                lowPriorityCellCount += 1.0f;
            }
            if (cell.reveal > 0.025f) {
                revealCellCount += 1.0f;
            }
            if (cell.protect > 0.025f) {
                protectCellCount += 1.0f;
            }
            if (cell.preserveMood > 0.025f) {
                moodCellCount += 1.0f;
            }

            result.peakImportance = std::max(result.peakImportance, positive);
            positiveWeightSum += positive;
            const float dx = x - 0.5f;
            const float dy = y - 0.5f;
            const float edgeDistance = SaturateFloat(std::sqrt(dx * dx + dy * dy) / 0.707107f);
            centerWeightSum += positive * (1.0f - edgeDistance);
            edgeWeightSum += positive * edgeDistance;
        }
    }

    const float invCells = 1.0f / static_cast<float>(kDevelopSubjectImportanceMapCellCount);
    result.positiveCoverage = SaturateFloat(positiveCellCount * invCells);
    result.lowPriorityCoverage = SaturateFloat(lowPriorityCellCount * invCells);
    result.coverage = SaturateFloat(result.positiveCoverage + result.lowPriorityCoverage);
    result.revealCoverage = SaturateFloat(revealCellCount * invCells);
    result.protectCoverage = SaturateFloat(protectCellCount * invCells);
    result.moodCoverage = SaturateFloat(moodCellCount * invCells);
    result.meanImportance = SaturateFloat(positiveWeightSum * invCells);
    result.centerBias = positiveWeightSum > 0.001f
        ? SaturateFloat(centerWeightSum / positiveWeightSum)
        : 0.0f;
    result.edgeBias = positiveWeightSum > 0.001f
        ? SaturateFloat(edgeWeightSum / positiveWeightSum)
        : 0.0f;
    result.mapConfidence = SaturateFloat(
        result.peakImportance * 0.30f +
        result.positiveCoverage * 0.30f +
        result.lowPriorityCoverage * 0.16f +
        result.meanImportance * 0.16f +
        (result.active ? 0.08f : 0.0f));
    if (result.positiveCoverage <= 0.001f && result.lowPriorityCoverage > 0.001f) {
        result.reason =
            "Only low-priority subject marks are active; Auto can spend less exposure/detail budget there while preserving scene intent.";
    }
    return result;
}

nlohmann::json DevelopSubjectImportanceInterpretationToJson(
    const DevelopSubjectImportanceInterpretation& map) {
    nlohmann::json cells = nlohmann::json::array();
    for (int yIndex = 0; yIndex < map.gridHeight; ++yIndex) {
        for (int xIndex = 0; xIndex < map.gridWidth; ++xIndex) {
            const DevelopSubjectImportanceMapCell& cell =
                map.cells[static_cast<std::size_t>(yIndex * map.gridWidth + xIndex)];
            cells.push_back({
                { "x", xIndex },
                { "y", yIndex },
                { "importance", cell.importance },
                { "reveal", cell.reveal },
                { "protect", cell.protect },
                { "preserveMood", cell.preserveMood },
                { "lowPriority", cell.lowPriority }
            });
        }
    }

    return {
        { "version", kDevelopSubjectImportanceMapVersion },
        { "enabled", map.enabled },
        { "active", map.active },
        { "status", map.status },
        { "reason", map.reason },
        { "gridWidth", map.gridWidth },
        { "gridHeight", map.gridHeight },
        { "activeRegionCount", map.activeRegionCount },
        { "activeStrokeCount", map.activeStrokeCount },
        { "coverage", map.coverage },
        { "positiveCoverage", map.positiveCoverage },
        { "lowPriorityCoverage", map.lowPriorityCoverage },
        { "revealCoverage", map.revealCoverage },
        { "protectCoverage", map.protectCoverage },
        { "moodCoverage", map.moodCoverage },
        { "peakImportance", map.peakImportance },
        { "meanImportance", map.meanImportance },
        { "centerBias", map.centerBias },
        { "edgeBias", map.edgeBias },
        { "mapConfidence", map.mapConfidence },
        { "cells", std::move(cells) }
    };
}

float DevelopSubjectImportanceCellPositive(const DevelopSubjectImportanceMapCell& cell) {
    return SaturateFloat(std::max({ cell.importance, cell.reveal, cell.protect, cell.preserveMood }));
}

DevelopSubjectRefinedMap BuildDevelopSubjectRefinedMap(
    const DevelopSubjectImportanceInterpretation& sourceMap,
    const DevelopSubjectSceneIntent& subjectIntent) {
    DevelopSubjectRefinedMap refined;
    refined.enabled = sourceMap.enabled;
    refined.gridWidth = sourceMap.gridWidth;
    refined.gridHeight = sourceMap.gridHeight;
    if (!sourceMap.enabled) {
        return refined;
    }
    if (!sourceMap.active) {
        refined.status = sourceMap.status;
        refined.reason = sourceMap.reason;
        return refined;
    }

    refined.active = true;
    refined.status = "refinedUserMarks";
    refined.reason =
        "SubjectRefinedMapV1 blends the compact interpreted user marks with neighbor support and solved subject/readability/protection/mood axes. Boundary hints are mark-structure hints; true image-edge refinement remains deferred.";

    const int width = std::max(1, sourceMap.gridWidth);
    const int height = std::max(1, sourceMap.gridHeight);
    auto cellAt = [&](int x, int y) -> const DevelopSubjectImportanceMapCell& {
        const int clampedX = std::clamp(x, 0, width - 1);
        const int clampedY = std::clamp(y, 0, height - 1);
        return sourceMap.cells[static_cast<std::size_t>(clampedY * width + clampedX)];
    };

    const float subjectPressure = SaturateFloat(
        subjectIntent.subjectPriority * 0.42f +
        subjectIntent.userGuidanceStrength * 0.22f +
        sourceMap.mapConfidence * 0.20f -
        subjectIntent.sceneIntegrity * 0.10f);
    const float readabilityPressure = SaturateFloat(
        subjectIntent.improveReadability * 0.42f +
        subjectIntent.readabilityPressure * 0.22f +
        sourceMap.revealCoverage * 0.16f -
        subjectIntent.preserveMood * 0.08f);
    const float protectionPressure = SaturateFloat(
        subjectIntent.protectionPressure * 0.42f +
        sourceMap.protectCoverage * 0.22f +
        subjectIntent.importanceProtection * 0.18f);
    const float moodPressure = SaturateFloat(
        subjectIntent.preserveMood * 0.42f +
        subjectIntent.moodPreservationPressure * 0.22f +
        sourceMap.moodCoverage * 0.18f);

    float coverageCells = 0.0f;
    float lowPriorityCells = 0.0f;
    float readabilityCells = 0.0f;
    float protectionCells = 0.0f;
    float moodCells = 0.0f;
    float confidenceSum = 0.0f;
    float boundarySum = 0.0f;

    for (int yIndex = 0; yIndex < height; ++yIndex) {
        for (int xIndex = 0; xIndex < width; ++xIndex) {
            const std::size_t index = static_cast<std::size_t>(yIndex * width + xIndex);
            const DevelopSubjectImportanceMapCell& sourceCell = sourceMap.cells[index];
            const float positive = DevelopSubjectImportanceCellPositive(sourceCell);
            const float lowPriority = SaturateFloat(sourceCell.lowPriority);

            float neighborPositiveSum = 0.0f;
            float neighborLowSum = 0.0f;
            float neighborCount = 0.0f;
            float boundaryHint = 0.0f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const DevelopSubjectImportanceMapCell& neighbor = cellAt(xIndex + dx, yIndex + dy);
                    const float neighborPositive = DevelopSubjectImportanceCellPositive(neighbor);
                    const float neighborLow = SaturateFloat(neighbor.lowPriority);
                    neighborPositiveSum += neighborPositive;
                    neighborLowSum += neighborLow;
                    neighborCount += 1.0f;
                    boundaryHint = std::max(
                        boundaryHint,
                        std::max(
                            std::fabs(positive - neighborPositive),
                            std::fabs(lowPriority - neighborLow)));
                }
            }
            const float neighborPositive =
                neighborCount > 0.0f ? SaturateFloat(neighborPositiveSum / neighborCount) : 0.0f;
            const float neighborLow =
                neighborCount > 0.0f ? SaturateFloat(neighborLowSum / neighborCount) : 0.0f;
            boundaryHint = SaturateFloat(boundaryHint * 0.72f + std::fabs(positive - lowPriority) * 0.20f);

            DevelopSubjectRefinedMapCell& refinedCell = refined.cells[index];
            refinedCell.lowPriority = SaturateFloat(lowPriority * 0.82f + neighborLow * 0.12f);
            refinedCell.importance = SaturateFloat(
                positive * (0.66f + subjectPressure * 0.20f) +
                neighborPositive * 0.18f -
                refinedCell.lowPriority * 0.22f);
            refinedCell.readability = SaturateFloat(
                sourceCell.reveal * 0.64f +
                refinedCell.importance * 0.20f +
                readabilityPressure * 0.16f -
                refinedCell.lowPriority * 0.16f);
            refinedCell.protection = SaturateFloat(
                sourceCell.protect * 0.66f +
                refinedCell.importance * 0.16f +
                protectionPressure * 0.16f -
                refinedCell.lowPriority * 0.10f);
            refinedCell.preserveMood = SaturateFloat(
                sourceCell.preserveMood * 0.66f +
                refinedCell.importance * 0.14f +
                moodPressure * 0.18f +
                refinedCell.lowPriority * 0.04f -
                readabilityPressure * 0.06f);
            refinedCell.boundaryHint = boundaryHint;
            refinedCell.confidence = SaturateFloat(
                refinedCell.importance * 0.34f +
                neighborPositive * 0.20f +
                sourceMap.mapConfidence * 0.20f +
                subjectIntent.userGuidanceStrength * 0.12f +
                boundaryHint * 0.06f -
                refinedCell.lowPriority * 0.10f);

            if (refinedCell.importance > 0.025f || refinedCell.confidence > 0.08f) {
                coverageCells += 1.0f;
            }
            if (refinedCell.lowPriority > 0.025f) {
                lowPriorityCells += 1.0f;
            }
            if (refinedCell.readability > 0.025f) {
                readabilityCells += 1.0f;
            }
            if (refinedCell.protection > 0.025f) {
                protectionCells += 1.0f;
            }
            if (refinedCell.preserveMood > 0.025f) {
                moodCells += 1.0f;
            }
            refined.peakImportance = std::max(refined.peakImportance, refinedCell.importance);
            confidenceSum += refinedCell.confidence;
            boundarySum += refinedCell.boundaryHint * std::max(refinedCell.importance, refinedCell.confidence);
        }
    }

    const float invCells = 1.0f / static_cast<float>(kDevelopSubjectImportanceMapCellCount);
    refined.coverage = SaturateFloat(coverageCells * invCells);
    refined.lowPriorityCoverage = SaturateFloat(lowPriorityCells * invCells);
    refined.readabilityCoverage = SaturateFloat(readabilityCells * invCells);
    refined.protectionCoverage = SaturateFloat(protectionCells * invCells);
    refined.moodCoverage = SaturateFloat(moodCells * invCells);
    refined.meanConfidence = SaturateFloat(confidenceSum * invCells);
    refined.boundaryHint = SaturateFloat(boundarySum * invCells);
    if (refined.coverage <= 0.001f && refined.lowPriorityCoverage > 0.001f) {
        refined.reason =
            "SubjectRefinedMapV1 only found low-priority guidance, so it can reduce budget pressure without creating a positive subject target.";
    }
    return refined;
}

nlohmann::json DevelopSubjectRefinedMapToJson(const DevelopSubjectRefinedMap& map) {
    nlohmann::json cells = nlohmann::json::array();
    for (int yIndex = 0; yIndex < map.gridHeight; ++yIndex) {
        for (int xIndex = 0; xIndex < map.gridWidth; ++xIndex) {
            const DevelopSubjectRefinedMapCell& cell =
                map.cells[static_cast<std::size_t>(yIndex * map.gridWidth + xIndex)];
            cells.push_back({
                { "x", xIndex },
                { "y", yIndex },
                { "importance", cell.importance },
                { "confidence", cell.confidence },
                { "readability", cell.readability },
                { "protection", cell.protection },
                { "preserveMood", cell.preserveMood },
                { "lowPriority", cell.lowPriority },
                { "boundaryHint", cell.boundaryHint }
            });
        }
    }

    return {
        { "version", kDevelopSubjectRefinedMapVersion },
        { "sourceMapVersion", map.sourceMapVersion },
        { "enabled", map.enabled },
        { "active", map.active },
        { "status", map.status },
        { "reason", map.reason },
        { "gridWidth", map.gridWidth },
        { "gridHeight", map.gridHeight },
        { "coverage", map.coverage },
        { "lowPriorityCoverage", map.lowPriorityCoverage },
        { "readabilityCoverage", map.readabilityCoverage },
        { "protectionCoverage", map.protectionCoverage },
        { "moodCoverage", map.moodCoverage },
        { "peakImportance", map.peakImportance },
        { "confidence", map.meanConfidence },
        { "boundaryHint", map.boundaryHint },
        { "cells", std::move(cells) }
    };
}

void ApplyDevelopSubjectRefinedMap(
    DevelopSubjectSceneIntent& subjectIntent,
    const DevelopSubjectImportanceInterpretation& importanceMap) {
    const DevelopSubjectRefinedMap refinedMap =
        BuildDevelopSubjectRefinedMap(importanceMap, subjectIntent);
    subjectIntent.refinedImportanceMap = DevelopSubjectRefinedMapToJson(refinedMap);
    subjectIntent.refinedMapCoverage = refinedMap.coverage;
    subjectIntent.refinedMapLowPriorityCoverage = refinedMap.lowPriorityCoverage;
    subjectIntent.refinedMapReadabilityCoverage = refinedMap.readabilityCoverage;
    subjectIntent.refinedMapProtectionCoverage = refinedMap.protectionCoverage;
    subjectIntent.refinedMapMoodCoverage = refinedMap.moodCoverage;
    subjectIntent.refinedMapPeak = refinedMap.peakImportance;
    subjectIntent.refinedMapConfidence = refinedMap.meanConfidence;
    subjectIntent.refinedMapBoundaryHint = refinedMap.boundaryHint;
}


DevelopSubjectImportanceSummary SummarizeDevelopSubjectImportance(
    const EditorNodeGraph::DevelopSubjectImportanceMap& importance) {
    DevelopSubjectImportanceSummary summary;
    summary.enabled = importance.enabled;
    if (!importance.enabled) {
        return summary;
    }

    auto addModeWeight = [&](EditorNodeGraph::DevelopSubjectImportanceMode mode, float weight) {
        switch (mode) {
            case EditorNodeGraph::DevelopSubjectImportanceMode::Reveal:
                summary.reveal = SaturateFloat(summary.reveal + weight);
                break;
            case EditorNodeGraph::DevelopSubjectImportanceMode::Protect:
                summary.protect = SaturateFloat(summary.protect + weight);
                break;
            case EditorNodeGraph::DevelopSubjectImportanceMode::PreserveMood:
                summary.preserveMood = SaturateFloat(summary.preserveMood + weight);
                break;
            case EditorNodeGraph::DevelopSubjectImportanceMode::Ignore:
                summary.ignore = SaturateFloat(summary.ignore + weight);
                break;
            case EditorNodeGraph::DevelopSubjectImportanceMode::Important:
            default:
                summary.important = SaturateFloat(summary.important + weight);
                break;
        }
    };

    for (const EditorNodeGraph::DevelopSubjectImportanceRegion& region : importance.regions) {
        if (!region.enabled || region.strength <= 0.001f) {
            continue;
        }
        const float softArea = std::clamp(
            std::sqrt(std::max(0.0001f, region.radiusX * region.radiusY)) * 2.2f,
            0.08f,
            1.0f);
        const float featherSupport = 0.72f + std::clamp(region.feather, 0.0f, 1.0f) * 0.28f;
        const float weight = SaturateFloat(region.strength * softArea * featherSupport);
        if (weight <= 0.001f) {
            continue;
        }
        ++summary.activeRegionCount;
        summary.strength = SaturateFloat(summary.strength + weight);
        addModeWeight(region.mode, weight);
    }

    for (const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke : importance.strokes) {
        if (!stroke.enabled || stroke.strength <= 0.001f || stroke.points.empty()) {
            continue;
        }

        float strokeLength = 0.0f;
        for (std::size_t i = 1; i < stroke.points.size(); ++i) {
            const float dx = stroke.points[i].x - stroke.points[i - 1].x;
            const float dy = stroke.points[i].y - stroke.points[i - 1].y;
            strokeLength += std::sqrt(dx * dx + dy * dy);
        }
        const float pointSupport =
            static_cast<float>(std::min<std::size_t>(stroke.points.size(), 64)) * stroke.radius * 0.055f;
        const float pathSupport = strokeLength * stroke.radius * 1.65f;
        const float softCoverage = std::clamp(pointSupport + pathSupport, 0.025f, 0.85f);
        const float featherSupport = 0.70f + std::clamp(stroke.feather, 0.0f, 1.0f) * 0.30f;
        const float weight = SaturateFloat(stroke.strength * softCoverage * featherSupport);
        if (weight <= 0.001f) {
            continue;
        }

        ++summary.activeStrokeCount;
        if (stroke.subtract) {
            summary.ignore = SaturateFloat(summary.ignore + weight * 0.70f);
            summary.strength = std::max(0.0f, summary.strength - weight * 0.35f);
            continue;
        }
        summary.strength = SaturateFloat(summary.strength + weight);
        addModeWeight(stroke.mode, weight);
    }

    summary.subjectPriority = SaturateFloat(
        summary.important * 0.52f +
        summary.reveal * 0.42f +
        summary.protect * 0.28f -
        summary.ignore * 0.34f);
    summary.readability = SaturateFloat(
        summary.reveal * 0.62f +
        summary.important * 0.26f -
        summary.preserveMood * 0.22f -
        summary.ignore * 0.18f);
    summary.protection = SaturateFloat(
        summary.protect * 0.68f +
        summary.important * 0.18f +
        summary.preserveMood * 0.12f);
    summary.mood = SaturateFloat(
        summary.preserveMood * 0.72f +
        summary.protect * 0.12f -
        summary.reveal * 0.20f);
    summary.lowPriority = SaturateFloat(summary.ignore);
    return summary;
}

void AddDevelopSubjectSolveNote(
    nlohmann::json& notes,
    const char* id,
    const std::string& text,
    float strength) {
    if (!notes.is_array() || notes.size() >= 5 || text.empty() || strength <= 0.001f) {
        return;
    }
    notes.push_back({
        { "id", id },
        { "text", text },
        { "strength", SaturateFloat(strength) }
    });
}

nlohmann::json BuildDevelopSubjectSolveNotes(const DevelopSubjectSceneIntent& subjectIntent) {
    nlohmann::json notes = nlohmann::json::array();
    const bool hasMarks =
        subjectIntent.importanceRegionCount > 0 ||
        subjectIntent.importanceStrokeCount > 0;
    const int interpretedCells = static_cast<int>(
        std::round(subjectIntent.importanceMapCoverage *
            static_cast<float>(kDevelopSubjectImportanceMapCellCount)));

    if (hasMarks && subjectIntent.importanceMapConfidence > 0.01f) {
        char buffer[256];
        std::snprintf(
            buffer,
            sizeof(buffer),
            "Interpreted %d subject-map cell%s from marked regions/strokes; Auto treats them as soft scoring bias, not a hard mask.",
            std::max(1, interpretedCells),
            std::max(1, interpretedCells) == 1 ? "" : "s");
        AddDevelopSubjectSolveNote(
            notes,
            "interpretedMapBias",
            buffer,
            subjectIntent.importanceMapConfidence);
        if (subjectIntent.refinedMapConfidence > 0.08f) {
            AddDevelopSubjectSolveNote(
                notes,
                "refinedMapBias",
                "Refined subject-map confidence is guiding readability, protection, and mood tradeoffs while true image-edge refinement remains deferred.",
                subjectIntent.refinedMapConfidence);
        }
    } else if (subjectIntent.userGuidanceActive && !hasMarks) {
        AddDevelopSubjectSolveNote(
            notes,
            "intentControlBias",
            "Subject / Scene and Mood / Readability controls are biasing candidate scores without a painted subject map.",
            subjectIntent.userGuidanceStrength);
    } else if (subjectIntent.automaticOnly && subjectIntent.automaticConfidence > 0.08f) {
        AddDevelopSubjectSolveNote(
            notes,
            "automaticWeakPrior",
            "Auto found only weak automatic subject evidence, so subject priority remains a conservative scene-level bias.",
            subjectIntent.automaticConfidence);
    }

    if (subjectIntent.importanceReveal > 0.12f ||
        subjectIntent.importanceMapRevealCoverage > 0.02f ||
        subjectIntent.improveReadability > subjectIntent.preserveMood + 0.16f) {
        AddDevelopSubjectSolveNote(
            notes,
            "readabilityBias",
            "Reveal/readability evidence raises subject-readable midtone and local-range candidate fit while preserving noise and halo guards.",
            std::max({
                subjectIntent.importanceReveal,
                subjectIntent.importanceMapRevealCoverage,
                std::max(0.0f, subjectIntent.improveReadability - subjectIntent.preserveMood) }));
    }

    if (subjectIntent.importanceProtect > 0.12f ||
        subjectIntent.importanceMapProtectCoverage > 0.02f ||
        subjectIntent.protectionPressure > 0.18f) {
        AddDevelopSubjectSolveNote(
            notes,
            "protectionBias",
            "Protect evidence raises clipping, detail, and over-compression safeguards for marked or likely important content.",
            std::max({
                subjectIntent.importanceProtect,
                subjectIntent.importanceMapProtectCoverage,
                subjectIntent.protectionPressure }));
    }

    if (subjectIntent.importancePreserveMood > 0.12f ||
        subjectIntent.importanceMapMoodCoverage > 0.02f ||
        subjectIntent.preserveMood > subjectIntent.improveReadability + 0.16f) {
        AddDevelopSubjectSolveNote(
            notes,
            "moodPreservationBias",
            "Mood-preserve evidence pushes back on forcing low-key or atmosphere-critical areas into gray mids.",
            std::max({
                subjectIntent.importancePreserveMood,
                subjectIntent.importanceMapMoodCoverage,
                std::max(0.0f, subjectIntent.preserveMood - subjectIntent.improveReadability) }));
    }

    if (subjectIntent.importanceIgnore > 0.12f ||
        subjectIntent.importanceMapLowPriorityCoverage > 0.02f ||
        subjectIntent.importanceLowPriority > 0.12f) {
        AddDevelopSubjectSolveNote(
            notes,
            "lowPriorityBias",
            "Low-priority marks reduce exposure/detail budget pressure in those areas instead of making the whole image brighter.",
            std::max({
                subjectIntent.importanceIgnore,
                subjectIntent.importanceMapLowPriorityCoverage,
                subjectIntent.importanceLowPriority }));
    }

    if (subjectIntent.subjectSceneAxis > 0.18f) {
        AddDevelopSubjectSolveNote(
            notes,
            "subjectPriorityAxis",
            "The solved Subject / Scene axis leans toward marked or likely subject priority.",
            subjectIntent.subjectSceneAxis);
    } else if (subjectIntent.subjectSceneAxis < -0.18f) {
        AddDevelopSubjectSolveNote(
            notes,
            "sceneIntegrityAxis",
            "The solved Subject / Scene axis leans toward global scene integrity, so subject evidence stays restrained.",
            -subjectIntent.subjectSceneAxis);
    }

    if (notes.empty() && !subjectIntent.reason.empty()) {
        AddDevelopSubjectSolveNote(notes, "intentReason", subjectIntent.reason, 0.25f);
    }
    return notes;
}

DevelopSubjectSceneIntent ResolveDevelopSubjectSceneIntent(
    EditorNodeGraph::DevelopAutoIntent intent,
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    const EditorNodeGraph::DevelopSubjectImportanceMap& importance,
    const DevelopDynamicRange::DevelopToneAutoStats& stats,
    const DevelopDynamicRange::DevelopDynamicRangeRegionEvidence& regionEvidence) {
    DevelopSubjectSceneIntent subjectIntent;
    subjectIntent.evidence =
        DevelopDynamicRange::DevelopDynamicRangeRegionEvidenceToJson(regionEvidence);
    const DevelopSubjectImportanceSummary importanceSummary =
        SummarizeDevelopSubjectImportance(importance);
    const DevelopSubjectImportanceInterpretation importanceMap =
        InterpretDevelopSubjectImportanceMap(importance);
    subjectIntent.importanceMap = DevelopSubjectImportanceInterpretationToJson(importanceMap);
    subjectIntent.importanceRegionCount = importanceSummary.activeRegionCount;
    subjectIntent.importanceStrokeCount = importanceSummary.activeStrokeCount;
    subjectIntent.importanceStrength = importanceSummary.strength;
    subjectIntent.importanceImportant = importanceSummary.important;
    subjectIntent.importanceReveal = importanceSummary.reveal;
    subjectIntent.importanceProtect = importanceSummary.protect;
    subjectIntent.importancePreserveMood = importanceSummary.preserveMood;
    subjectIntent.importanceIgnore = importanceSummary.ignore;
    subjectIntent.importanceSubjectPriority = importanceSummary.subjectPriority;
    subjectIntent.importanceReadability = importanceSummary.readability;
    subjectIntent.importanceProtection = importanceSummary.protection;
    subjectIntent.importanceMood = importanceSummary.mood;
    subjectIntent.importanceLowPriority = importanceSummary.lowPriority;
    subjectIntent.importanceMapCoverage = importanceMap.coverage;
    subjectIntent.importanceMapPositiveCoverage = importanceMap.positiveCoverage;
    subjectIntent.importanceMapLowPriorityCoverage = importanceMap.lowPriorityCoverage;
    subjectIntent.importanceMapRevealCoverage = importanceMap.revealCoverage;
    subjectIntent.importanceMapProtectCoverage = importanceMap.protectCoverage;
    subjectIntent.importanceMapMoodCoverage = importanceMap.moodCoverage;
    subjectIntent.importanceMapPeak = importanceMap.peakImportance;
    subjectIntent.importanceMapConfidence = importanceMap.mapConfidence;
    subjectIntent.importanceMapCenterBias = importanceMap.centerBias;
    subjectIntent.importanceMapEdgeBias = importanceMap.edgeBias;
    subjectIntent.userSubjectSceneBias = std::clamp(guidance.subjectSceneBias, -1.0f, 1.0f);
    subjectIntent.userMoodReadabilityBias = std::clamp(guidance.moodReadabilityBias, -1.0f, 1.0f);
    subjectIntent.userGuidanceStrength = std::max(
        std::fabs(subjectIntent.userSubjectSceneBias),
        std::fabs(subjectIntent.userMoodReadabilityBias));
    subjectIntent.userGuidanceStrength = std::max(
        subjectIntent.userGuidanceStrength,
        std::max(importanceSummary.strength, importanceSummary.lowPriority));
    subjectIntent.userGuidanceStrength = std::max(
        subjectIntent.userGuidanceStrength,
        importanceMap.mapConfidence * 0.72f);
    const bool userIntentActive = subjectIntent.userGuidanceStrength > 0.015f;
    auto applyUserSubjectIntent = [&]() {
        if (!userIntentActive) {
            return;
        }
        const float subjectBias = std::max(0.0f, subjectIntent.userSubjectSceneBias);
        const float sceneBias = std::max(0.0f, -subjectIntent.userSubjectSceneBias);
        const float readabilityBias = std::max(0.0f, subjectIntent.userMoodReadabilityBias);
        const float moodBias = std::max(0.0f, -subjectIntent.userMoodReadabilityBias);

        // User axes and marked regions are stronger than the weak automatic
        // prior, but clipping/noise/halo safeguards still decide viability.
        subjectIntent.userGuidanceStatus = importanceSummary.activeStrokeCount > 0
            ? "importanceBrush"
            : (importanceSummary.activeRegionCount > 0 ? "importanceRegions" : "intentControls");
        subjectIntent.userGuidanceActive = true;
        subjectIntent.automaticOnly = false;
        subjectIntent.subjectPriority = SaturateFloat(
            subjectIntent.subjectPriority +
            subjectBias * 0.34f +
            readabilityBias * 0.06f +
            importanceSummary.subjectPriority * 0.34f +
            importanceSummary.reveal * 0.08f -
            importanceMap.lowPriorityCoverage * 0.12f +
            importanceMap.mapConfidence * 0.08f +
            importanceMap.centerBias * importanceMap.positiveCoverage * 0.06f -
            sceneBias * 0.20f -
            importanceSummary.lowPriority * 0.20f);
        subjectIntent.sceneIntegrity = SaturateFloat(
            subjectIntent.sceneIntegrity +
            sceneBias * 0.34f +
            moodBias * 0.06f +
            importanceSummary.lowPriority * 0.22f +
            importanceSummary.mood * 0.10f +
            importanceMap.lowPriorityCoverage * 0.10f +
            importanceMap.edgeBias * importanceMap.positiveCoverage * 0.04f -
            subjectBias * 0.08f -
            importanceSummary.reveal * 0.08f);
        subjectIntent.improveReadability = SaturateFloat(
            subjectIntent.improveReadability +
            readabilityBias * 0.34f +
            subjectBias * 0.06f +
            importanceSummary.readability * 0.38f +
            importanceMap.revealCoverage * 0.10f +
            importanceMap.mapConfidence * 0.04f -
            moodBias * 0.16f -
            importanceSummary.mood * 0.18f -
            importanceSummary.lowPriority * 0.10f);
        subjectIntent.preserveMood = SaturateFloat(
            subjectIntent.preserveMood +
            moodBias * 0.34f +
            sceneBias * 0.08f +
            importanceSummary.mood * 0.40f +
            importanceSummary.protection * 0.08f +
            importanceMap.moodCoverage * 0.10f +
            importanceMap.lowPriorityCoverage * 0.05f -
            readabilityBias * 0.12f -
            importanceSummary.reveal * 0.14f);
        subjectIntent.protectionPressure = SaturateFloat(
            subjectIntent.protectionPressure +
            importanceSummary.protection * 0.34f +
            importanceMap.protectCoverage * 0.08f);
    };
    if (!stats.valid) {
        subjectIntent.id = "pendingSubjectEvidence";
        subjectIntent.label = "Pending Subject Evidence";
        subjectIntent.reason =
            "Develop needs rendered statistics before it can estimate subject or scene priority.";
        applyUserSubjectIntent();
        if (userIntentActive) {
            subjectIntent.id = "userGuidedPendingSubjectEvidence";
            subjectIntent.label = "User Guided Pending Evidence";
            subjectIntent.reason =
                "User subject/scene intent is active, but Develop still needs rendered statistics before it can validate the tradeoff.";
        }
        ApplyDevelopSubjectRefinedMap(subjectIntent, importanceMap);
        subjectIntent.solveNotes = BuildDevelopSubjectSolveNotes(subjectIntent);
        return subjectIntent;
    }

    const bool brightIntent = intent == EditorNodeGraph::DevelopAutoIntent::BrightNatural;
    const bool darkIntent = intent == EditorNodeGraph::DevelopAutoIntent::DarkNatural;
    const bool flatIntent = intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase;
    const bool rangeIntent = intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail;
    const bool punchyIntent = intent == EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast;

    subjectIntent.centerPrior = SaturateFloat(regionEvidence.subjectCenterPrior);
    subjectIntent.readabilityPressure = SaturateFloat(regionEvidence.subjectReadabilityPressure);
    subjectIntent.protectionPressure = SaturateFloat(regionEvidence.subjectProtectionPressure);
    subjectIntent.moodPreservationPressure =
        SaturateFloat(regionEvidence.subjectMoodPreservationPressure);
    subjectIntent.automaticConfidence =
        SaturateFloat(regionEvidence.subjectImportanceConfidence);

    subjectIntent.subjectPriority = SaturateFloat(
        0.42f +
        subjectIntent.automaticConfidence * 0.28f +
        subjectIntent.readabilityPressure * 0.16f +
        subjectIntent.protectionPressure * 0.12f +
        std::max(0.0f, guidance.shadowLift) * 0.04f +
        (brightIntent ? 0.07f : 0.0f) +
        (flatIntent ? 0.05f : 0.0f) +
        (rangeIntent ? 0.04f : 0.0f) -
        subjectIntent.moodPreservationPressure * 0.08f -
        (darkIntent ? 0.04f : 0.0f));
    subjectIntent.sceneIntegrity = SaturateFloat(
        0.52f +
        subjectIntent.moodPreservationPressure * 0.22f +
        regionEvidence.localExposureDamageRisk * 0.10f +
        regionEvidence.localHaloRisk * 0.08f +
        (darkIntent ? 0.08f : 0.0f) +
        (punchyIntent ? 0.05f : 0.0f) -
        subjectIntent.readabilityPressure * 0.08f -
        (brightIntent ? 0.05f : 0.0f));
    subjectIntent.improveReadability = SaturateFloat(
        0.42f +
        subjectIntent.readabilityPressure * 0.34f +
        regionEvidence.localShadowHotspotRisk * 0.12f +
        std::max(0.0f, guidance.shadowLift) * 0.08f +
        (brightIntent ? 0.10f : 0.0f) +
        (flatIntent ? 0.08f : 0.0f) +
        (rangeIntent ? 0.05f : 0.0f) -
        regionEvidence.shadowNoiseLiftRisk * 0.14f -
        subjectIntent.moodPreservationPressure * 0.10f -
        (darkIntent ? 0.06f : 0.0f));
    subjectIntent.preserveMood = SaturateFloat(
        0.45f +
        subjectIntent.moodPreservationPressure * 0.34f +
        regionEvidence.shadowNoiseLiftRisk * 0.10f +
        regionEvidence.localExposureHaloStress * 0.06f +
        (darkIntent ? 0.14f : 0.0f) +
        (punchyIntent ? 0.06f : 0.0f) -
        subjectIntent.readabilityPressure * 0.12f -
        (brightIntent ? 0.08f : 0.0f));
    subjectIntent.subjectSceneAxis =
        std::clamp(subjectIntent.subjectPriority - subjectIntent.sceneIntegrity, -1.0f, 1.0f);
    subjectIntent.moodReadabilityAxis =
        std::clamp(subjectIntent.improveReadability - subjectIntent.preserveMood, -1.0f, 1.0f);

    applyUserSubjectIntent();
    subjectIntent.subjectSceneAxis =
        std::clamp(subjectIntent.subjectPriority - subjectIntent.sceneIntegrity, -1.0f, 1.0f);
    subjectIntent.moodReadabilityAxis =
        std::clamp(subjectIntent.improveReadability - subjectIntent.preserveMood, -1.0f, 1.0f);

    if (userIntentActive) {
        subjectIntent.id = "userGuidedSubjectSceneIntent";
        subjectIntent.label = "User Guided Subject / Scene";
        const bool markedBrushActive = importanceSummary.activeStrokeCount > 0;
        const bool markedRegionActive = importanceSummary.activeRegionCount > 0;
        const char* markedNoun = markedBrushActive ? "brush strokes" : "regions";
        if ((markedBrushActive || markedRegionActive) && importanceSummary.reveal > 0.18f) {
            subjectIntent.id = markedBrushActive ? "importanceBrushReveal" : "importanceRegionReveal";
            subjectIntent.label = markedBrushActive ? "Painted Reveal" : "Marked Region Reveal";
            subjectIntent.reason =
                std::string("Marked reveal/important ") + markedNoun + " are active, so Auto scores subject-readable local range and midtone candidates higher while keeping quality guards.";
        } else if ((markedBrushActive || markedRegionActive) && importanceSummary.protect > 0.18f) {
            subjectIntent.id = markedBrushActive ? "importanceBrushProtect" : "importanceRegionProtect";
            subjectIntent.label = markedBrushActive ? "Painted Protection" : "Marked Region Protection";
            subjectIntent.reason =
                std::string("Marked protection ") + markedNoun + " are active, so Auto scores clipping, detail, and over-compression safeguards higher for important content.";
        } else if ((markedBrushActive || markedRegionActive) && importanceSummary.preserveMood > 0.18f) {
            subjectIntent.id = markedBrushActive ? "importanceBrushMood" : "importanceRegionMood";
            subjectIntent.label = markedBrushActive ? "Painted Mood Preservation" : "Marked Mood Preservation";
            subjectIntent.reason =
                std::string("Marked mood-preserve ") + markedNoun + " are active, so Auto avoids forcing low-key or atmosphere-critical regions into neutral mids.";
        } else if ((markedBrushActive || markedRegionActive) && importanceSummary.ignore > 0.18f) {
            subjectIntent.id = markedBrushActive ? "importanceBrushLowPriority" : "importanceRegionLowPriority";
            subjectIntent.label = markedBrushActive ? "Painted Low Priority" : "Marked Low Priority";
            subjectIntent.reason =
                std::string("Low-priority ") + markedNoun + " are active, so Auto spends less exposure and cleanup budget there while protecting the overall scene.";
        } else if (markedBrushActive || markedRegionActive) {
            subjectIntent.id = markedBrushActive ? "importanceBrushGuidance" : "importanceRegionGuidance";
            subjectIntent.label = markedBrushActive ? "Painted Important Areas" : "Marked Important Regions";
            subjectIntent.reason =
                std::string("Marked important ") + markedNoun + " are biasing Auto's candidate scores without becoming hard-edged masks.";
        } else if (subjectIntent.userSubjectSceneBias > 0.30f &&
            subjectIntent.userMoodReadabilityBias > 0.20f) {
            subjectIntent.label = "User Guided Subject Readability";
            subjectIntent.reason =
                "Subject priority and readability intent are active, so Auto scores subject-readable local range and midtone candidates higher while keeping safety guards.";
        } else if (subjectIntent.userSubjectSceneBias > 0.30f &&
            subjectIntent.userMoodReadabilityBias < -0.20f) {
            subjectIntent.label = "User Guided Protected Mood";
            subjectIntent.reason =
                "Subject priority is active while mood preservation is favored, so Auto protects the marked/likely subject without forcing a low-key scene into gray mids.";
        } else if (subjectIntent.userSubjectSceneBias < -0.30f) {
            subjectIntent.label = "User Guided Scene Integrity";
            subjectIntent.reason =
                "Global scene integrity is favored, so Auto keeps subject evidence as a softer bias and avoids spending all range on the likely subject.";
        } else if (subjectIntent.userMoodReadabilityBias > 0.25f) {
            subjectIntent.label = "User Guided Readability";
            subjectIntent.reason =
                "Readability intent is active, so Auto scores useful shadow/midtone candidates higher when quality allows.";
        } else if (subjectIntent.userMoodReadabilityBias < -0.25f) {
            subjectIntent.label = "User Guided Mood Preservation";
            subjectIntent.reason =
                "Mood preservation intent is active, so Auto avoids unnecessary subject lift and protects low-key atmosphere.";
        } else {
            subjectIntent.reason =
                "User subject/scene intent controls are active and are biasing Auto's candidate scores without creating a hard mask.";
        }
    } else if (!regionEvidence.valid) {
        subjectIntent.id = "awaitingRenderedSubjectEvidence";
        subjectIntent.label = "Awaiting Subject Evidence";
        subjectIntent.reason =
            "Auto has no rendered candidate metrics yet, so subject priority remains a neutral weak prior.";
    } else if (subjectIntent.readabilityPressure > 0.42f &&
        subjectIntent.automaticConfidence > 0.34f &&
        subjectIntent.preserveMood < subjectIntent.improveReadability + 0.12f) {
        subjectIntent.id = "automaticReadabilityBias";
        subjectIntent.label = "Automatic Readability Bias";
        subjectIntent.reason =
            "A likely important central region is dark enough that Auto should score readable-shadow and local-range candidates a little higher.";
    } else if (subjectIntent.protectionPressure > 0.38f &&
        subjectIntent.automaticConfidence > 0.32f) {
        subjectIntent.id = "automaticProtectionBias";
        subjectIntent.label = "Automatic Protection Bias";
        subjectIntent.reason =
            "A likely important central or structured region has highlight/protection pressure, so Auto should avoid sacrificing it to global range moves.";
    } else if (subjectIntent.moodPreservationPressure > 0.38f ||
        subjectIntent.preserveMood > subjectIntent.improveReadability + 0.16f) {
        subjectIntent.id = "automaticMoodPreservationBias";
        subjectIntent.label = "Automatic Mood Preservation Bias";
        subjectIntent.reason =
            "The weak subject prior looks more like low-key or silhouette intent, so Auto should avoid forcing all dark regions into gray mids.";
    } else {
        subjectIntent.id = "automaticSceneBalance";
        subjectIntent.label = "Automatic Scene Balance";
        subjectIntent.reason =
            "No user importance brush is active; Auto is using a weak composition/detail prior while preserving the whole scene.";
    }
    ApplyDevelopSubjectRefinedMap(subjectIntent, importanceMap);
    subjectIntent.solveNotes = BuildDevelopSubjectSolveNotes(subjectIntent);
    return subjectIntent;
}

nlohmann::json DevelopSubjectSceneIntentToJson(const DevelopSubjectSceneIntent& subjectIntent) {
    return {
        { "version", kDevelopSubjectSceneIntentVersion },
        { "id", subjectIntent.id },
        { "label", subjectIntent.label },
        { "reason", subjectIntent.reason },
        { "solveNotesVersion", kDevelopSubjectImportanceSolveNotesVersion },
        { "solveNotes", subjectIntent.solveNotes },
        { "userGuidanceStatus", subjectIntent.userGuidanceStatus },
        { "userGuidanceActive", subjectIntent.userGuidanceActive },
        { "automaticOnly", subjectIntent.automaticOnly },
        { "userSubjectSceneBias", subjectIntent.userSubjectSceneBias },
        { "userMoodReadabilityBias", subjectIntent.userMoodReadabilityBias },
        { "userGuidanceStrength", subjectIntent.userGuidanceStrength },
        { "automaticConfidence", subjectIntent.automaticConfidence },
        { "centerPrior", subjectIntent.centerPrior },
        { "readabilityPressure", subjectIntent.readabilityPressure },
        { "protectionPressure", subjectIntent.protectionPressure },
        { "moodPreservationPressure", subjectIntent.moodPreservationPressure },
        { "subjectPriority", subjectIntent.subjectPriority },
        { "sceneIntegrity", subjectIntent.sceneIntegrity },
        { "improveReadability", subjectIntent.improveReadability },
        { "preserveMood", subjectIntent.preserveMood },
        { "subjectSceneAxis", subjectIntent.subjectSceneAxis },
        { "moodReadabilityAxis", subjectIntent.moodReadabilityAxis },
        { "importanceRegionCount", subjectIntent.importanceRegionCount },
        { "importanceStrokeCount", subjectIntent.importanceStrokeCount },
        { "importanceStrength", subjectIntent.importanceStrength },
        { "importanceImportant", subjectIntent.importanceImportant },
        { "importanceReveal", subjectIntent.importanceReveal },
        { "importanceProtect", subjectIntent.importanceProtect },
        { "importancePreserveMood", subjectIntent.importancePreserveMood },
        { "importanceIgnore", subjectIntent.importanceIgnore },
        { "importanceSubjectPriority", subjectIntent.importanceSubjectPriority },
        { "importanceReadability", subjectIntent.importanceReadability },
        { "importanceProtection", subjectIntent.importanceProtection },
        { "importanceMood", subjectIntent.importanceMood },
        { "importanceLowPriority", subjectIntent.importanceLowPriority },
        { "importanceMap", subjectIntent.importanceMap },
        { "importanceMapCoverage", subjectIntent.importanceMapCoverage },
        { "importanceMapPositiveCoverage", subjectIntent.importanceMapPositiveCoverage },
        { "importanceMapLowPriorityCoverage", subjectIntent.importanceMapLowPriorityCoverage },
        { "importanceMapRevealCoverage", subjectIntent.importanceMapRevealCoverage },
        { "importanceMapProtectCoverage", subjectIntent.importanceMapProtectCoverage },
        { "importanceMapMoodCoverage", subjectIntent.importanceMapMoodCoverage },
        { "importanceMapPeak", subjectIntent.importanceMapPeak },
        { "importanceMapConfidence", subjectIntent.importanceMapConfidence },
        { "importanceMapCenterBias", subjectIntent.importanceMapCenterBias },
        { "importanceMapEdgeBias", subjectIntent.importanceMapEdgeBias },
        { "refinedImportanceMap", subjectIntent.refinedImportanceMap },
        { "refinedMapCoverage", subjectIntent.refinedMapCoverage },
        { "refinedMapLowPriorityCoverage", subjectIntent.refinedMapLowPriorityCoverage },
        { "refinedMapReadabilityCoverage", subjectIntent.refinedMapReadabilityCoverage },
        { "refinedMapProtectionCoverage", subjectIntent.refinedMapProtectionCoverage },
        { "refinedMapMoodCoverage", subjectIntent.refinedMapMoodCoverage },
        { "refinedMapPeak", subjectIntent.refinedMapPeak },
        { "refinedMapConfidence", subjectIntent.refinedMapConfidence },
        { "refinedMapBoundaryHint", subjectIntent.refinedMapBoundaryHint },
        { "evidence", subjectIntent.evidence }
    };
}


} // namespace Stack::Editor::DevelopSubjectImportance

using namespace Stack::Editor::DevelopSubjectImportance;

void EditorModule::NormalizeDevelopSubjectImportance(EditorNodeGraph::DevelopSubjectImportanceMap& importance) {
    importance.schemaVersion = std::max(1, importance.schemaVersion);
    importance.overlayOpacity = std::clamp(importance.overlayOpacity, 0.05f, 1.0f);
    importance.interpretedMapOpacity = std::clamp(importance.interpretedMapOpacity, 0.05f, 1.0f);
    importance.refinedMapOpacity = std::clamp(importance.refinedMapOpacity, 0.05f, 1.0f);
    const int brushModeIndex = std::clamp(
        static_cast<int>(importance.brushMode),
        static_cast<int>(EditorNodeGraph::DevelopSubjectImportanceMode::Important),
        static_cast<int>(EditorNodeGraph::DevelopSubjectImportanceMode::Ignore));
    importance.brushMode = static_cast<EditorNodeGraph::DevelopSubjectImportanceMode>(brushModeIndex);
    importance.brushRadius = std::clamp(importance.brushRadius, 0.005f, 0.25f);
    importance.brushFeather = std::clamp(importance.brushFeather, 0.0f, 1.0f);
    importance.brushStrength = std::clamp(importance.brushStrength, 0.0f, 1.0f);
    if (importance.regions.size() > 32) {
        importance.regions.resize(32);
    }
    if (importance.strokes.size() > 128) {
        importance.strokes.erase(
            importance.strokes.begin(),
            importance.strokes.begin() + static_cast<std::ptrdiff_t>(importance.strokes.size() - 128));
    }

    int maxRegionId = 0;
    for (EditorNodeGraph::DevelopSubjectImportanceRegion& region : importance.regions) {
        if (region.id <= 0) {
            region.id = ++maxRegionId;
        }
        maxRegionId = std::max(maxRegionId, region.id);
        const int modeIndex = std::clamp(
            static_cast<int>(region.mode),
            static_cast<int>(EditorNodeGraph::DevelopSubjectImportanceMode::Important),
            static_cast<int>(EditorNodeGraph::DevelopSubjectImportanceMode::Ignore));
        region.mode = static_cast<EditorNodeGraph::DevelopSubjectImportanceMode>(modeIndex);
        region.centerX = std::clamp(region.centerX, 0.0f, 1.0f);
        region.centerY = std::clamp(region.centerY, 0.0f, 1.0f);
        region.radiusX = std::clamp(region.radiusX, 0.01f, 1.0f);
        region.radiusY = std::clamp(region.radiusY, 0.01f, 1.0f);
        region.feather = std::clamp(region.feather, 0.0f, 1.0f);
        region.strength = std::clamp(region.strength, 0.0f, 1.0f);
    }
    importance.nextRegionId = std::max(importance.nextRegionId, maxRegionId + 1);
    int maxStrokeId = 0;
    for (EditorNodeGraph::DevelopSubjectImportanceStroke& stroke : importance.strokes) {
        if (stroke.id <= 0) {
            stroke.id = ++maxStrokeId;
        }
        maxStrokeId = std::max(maxStrokeId, stroke.id);
        const int modeIndex = std::clamp(
            static_cast<int>(stroke.mode),
            static_cast<int>(EditorNodeGraph::DevelopSubjectImportanceMode::Important),
            static_cast<int>(EditorNodeGraph::DevelopSubjectImportanceMode::Ignore));
        stroke.mode = static_cast<EditorNodeGraph::DevelopSubjectImportanceMode>(modeIndex);
        stroke.radius = std::clamp(stroke.radius, 0.005f, 0.25f);
        stroke.feather = std::clamp(stroke.feather, 0.0f, 1.0f);
        stroke.strength = std::clamp(stroke.strength, 0.0f, 1.0f);
        if (stroke.points.size() > 192) {
            stroke.points.erase(
                stroke.points.begin(),
                stroke.points.begin() + static_cast<std::ptrdiff_t>(stroke.points.size() - 192));
        }
        for (EditorNodeGraph::DevelopSubjectImportanceStrokePoint& point : stroke.points) {
            point.x = std::clamp(point.x, 0.0f, 1.0f);
            point.y = std::clamp(point.y, 0.0f, 1.0f);
        }
    }
    importance.nextStrokeId = std::max(importance.nextStrokeId, maxStrokeId + 1);
    if (importance.regions.empty()) {
        importance.activeRegionId = 0;
    } else {
        const auto activeIt = std::find_if(
            importance.regions.begin(),
            importance.regions.end(),
            [&](const EditorNodeGraph::DevelopSubjectImportanceRegion& region) {
                return region.id == importance.activeRegionId;
            });
        if (activeIt == importance.regions.end()) {
            importance.activeRegionId = importance.regions.front().id;
        }
    }
    if (importance.strokes.empty()) {
        importance.activeStrokeId = 0;
    } else {
        const auto activeStrokeIt = std::find_if(
            importance.strokes.begin(),
            importance.strokes.end(),
            [&](const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke) {
                return stroke.id == importance.activeStrokeId;
            });
        if (activeStrokeIt == importance.strokes.end()) {
            importance.activeStrokeId = importance.strokes.back().id;
        }
    }
}

namespace {

const EditorNodeGraph::Node* FindUpstreamRawSourceNode(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& rawDomainNode) {
    const EditorNodeGraph::Link* rawInput = graph.FindInputLink(rawDomainNode.id, EditorNodeGraph::kRawInputSocketId);
    std::unordered_set<int> visited;
    while (rawInput) {
        if (!visited.insert(rawInput->fromNodeId).second) {
            return nullptr;
        }

        const EditorNodeGraph::Node* upstream = graph.FindNode(rawInput->fromNodeId);
        if (!upstream) {
            return nullptr;
        }
        if (upstream->kind == EditorNodeGraph::NodeKind::RawSource) {
            return upstream;
        }
        if (upstream->kind != EditorNodeGraph::NodeKind::RawNeuralDenoise) {
            return nullptr;
        }
        rawInput = graph.FindInputLink(upstream->id, EditorNodeGraph::kRawInputSocketId);
    }
    return nullptr;
}

} // namespace

bool EditorModule::GetDevelopSubjectImportanceViewportState(DevelopSubjectViewportState& outState) const {
    outState = DevelopSubjectViewportState{};
    const int selectedNodeId = m_NodeGraph.GetSelectedNodeId();
    const EditorNodeGraph::Node* node = selectedNodeId > 0 ? m_NodeGraph.FindNode(selectedNodeId) : nullptr;
    if (!node || node->kind != EditorNodeGraph::NodeKind::RawDevelop) {
        return false;
    }

    const EditorNodeGraph::DevelopSubjectImportanceMap& importance = node->rawDevelop.subjectImportance;
    if (!importance.enabled ||
        !importance.showOverlay ||
        (importance.regions.empty() && importance.strokes.empty() && !importance.brushEnabled)) {
        return false;
    }

    outState.nodeId = node->id;
    outState.enabled = importance.enabled;
    outState.showOverlay = importance.showOverlay;
    outState.overlayOpacity = importance.overlayOpacity;
    outState.showInterpretedMapOverlay = importance.showInterpretedMapOverlay;
    outState.interpretedMapOpacity = importance.interpretedMapOpacity;
    outState.showRefinedMapOverlay = importance.showRefinedMapOverlay;
    outState.refinedMapOpacity = importance.refinedMapOpacity;
    outState.brushEnabled = importance.brushEnabled;
    outState.brushSubtract = importance.brushSubtract;
    outState.brushMode = importance.brushMode;
    outState.brushRadius = importance.brushRadius;
    outState.brushFeather = importance.brushFeather;
    outState.brushStrength = importance.brushStrength;
    outState.activeRegionId = importance.activeRegionId;
    outState.activeStrokeId = importance.activeStrokeId;
    outState.regions.reserve(importance.regions.size());
    outState.strokes.reserve(importance.strokes.size());

    int firstRegionId = 0;
    bool activeRegionFound = false;
    for (const EditorNodeGraph::DevelopSubjectImportanceRegion& region : importance.regions) {
        if (firstRegionId == 0) {
            firstRegionId = region.id;
        }
        activeRegionFound = activeRegionFound || region.id == importance.activeRegionId;
        DevelopSubjectViewportRegion viewportRegion;
        viewportRegion.id = region.id;
        viewportRegion.mode = region.mode;
        viewportRegion.enabled = region.enabled;
        viewportRegion.centerX = region.centerX;
        viewportRegion.centerY = region.centerY;
        viewportRegion.radiusX = region.radiusX;
        viewportRegion.radiusY = region.radiusY;
        viewportRegion.feather = region.feather;
        viewportRegion.strength = region.strength;
        outState.regions.push_back(viewportRegion);
    }
    if (!activeRegionFound) {
        outState.activeRegionId = firstRegionId;
    }

    int firstStrokeId = 0;
    bool activeStrokeFound = false;
    for (const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke : importance.strokes) {
        if (firstStrokeId == 0) {
            firstStrokeId = stroke.id;
        }
        activeStrokeFound = activeStrokeFound || stroke.id == importance.activeStrokeId;
        DevelopSubjectViewportStroke viewportStroke;
        viewportStroke.id = stroke.id;
        viewportStroke.mode = stroke.mode;
        viewportStroke.enabled = stroke.enabled;
        viewportStroke.subtract = stroke.subtract;
        viewportStroke.radius = stroke.radius;
        viewportStroke.feather = stroke.feather;
        viewportStroke.strength = stroke.strength;
        viewportStroke.points.reserve(stroke.points.size());
        for (const EditorNodeGraph::DevelopSubjectImportanceStrokePoint& point : stroke.points) {
            viewportStroke.points.push_back({ point.x, point.y });
        }
        outState.strokes.push_back(std::move(viewportStroke));
    }
    if (!activeStrokeFound) {
        outState.activeStrokeId = firstStrokeId;
    }

    if (importance.showInterpretedMapOverlay) {
        const DevelopSubjectImportanceInterpretation interpretation =
            InterpretDevelopSubjectImportanceMap(importance);
        outState.interpretedMapActive =
            interpretation.active &&
            interpretation.coverage > 0.001f &&
            interpretation.gridWidth > 0 &&
            interpretation.gridHeight > 0;
        if (outState.interpretedMapActive) {
            outState.interpretedMapGridWidth = interpretation.gridWidth;
            outState.interpretedMapGridHeight = interpretation.gridHeight;
            outState.interpretedMapCells.reserve(interpretation.cells.size());
            for (const DevelopSubjectImportanceMapCell& cell : interpretation.cells) {
                DevelopSubjectViewportMapCell viewportCell;
                viewportCell.importance = cell.importance;
                viewportCell.reveal = cell.reveal;
                viewportCell.protect = cell.protect;
                viewportCell.preserveMood = cell.preserveMood;
                viewportCell.lowPriority = cell.lowPriority;
                outState.interpretedMapCells.push_back(viewportCell);
            }
        }
    }

    if (importance.showRefinedMapOverlay) {
        auto appendRefinedMapCellFromJson = [&](const nlohmann::json& cellJson) {
            DevelopSubjectViewportMapCell viewportCell;
            viewportCell.importance = cellJson.value("importance", 0.0f);
            viewportCell.reveal = cellJson.value("readability", 0.0f);
            viewportCell.protect = cellJson.value("protection", 0.0f);
            viewportCell.preserveMood = cellJson.value("preserveMood", 0.0f);
            viewportCell.lowPriority = cellJson.value("lowPriority", 0.0f);
            viewportCell.confidence = cellJson.value("confidence", 0.0f);
            viewportCell.boundaryHint = cellJson.value("boundaryHint", 0.0f);
            outState.refinedMapCells.push_back(viewportCell);
        };
        const nlohmann::json solvedRefinedMap =
            node->rawDevelop.integratedToneLayerJson.value(
                "autoSubjectSceneRefinedMap",
                nlohmann::json::object());
        const nlohmann::json solvedRefinedCells =
            solvedRefinedMap.value("cells", nlohmann::json::array());
        if (solvedRefinedMap.value("version", std::string()) == kDevelopSubjectRefinedMapVersion &&
            solvedRefinedMap.value("active", false) &&
            solvedRefinedCells.is_array() &&
            !solvedRefinedCells.empty()) {
            outState.refinedMapActive = true;
            outState.refinedMapGridWidth = solvedRefinedMap.value("gridWidth", 0);
            outState.refinedMapGridHeight = solvedRefinedMap.value("gridHeight", 0);
            outState.refinedMapCells.reserve(solvedRefinedCells.size());
            for (const nlohmann::json& cellJson : solvedRefinedCells) {
                if (cellJson.is_object()) {
                    appendRefinedMapCellFromJson(cellJson);
                }
            }
            outState.refinedMapActive =
                outState.refinedMapGridWidth > 0 &&
                outState.refinedMapGridHeight > 0 &&
                !outState.refinedMapCells.empty();
        }
        if (!outState.refinedMapActive) {
            const DevelopSubjectImportanceInterpretation interpretation =
                InterpretDevelopSubjectImportanceMap(importance);
            DevelopSubjectSceneIntent fallbackIntent;
            fallbackIntent.userGuidanceStrength = interpretation.mapConfidence;
            fallbackIntent.subjectPriority = SaturateFloat(
                0.45f +
                interpretation.positiveCoverage * 0.20f +
                interpretation.centerBias * 0.12f);
            fallbackIntent.sceneIntegrity = SaturateFloat(
                0.45f +
                interpretation.lowPriorityCoverage * 0.16f +
                interpretation.edgeBias * 0.10f);
            fallbackIntent.improveReadability = SaturateFloat(
                0.42f + interpretation.revealCoverage * 0.30f);
            fallbackIntent.preserveMood = SaturateFloat(
                0.42f +
                interpretation.moodCoverage * 0.24f +
                interpretation.lowPriorityCoverage * 0.12f);
            fallbackIntent.protectionPressure = SaturateFloat(
                interpretation.protectCoverage * 0.36f);
            const DevelopSubjectRefinedMap refinedMap =
                BuildDevelopSubjectRefinedMap(interpretation, fallbackIntent);
            outState.refinedMapActive =
                refinedMap.active &&
                (refinedMap.coverage > 0.001f || refinedMap.lowPriorityCoverage > 0.001f);
            if (outState.refinedMapActive) {
                outState.refinedMapGridWidth = refinedMap.gridWidth;
                outState.refinedMapGridHeight = refinedMap.gridHeight;
                outState.refinedMapCells.reserve(refinedMap.cells.size());
                for (const DevelopSubjectRefinedMapCell& cell : refinedMap.cells) {
                    DevelopSubjectViewportMapCell viewportCell;
                    viewportCell.importance = cell.importance;
                    viewportCell.reveal = cell.readability;
                    viewportCell.protect = cell.protection;
                    viewportCell.preserveMood = cell.preserveMood;
                    viewportCell.lowPriority = cell.lowPriority;
                    viewportCell.confidence = cell.confidence;
                    viewportCell.boundaryHint = cell.boundaryHint;
                    outState.refinedMapCells.push_back(viewportCell);
                }
            }
        }
    }

    return outState.brushEnabled ||
        !outState.regions.empty() ||
        !outState.strokes.empty() ||
        outState.interpretedMapActive ||
        outState.refinedMapActive;
}

bool EditorModule::SetDevelopSubjectImportanceActiveRegion(int nodeId, int regionId) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::RawDevelop || regionId <= 0) {
        return false;
    }
    EditorNodeGraph::DevelopSubjectImportanceMap& importance = node->rawDevelop.subjectImportance;
    const auto regionIt = std::find_if(
        importance.regions.begin(),
        importance.regions.end(),
        [&](const EditorNodeGraph::DevelopSubjectImportanceRegion& region) {
            return region.id == regionId;
        });
    if (regionIt == importance.regions.end() || importance.activeRegionId == regionId) {
        return false;
    }
    importance.activeRegionId = regionId;
    m_Dirty = true;
    return true;
}

bool EditorModule::UpdateDevelopSubjectImportanceRegionFromViewport(
    int nodeId,
    int regionId,
    float centerX,
    float centerY,
    float radiusX,
    float radiusY) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::RawDevelop || regionId <= 0) {
        return false;
    }

    EditorNodeGraph::DevelopSubjectImportanceMap& importance = node->rawDevelop.subjectImportance;
    auto regionIt = std::find_if(
        importance.regions.begin(),
        importance.regions.end(),
        [&](const EditorNodeGraph::DevelopSubjectImportanceRegion& region) {
            return region.id == regionId;
        });
    if (regionIt == importance.regions.end()) {
        return false;
    }

    const float nextCenterX = std::clamp(centerX, 0.0f, 1.0f);
    const float nextCenterY = std::clamp(centerY, 0.0f, 1.0f);
    const float nextRadiusX = std::clamp(radiusX, 0.01f, 1.0f);
    const float nextRadiusY = std::clamp(radiusY, 0.01f, 1.0f);
    const bool geometryChanged =
        std::abs(regionIt->centerX - nextCenterX) > 0.0001f ||
        std::abs(regionIt->centerY - nextCenterY) > 0.0001f ||
        std::abs(regionIt->radiusX - nextRadiusX) > 0.0001f ||
        std::abs(regionIt->radiusY - nextRadiusY) > 0.0001f;
    const bool activeChanged = importance.activeRegionId != regionId;
    if (!geometryChanged && !activeChanged) {
        return false;
    }

    regionIt->centerX = nextCenterX;
    regionIt->centerY = nextCenterY;
    regionIt->radiusX = nextRadiusX;
    regionIt->radiusY = nextRadiusY;
    importance.enabled = true;
    importance.showOverlay = true;
    importance.activeRegionId = regionId;
    NormalizeDevelopSubjectImportance(importance);

    if (geometryChanged) {
        RecordRawDevelopInteraction(nodeId);
        const EditorNodeGraph::Node* rawSourceNode = FindUpstreamRawSourceNode(m_NodeGraph, *node);
        if (node->rawDevelop.uiMode == EditorNodeGraph::RawDevelopUiMode::Auto &&
            rawSourceNode &&
            rawSourceNode->kind == EditorNodeGraph::NodeKind::RawSource) {
            (void)UpdateDevelopAutoState(nodeId, node->rawDevelop, rawSourceNode->rawSource.metadata, true, false);
        }
        MarkRenderDirty(nodeId);
    } else {
        m_Dirty = true;
    }
    return true;
}

int EditorModule::BeginDevelopSubjectImportanceBrushStroke(int nodeId, float x, float y) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::RawDevelop) {
        return 0;
    }

    EditorNodeGraph::DevelopSubjectImportanceMap& importance = node->rawDevelop.subjectImportance;
    if (!importance.brushEnabled) {
        return 0;
    }

    EditorNodeGraph::DevelopSubjectImportanceStroke stroke;
    stroke.id = std::max(1, importance.nextStrokeId++);
    stroke.mode = importance.brushMode;
    stroke.enabled = true;
    stroke.subtract = importance.brushSubtract;
    stroke.radius = importance.brushRadius;
    stroke.feather = importance.brushFeather;
    stroke.strength = importance.brushStrength;
    stroke.points.push_back({
        std::clamp(x, 0.0f, 1.0f),
        std::clamp(y, 0.0f, 1.0f)
    });

    importance.enabled = true;
    importance.showOverlay = true;
    importance.activeStrokeId = stroke.id;
    importance.strokes.push_back(std::move(stroke));
    NormalizeDevelopSubjectImportance(importance);
    RecordRawDevelopInteraction(nodeId);
    m_Dirty = true;
    return importance.activeStrokeId;
}

bool EditorModule::AppendDevelopSubjectImportanceBrushStroke(int nodeId, int strokeId, float x, float y) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::RawDevelop || strokeId <= 0) {
        return false;
    }

    EditorNodeGraph::DevelopSubjectImportanceMap& importance = node->rawDevelop.subjectImportance;
    auto strokeIt = std::find_if(
        importance.strokes.begin(),
        importance.strokes.end(),
        [&](const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke) {
            return stroke.id == strokeId;
        });
    if (strokeIt == importance.strokes.end()) {
        return false;
    }

    const float nextX = std::clamp(x, 0.0f, 1.0f);
    const float nextY = std::clamp(y, 0.0f, 1.0f);
    RecordRawDevelopInteraction(nodeId);
    if (!strokeIt->points.empty()) {
        const EditorNodeGraph::DevelopSubjectImportanceStrokePoint& last = strokeIt->points.back();
        const float dx = nextX - last.x;
        const float dy = nextY - last.y;
        const float minDistance = std::max(strokeIt->radius * 0.35f, 0.003f);
        if (dx * dx + dy * dy < minDistance * minDistance) {
            return false;
        }
    }

    if (strokeIt->points.size() >= 192) {
        strokeIt->points.erase(strokeIt->points.begin());
    }
    strokeIt->points.push_back({ nextX, nextY });
    importance.activeStrokeId = strokeId;
    m_Dirty = true;
    return true;
}

bool EditorModule::EndDevelopSubjectImportanceBrushStroke(int nodeId, int strokeId) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::RawDevelop || strokeId <= 0) {
        return false;
    }

    EditorNodeGraph::DevelopSubjectImportanceMap& importance = node->rawDevelop.subjectImportance;
    const auto strokeIt = std::find_if(
        importance.strokes.begin(),
        importance.strokes.end(),
        [&](const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke) {
            return stroke.id == strokeId;
        });
    if (strokeIt == importance.strokes.end()) {
        return false;
    }

    importance.enabled = true;
    importance.showOverlay = true;
    importance.activeStrokeId = strokeId;
    NormalizeDevelopSubjectImportance(importance);
    RecordRawDevelopInteraction(nodeId);

    const EditorNodeGraph::Node* rawSourceNode = FindUpstreamRawSourceNode(m_NodeGraph, *node);
    if (node->rawDevelop.uiMode == EditorNodeGraph::RawDevelopUiMode::Auto &&
        rawSourceNode &&
        rawSourceNode->kind == EditorNodeGraph::NodeKind::RawSource) {
        (void)UpdateDevelopAutoState(nodeId, node->rawDevelop, rawSourceNode->rawSource.metadata, true, false);
    }
    MarkRenderDirty(nodeId);
    return true;
}

