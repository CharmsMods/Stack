// Actual map editor, helpers and recipe math with native ImGui input; no GL.
#include "Editor/Internal/RawLab/RawDenoiseMapEditor.h"
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>

namespace {
using namespace Stack::Editor::RawLabInternal;
using namespace Stack::RawRecipe;
void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
struct Fixture {
    RawRgbDenoiseRecipe recipe;
    RawDenoiseMapEditorState state;
    RawDenoiseMapEditorResult result;
    ImRect graph;
    ImGuiID mapId = 0;
    bool disabled = false, visible = true, openModal = false;
    Fixture() {
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr; io.LogFilename = nullptr;
        io.DisplaySize = {800, 900}; io.DeltaTime = 1.f / 60;
        unsigned char* pixels; int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        RawDenoiseControlPoint point;
        point.id = 1; point.linkGroup = 7;
        recipe.lumaMap.points.push_back(point);
        point.id = 2; recipe.chromaMap.points.push_back(point);
        point.id = 3; point.linkGroup = 0; point.frequency = .2f;
        recipe.lumaMap.points.push_back(point);
        recipe.nextControlPointId = 8;
        Frame(); Frame();
    }
    ~Fixture() { ImGui::DestroyContext(); }
    void Frame() {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize({500, 850});
        ImGui::Begin("Map", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        result = {};
        mapId = ImGui::GetID("##RawDenoiseFrequencyLuminanceMap");
        if (visible) {
            ImGui::BeginDisabled(disabled);
            result = RenderRawDenoiseMapEditor(recipe, state);
            ImGui::EndDisabled();
            // Recover the actual plotted geometry, without private state hooks.
            graph = ImRect({FLT_MAX, FLT_MAX}, {-FLT_MAX, -FLT_MAX});
            for (const auto& vertex : ImGui::GetWindowDrawList()->VtxBuffer)
                if (vertex.col == IM_COL32(14, 17, 20, 255)) graph.Add(vertex.pos);
        } else {
            ImGui::SetCursorPos({8, 40}); ImGui::Button("Other", {484, 280});
        }
        if (openModal) { ImGui::OpenPopup("Takeover"); openModal = false; }
        if (ImGui::BeginPopupModal("Takeover")) {
            ImGui::TextUnformatted("Another surface owns input"); ImGui::EndPopup();
        }
        ImGui::End(); ImGui::Render();
    }
    ImVec2 At(float frequency, float ev) const {
        return {graph.Min.x + frequency * graph.GetWidth(), graph.Max.y -
            (ev - recipe.lumaMap.minimumEv) / (recipe.lumaMap.maximumEv - recipe.lumaMap.minimumEv) * graph.GetHeight()};
    }
    void Move(ImVec2 point) { ImGui::GetIO().AddMousePosEvent(point.x, point.y); Frame(); }
    void Press(ImVec2 point, int button = 0) { Move(point); ImGui::GetIO().AddMouseButtonEvent(button, true); Frame(); }
    void Release(int button = 0) { ImGui::GetIO().AddMouseButtonEvent(button, false); Frame(); }
    void Click(ImVec2 point, int button = 0) { Press(point, button); Release(button); }
    bool Active() const { return ImGui::GetActiveID() == mapId; }
    nlohmann::json Snapshot() const {
        return {SerializeRawDenoiseControlMap(recipe.lumaMap),
            SerializeRawDenoiseControlMap(recipe.chromaMap), recipe.nextControlPointId};
    }
    void Grab() {
        Press(At(.5f, -2));
        Require(state.draggingPoint && Active(), "Point did not acquire native ownership");
        Move(At(.6f, -1));
        Require(result.recipeChanged && state.pointDragDiagnosticActive,
            "Owned drag did not edit or enable its diagnostic");
        Require(recipe.lumaMap.points[0].frequency == recipe.chromaMap.points[0].frequency &&
            recipe.lumaMap.points[0].sceneEv == recipe.chromaMap.points[0].sceneEv &&
            recipe.lumaMap.points[1].frequency == .2f, "Owned drag broke linked/unrelated points");
    }
    void Stopped(const nlohmann::json& before) {
        Require(Snapshot() == before && !result.recipeChanged,
            "Lost ownership changed authored or linked map data");
        Require(!state.draggingPoint && !state.pointDragDiagnosticActive,
            "Lost ownership retained point drag or transient diagnostic");
    }
};
void Disabled(bool outside) {
    Fixture f; f.Grab(); const auto before = f.Snapshot();
    f.disabled = true; f.Move(f.At(outside ? 1.2f : .8f, 1));
    f.Stopped(before);
    Require(f.result.viewChanged && !f.Active(), "Disabled drag did not end native/diagnostic ownership");
    f.Frame(); Require(!f.result.viewChanged, "Diagnostic closure repeated on idle disabled frame");
    f.disabled = false; f.Move(f.At(.3f, 0)); f.Stopped(before);
    f.Release();
    f.Press(f.At(.6f, -1)); f.Move(f.At(.7f, 0));
    Require(f.result.recipeChanged && f.recipe.lumaMap.points[0].frequency > .69f,
        "Fresh gesture failed after reenabling");
}
void LostOwner() {
    Fixture f; f.Grab(); const auto before = f.Snapshot();
    ImGui::ClearActiveID(); f.Move(f.At(.8f, 1)); f.Stopped(before);
    Require(f.result.viewChanged, "Lost ID did not close transient diagnostic");
}
void ModalTakeover() {
    Fixture f; f.Grab(); const auto before = f.Snapshot();
    f.openModal = true; f.Frame(); f.Move(f.At(.8f, 1)); f.Stopped(before);
    Require(f.result.viewChanged, "Modal takeover did not close transient diagnostic");
}
void AddDelete(bool disabled) {
    Fixture f; f.disabled = disabled; f.state.linkNewPoints = true;
    const auto before = f.Snapshot(); const auto position = f.At(.85f, 3);
    f.Click(position); f.Click(position);
    if (disabled) {
        f.Stopped(before);
        f.Click(f.At(.5f, -2), 1); f.Stopped(before);
        return;
    }
    Require(f.recipe.lumaMap.points.size() == 3 && f.recipe.chromaMap.points.size() == 2,
        "Double-click failed to add a linked point pair");
    Require(f.recipe.lumaMap.points.back().linkGroup != 0 &&
        f.recipe.lumaMap.points.back().linkGroup == f.recipe.chromaMap.points.back().linkGroup,
        "New map points lost their link identity");
    f.Click(position, 1);
    const auto after = f.Snapshot();
    Require(after[0] == before[0] && after[1] == before[1],
        "Right-click failed to delete only the added linked pair");
}
void FocusLost() {
    Fixture f; f.Grab(); const auto before = f.Snapshot();
    ImGui::GetIO().AddFocusEvent(false); f.Frame(); f.Stopped(before);
    Require(f.result.viewChanged, "Focus loss did not close transient diagnostic");
}
void Hidden(bool sameMap) {
    Fixture f; f.Grab(); const auto before = f.Snapshot();
    f.visible = false; f.Release(); f.Frame();
    if (sameMap) {
        f.visible = true;
        const auto blank = f.At(.85f, 3);
        ImGui::GetIO().AddMousePosEvent(blank.x, blank.y);
        ImGui::GetIO().AddMouseButtonEvent(0, true); f.Frame();
        f.Stopped(before); // Fresh same-ID activation must not inherit the old point.
    } else {
        f.Press({200, 100});
        const auto owner = ImGui::GetActiveID();
        Require(owner != 0 && owner != f.mapId, "Other control did not acquire ownership");
        f.visible = true; f.Frame(); f.Stopped(before);
        Require(ImGui::GetActiveID() == owner, "Map stole another control's ownership");
    }
}
void OrdinaryRelease() {
    Fixture f; f.Grab(); const auto before = f.Snapshot(); f.Release();
    f.Stopped(before); Require(f.result.viewChanged, "Release did not restore selected diagnostic mode");
}
}
int main() {
    int failed = 0;
    const auto run = [&](const char* name, auto test) {
        try { test(); } catch (const std::exception& error) {
            ++failed; std::cerr << name << ": " << error.what() << '\n';
        }
    };
    run("disabled inside", [] { Disabled(false); });
    run("disabled outside", [] { Disabled(true); });
    run("lost owner", LostOwner); run("focus loss", FocusLost);
    run("modal takeover", ModalTakeover);
    run("linked add/delete", [] { AddDelete(false); });
    run("disabled add/delete", [] { AddDelete(true); });
    run("hidden other", [] { Hidden(false); });
    run("hidden same map", [] { Hidden(true); });
    run("ordinary release", OrdinaryRelease);
    if (!failed) std::cout << "Denoise map native ownership passed\n";
    return failed ? 1 : 0;
}
