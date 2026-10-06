#include "Editor/Internal/RawLab/RawDenoiseMapEditor.h"

#include "Editor/Internal/RawLab/RawLabUiSupport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <imgui_internal.h>

namespace Stack::Editor::RawLabInternal {
namespace {

using Stack::RawRecipe::RawDenoiseControlMap;
using Stack::RawRecipe::RawDenoiseControlPoint;
using Stack::RawRecipe::RawDenoiseMapLayer;

RawDenoiseControlMap& ActiveMap(
    Stack::RawRecipe::RawRgbDenoiseRecipe& recipe,
    int layer) {
    return layer == 1 ? recipe.chromaMap : recipe.lumaMap;
}

const RawDenoiseControlMap& MapForLayer(
    const Stack::RawRecipe::RawRgbDenoiseRecipe& recipe,
    int layer) {
    return layer == 1 ? recipe.chromaMap : recipe.lumaMap;
}

RawDenoiseControlPoint* FindPoint(
    RawDenoiseControlMap& map,
    std::uint64_t id) {
    const auto item = std::find_if(
        map.points.begin(),
        map.points.end(),
        [id](const RawDenoiseControlPoint& point) {
            return point.id == id;
        });
    return item == map.points.end() ? nullptr : &*item;
}

const RawDenoiseControlPoint* FindPoint(
    const RawDenoiseControlMap& map,
    std::uint64_t id) {
    const auto item = std::find_if(
        map.points.begin(),
        map.points.end(),
        [id](const RawDenoiseControlPoint& point) {
            return point.id == id;
        });
    return item == map.points.end() ? nullptr : &*item;
}

RawDenoiseControlPoint* FindLinkedPoint(
    Stack::RawRecipe::RawRgbDenoiseRecipe& recipe,
    int activeLayer,
    std::uint64_t linkGroup) {
    if (linkGroup == 0) return nullptr;
    RawDenoiseControlMap& other = ActiveMap(recipe, activeLayer == 0 ? 1 : 0);
    const auto item = std::find_if(
        other.points.begin(),
        other.points.end(),
        [linkGroup](const RawDenoiseControlPoint& point) {
            return point.linkGroup == linkGroup;
        });
    return item == other.points.end() ? nullptr : &*item;
}

ImU32 MapColor(float multiplier, int layer, float alphaScale = 1.0f) {
    const float normalized = std::clamp(multiplier / 2.0f, 0.0f, 1.0f);
    const float alpha = std::clamp(
        (0.10f + normalized * 0.58f) * alphaScale, 0.0f, 1.0f);
    if (layer == 1) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(
            0.58f + normalized * 0.38f,
            0.20f + normalized * 0.16f,
            0.11f + normalized * 0.35f,
            alpha));
    }
    return ImGui::ColorConvertFloat4ToU32(ImVec4(
        0.10f + normalized * 0.18f,
        0.45f + normalized * 0.38f,
        0.52f + normalized * 0.42f,
        alpha));
}

ImVec2 PointScreenPosition(
    const RawDenoiseControlPoint& point,
    const RawDenoiseControlMap& map,
    const ImRect& graph) {
    const float evRange = std::max(1.0f, map.maximumEv - map.minimumEv);
    return ImVec2(
        graph.Min.x + point.frequency * graph.GetWidth(),
        graph.Max.y - (point.sceneEv - map.minimumEv) /
            evRange * graph.GetHeight());
}

void DrawPoint(
    ImDrawList* drawList,
    const ImVec2& position,
    int layer,
    bool active,
    bool selected) {
    const ImU32 fill = layer == 1
        ? IM_COL32(242, 119, 86, active ? 245 : 100)
        : IM_COL32(82, 215, 226, active ? 245 : 100);
    const ImU32 outline = selected
        ? IM_COL32(255, 255, 255, 255)
        : IM_COL32(21, 24, 28, active ? 230 : 100);
    const float radius = selected ? 6.5f : 5.0f;
    if (layer == 1) {
        const ImVec2 diamond[4] = {
            ImVec2(position.x, position.y - radius),
            ImVec2(position.x + radius, position.y),
            ImVec2(position.x, position.y + radius),
            ImVec2(position.x - radius, position.y)
        };
        drawList->AddConvexPolyFilled(diamond, 4, fill);
        drawList->AddPolyline(diamond, 4, outline, ImDrawFlags_Closed, 1.5f);
    } else {
        drawList->AddCircleFilled(position, radius, fill, 18);
        drawList->AddCircle(position, radius, outline, 18, 1.5f);
    }
}

