#include <imgui.h>
#include <iostream>
#include <stdexcept>

// The generator prepends the actual production shortcut condition.
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main() {
    ImGui::CreateContext();
    try {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.DisplaySize = {600, 400};
        io.DeltaTime = 1.f / 60;
        // Deliberately test an activation and key delivered within one frame.
        io.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        double numeric = 123;
        bool directInput = false, focusInput = false, openPopup = false;
        bool deletesPin = false, text = false, active = false;
        ImVec2 inputCenter{};
        auto frame = [&] {
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({600, 400});
            ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_NoDecoration);
            ImGui::BeginChild("Exposure", {550, 120});
            if (directInput) {
                ImGui::InputScalar("Direct exact", ImGuiDataType_Double, &numeric);
                auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                inputCenter = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
            }
            if (openPopup) ImGui::OpenPopup("Exact value");
            ImGui::SetNextWindowPos({40, 40});
            if (ImGui::BeginPopup("Exact value")) {
                if (focusInput) ImGui::SetKeyboardFocusHere();
                ImGui::InputScalar("##exact", ImGuiDataType_Double, &numeric);
                ImGui::EndPopup();
            }
            ImGui::EndChild();
            ImGui::BeginChild("Color Warp", {550, 200});
            text = io.WantTextInput;
            active = ImGui::IsAnyItemActive();
            deletesPin = DeleteShortcut(0);
            ImGui::EndChild();
            ImGui::End();
            ImGui::Render();
        };
        frame();
        frame();
        openPopup = focusInput = true;
        frame();
        focusInput = false;
        frame();
        frame();
        require(io.WantTextInput, "InputScalar must own text input");
        io.AddKeyEvent(ImGuiKey_Backspace, true);
        frame();
        require(!deletesPin && text && active, "Active InputScalar must retain its pin on Backspace");
        io.AddKeyEvent(ImGuiKey_Backspace, false);
        frame();
        io.AddKeyEvent(ImGuiKey_Delete, true);
        frame();
        require(!deletesPin && text && active, "Active InputScalar must retain its pin on Delete");
        io.AddKeyEvent(ImGuiKey_Delete, false);
        frame();

        // Dismiss the popup, then focus the ordinary Controls root.
        openPopup = false;
        io.AddKeyEvent(ImGuiKey_Escape, true);
        frame();
        io.AddKeyEvent(ImGuiKey_Escape, false);
        frame();
        io.AddMousePosEvent(400, 300);
        io.AddMouseButtonEvent(0, true);
        frame();
        io.AddMouseButtonEvent(0, false);
        frame();
        frame();
        io.AddKeyEvent(ImGuiKey_Delete, true);
        frame();
        require(deletesPin && !text && !active, "Normal released pin selection must allow Delete");
        io.AddKeyEvent(ImGuiKey_Delete, false);
        frame();

        // WantTextInput still reflects the previous frame on activation.
        directInput = true;
        frame();
        frame();
        io.AddMousePosEvent(inputCenter.x - 60, inputCenter.y);
        io.AddMouseButtonEvent(0, true);
        io.AddKeyEvent(ImGuiKey_Delete, true);
        frame();
        require(active && !text && !deletesPin,
            "Same-frame InputScalar activation must retain pin before WantTextInput updates");
        std::cout << "PASS Color Warp shortcut: active Backspace/Delete, normal Delete, same-frame InputScalar activation\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        ImGui::DestroyContext();
        return 1;
    }
    ImGui::DestroyContext();
    return 0;
}
