#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "Editor/NodeGraph/UI/GraphKeyboardInsertion.h"
#include "Editor/EditorModule.h"
#include "Editor/Internal/GraphConnectionContext.h"
#include "Editor/Internal/GraphEditorCommands.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"
#include "Editor/NodeGraph/UI/NodeCatalogCommand.h"
#include <algorithm>
#include <cmath>

namespace {
using namespace EditorNodeGraph;
using State = Stack::Editor::GraphKeyboardInsertion;
using Stage = State::Stage;

ImU32 Color(ImVec4 color, float opacity = 1.f) {
    color.w *= opacity;
    return ImGui::ColorConvertFloat4ToU32(color);
}

ImVec2 Screen(Vec2 point) { return {point.x, point.y}; }

void Curve(ImDrawList& draw, ImVec2 from, ImVec2 to, ImU32 color, float width) {
    const float bend = std::max(18.f, std::abs(to.x - from.x) * .45f);
    draw.AddBezierCubic(from, {from.x + bend, from.y}, {to.x - bend, to.y}, to, color, width);
}

void Stroke(ImDrawList& draw, const Stack::Editor::NodeGraphUIVisuals::ProjectedWire& path,
    ImU32 color, float width) {
    for (std::size_t i = 1; i < path.samples.size(); ++i)
        draw.AddLine(path.samples[i - 1].point, path.samples[i].point, color, width);
}
}

void EditorNodeGraphUI::SetKeyboardRoomTargets(const Graph& graph) {
    auto& state = *m_KeyboardInsertion;
    state.ghostStart = state.ghostCurrent;
    auto previousNodes = std::move(state.nodes);
    auto previousGroups = std::move(state.groups);
    state.nodes.clear();
    state.groups.clear();
    const bool reduce = Stack::Editor::NodeGraphUIVisuals::GraphReducedMotion();
    for (const auto& placement : state.placement.nodes) {
        const auto old = previousNodes.find(placement.id);
        const auto start = old == previousNodes.end() ? DisplayNodePosition(*graph.FindNode(placement.id)) : old->second.current;
        state.nodes.emplace(placement.id, State::Position{start, reduce ? placement.after : start, placement.after});
    }
    for (const auto& [id, motion] : previousNodes) if (!state.nodes.count(id)) {
        if (const auto* node = graph.FindNode(id))
            state.nodes.emplace(id, State::Position{motion.current, reduce ? node->position : motion.current, node->position});
    }
    for (const auto& placement : state.placement.groups) {
        const auto old = previousGroups.find(placement.id);
        const auto start = old == previousGroups.end() ? placement.before : old->second.current;
        state.groups.emplace(placement.id, State::Frame{start, reduce ? placement.after : start, placement.after});
    }
    for (const auto& [id, motion] : previousGroups) if (!state.groups.count(id)) {
        const auto found = std::find_if(graph.GetGroups().begin(), graph.GetGroups().end(), [&](const auto& group) { return group.id == id; });
        if (found == graph.GetGroups().end()) continue;
        const GraphRect authored{found->position, {found->position.x + found->size.x, found->position.y + found->size.y}};
        state.groups.emplace(id, State::Frame{motion.current, reduce ? authored : motion.current, authored});
    }
    // Rigid groups stay synchronized. Ungrouped neighbors pick up the pull a
    // little later with distance; these delays are prepared only on retarget.
    if (state.groups.empty()) for (auto& [id, motion] : state.nodes) {
        const float distance = std::abs(motion.target.x-state.ghostPosition.x);
        motion.delay = std::min(.04, double(distance / 300.f) * .018);
    }
    state.roomStart = ImGui::GetTime();
    state.roomMoving = true;
}

