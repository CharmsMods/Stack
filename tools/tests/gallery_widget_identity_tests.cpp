#include "Editor/Internal/RawLab/RawLabGalleryHitTesting.h"
#include <iostream>
#include <string>
#include <vector>
#include <limits>
#include <cmath>
#include <cstdlib>
#include <new>
#include "Utils/Utf8Text.h"

static bool trackFitAllocations = false;
static std::size_t fitAllocations = 0;
void* operator new(std::size_t size) {
    if (trackFitAllocations) ++fitAllocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

// PRODUCTION_KEY_HELPER
// PRODUCTION_LIST_HELPER
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
struct RowSnapshot {
    ImRect rect;
    ImVec2 next, maximum;
    ImGuiID id;
    ImVector<ImDrawVert> vertices;
};
bool ListRows() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = {700, 400}; io.DeltaTime = 1.f / 60;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    bool passed = true;
    auto render = [&](const std::string& label, bool native, bool disabled, bool selected,
                      bool focus = false, int* presses = nullptr, int* doubles = nullptr,
                      int* contexts = nullptr) {
        ImGui::NewFrame(); ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize({700, 400});
        ImGui::Begin("List", nullptr, ImGuiWindowFlags_NoTitleBar);
        ImGui::SetCursorScreenPos({50, 100});
        if (focus) ImGui::SetKeyboardFocusHere();
        ImGui::BeginDisabled(disabled);
        auto* window = ImGui::GetCurrentWindow();
        const int first = window->DrawList->VtxBuffer.Size;
        const ImGuiID expected = ImGui::GetID(label.c_str());
        const bool pressed = native
            ? ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)
            : GalleryListSelectable(label, selected);
        if (pressed && presses) ++*presses;
        if (pressed && doubles && ImGui::IsMouseDoubleClicked(0)) ++*doubles;
        RowSnapshot result{ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()),
            window->DC.CursorPos, window->DC.CursorMaxPos, ImGui::GetItemID(), {}};
        passed = Check(result.id == expected, "literal row retains original native ID") && passed;
        for (int i = first; i < window->DrawList->VtxBuffer.Size; ++i)
            result.vertices.push_back(window->DrawList->VtxBuffer[i]);
        ImGui::EndDisabled();
        if (contexts && ImGui::BeginPopupContextItem("row context")) {
            ++*contexts; ImGui::CloseCurrentPopup(); ImGui::EndPopup();
        }
        ImGui::End(); ImGui::Render();
        return result;
    };
    render("warm", false, false, false); render("warm", false, false, false);
    for (const auto& label : {std::string("ordinary.raw"), std::string("A##suffix.raw"),
            std::string("A##project    Single image"), std::string("##") + std::string(160, 'W')}) {
        std::string control = label;
        for (char& c : control) if (c == '#') c = 'X'; // default font has equal advances
        for (bool disabled : {false, true}) for (bool selected : {false, true}) {
            const auto actual = render(label, false, disabled, selected);
            const auto reference = render(control, true, disabled, selected);
            passed = Check(actual.rect.Min.x == reference.rect.Min.x && actual.rect.Min.y == reference.rect.Min.y &&
                actual.rect.Max.x == reference.rect.Max.x && actual.rect.Max.y == reference.rect.Max.y &&
                actual.next.x == reference.next.x && actual.next.y == reference.next.y &&
                actual.maximum.x == reference.maximum.x && actual.maximum.y == reference.maximum.y,
                "literal geometry and clipper stride match native full text") && passed;
            passed = Check(actual.vertices.Size == reference.vertices.Size,
                "literal suffix glyphs and long-label clipping match native full text") && passed;
            if (actual.vertices.Size == reference.vertices.Size)
                for (int i = 0; i < actual.vertices.Size; ++i)
                    passed = Check(actual.vertices[i].col == reference.vertices[i].col,
                        "selected/disabled literal paint retains native color") && passed;
        }
    }
    const std::string label = "A##suffix.raw";
    int presses = 0, doubles = 0, contexts = 0;
    auto frame = [&](bool disabled = false, bool focus = false) {
        return render(label, false, disabled, false, focus, &presses, &doubles, &contexts);
    };
    io.AddMousePosEvent(600, 105); frame();
    io.AddMouseButtonEvent(0, true); frame(); io.AddMouseButtonEvent(0, false); frame();
    passed = Check(presses == 1, "full-row single release selects once") && passed;
    io.AddMouseButtonEvent(0, true); frame(); io.AddMouseButtonEvent(0, false); frame();
    passed = Check(doubles == 1, "native double-click remains available to caller") && passed;
    io.AddMouseButtonEvent(1, true); frame(); io.AddMouseButtonEvent(1, false); frame(); frame();
    passed = Check(contexts == 1, "context popup remains owned by same row") && passed;
    // Release outside must not activate the row; keep the interval beyond double-click.
    for (int i = 0; i < 25; ++i) frame();
    const int beforeCancel = presses;
    io.AddMouseButtonEvent(0, true); frame();
    io.AddMousePosEvent(600, 180); frame();
    io.AddMouseButtonEvent(0, false); frame();
    passed = Check(presses == beforeCancel, "drag outside cancels native release") && passed;
    io.AddMousePosEvent(600, 105); frame();
    const int beforeDisabled = presses;
    io.AddMouseButtonEvent(0, true); frame(true); io.AddMouseButtonEvent(0, false); frame(true);
    passed = Check(presses == beforeDisabled, "disabled row cannot select") && passed;
    io.AddMousePosEvent(-100, -100); frame(false, true); frame();
    io.AddKeyEvent(ImGuiKey_Tab, true); frame(); io.AddKeyEvent(ImGuiKey_Tab, false); auto focused = frame(); frame();
    passed = Check(GImGui->NavId == focused.id, "Tab reaches sole native row stop") && passed;
    const int beforeSpace = presses;
    io.AddKeyEvent(ImGuiKey_Space, true); frame(); io.AddKeyEvent(ImGuiKey_Space, false); frame();
    passed = Check(presses == beforeSpace + 1, "Space activates native row") && passed;
    int owners[2]{};
    for (int step = 0; step < 6; ++step) {
        io.AddMousePosEvent(300, 145);
        if (step == 2) io.AddMouseButtonEvent(1, true);
        if (step == 3) io.AddMouseButtonEvent(1, false);
        ImGui::NewFrame(); ImGui::SetNextWindowPos({0, 0}); ImGui::SetNextWindowSize({700, 400});
        ImGui::Begin("Siblings", nullptr, ImGuiWindowFlags_NoTitleBar);
        for (int i = 0; i < 2; ++i) {
            const std::string key = i ? "B###same.raw" : "A###same.raw";
            PushGalleryKeyID(key);
            ImGui::SetCursorScreenPos({50, 100.f + 40.f * i});
            GalleryListSelectable(key, false);
            if (ImGui::BeginPopupContextItem("RawGallerySourceListContext")) {
                ++owners[i]; ImGui::CloseCurrentPopup(); ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        ImGui::End(); ImGui::Render();
    }
    passed = Check(owners[0] == 0 && owners[1] == 1,
        "same-frame literal siblings retain distinct context ownership") && passed;
    ImGui::DestroyContext();
    return passed;
}

// Exercise the production lambda with real font measurement, not a stub width.
bool FitTextTests() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = {700, 400}; io.DeltaTime = 1.f / 60;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();
    auto* galleryFont = ImGui::GetFont();
    float galleryTextSize = 8.06f, galleryTextWidth = 152;
    // PRODUCTION_FIT_TEXT
    // Previous exact tail-removal behavior is the semantic oracle.
    const auto oldFit = [&](const std::string& value) {
        const auto measure = [&](const std::string& text) {
            return galleryFont->CalcTextSizeA(galleryTextSize,
                std::numeric_limits<float>::max(), 0, text.c_str()).x;
        };
        if (value.empty() || measure(value) <= galleryTextWidth) return value;
        if (measure("...") > galleryTextWidth) return std::string();
        std::string fitted = value;
        while (!fitted.empty() && measure(fitted + "...") > galleryTextWidth)
            Stack::Utf8Text::RemoveLastCodePoint(fitted);
        return fitted.empty() ? std::string("...") : fitted + "...";
    };
    const std::string longName = std::string(240, 'W') + ".raw";
    const std::vector<std::string> labels = {"", "ordinary.raw", longName,
        u8"旅行_写真_éclair_📷_long_capture_name_edited_project.raw",
        "17  " + std::string(180, 'M') + "  2/3",
        "short\n" + longName, longName + "\nshort",
        "short\r\n" + longName, longName + "\r\nshort",
        std::string(60, 'W') + std::string("\xE2\x82", 2),
        std::string("\xFF\x80", 2) + longName,
        std::string(25, 'W') + std::string("\xE2\x82", 2) + longName};
    bool passed = true;
    for (const auto& label : labels)
        for (float size : {8.f, 8.06f, 9.375f, 10.f})
            for (float available : {12.f, 118.f, 152.f, 200.f}) {
                galleryTextSize = size; galleryTextWidth = available;
                const char* remaining = nullptr;
                galleryFont->CalcTextSizeA(size,
                    std::nextafter(available, std::numeric_limits<float>::infinity()),
                    0, label.c_str(), nullptr, &remaining);
                passed = Check(remaining >= label.c_str() && remaining <= label.c_str() + label.size(),
                    "font prefix remains inside original string") && passed;
                passed = Check(fitGalleryText(label) == oldFit(label),
                    "bounded fit preserves exact UTF-8/multiline/CRLF output") && passed;
            }
    for (float size : {8.f, 8.06f, 9.375f, 10.f}) {
        galleryTextSize = size;
        for (int length : {1, 2, 12, 31, 32, 33}) {
            const std::string candidate = std::string(length, 'W') + "...";
            const float edge = galleryFont->CalcTextSizeA(size,
                std::numeric_limits<float>::max(), 0, candidate.c_str()).x;
            for (float available : {std::nextafter(edge, 0.f), edge,
                    std::nextafter(edge, std::numeric_limits<float>::infinity())}) {
                galleryTextWidth = available;
                passed = Check(fitGalleryText(longName) == oldFit(longName),
                    "bounded fit preserves inclusive float-neighbor boundary") && passed;
            }
        }
    }
    galleryTextSize = 8.06f; galleryTextWidth = 152;
    fitGalleryText(longName); // warm font glyph state before allocation measurement
    fitAllocations = 0; trackFitAllocations = true;
    std::size_t outputBytes = 0;
    for (int i = 0; i < 64; ++i) outputBytes += fitGalleryText(longName).size();
    trackFitAllocations = false;
    passed = Check(fitAllocations < 1024 && outputBytes == 64 * oldFit(longName).size(),
        "long filenames do not copy and rescan every removed suffix") && passed;
    ImGui::EndFrame(); ImGui::DestroyContext();
    return passed;
}

int main() {
    bool passed = FitTextTests();
    passed = ListRows() && passed;
    for (int kind = 0; kind < 3; ++kind)
        for (bool reorder : {false, true})
            for (bool sameFileName : {false, true})
                passed = Scenario(kind, reorder, sameFileName) && passed;
    if (!passed) return 1;
    std::cout << "Main gallery identity passed (stack tiles, source rows, group headers, reorder, ordinary IDs, literal list rows/input/clipping, bounded text fitting).\n";
}
