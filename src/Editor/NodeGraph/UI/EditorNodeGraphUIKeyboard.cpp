#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "Editor/NodeGraph/UI/GraphKeyboardInsertion.h"
#include "Editor/EditorModule.h"
#include "Editor/Internal/GraphConnectionContext.h"
#include "Editor/Internal/GraphEditorCommands.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"
#include "Editor/NodeGraph/UI/NodeCatalogCommand.h"
#include "Editor/NodeGraph/UI/NodeNameSearch.h"
#include "Editor/NodeGraph/UI/GraphLayoutMotionTiming.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Utils/Utf8Text.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace {
using namespace EditorNodeGraph;
using State = Stack::Editor::GraphKeyboardInsertion;
using Stage = State::Stage;

constexpr double RoomDelay = .12, RoomTime = .26, PanTime = .30;

std::string EntryKey(const EditorNodeGraphDefinitions::NodeCatalogEntry& entry) {
    return std::to_string(int(entry.kind)) + ":" + std::to_string(entry.value);
}

float Progress(double elapsed, double duration) {
    const auto t = float(std::clamp(elapsed / duration, 0., 1.));
    return t * t * (3.f - 2.f * t);
}

Vec2 Blend(Vec2 a, Vec2 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

bool EditQuery(State& state, bool activation) {
    const auto& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) state.selectAll = true;
    bool changed = false;
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        if (state.selectAll) state.query.clear();
        else Stack::Utf8Text::RemoveLastCodePoint(state.query);
        state.selectAll = false;
        changed = true;
    }
    if (io.KeyCtrl || io.KeySuper || io.KeyAlt) return changed;
    for (const auto character : io.InputQueueCharacters) {
        const bool openingCharacter = (character == '/' || character == '@') &&
            (activation || (state.stage == Stage::Find && state.query.empty()));
        if (character < 32 || character == 127 || openingCharacter) continue;
        if (state.selectAll) state.query.clear();
        state.selectAll = false;
        char encoded[5];
        const int length = ImTextCharToUtf8(encoded, character);
        if (state.query.size() + length > 128) continue;
        state.query.append(encoded, length);
        changed = true;
    }
    return changed;
}
}

void EditorNodeGraphUI::ResetKeyboardInsertion() { m_KeyboardInsertion.reset(); }

void EditorNodeGraphUI::FindKeyboardTargets(const Graph& graph) {
    auto& state = *m_KeyboardInsertion;
    state.targets.clear();
    state.target = 0;
    const auto* node = graph.FindNode(state.anchor);
    if (!node) return;
    const auto add = [&](const SocketDefinition& port) {
        const auto begin = state.targets.size();
        const auto append = [&](const Link& wire) {
            if ((port.direction == SocketDirection::Output ? wire.fromSocketId : wire.toSocketId) == port.id)
                state.targets.push_back({node->id, port.id, port.direction, wire});
        };
        if (port.direction == SocketDirection::Output) graph.ForEachOutgoingLink(node->id, append);
        else graph.ForEachIncomingLink(node->id, append);
        if (state.targets.size() == begin) state.targets.push_back({node->id, port.id, port.direction, {}});
    };
    const auto& ports = PresentedSockets(graph, *node);
    for (auto side : {SocketDirection::Input, SocketDirection::Output})
        for (const auto& port : ports) if (port.direction == side) add(port);
    const auto primary = EditorNodeGraphDefinitions::DefaultOutputSocket(*node);
    const auto best = std::find_if(state.targets.begin(), state.targets.end(), [&](const auto& target) {
        return target.direction == SocketDirection::Output && target.socket == primary;
    });
    const auto firstOutput = std::find_if(state.targets.begin(), state.targets.end(), [](const auto& target) {
        return target.direction == SocketDirection::Output;
    });
    if (best != state.targets.end()) state.target = best - state.targets.begin();
    else if (firstOutput != state.targets.end()) state.target = firstOutput - state.targets.begin();
}