void EditorNodeGraphUI::PrepareKeyboardRoom(Graph& graph) {
    auto& state = *m_KeyboardInsertion;
    state.prepared = true;
    state.roomError.clear();
    const auto& target = state.targets[state.target];
    if (target.wire && target.wire->ownership == Link::Ownership::ManagedSourceBinding) {
        state.roomError = "Stack manages this source connection.";
        state.placement = {};
        SetKeyboardRoomTargets(graph);
        return;
    }
    auto bounds = state.stage == Stage::Choose ? GraphRect{} : state.ghostBounds;
    bounds.min.x = std::min(-10.f, bounds.min.x);
    bounds.max.x = std::max(255.f, bounds.max.x);
    bounds.max.y = std::max(58.f, bounds.max.y);
    if (state.stage == Stage::Results && !state.results.empty() && state.results[state.result].error.empty()) {
        const auto measured = state.results[state.result].bounds;
        bounds.min.x = std::min(measured.min.x, bounds.min.x);
        bounds.min.y = std::min(measured.min.y, bounds.min.y);
        bounds.max.x = std::max(measured.max.x, bounds.max.x);
        bounds.max.y = std::max(measured.max.y, bounds.max.y);
    }
    const auto* anchor = graph.FindNode(target.nodeId);
    const auto anchorBounds = MeasureNodeForLayout(graph, *anchor);
    const bool right = target.direction == SocketDirection::Output;
    const float x = right ? anchorBounds.max.x + 50.f - bounds.min.x : anchorBounds.min.x - 50.f - bounds.max.x;
    const auto anchorPin = ScreenToGraph(right ? OutputPinScreenPos(*anchor, target.socket) : InputPinScreenPos(*anchor, target.socket));
    float row = anchorPin.y;
    // Follow the selected branch's destination row rather than the junction's
    // shared output row. Authored coordinates keep previews independent of motion.
    if (target.wire) {
        const int peerId = right ? target.wire->toNodeId : target.wire->fromNodeId;
        if (const auto* peer = graph.FindNode(peerId)) {
            const auto peerPin = ScreenToGraph(right ? InputPinScreenPos(*peer, target.wire->toSocketId) : OutputPinScreenPos(*peer, target.wire->fromSocketId));
            row = peerPin.y - DisplayNodePosition(*peer).y + peer->position.y;
        }
    }
    GraphRect visual = bounds;
    const State::Result* selected = state.stage == Stage::Results && !state.results.empty() &&
        state.results[state.result].error.empty() ? &state.results[state.result] : nullptr;
    if (selected) visual = selected->bounds;
    const auto alignPin = selected ? (right ? selected->input : selected->output) : std::optional<Vec2>{};
    const float localRow = alignPin ? alignPin->y : (visual.min.y+visual.max.y)*.5f;
    Vec2 position{anchor->position.x + x, row-localRow};
    // Reuse the locked origin while browsing smaller candidates. Only a larger
    // reservation asks the room solver to move neighbors again.
    const bool sameRoom = state.previewCatalog != ~std::size_t{} &&
        bounds.min.x == state.ghostBounds.min.x && bounds.min.y == state.ghostBounds.min.y &&
        bounds.max.x == state.ghostBounds.max.x && bounds.max.y == state.ghostBounds.max.y;
    if (sameRoom) position = state.ghostPosition;
    if (selected) state.previewCatalog = selected->catalogIndex;
    state.ghostVisual = visual;
    if (sameRoom) {
        state.ghostStart = state.ghostCurrent;
        // Continue from current displayed positions when shape changes mid-flight.
        for (auto& [id, motion] : state.nodes) { motion.start=motion.current; motion.delay=0; }
        for (auto& [id, motion] : state.groups) motion.start=motion.current;
        state.roomStart=ImGui::GetTime();
        state.roomMoving=true;
        return;
    }

    // The placeholder only exists in a geometry candidate. Keep the selected
    // node fixed so the user can continue stepping through its pins.
    auto geometry = graph.CloneForAnalysis();
    auto* ghost = geometry.AddPreviewNode(position);
    const int ghostId = ghost->id;
    const auto measure = [&](const Graph&, const Node& node) {
        if (node.id == ghostId) return bounds;
        return MeasureNodeForLayout(graph, *graph.FindNode(node.id));
    };
    Stack::GraphLayout::RoomGesture room;
    Stack::GraphLayout::PlacementEdit placement;
    if (!room.Begin(geometry, {anchor->id, ghostId}, measure, state.roomError, {}, {}, true) ||
        !room.Move({}, placement, state.roomError)) return;
    placement.nodes.erase(std::remove_if(placement.nodes.begin(), placement.nodes.end(),
        [&](const auto& item) { return item.id == ghostId; }), placement.nodes.end());
    state.placement = std::move(placement);
    if (state.ghostOpened < 0) state.ghostOpened = ImGui::GetTime();
    const GraphRect next{{position.x + visual.min.x, position.y + visual.min.y},
        {position.x + visual.max.x, position.y + visual.max.y}};
    if (state.ghostBounds.max.x == 0 || Stack::Editor::NodeGraphUIVisuals::GraphReducedMotion())
        state.ghostCurrent = next;
    state.ghostStart = state.ghostCurrent;
    state.ghostBounds = bounds;
    state.ghostPosition = position;
    if (!state.pickerPlaced) {
        state.pickerPosition = {position.x+bounds.min.x, position.y+bounds.max.y};
        state.pickerPlaced = true;
    } else state.pickerPosition.y = std::max(state.pickerPosition.y, position.y+bounds.max.y);
    SetKeyboardRoomTargets(graph);
}

