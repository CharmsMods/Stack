#include "imgui.h"
#include <cmath>
#include <iostream>
#include <vector>

struct Item { int index; ImVec2 position; };
struct Result { std::vector<Item> visible; float scrollMax = 0; };
Result Frame(bool clipped, int columnCount, float requestedScroll, float alpha,
    float paddingOverride, bool borders) {
    auto& io = ImGui::GetIO(); io.DisplaySize = ImVec2(500, 300); io.DeltaTime = 1.f / 60;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(450, 280));
    ImGui::Begin("Browser", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
    constexpr float kCardWidth = 138.f;
    const float contentWidth = columnCount * 138.f;
    ImGui::BeginChild("Results", ImVec2(contentWidth + 20, 220), false, ImGuiWindowFlags_NoNav);
    if (requestedScroll >= 0) ImGui::SetScrollY(requestedScroll);
    Result result;
    // Production table setup is unchanged. Overrides below are extra style
    // sensitivity cases, not claims that the browser currently offers them.
// PRODUCTION_TABLE
        if (paddingOverride >= 0) ImGui::GetStyle().CellPadding.y = paddingOverride;
        const int count = 83, rowCount = (count + columnCount - 1) / columnCount;
        const auto row = [&](int r) {
            ImGui::TableNextRow();
            for (int column = 0; column < columnCount; ++column) {
                ImGui::TableSetColumnIndex(column);
                const int index = r * columnCount + column;
                if (index >= count || alpha <= .01f) continue;
                ImGui::PushID(index);
// PRODUCTION_CARD
                if (ImGui::IsItemVisible()) result.visible.push_back({index, min});
                ImGui::PopID();
            }
        };
        if (clipped) {
            ImGuiListClipper clipper;
// PRODUCTION_STRIDE
            while (clipper.Step())
                for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) row(r);
        } else for (int r = 0; r < rowCount; ++r) row(r);
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    result.scrollMax = ImGui::GetScrollMaxY();
    ImGui::EndChild(); ImGui::End(); ImGui::Render();
    return result;
}
Result Run(bool clipped, int columns, float scroll, float alpha, float padding, bool borders) {
    ImGui::CreateContext(); auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
    unsigned char* pixels; int w, h; io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    Frame(clipped, columns, scroll, alpha, padding, borders);
    Frame(clipped, columns, scroll, alpha, padding, borders);
    auto result = Frame(clipped, columns, -1, alpha, padding, borders);
    ImGui::DestroyContext(); return result;
}
int main() {
    for (int columns : {1, 2}) for (float scroll : {0.f, 83.f, 500.f, 2500.f, 100000.f})
    for (float alpha : {.1f, 1.f}) for (float padding : {-1.f, 2.f, 9.f})
    for (bool borders : {false, true}) {
        const auto expected = Run(false, columns, scroll, alpha, padding, borders);
        const auto actual = Run(true, columns, scroll, alpha, padding, borders);
        bool same = actual.visible.size() == expected.visible.size() &&
            std::abs(actual.scrollMax - expected.scrollMax) < .01f;
        for (size_t i = 0; same && i < actual.visible.size(); ++i)
            same = actual.visible[i].index == expected.visible[i].index &&
                std::abs(actual.visible[i].position.x - expected.visible[i].position.x) < .01f &&
                std::abs(actual.visible[i].position.y - expected.visible[i].position.y) < .01f;
        if (!same) {
            std::cerr << "Browser clipping differs: columns=" << columns << " scroll=" << scroll
                << " borders=" << borders << " alpha=" << alpha << " padding=" << padding << " max=" << actual.scrollMax
                << '/' << expected.scrollMax << '\n'; return 1;
        }
    }
    std::cout << "Node browser clipped rows match unclipped geometry and extent.\n";
}