void EditorNodeGraphUI::BringKeyboardBoundsIntoView(const GraphRect& bounds) {
    auto& state = *m_KeyboardInsertion;
    const auto low = GraphToScreen(bounds.min), high = GraphToScreen(bounds.max);
    const auto shift = [](float lo, float hi, float first, float last) {
        const float margin = std::min(56.f, (last - first) * .15f);
        first += margin;
        last -= margin;
        if (hi - lo > last - first) return (first + last - lo - hi) * .5f;
        if (lo < first) return first - lo;
        return std::min(0.f, last - hi);
    };
    const Vec2 delta{shift(low.x, high.x, m_CanvasMin.x, m_CanvasMax.x),
        shift(low.y, high.y, m_CanvasMin.y, m_CanvasMax.y)};
    state.panStart = m_Pan;
    state.panTarget = {m_Pan.x + delta.x, m_Pan.y + delta.y};
    state.panStartTime = delta.x == 0 && delta.y == 0 ? -1 : ImGui::GetTime();
}

void EditorNodeGraphUI::RankKeyboardResults(EditorModule& editor, const Graph& graph) {
    auto& state = *m_KeyboardInsertion;
    state.results.clear();
    state.result = 0;
    state.error.clear();
    const Stack::Editor::NodeNameSearch query(state.query);
    if (query.joined.empty()) return;
    const auto published = Stack::Editor::PublishedConnectionContext(editor);
    const auto& entries = Stack::Editor::RegisteredNodeCatalog();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        const int rank = Stack::Editor::RankNodeName(query, entry.label, entry.searchAliases, entry.category);
        if (rank < 0) continue;
        auto found = state.compatibility.find(i);
        if (found == state.compatibility.end()) {
            State::Result result{i, rank};
            if (!state.available.count(EntryKey(entry))) result.error = "Unavailable in this graph.";
            else if (const auto insertion = Stack::GraphModel::PrepareCatalogInsertion(graph, state.targets[state.target],
                    entry, {}, result.error, &published)) {
                const auto layout = BuildLogicalNodeLayout(insertion->graph, *insertion->graph.FindNode(insertion->nodeId));
                const auto& bounds = layout.bounds.persistentVisual;
                result.bounds = {bounds.min, bounds.max};
                for (const auto& socket : layout.sockets) {
                    if (socket.direction == SocketDirection::Input && socket.socketId == insertion->input)
                        result.input = socket.center;
                    if (socket.direction == SocketDirection::Output && socket.socketId == insertion->output)
                        result.output = socket.center;
                }
            }
            found = state.compatibility.emplace(i, std::move(result)).first;
        }
        auto result = found->second;
        result.rank = rank;
        state.results.push_back(std::move(result));
    }
    std::stable_sort(state.results.begin(), state.results.end(), [](const auto& a, const auto& b) {
        return std::pair{!a.error.empty(), a.rank} < std::pair{!b.error.empty(), b.rank};
    });
}