void EditorNodeGraphUI::CommitKeyboardInsertion(EditorModule& editor) {
    auto& state = *m_KeyboardInsertion;
    state.commit = false;
    if (!state.prepared || !state.roomError.empty() || state.ghostBounds.max.x == 0) return;
    const auto& target = state.targets[state.target];
    const auto& result = state.results[state.result];
    const auto& entries = Stack::Editor::RegisteredNodeCatalog();
    const auto published = Stack::Editor::PublishedConnectionContext(editor);
    int id = 0;
    std::string error;
    const bool applied = Stack::Editor::ApplyGraphCommand(editor, [&](Graph& graph) {
        auto insertion = Stack::GraphModel::PrepareCatalogInsertion(graph, target, entries[result.catalogIndex],
            state.ghostPosition, error, &published);
        if (!insertion || !Stack::GraphLayout::ApplyPlacement(insertion->graph, state.placement, error))
            throw std::runtime_error(error);
        id = insertion->nodeId;
        insertion->graph.SelectNode(id);
        graph = std::move(insertion->graph);
    }, &error);
    if (!applied) {
        state.error = std::move(error);
        return;
    }
    editor.SelectGraphNode(id);
    const auto after = editor.GetGraphEditorContext();
    state.revision = after.graph->GetStructureRevision();
    state.documentRevision = after.documentRevision;
    state.graph = after.graph;
    state.stage = Stage::Inserted;
    state.inserted = id;
    state.oldWire = target.wire;
    const auto finishRoom = [&](auto& motions) {
        for (auto& [key, motion] : motions) motion.start = motion.current;
    };
    finishRoom(state.nodes);
    finishRoom(state.groups);
    const auto offset = Vec2{state.ghostCurrent.min.x - state.ghostVisual.min.x,
        state.ghostCurrent.min.y - state.ghostVisual.min.y};
    state.nodes.emplace(id, State::Position{offset, offset, state.ghostPosition});
    state.roomStart = ImGui::GetTime();
    state.roomMoving = true;
}

float EditorNodeGraphUI::KeyboardWireOpacity(const Link& link) const {
    if (m_RenderPreviewOnly || m_RenderCaptureOnly || !m_KeyboardInsertion || m_KeyboardInsertion->stage != Stage::Inserted ||
        (link.fromNodeId != m_KeyboardInsertion->inserted && link.toNodeId != m_KeyboardInsertion->inserted)) return 1.f;
    if (Stack::Editor::NodeGraphUIVisuals::GraphReducedMotion()) return 1.f;
    return float(std::clamp((ImGui::GetTime() - m_KeyboardInsertion->roomStart) / .16, 0., 1.));
}

