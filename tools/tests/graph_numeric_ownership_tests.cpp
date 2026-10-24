#include "Utils/GraphNumericControls.h"
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>
#include <string>

void ValidateGraphNumericOwnership() {
    const auto require = [](bool ok, const char* message) {
        if (!ok) throw std::runtime_error(message);
    };
    auto* previous = ImGui::GetCurrentContext();
    auto* context = ImGui::CreateContext();
    struct Cleanup {
        ImGuiContext* owned;
        ImGuiContext* previous;
        ~Cleanup() { ImGui::DestroyContext(owned); ImGui::SetCurrentContext(previous); }
    } cleanup{context, previous};
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {600, 360};
    io.DeltaTime = 1.f / 60.f;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    // Independent live tabs may share saved document IDs. Replacement within
    // the same tab may instead reuse graph addresses and integer node IDs.
    for (int scenario = 0; scenario < 3; ++scenario) {
        int graphOwners[2]{};
        float values[2]{1, 2};
        int integers[2]{1, 2};
        int selected = 0;
        bool clearInput = false, disabled = false;
        ImGuiExtras::GraphNodeControlScopeConfig config;
        config.scale = config.interactionScale = 1;
        config.dragReferenceWidth = 200;
        config.useScrubHandles = config.allowSliderTextEntry = true;
        const auto frame = [&] {
            ImGui::NewFrame();
            if (clearInput) { ImGui::ClearActiveID(); clearInput = false; }
            ImGuiExtras::BeginFrameInputRouting();
            ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Numeric ownership", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
            ImGui::PushID(scenario);
            ImGui::PushID(7); // Both documents intentionally reuse this node ID.
            {
                const auto* graph = &graphOwners[scenario == 0 ? selected : 0];
                const char* document = scenario == 1 && selected ? "replacement" : "document";
                const char* node = scenario == 2 && selected ? "replacement-node" : "node";
                ImGuiExtras::BeginGraphWheelFrame(graph, true);
                ImGuiExtras::GraphNumericNodeScope owner(graph, 7, false, false, nullptr, nullptr, document, node);
                ImGui::SetCursorPos({40, 50});
                ImGui::BeginDisabled(disabled);
                if (scenario == 2)
                    ImGuiExtras::GraphNumericInt("Value", "value", &integers[selected], -16, 16,
                        "%d", 300, config, 0., [](bool, bool) {});
                else
                    ImGuiExtras::GraphNumericFloat("Value", "value", &values[selected], -16, 16,
                        "%.3f", 300, config, 0., [](bool, bool) {});
                ImGui::EndDisabled();
            }
            ImGui::PopID(); ImGui::PopID();
            ImGui::End(); ImGui::Render();
        };
        const auto click = [&](int button) {
            io.AddMousePosEvent(335, 60); frame();
            io.AddMouseButtonEvent(button, true); frame();
            io.AddMouseButtonEvent(button, false); frame(); frame();
        };
        const auto type = [&](const char* text) {
            io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddKeyEvent(ImGuiKey_A, true); frame();
            io.AddKeyEvent(ImGuiKey_A, false); io.AddKeyEvent(ImGuiMod_Ctrl, false); frame();
            io.AddInputCharactersUTF8(text); frame(); frame();
        };
        io.AddMousePosEvent(-100, -100); frame(); frame();
        click(ImGuiMouseButton_Right);
        type("9");
        require(ImGui::GetActiveID() != 0 && values[0] == 1 && integers[0] == 1,
            "Numeric fixture failed to keep an uncommitted text draft.");
        // Middle-clicking another dirty project pill switches before its next
        // canvas frame, without a left-click deactivation in the old canvas.
        io.AddMousePosEvent(500, 300);
        io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true); frame();
        selected = 1; clearInput = true; disabled = scenario == 0;
        frame();
        require(values[1] == 2 && integers[1] == 2,
            "A project/node switch committed another owner's numeric draft.");
        io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
        disabled = false; frame();
        selected = 0; clearInput = true; frame(); frame();
        require(values[0] == 1 && integers[0] == 1,
            "Returning to a project revived its abandoned numeric draft.");
        click(ImGuiMouseButton_Left);
        type("4");
        io.AddKeyEvent(ImGuiKey_Enter, true); frame();
        io.AddKeyEvent(ImGuiKey_Enter, false); frame();
        require((scenario == 2 ? integers[0] : values[0]) == 4 &&
            values[1] == 2 && integers[1] == 2,
            "An intentional same-owner numeric edit failed after switching.");
        ImGui::ClearActiveID();
    }
    std::cout << "Graph numeric ownership interactions passed\n";
}