std::uint64_t PointAtScreenPosition(
    const RawDenoiseControlMap& map,
    const ImRect& graph,
    const ImVec2& mouse) {
    std::uint64_t found = 0;
    float nearestSquared = 10.0f * 10.0f;
    for (const RawDenoiseControlPoint& point : map.points) {
        const ImVec2 position = PointScreenPosition(point, map, graph);
        const float dx = mouse.x - position.x;
        const float dy = mouse.y - position.y;
        const float distanceSquared = dx * dx + dy * dy;
        if (distanceSquared <= nearestSquared) {
            nearestSquared = distanceSquared;
            found = point.id;
        }
    }
    return found;
}

void MovePointFromScreen(
    RawDenoiseControlPoint& point,
    const RawDenoiseControlMap& map,
    const ImRect& graph,
    const ImVec2& mouse) {
    point.frequency = std::clamp(
        (mouse.x - graph.Min.x) / std::max(1.0f, graph.GetWidth()),
        0.0f,
        1.0f);
    const float vertical = std::clamp(
        (graph.Max.y - mouse.y) / std::max(1.0f, graph.GetHeight()),
        0.0f,
        1.0f);
    point.sceneEv = map.minimumEv +
        vertical * (map.maximumEv - map.minimumEv);
    if (point.frequency < 0.025f) {
        point.frequency = 0.0f;
    }
    if (point.frequency > 0.975f) {
        point.frequency = 1.0f;
    }
    if (vertical < 0.025f) {
        point.sceneEv = map.minimumEv;
    }
    if (vertical > 0.975f) {
        point.sceneEv = map.maximumEv;
    }
}

void DeletePointAndLinked(
    Stack::RawRecipe::RawRgbDenoiseRecipe& recipe,
    int activeLayer,
    std::uint64_t pointId) {
    RawDenoiseControlMap& map = ActiveMap(recipe, activeLayer);
    const RawDenoiseControlPoint* point = FindPoint(map, pointId);
    const std::uint64_t linkGroup = point ? point->linkGroup : 0;
    map.points.erase(
        std::remove_if(
            map.points.begin(), map.points.end(),
            [pointId](const RawDenoiseControlPoint& value) {
                return value.id == pointId;
            }),
        map.points.end());
    if (linkGroup == 0) return;
    RawDenoiseControlMap& other = ActiveMap(recipe, activeLayer == 0 ? 1 : 0);
    other.points.erase(
        std::remove_if(
            other.points.begin(), other.points.end(),
            [linkGroup](const RawDenoiseControlPoint& value) {
                return value.linkGroup == linkGroup;
            }),
        other.points.end());
}

} // namespace

