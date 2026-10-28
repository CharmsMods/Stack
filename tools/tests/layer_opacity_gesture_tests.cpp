#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <string>
#include <vector>
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
// Production lookup and footer draw are appended by the generator. The model
// stores only fields required by those blocks; no routing/history is simulated.
inline constexpr const char* kRawBackgroundId = "background";
struct RawAdjustmentLayer {
    std::string id, name;
    bool enabled = true;
    int layerMask = 0;
};
struct RawLayerStackState {
    RawAdjustmentLayer background{kRawBackgroundId,"Background"};
    std::vector<RawAdjustmentLayer> layers{{"layer-a","Adjustment"}};
};
RawAdjustmentLayer* FindRawAdjustmentLayer(RawLayerStackState&, const std::string&);
struct FooterFixture {
    struct Document {
        RawLayerStackState state;
        RawLayerStackState& State() { return state; }
    };
    struct Project { Document rawLayers; } project;
    Project* m_Project = &project;
    std::string m_SelectedRawAdjustmentLayer;
    struct Row { std::string id, name; bool enabled; int mask; bool toolMasks; };
    void OpenRawLayerMaskGraph(const std::string&) {}
    void LabTooltip(const char*) {}
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
        FooterFixture footer;
        const auto footerFrame = [&](const std::string& id) {
            footer.m_SelectedRawAdjustmentLayer=id;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0,0});
            ImGui::SetNextWindowSize({500,300});
            ImGui::Begin("Footer",nullptr,ImGuiWindowFlags_NoDecoration);
            footer.Draw();
            const ImGuiID last=GImGui->LastItemData.ID;
            ImGui::End();
            ImGui::Render();
            return last;
        };
        footerFrame(""); // Let the new window establish layout.
        require(footerFrame("")==0,
            "Canonical Background selection must show explanation, not Graph/actions buttons");
        require(footerFrame(kRawBackgroundId)==0,
            "Explicit Background identity must not expose adjustment footer actions");
        require(footerFrame("layer-a")!=0,
            "Adjustment selection must retain its Graph/actions footer");
        require(footerFrame("missing")==0,
            "Missing adjustment must not expose footer actions");
        std::cout << "PASS layer footer: canonical/explicit Background protected; adjustment actions retained\n";
        std::cout << "PASS layer opacity: unchanged Enter/Escape close; normal drag remains one gesture\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        ImGui::DestroyContext();
        return 1;
    }
    ImGui::DestroyContext();
}