void EditorNodeGraphUI::DrawKeyboardInsertion(const Graph& graph, ImDrawList& draw,
    const Stack::Editor::NodeGraphUIVisuals::GraphStyleTokens& style) {
    auto& state = *m_KeyboardInsertion;
    if (state.graph != &graph) return;
    const auto now = ImGui::GetTime();
    const bool reduce = Stack::Editor::NodeGraphUIVisuals::GraphReducedMotion();
    const auto fade = [&](double start, double duration) {
        return reduce ? 1.f : float(std::clamp((now - start) / duration, 0., 1.));
    };
    if (state.stage == Stage::Inserted) {
        if (state.oldWire && graph.FindNode(state.oldWire->fromNodeId) && graph.FindNode(state.oldWire->toNodeId))
            Stroke(draw, WirePath(graph, *state.oldWire), Color(style.selected, 1.f - fade(state.roomStart, .16)), 2.f);
        if (const auto* inserted = graph.FindNode(state.inserted)) {
            const auto& result = state.results[state.result];
            const float alpha = 1.f-fade(state.roomStart, .22);
            const auto origin = DisplayNodePosition(*inserted);
            for (const auto& local : {result.input, result.output}) if (local) {
                const auto center = GraphToScreen({origin.x+local->x, origin.y+local->y});
                draw.AddCircle(Screen(center), 5.f, Color(style.selected, alpha), 16, 1.5f);
            }
        }
        return;
    }
    const auto panel = [&](ImVec2 at, ImVec2 size, float alpha) {
        const ImVec2 end{at.x + size.x, at.y + size.y};
        draw.AddRectFilled(at, end, Color(style.nodeSurface, .98f * alpha), 8.f);
        draw.AddRect(at, end, Color(style.selected, .35f * alpha), 8.f);
    };
    if (state.stage == Stage::Find) {
        const float alpha = fade(state.opened, .12);
        const auto outline = [&](int id, float opacity) {
            if (!id || opacity == 0.f) return;
            const auto* cache = FindNodeLayoutCache(id);
            if (!cache) return;
            const auto& rect = cache->frameRect;
            draw.AddRect({rect.min.x - 3, rect.min.y - 3}, {rect.max.x + 3, rect.max.y + 3},
                Color(style.selected, opacity * .75f), 7.f, 0, 2.f);
        };
        const float matchAlpha = fade(state.matchChanged, .12);
        outline(state.previousMatch, 1.f - matchAlpha);
        outline(state.matched, matchAlpha);
        const ImVec2 at{m_CanvasMin.x + 14, m_CanvasMin.y + 14};
        const char* label = state.query.empty() ? "Type a node name" : state.query.c_str();
        const char* hint = !state.query.empty() && !state.matched ? "No matching node" : "Enter to select   Esc to close";
        const float width = std::min(320.f, m_CanvasMax.x - at.x - 10.f);
        panel(at, {width, 54}, alpha);
        const ImVec4 clip{at.x + 10, at.y, at.x + width - 10, at.y + 54};
        draw.AddText(ImGui::GetFont(), ImGui::GetFontSize(), {at.x + 10, at.y + 8}, Color(style.text, alpha), label, nullptr, 0, &clip);
        draw.AddText({at.x + 10, at.y + 29}, Color(style.mutedText, alpha), hint);
        return;
    }
    const bool closing = state.stage == Stage::Closing;
    if (state.targets.empty()) return;
    const auto& target = state.targets[state.target];
    const auto* node = graph.FindNode(target.nodeId);
    if (!node) return;
    const bool right = target.direction == SocketDirection::Output;
    const auto pin = Screen(right ? OutputPinScreenPos(*node, target.socket) : InputPinScreenPos(*node, target.socket));
    if (!closing) {
        if (target.wire && graph.FindNode(target.wire->fromNodeId) && graph.FindNode(target.wire->toNodeId))
            Stroke(draw, WirePath(graph, *target.wire), Color(style.selected, .85f), 2.8f);
        if (target.wire) {
            const auto* other = graph.FindNode(right ? target.wire->toNodeId : target.wire->fromNodeId);
            if (other) {
                const auto end = Screen(right ? InputPinScreenPos(*other, target.wire->toSocketId) : OutputPinScreenPos(*other, target.wire->fromSocketId));
                draw.AddCircle(end, 5.f, Color(style.selected, .75f), 16, 1.5f);
            }
        }
        draw.AddCircleFilled(pin, 6.f, Color(style.selected, .26f), 16);
        draw.AddCircle(pin, 6.f, Color(style.selected), 16, 2.f);
    }

    const auto* result = state.results.empty() ? nullptr : &state.results[state.result];
    const auto& entries = Stack::Editor::RegisteredNodeCatalog();
    const char* label = result ? entries[result->catalogIndex].label.c_str() : "New node";
    const auto& bounds = state.ghostVisual;
    const auto low = GraphToScreen(state.ghostCurrent.min);
    const auto high = GraphToScreen(state.ghostCurrent.max);
    const bool ghostVisible = bounds.max.x > 0 && (closing || (state.prepared && state.roomError.empty()));
    if (ghostVisible) {
        const float alpha = closing ? 1.f - fade(state.roomStart, .22) : fade(state.ghostOpened, .14);
        const float row = (low.y + high.y) * .5f;
        const auto previewPin = [&](const std::optional<Vec2>& local, float fallbackX) {
            return local ? ImVec2{low.x + (local->x - bounds.min.x) * (high.x-low.x)/std::max(1.f,bounds.max.x-bounds.min.x),
                low.y + (local->y - bounds.min.y) * (high.y-low.y)/std::max(1.f,bounds.max.y-bounds.min.y)} : ImVec2{fallbackX, row};
        };
        const auto input = previewPin(result ? result->input : std::nullopt, low.x);
        const auto output = previewPin(result ? result->output : std::nullopt, high.x);
        const bool showInput = right || target.wire.has_value();
        const bool showOutput = !right || target.wire.has_value();
        if (showInput) {
            const auto* from = target.wire ? graph.FindNode(target.wire->fromNodeId) : node;
            if (from) Curve(draw, Screen(OutputPinScreenPos(*from, target.wire ? target.wire->fromSocketId : target.socket)), input,
                Color(style.selected, .5f * alpha), 1.5f);
        }
        if (showOutput) {
            const auto* to = target.wire ? graph.FindNode(target.wire->toNodeId) : node;
            if (to) Curve(draw, output, Screen(InputPinScreenPos(*to, target.wire ? target.wire->toSocketId : target.socket)),
                Color(style.selected, .5f * alpha), 1.5f);
        }
        draw.AddRectFilled(Screen(low), Screen(high), Color(style.nodeSurface, .68f * alpha), 7.f);
        draw.AddRect(Screen(low), Screen(high), Color(style.selected, .64f * alpha), 7.f, 0, 1.5f);
        if (showInput) draw.AddCircleFilled(input, 3.f, Color(style.selected, alpha), 12);
        if (showOutput) draw.AddCircleFilled(output, 3.f, Color(style.selected, alpha), 12);
        const ImVec4 clip{low.x + 6, low.y, high.x - 6, high.y};
        draw.AddText(ImGui::GetFont(), ImGui::GetFontSize(), {low.x + 9, low.y + 8},
            Color(style.text, alpha), label, nullptr, 0, &clip);
    }
    if (closing) return;

    const float alpha = state.stage == Stage::Results ? fade(state.resultsStart, .12) : 1.f;
    constexpr std::size_t maxRows = 6;
    const auto count = std::min(maxRows, state.results.size());
    const auto first = state.result >= maxRows ? state.result - maxRows + 1 : 0;
    const float width = std::min(340.f, m_CanvasMax.x - m_CanvasMin.x - 20.f);
    const float height = 56.f + (state.stage == Stage::Results ? maxRows : 0) * 25.f;
    if (!state.pickerPlaced && ghostVisible) {
        state.pickerPosition = {state.ghostPosition.x + state.ghostBounds.min.x,
            state.ghostPosition.y + state.ghostBounds.max.y};
        state.pickerPlaced = true;
    }
    const auto home = state.pickerPlaced ? GraphToScreen(state.pickerPosition) : Vec2{pin.x, pin.y};
    const ImVec2 at{std::clamp(home.x, m_CanvasMin.x + 10.f,
            std::max(m_CanvasMin.x + 10.f, m_CanvasMax.x - width - 10.f)),
        std::clamp(home.y + 12.f, m_CanvasMin.y + 10.f,
            std::max(m_CanvasMin.y + 10.f, m_CanvasMax.y - height - 10.f))};
    panel(at, {width, height}, alpha);
    const ImVec4 clip{at.x + 9, at.y, at.x + width - 9, at.y + height};
    const auto text = [&](float y, ImVec4 color, const char* value) {
        draw.AddText(ImGui::GetFont(), ImGui::GetFontSize(), {at.x + 10, at.y + y}, Color(color, alpha), value, nullptr, 0, &clip);
    };
    text(7, style.text, state.stage == Stage::Choose ? "Choose a connection" :
        state.query.empty() ? "Type a node name" : state.query.c_str());
    const float selectedRow = float(state.result-first);
    if (state.rowTarget != selectedRow) {
        state.rowStart=state.rowCurrent;
        state.rowTarget=selectedRow;
        state.rowChanged=now;
    }
    const float slide=fade(state.rowChanged,.10);
    state.rowCurrent=state.rowStart+(state.rowTarget-state.rowStart)*slide*slide*(3.f-2.f*slide);
    if (count) {
        const float y=26.f+state.rowCurrent*25.f;
        draw.AddRectFilled({at.x+5,at.y+y-2},{at.x+width-5,at.y+y+21},Color(style.selected,.18f*alpha),4.f);
        draw.AddRectFilled({at.x+5,at.y+y+3},{at.x+7,at.y+y+16},Color(style.selected,.8f*alpha),1.f);
    }
    for (std::size_t i = first; i < std::min(first + count, state.results.size()); ++i) {
        const auto& row = state.results[i];
        const float y = 26.f + (i - first) * 25.f;
        draw.AddCircle({at.x+18,at.y+y+8},3.f,Color(row.error.empty()?style.selected:style.mutedText,alpha),12,1.f);
        draw.AddText(ImGui::GetFont(),ImGui::GetFontSize(),{at.x+30,at.y+y},
            Color(row.error.empty()?style.text:style.mutedText,alpha),entries[row.catalogIndex].label.c_str(),nullptr,0,&clip);
    }
    const char* footer = !state.roomError.empty() ? state.roomError.c_str() : !state.error.empty() ? state.error.c_str() :
        result && !result->error.empty() ? result->error.c_str() :
        state.stage == Stage::Choose ? "Arrows / Space to move   Enter to lock" :
        state.stage == Stage::Locked ? "Esc to choose another connection" :
        !result ? "No matching nodes" : "Enter to insert   Esc to go back";
    text(height - 20, style.mutedText, footer);
}
