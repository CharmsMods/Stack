#include "Utils/GraphNumericControls.h"
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {
template<class T>
void DisabledTransitions() {
    const auto require = [](bool ok, const char* message) {
        if (!ok) throw std::runtime_error(message);
    };
    auto& io = ImGui::GetIO();
    T value = 1;
    bool disabled = false, cursorCaptured = false;
    // GraphNumericCapture is a function pointer, matching the production
    // per-item capture contract rather than inspecting private NumericState.
    static bool itemActive = false, itemDeactivated = false;
    const auto capture = [](bool, bool) {
        itemActive = ImGui::IsItemActive();
        itemDeactivated = ImGui::IsItemDeactivated();
    };
    int owner = 0;
    ImGuiExtras::GraphNodeControlScopeConfig config;
    config.scale = config.interactionScale = 1;
    config.dragReferenceWidth = 200;
    config.useScrubHandles = config.allowSliderTextEntry = true;
    const auto frame = [&] {
        ImGui::NewFrame();
        ImGuiExtras::BeginFrameInputRouting();
        ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Numeric edit authority", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PushID(std::is_integral_v<T> ? "int" : "float");
        bool changed = false;
        {
            ImGuiExtras::BeginGraphWheelFrame(&owner, true);
            ImGuiExtras::GraphNumericNodeScope scope(&owner, 7, false, false, nullptr, nullptr, "document", "node");
            ImGui::SetCursorPos({40, 50}); ImGui::BeginDisabled(disabled);
            if constexpr (std::is_integral_v<T>)
                changed = ImGuiExtras::GraphNumericInt("Value", "value", &value, -16, 16,
                    "%d", 300, config, 0., capture);
            else
                changed = ImGuiExtras::GraphNumericFloat("Value", "value", &value, -16.f, 16.f,
                    "%.3f", 300, config, 0., capture);
            ImGui::EndDisabled();
        }
        cursorCaptured = ImGuiExtras::ConsumeCursorCaptureRequest(nullptr);
        ImGui::PopID(); ImGui::End(); ImGui::Render();
        return changed;
    };
    const auto click = [&](int button, float x = 335.f) {
        io.AddMousePosEvent(x, 60); frame();
        io.AddMouseButtonEvent(button, true); frame();
        io.AddMouseButtonEvent(button, false); frame(); frame();
    };
    const auto type = [&](const char* text) {
        io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddKeyEvent(ImGuiKey_A, true); frame();
        io.AddKeyEvent(ImGuiKey_A, false); io.AddKeyEvent(ImGuiMod_Ctrl, false); frame();
        io.AddInputCharactersUTF8(text); frame(); frame();
    };
    const auto key = [&](ImGuiKey key) {
        io.AddKeyEvent(key, true); frame(); io.AddKeyEvent(key, false); frame();
    };
    io.AddMousePosEvent(-100, -100); frame(); frame();
    click(ImGuiMouseButton_Right); type("9");
    require(value == 1 && ImGui::GetActiveID() != 0, "Fixture failed to retain active numeric draft");
    disabled = true;
    require(!frame() && value == 1, "Disabled numeric input committed its pending draft");
    require(!itemActive && itemDeactivated, "Disabled numeric input did not expose its native deactivation");
    value = 3; frame(); // A new driven/current value must survive reentry.
    disabled = false; frame(); frame();
    require(value == 3 && ImGui::GetActiveID() == 0,
        "Reenabled numeric input revived or focused an abandoned draft");
    click(ImGuiMouseButton_Left); type("4"); key(ImGuiKey_Enter);
    require(value == 4, "Reenabled numeric input failed to commit intentional Enter edit");
    click(ImGuiMouseButton_Left); type("7"); click(ImGuiMouseButton_Left, 500);
    require(value == 7, "Ordinary numeric focus loss no longer commits");
    click(ImGuiMouseButton_Left); type("9"); key(ImGuiKey_Escape);
    require(value == 7, "Escape committed a numeric draft");
    click(ImGuiMouseButton_Right); // Return to dragging.
    io.AddMousePosEvent(335, 60); frame(); io.AddMouseButtonEvent(0, true); frame();
    io.AddMousePosEvent(345, 60); frame();
    require(value != 7 && itemActive && cursorCaptured, "Fixture failed to start numeric drag");
    disabled = true; value = 11; io.AddKeyEvent(ImGuiKey_Escape, true);
    require(!frame() && value == 11, "Disabled numeric drag restored an obsolete value on Escape");
    require(!itemActive && itemDeactivated && !cursorCaptured,
        "Disabled numeric drag failed to end its input or cursor capture");
    io.AddKeyEvent(ImGuiKey_Escape, false); io.AddMouseButtonEvent(0, false); frame();
    disabled = false; frame();
    require(value == 11, "Reenabled numeric drag replayed a stale edit");
}
}

void ValidateGraphNumericDisabledTransitions() {
    auto* previous = ImGui::GetCurrentContext();
    auto* context = ImGui::CreateContext();
    struct Cleanup {
        ImGuiContext* owned;
        ImGuiContext* previous;
        ~Cleanup() { ImGui::DestroyContext(owned); ImGui::SetCurrentContext(previous); }
    } cleanup{context, previous};
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.DisplaySize = {600, 360}; io.DeltaTime = 1.f / 60.f;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    DisabledTransitions<float>(); DisabledTransitions<int>();
    std::cout << "Graph numeric disabled transitions passed\n";
}