bool EditorNodeGraphUI::UpdateKeyboardInsertion(EditorModule& editor, Graph& graph) {
    const auto& io = ImGui::GetIO();
    if (m_KeyboardInsertion && (m_KeyboardInsertion->graph != &graph ||
            m_KeyboardInsertion->lastFrame < ImGui::GetFrameCount() - 1 ||
            m_KeyboardInsertion->revision != graph.GetStructureRevision() ||
            m_KeyboardInsertion->documentRevision != m_GraphContext.documentRevision))
        m_KeyboardInsertion.reset();
    const bool activation = !io.KeyCtrl && !io.KeySuper && !io.KeyAlt &&
        ((!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Slash, false)) ||
            (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_2, false)) ||
            std::any_of(io.InputQueueCharacters.begin(), io.InputQueueCharacters.end(),
                [](auto character) { return character == '@' || character == '/'; }));
    if (!m_KeyboardInsertion && !activation) return false;
    if (!editor.CanConsumeEditorCommandKeys() || io.AppFocusLost ||
        (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) ||
        !ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsAnyMouseDown() ||
        ImGui::IsAnyItemActive() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) ||
        m_DragNodeId > 0 || m_DragInputNodeId > 0 || m_DragOutputNodeId > 0 ||
        m_MiddlePanCaptureActive || io.MouseWheel != 0 ||
        (io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Z) || ImGui::IsKeyPressed(ImGuiKey_Y)))) {
        m_KeyboardInsertion.reset();
        return false;
    }
    const auto now = ImGui::GetTime();

    const bool opening = !m_KeyboardInsertion || (activation &&
        (m_KeyboardInsertion->stage == Stage::Closing || m_KeyboardInsertion->stage == Stage::Inserted));
    if (opening) {
        m_KeyboardInsertion = std::make_unique<State>();
        m_KeyboardInsertion->graph = &graph;
        m_KeyboardInsertion->revision = graph.GetStructureRevision();
        m_KeyboardInsertion->documentRevision = m_GraphContext.documentRevision;
        m_KeyboardInsertion->opened = now;
        m_SmoothZoomActive = false;
        editor.CancelGraphAutoFocusTracking();
    }
    auto& state = *m_KeyboardInsertion;
    state.lastFrame = ImGui::GetFrameCount();
    const bool reduce = Stack::Editor::NodeGraphUIVisuals::GraphReducedMotion();
    if (state.panStartTime >= 0) {
        const float t = reduce ? 1.f : Progress(now - state.panStartTime, PanTime);
        m_Pan = Blend(state.panStart, state.panTarget, t);
        if (t == 1) state.panStartTime = -1;
    }
    if (state.roomMoving) {
        const auto settle = [&](double delay) { return reduce ? 1.f :
            Stack::Editor::GraphLayoutMotionTiming::ElasticProgress(now - state.roomStart - delay, RoomTime - delay); };
        const float room = settle(0);
        const auto elasticBlend = [](Vec2 from, Vec2 to, float t) {
            // Limit the elastic tail to two canvas units even after large moves.
            const float distance = std::max(std::abs(to.x-from.x), std::abs(to.y-from.y));
            return Blend(from, to, std::min(t, 1.f + 2.f / std::max(1.f, distance)));
        };
        for (auto& [id, motion] : state.nodes) motion.current = elasticBlend(motion.start, motion.target, settle(motion.delay));
        for (auto& [id, motion] : state.groups)
            motion.current = {elasticBlend(motion.start.min, motion.target.min, room), elasticBlend(motion.start.max, motion.target.max, room)};
        const auto& pos = state.ghostPosition;
        const auto& bounds = state.ghostVisual;
        const float shape = reduce ? 1.f : Progress(now-state.roomStart, .18);
        state.ghostCurrent = {
            Blend(state.ghostStart.min, {pos.x + bounds.min.x, pos.y + bounds.min.y}, shape),
            Blend(state.ghostStart.max, {pos.x + bounds.max.x, pos.y + bounds.max.y}, shape)};
        if (reduce || now-state.roomStart >= RoomTime) state.roomMoving = false;
    }
    if (state.stage == Stage::Closing || state.stage == Stage::Inserted) {
        if (reduce || now - state.roomStart >= RoomTime)
            m_KeyboardInsertion.reset();
        return false;
    }

    const auto pressed = [](ImGuiKey key, bool repeat = false) {
        if (!ImGui::IsKeyPressed(key, repeat)) return false;
        ImGui::SetKeyOwner(key, ImGui::GetID("##graphKeyboard"), ImGuiInputFlags_LockThisFrame);
        // ImGui may have queued activation for a previously focused node
        // widget before this canvas sees the key.
        if (key == ImGuiKey_Enter || key == ImGuiKey_KeypadEnter || key == ImGuiKey_Space) {
            auto& context = *ImGui::GetCurrentContext();
            context.NavActivateId = context.NavActivateDownId = context.NavActivatePressedId = 0;
        }
        if (key == ImGuiKey_LeftArrow || key == ImGuiKey_RightArrow ||
            key == ImGuiKey_UpArrow || key == ImGuiKey_DownArrow) ImGui::NavMoveRequestCancel();
        return true;
    };
    if (pressed(ImGuiKey_Escape)) {
        state.error.clear();
        if (state.stage == Stage::Results) {
            state.stage = Stage::Locked;
            state.query.clear();
            state.selectAll = false;
            state.results.clear();
        } else if (state.stage == Stage::Locked) state.stage = Stage::Choose;
        else if (state.stage == Stage::Choose) {
            state.stage = Stage::Closing;
            state.panStartTime = -1;
            state.placement = {};
            SetKeyboardRoomTargets(graph);
        } else m_KeyboardInsertion.reset();
        return true;
    }
    const bool enter = pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter);
    if (state.stage != Stage::Choose) pressed(ImGuiKey_Space);
    if (state.stage == Stage::Find) {
        if (EditQuery(state, opening)) {
            const Stack::Editor::NodeNameSearch query(state.query);
            int bestRank = 6, best = 0;
            if (!query.joined.empty()) for (const auto& node : graph.GetNodes()) {
                const auto rank = Stack::Editor::RankNodeName(query, node.title);
                if (rank >= 0 && rank < bestRank) { best = node.id; bestRank = rank; }
            }
            if (best != state.matched) {
                state.previousMatch = state.matched;
                state.matchChanged = now;
                state.matched = best;
                if (const auto* node = graph.FindNode(best)) {
                    const auto bounds = NodeGraphBounds(graph, *node);
                    BringKeyboardBoundsIntoView({bounds.min, bounds.max});
                } else state.panStartTime = -1;
            }
        }
        if (enter && state.matched && m_GraphContext.selectNode) {
            state.anchor = state.matched;
            m_GraphContext.selectNode(state.anchor);
            state.stage = Stage::Choose;
            state.query.clear();
            state.selectAll = false;
            state.roomAfter = now + RoomDelay;
            FindKeyboardTargets(graph);
        }
        return true;
    }
    if (state.targets.empty()) return true;
    if (state.stage == Stage::Choose) {
        const auto previous = state.target;
        if (pressed(ImGuiKey_Space, true)) state.target = (state.target + 1) % state.targets.size();
        const bool left = pressed(ImGuiKey_LeftArrow, true), right = pressed(ImGuiKey_RightArrow, true);
        if (left || right) {
            const auto side = left ? SocketDirection::Input : SocketDirection::Output;
            if (side != state.targets[state.target].direction) {
                const auto found = std::find_if(state.targets.begin(), state.targets.end(),
                    [&](const auto& item) { return item.direction == side; });
                if (found != state.targets.end()) state.target = found - state.targets.begin();
            }
        }
        const bool up = pressed(ImGuiKey_UpArrow, true), down = pressed(ImGuiKey_DownArrow, true);
        if (up || down) {
            const auto& current = state.targets[state.target];
            for (int step = int(state.target) + (up ? -1 : 1); step >= 0 && step < int(state.targets.size()); step += up ? -1 : 1) {
                if (state.targets[step].direction != current.direction) break;
                if (state.targets[step].socket != current.socket) {
                    while (step > 0 && state.targets[step - 1].socket == state.targets[step].socket &&
                        state.targets[step - 1].direction == current.direction) --step;
                    state.target = std::size_t(step);
                    break;
                }
            }
        }
        if (previous != state.target) {
            state.pickerPlaced = false;
            state.previewCatalog = ~std::size_t{};
            state.prepared = false;
            state.roomAfter = now + RoomDelay;
            state.compatibility.clear();
            state.roomError.clear();
        }
        if (enter) {
            state.stage = Stage::Locked;
            state.available.clear();
            for (const auto& entry : m_GraphContext.queryAvailableNodes(std::nullopt))
                state.available.insert(EntryKey(entry));
        }
        if (!state.prepared && (enter || now >= state.roomAfter)) PrepareKeyboardRoom(graph);
        if (enter && state.prepared && state.roomError.empty()) {
            const auto pos = state.ghostPosition;
            const auto bounds = state.ghostBounds;
            BringKeyboardBoundsIntoView({{pos.x + bounds.min.x, pos.y + bounds.min.y}, {pos.x + bounds.max.x, pos.y + bounds.max.y}});
        }
        return true;
    }
    if (EditQuery(state, false)) {
        if (state.stage != Stage::Results) state.resultsStart = now;
        state.stage = Stage::Results;
        RankKeyboardResults(editor, graph);
    }
    if (state.stage == Stage::Results && !state.results.empty()) {
        const auto old = state.result;
        if (pressed(ImGuiKey_UpArrow, true) && state.result > 0) --state.result;
        if (pressed(ImGuiKey_DownArrow, true) && state.result + 1 < state.results.size()) ++state.result;
        if (state.result != old) state.error.clear();
        const auto& result = state.results[state.result];
        if (result.error.empty() && state.roomError.empty() && state.previewCatalog != result.catalogIndex) {
            PrepareKeyboardRoom(graph);
            if (state.roomError.empty()) {
                const auto pos = state.ghostPosition;
                const auto bounds = state.ghostBounds;
                BringKeyboardBoundsIntoView({{pos.x + bounds.min.x, pos.y + bounds.min.y},
                    {std::max(pos.x + bounds.max.x, state.pickerPosition.x + 340.f/m_Zoom),
                        std::max(pos.y + bounds.max.y, state.pickerPosition.y + 218.f/m_Zoom)}});
            }
        }
        if (enter) {
            if (result.error.empty()) state.commit = true;
            else state.error = result.error;
        }
    }
    return true;
}