RawDenoiseMapEditorResult RenderRawDenoiseMapEditor(
    Stack::RawRecipe::RawRgbDenoiseRecipe& recipe,
    RawDenoiseMapEditorState& state) {
    RawDenoiseMapEditorResult result;
    state.activeLayer = std::clamp(state.activeLayer, 0, 1);

    if (state.pointDragDiagnosticActive &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        state.pointDragDiagnosticActive = false;
        result.viewChanged = true;
    }

    ImGui::PushID("RawDenoiseMapLayerTabs");
    if (BareTextButton("Luma", state.activeLayer == 0)) {
        state.activeLayer = 0;
        state.selectedPointId = 0;
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (BareTextButton("Chroma", state.activeLayer == 1)) {
        state.activeLayer = 1;
        state.selectedPointId = 0;
    }
    ImGui::PopID();
    ImGui::SameLine();
    ImGui::TextDisabled("x frequency   y source EV");

    RawDenoiseControlMap& activeMap = ActiveMap(recipe, state.activeLayer);
    const RawDenoiseControlMap& inactiveMap =
        MapForLayer(recipe, state.activeLayer == 0 ? 1 : 0);
    const ImVec2 size = CompactLabGraphSize(210.0f, 190.0f, 280.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImRect graph(origin, ImVec2(origin.x + size.x, origin.y + size.y));
    ImGui::InvisibleButton(
        "##RawDenoiseFrequencyLuminanceMap",
        size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(graph.Min, graph.Max, IM_COL32(14, 17, 20, 255));

    constexpr int columns = 24;
    constexpr int rows = 18;
    for (int y = 0; y < rows; ++y) {
        const float ev = activeMap.maximumEv -
            (static_cast<float>(y) + 0.5f) /
                static_cast<float>(rows) *
                (activeMap.maximumEv - activeMap.minimumEv);
        for (int x = 0; x < columns; ++x) {
            const float frequency =
                (static_cast<float>(x) + 0.5f) /
                static_cast<float>(columns);
            const float multiplier =
                Stack::RawRecipe::EvaluateRawDenoiseControlMap(
                    activeMap, frequency, ev);
            const ImVec2 minimum(
                graph.Min.x + graph.GetWidth() *
                    static_cast<float>(x) / static_cast<float>(columns),
                graph.Min.y + graph.GetHeight() *
                    static_cast<float>(y) / static_cast<float>(rows));
            const ImVec2 maximum(
                graph.Min.x + graph.GetWidth() *
                    static_cast<float>(x + 1) / static_cast<float>(columns),
                graph.Min.y + graph.GetHeight() *
                    static_cast<float>(y + 1) / static_cast<float>(rows));
            drawList->AddRectFilled(
                minimum,
                maximum,
                MapColor(multiplier, state.activeLayer));
        }
    }

    const float octaveRange = std::max(
        1.0f,
        std::log2(std::max(8.0f, recipe.maximumStructureSize) / 4.0f));
    for (int octave = 0;
         static_cast<float>(octave) <= octaveRange + 0.001f;
         ++octave) {
        const float frequency = 1.0f -
            static_cast<float>(octave) / octaveRange;
        const float x = graph.Min.x + frequency * graph.GetWidth();
        drawList->AddLine(
            ImVec2(x, graph.Min.y),
            ImVec2(x, graph.Max.y),
            IM_COL32(255, 255, 255, 24));
    }
    for (float ev : { -6.0f, -3.0f, 0.0f, 3.0f }) {
        if (ev < activeMap.minimumEv || ev > activeMap.maximumEv) continue;
        const float y = graph.Max.y -
            (ev - activeMap.minimumEv) /
                (activeMap.maximumEv - activeMap.minimumEv) * graph.GetHeight();
        drawList->AddLine(
            ImVec2(graph.Min.x, y),
            ImVec2(graph.Max.x, y),
            IM_COL32(255, 255, 255, 25));
        char label[16];
        std::snprintf(label, sizeof(label), "%+.0f EV", ev);
        drawList->AddText(
            ImVec2(graph.Min.x + 4.0f, y + 2.0f),
            IM_COL32(220, 225, 230, 120),
            label);
    }
    char largeStructureLabel[32];
    std::snprintf(
        largeStructureLabel,
        sizeof(largeStructureLabel),
        "%.0f px source",
        recipe.maximumStructureSize);
    drawList->AddText(
        ImVec2(graph.Min.x + 5.0f, graph.Max.y - 18.0f),
        IM_COL32(225, 230, 235, 150),
        largeStructureLabel);
    const char* fineLabel = "4 px source";
    drawList->AddText(
        ImVec2(
            graph.Max.x - ImGui::CalcTextSize(fineLabel).x - 5.0f,
            graph.Max.y - 18.0f),
        IM_COL32(225, 230, 235, 150),
        fineLabel);

    const RawDenoiseControlPoint* selectedForGraph =
        FindPoint(activeMap, state.selectedPointId);
    if (selectedForGraph) {
        constexpr int segments = 48;
        ImVec2 influence[segments + 1];
        const ImVec2 center = PointScreenPosition(
            *selectedForGraph, activeMap, graph);
        const bool spansAllFrequencies =
            selectedForGraph->frequency <= 0.0001f ||
            selectedForGraph->frequency >= 0.9999f;
        const bool spansAllLuminances =
            selectedForGraph->sceneEv <= activeMap.minimumEv + 0.0001f ||
            selectedForGraph->sceneEv >= activeMap.maximumEv - 0.0001f;
        const float radiusX = spansAllFrequencies
            ? graph.GetWidth() * 2.0f
            : selectedForGraph->frequencyRadius * graph.GetWidth();
        const float radiusY = spansAllLuminances
            ? graph.GetHeight() * 2.0f
            : selectedForGraph->luminanceRadiusEv /
                std::max(1.0f, activeMap.maximumEv - activeMap.minimumEv) *
                graph.GetHeight();
        for (int segment = 0; segment <= segments; ++segment) {
            const float angle = 2.0f * 3.14159265358979323846f *
                static_cast<float>(segment) / static_cast<float>(segments);
            influence[segment] = ImVec2(
                center.x + std::cos(angle) * radiusX,
                center.y + std::sin(angle) * radiusY);
        }
        drawList->PushClipRect(graph.Min, graph.Max, true);
        drawList->AddPolyline(
            influence,
            segments + 1,
            state.activeLayer == 1
                ? IM_COL32(242, 119, 86, 120)
                : IM_COL32(82, 215, 226, 120),
            ImDrawFlags_Closed,
            1.5f);
        drawList->PopClipRect();
    }

    for (const RawDenoiseControlPoint& point : inactiveMap.points) {
        DrawPoint(
            drawList,
            PointScreenPosition(point, inactiveMap, graph),
            state.activeLayer == 0 ? 1 : 0,
            false,
            false);
    }
    for (const RawDenoiseControlPoint& point : activeMap.points) {
        DrawPoint(
            drawList,
            PointScreenPosition(point, activeMap, graph),
            state.activeLayer,
            true,
            point.id == state.selectedPointId);
    }

    if (hovered) {
        state.hoveredPointId = PointAtScreenPosition(
            activeMap, graph, ImGui::GetIO().MousePos);
    } else {
        state.hoveredPointId = 0;
    }

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
        state.hoveredPointId == 0 &&
        activeMap.points.size() < Stack::RawRecipe::kMaxRawDenoiseControlPoints) {
        RawDenoiseControlPoint point;
        point.id = recipe.nextControlPointId++;
        MovePointFromScreen(point, activeMap, graph, ImGui::GetIO().MousePos);
        if (state.linkNewPoints) {
            point.linkGroup = recipe.nextControlPointId++;
        }
        activeMap.points.push_back(point);
        state.selectedPointId = point.id;
        if (point.linkGroup != 0) {
            RawDenoiseControlPoint linked = point;
            linked.id = recipe.nextControlPointId++;
            ActiveMap(recipe, state.activeLayer == 0 ? 1 : 0).points.push_back(linked);
        }
        result.recipeChanged = true;
    } else if (hovered && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        state.selectedPointId = state.hoveredPointId;
        state.draggingPoint = state.selectedPointId != 0;
    }

    if (state.draggingPoint && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!state.pointDragDiagnosticActive &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            state.pointDragDiagnosticActive = true;
            result.viewChanged = true;
        }
        RawDenoiseControlPoint* point = FindPoint(activeMap, state.selectedPointId);
        if (point) {
            MovePointFromScreen(*point, activeMap, graph, ImGui::GetIO().MousePos);
            if (RawDenoiseControlPoint* linked = FindLinkedPoint(
                    recipe, state.activeLayer, point->linkGroup)) {
                linked->frequency = point->frequency;
                linked->sceneEv = point->sceneEv;
                linked->frequencyRadius = point->frequencyRadius;
                linked->luminanceRadiusEv = point->luminanceRadiusEv;
            }
            result.recipeChanged = true;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        state.draggingPoint = false;
    }
    if (hovered && ImGui::IsItemClicked(ImGuiMouseButton_Right) &&
        state.hoveredPointId != 0) {
        DeletePointAndLinked(recipe, state.activeLayer, state.hoveredPointId);
        if (state.selectedPointId == state.hoveredPointId) {
            state.selectedPointId = 0;
        }
        result.recipeChanged = true;
    }

    drawList->AddRect(
        graph.Min,
        graph.Max,
        IM_COL32(185, 195, 205, hovered ? 150 : 85));
    ImGui::Spacing();

    float base = activeMap.baseMultiplier;
    if (BareSliderFloat(
            state.activeLayer == 0 ? "Luma Global" : "Chroma Global",
            "RawDenoiseMapBase",
            &base,
            0.0f,
            Stack::RawRecipe::kRawDenoiseMaximumMultiplier,
            "%.2f x auto")) {
        activeMap.baseMultiplier = base;
        result.recipeChanged = true;
    }

    RawDenoiseControlPoint* selected =
        FindPoint(activeMap, state.selectedPointId);
    if (selected) {
        float delta = selected->multiplierDelta;
        if (BareSliderFloat(
                "Point Amount",
                "RawDenoisePointAmount",
                &delta,
                0.0f,
                Stack::RawRecipe::kRawDenoiseMaximumMultiplier,
                "%.2f x auto")) {
            selected->multiplierDelta = delta;
            result.recipeChanged = true;
        }
        float frequencyRadius = selected->frequencyRadius;
        if (BareSliderFloat(
                "Frequency Reach",
                "RawDenoisePointFrequencyReach",
                &frequencyRadius,
                0.01f,
                1.0f,
                "%.2f")) {
            selected->frequencyRadius = frequencyRadius;
            if (RawDenoiseControlPoint* linked = FindLinkedPoint(
                    recipe, state.activeLayer, selected->linkGroup)) {
                linked->frequencyRadius = frequencyRadius;
            }
            result.recipeChanged = true;
        }
        float luminanceRadius = selected->luminanceRadiusEv;
        if (BareSliderFloat(
                "EV Reach",
                "RawDenoisePointEvReach",
                &luminanceRadius,
                0.05f,
                14.0f,
                "%.2f EV")) {
            selected->luminanceRadiusEv = luminanceRadius;
            if (RawDenoiseControlPoint* linked = FindLinkedPoint(
                    recipe, state.activeLayer, selected->linkGroup)) {
                linked->luminanceRadiusEv = luminanceRadius;
            }
            result.recipeChanged = true;
        }
        if (selected->linkGroup != 0) {
            if (BareTextButton("Unlink position")) {
                if (RawDenoiseControlPoint* linked = FindLinkedPoint(
                        recipe, state.activeLayer, selected->linkGroup)) {
                    linked->linkGroup = 0;
                }
                selected->linkGroup = 0;
                result.recipeChanged = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Luma/chroma positions linked");
        }
        if (BareTextButton("Delete point")) {
            DeletePointAndLinked(
                recipe, state.activeLayer, state.selectedPointId);
            state.selectedPointId = 0;
            result.recipeChanged = true;
        }
    } else {
        ImGui::TextDisabled("Double-click the map to add a neutral point. Right-click deletes.");
    }
    ImGui::TextDisabled(
        "Edges broaden a point to one axis; corners affect the whole map.");

    if (ImGui::Checkbox("Link new luma/chroma points", &state.linkNewPoints)) {
        result.viewChanged = true;
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Viewport diagnostic");
    ImGui::PushID("RawDenoiseViewportDiagnostics");
    const int effectiveDiagnosticMode =
        state.pointDragDiagnosticActive ? 2 : state.diagnosticMode;
    const auto diagnosticButton = [&](const char* label, int mode) {
        if (BareTextButton(label, effectiveDiagnosticMode == mode)) {
            state.diagnosticMode = state.diagnosticMode == mode ? 0 : mode;
            result.viewChanged = true;
        }
    };
    diagnosticButton("Coverage", 1);
    ImGui::SameLine(0.0f, 2.0f);
    diagnosticButton("Control", 2);
    ImGui::SameLine(0.0f, 2.0f);
    diagnosticButton("All change", 3);
    ImGui::SameLine(0.0f, 2.0f);
    diagnosticButton("Luma", 4);
    ImGui::SameLine(0.0f, 2.0f);
    diagnosticButton("Chroma", 5);
    ImGui::SameLine(0.0f, 2.0f);
    diagnosticButton("Noise", 6);
    ImGui::PopID();
    ImGui::TextDisabled("Control is shown automatically while dragging a point.");

    ImGui::TextDisabled("Diagnostic quality");
    ImGui::PushID("RawDenoiseDiagnosticQuality");
    if (BareTextButton("Match preview", !state.fullResolutionDiagnostics)) {
        state.fullResolutionDiagnostics = false;
        result.viewChanged = true;
    }
    LabTooltip(
        "Render diagnostics at the exact same proxy dimensions as the interactive image preview.");
    ImGui::SameLine(0.0f, 2.0f);
    if (BareTextButton("Full", state.fullResolutionDiagnostics)) {
        state.fullResolutionDiagnostics = true;
        result.viewChanged = true;
    }
    LabTooltip(
        "Attempt a native-resolution diagnostic. This is more precise but may be slower.");
    ImGui::PopID();

    return result;
}

} // namespace Stack::Editor::RawLabInternal
