#include "Editor/Internal/RawLab/RawLabGalleryHitTesting.h"
#include <iostream>
#include <string>

// PRODUCTION_KEY_HELPER
bool Check(bool value, const char* message) {
    if (!value) std::cerr << "FAILED: " << message << '\n';
    return value;
}
bool Scenario(int kind, bool reorder, bool sameFileName) {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = {700, 400}; io.DeltaTime = 1.f / 60;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    int clicks[2]{};
    ImGuiID originalIds[2]{};
    bool passed = true;
    for (int frame = 0; frame < 9; ++frame) {
        io.AddMousePosEvent(270, 140);
        if (frame == 4) io.AddMouseButtonEvent(0, true);
        if (frame == 5) io.AddMouseButtonEvent(0, false);
        ImGui::NewFrame(); ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize({700, 400});
        ImGui::Begin("Gallery");
        for (int submitted = 0; submitted < 2; ++submitted) {
            const int i = reorder && frame >= 4 ? 1 - submitted : submitted;
            const std::string key = sameFileName
                ? (i ? "B###same/frame.raw" : "A###same/frame.raw")
                : (i ? "B###same.raw" : "A###same.raw");
            if (kind == 0) {
                const struct { std::string animationKey; } item{"stack:" + key};
                // PRODUCTION_STACK_SCOPE
                // Permanent stacks use nullptr, so the stack scope must remain
                // distinct even as their cover record changes.
                const void* itemId = nullptr;
                // PRODUCTION_THUMBNAIL_SCOPE
                ImGui::SetCursorScreenPos({50.f + 170.f * i, 100});
                if (Stack::Editor::RawLabInternal::GalleryThumbnailButton("##tile",
                        {150, 100}, 100, ImGuiButtonFlags_PressedOnClickRelease |
                        ImGuiButtonFlags_PressedOnDoubleClick)) ++clicks[i];
            } else if (kind == 1) {
                const struct Source { std::string relativePathKey; } record{key};
                const auto* source = &record;
                // PRODUCTION_SOURCE_SCOPE
                ImGui::SetCursorScreenPos({50.f + 170.f * i, 100});
                const std::string row = sameFileName ? "frame.raw" : key;
                if (ImGui::Selectable(row.c_str(), false,
                        ImGuiSelectableFlags_AllowDoubleClick, {150, 100})) ++clicks[i];
            } else {
                const std::string groupKey = "folder:" + key;
                // PRODUCTION_GROUP_SCOPE
                ImGui::SetCursorScreenPos({50.f + 170.f * i, 100});
                ImGui::InvisibleButton("##GalleryFolderHeader", {150, 100});
                if (ImGui::IsItemClicked()) ++clicks[i];
            }
            const ImGuiID id = ImGui::GetItemID();
            if (frame == 0) originalIds[i] = id;
            passed = Check(id == originalIds[i], "identity survives submission reorder") && passed;
            if (kind == 0) { ImGui::PopID(); ImGui::PopID(); }
            ImGui::PopID();
        }
        passed = Check(originalIds[0] != originalIds[1], "literal path prefixes remain distinct before ###") && passed;
        // Ordinary names retain their existing IDs, including the parent scope.
        ImGui::PushID("parent");
        const ImGuiID ordinary = ImGui::GetID("folder/ordinary.raw");
        PushGalleryKeyID("folder/ordinary.raw");
        passed = Check(ImGui::GetCurrentWindow()->IDStack.back() == ordinary,
            "ordinary key identity and parent seed remain unchanged") && passed;
        ImGui::PopID(); ImGui::PopID();
        ImGui::End(); ImGui::Render();
    }
    passed = Check(clicks[0] == 0 && clicks[1] == 1, "second path selects exactly once without touching first") && passed;
    ImGui::DestroyContext();
    return passed;
}
int main() {
    bool passed = true;
    for (int kind = 0; kind < 3; ++kind)
        for (bool reorder : {false, true})
            for (bool sameFileName : {false, true})
                passed = Scenario(kind, reorder, sameFileName) && passed;
    if (!passed) return 1;
    std::cout << "Main gallery identity passed (stack tiles, source rows, group headers, reorder, ordinary IDs).\n";
}
