#include <imgui.h>
#include <iostream>
#include <stdexcept>
#include <utility>

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
// Narrow double: test UI calls/gesture boundaries, not layer validation or undo.
struct Layer { float opacity = 1.f; };
struct GestureProbe {
    Layer layer;
    bool active = false;
    int starts = 0, finishes = 0, edits = 0;
    void BeginGesture() { if (!active) ++starts; active = true; }
    void EndGesture() { if (active) ++finishes; active = false; }
    Layer State() const { return layer; }
};
Layer* FindRawAdjustmentLayer(Layer& layer, int) { return &layer; }
struct Fixture {
    struct Project { GestureProbe rawLayers; } project;
    Project* m_Project = &project;
    int m_SelectedRawAdjustmentLayer = 0;
    void ApplyRawLayerStackEdit(Layer candidate) {
        require(project.rawLayers.active, "Opacity edit escaped its gesture");
        project.rawLayers.layer = candidate;
        ++project.rawLayers.edits;
    }
    void Draw();
};
// Generator appends Fixture::Draw using the unchanged production opacity block.
int main() {
    ImGui::CreateContext();
    try {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.DisplaySize = {500, 300};
        io.DeltaTime = 1.f / 60;
        io.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        Fixture fixture;
        auto& gesture = fixture.project.rawLayers;
        ImVec2 min{}, max{};
        auto frame = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({500, 300});
            ImGui::Begin("Layers", nullptr, ImGuiWindowFlags_NoDecoration);
            fixture.Draw();
            min = ImGui::GetItemRectMin();
            max = ImGui::GetItemRectMax();
            ImGui::End();
            ImGui::Render();
        };
        frame(); frame();
        for (int cycle = 0; cycle < 2; ++cycle) {
            const auto finishKey = cycle == 0 ? ImGuiKey_Enter : ImGuiKey_Escape;
            io.AddKeyEvent(ImGuiMod_Ctrl, true);
            io.AddMousePosEvent((min.x + max.x) * .5f, (min.y + max.y) * .5f);
            io.AddMouseButtonEvent(0, true);
            frame();
            require(gesture.active && gesture.starts == cycle + 1, "Exact-value activation must begin gesture");
            io.AddMouseButtonEvent(0, false);
            io.AddKeyEvent(ImGuiMod_Ctrl, false);
            frame();
            io.AddKeyEvent(finishKey, true);
            frame();
            require(!gesture.active && gesture.finishes == cycle + 1 && gesture.edits == 0,
                "Unchanged exact-value deactivation must close gesture without edits");
            io.AddKeyEvent(finishKey, false);
            frame();
        }

        io.AddMousePosEvent(min.x + 70, (min.y + max.y) * .5f);
        io.AddMouseButtonEvent(0, true);
        frame();
        require(gesture.active && gesture.starts == 3 && gesture.edits > 0,
            "Normal opacity drag must begin an editing gesture");
        io.AddMousePosEvent(min.x + 140, (min.y + max.y) * .5f);
        frame();
        require(gesture.active && gesture.starts == 3 && gesture.finishes == 2,
            "Moving opacity must retain the same gesture");
        io.AddMouseButtonEvent(0, false);
        frame();
        require(!gesture.active && gesture.finishes == 3,
            "Edited opacity release must close gesture");
        std::cout << "PASS layer opacity: unchanged Enter/Escape close; normal drag remains one gesture\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        ImGui::DestroyContext();
        return 1;
    }
    ImGui::DestroyContext();
}
