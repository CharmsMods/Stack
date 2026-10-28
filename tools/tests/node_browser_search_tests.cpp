#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Named doubles supply a catalog and availability only. Actual ImGui owns input,
// and the production filter, keyboard gates, and search close-state are extracted.
struct NodeBrowserEntry { std::string label, category, searchAliases; };
static const std::vector<NodeBrowserEntry> entries{{"Brightness", "Color", ""}, {"Gaussian Blur", "Blur", "soften"}};
std::string EntryKey(const NodeBrowserEntry& entry) { return entry.label; }
namespace Stack::UiPerformance { enum class Counter { CatalogMatches }; void Add(Counter, std::size_t) {} }
float AnimateToward(float, float target, float, float) { return target; }
// PRODUCTION_CONTAINS
struct Graph { std::uint64_t GetStructureRevision() const { return 1; } };
struct Editor {
    int queries = 0;
    struct Context {
        int documentId = 1, graphId = 1; int* queries;
        auto queryAvailableNodes(std::optional<std::pair<int, std::string>>) const {
            ++*queries; return entries;
        }
    };
    Context GetGraphEditorContext() { return {1, 1, &queries}; }
    std::uint64_t GetRenderRevision() const { return 1; }
};
struct Fixture {
    enum class NodeBrowserMode { GeneralAdd, ConnectFromOutput, ConnectFromInput };
    enum class DrawerMode { None, NodeBrowser };
    NodeBrowserMode m_NodeBrowserMode = NodeBrowserMode::GeneralAdd, m_NodeBrowserFilterCacheMode = NodeBrowserMode::GeneralAdd;
    DrawerMode m_DrawerMode = DrawerMode::NodeBrowser;
    char m_NodeBrowserSearchBuffer[128]{};
    bool m_NodeBrowserFilterCacheValid = false, m_NodeBrowserVisibleCacheOpen = false;
    bool m_NodeBrowserEntryAnimationsSettled = false, m_NodeBrowserFocusSearch = true;
    bool m_NodeBrowserSearchFocused = false, m_NodeBrowserThumbnailCatalogEnsured = false;
    std::uint64_t m_NodeBrowserFilterCacheGraphRevision = 0, m_NodeBrowserFilterCacheRenderRevision = 0;
    int m_NodeBrowserFilterCacheDocumentId = 0, m_NodeBrowserFilterCacheGraphId = 0;
    int m_NodeBrowserDragFromNodeId = -1, m_NodeBrowserDragToNodeId = -1;
    int m_NodeBrowserFilterCacheDragFromNodeId = -1, m_NodeBrowserFilterCacheDragToNodeId = -1;
    std::string m_NodeBrowserDragFromSocketId, m_NodeBrowserDragToSocketId;
    std::string m_NodeBrowserFilterCacheDragFromSocketId, m_NodeBrowserFilterCacheDragToSocketId, m_NodeBrowserFilterCacheSearch;
    std::vector<std::size_t> m_NodeBrowserFilteredEntryIndices;
    std::unordered_set<std::string> m_NodeBrowserAvailableKeys, m_NodeBrowserVisibleKeys;
    std::unordered_map<std::string, float> m_NodeBrowserEntryAlpha, m_NodeBrowserCardHoverAnim;
    int m_PushedSourceNodeId = -1; float m_PushDistance = 0; std::vector<int> m_PushedNodeIds;
    bool IsCatalogDocked() const { return false; }
    // PRODUCTION_CLOSE
    Editor ownedEditor;
    std::vector<std::string> activated;
    int rebuilds = 0;
    void draw(bool interactiveOverlay = true, bool operationOpen = true) {
        auto* editor = &ownedEditor; const Graph graph;
        const bool isOpen = operationOpen; const float dt = 1.f / 60;
        const float alpha = isOpen ? 1.f : 0.f;
        std::vector<std::string> entryKeys; for (const auto& entry : entries) entryKeys.push_back(EntryKey(entry));
        // PRODUCTION_BEFORE_WINDOW
        ImGui::BeginDisabled(!interactiveOverlay);
        if (m_NodeBrowserFocusSearch && interactiveOverlay) { ImGui::SetKeyboardFocusHere(); m_NodeBrowserFocusSearch = false; }
        const char* prompt = "Search nodes";
        const auto searchFlags = isOpen ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_ReadOnly;
        // PRODUCTION_ORDERED_SEARCH
        if (filterChanged) ++rebuilds;
        const NodeBrowserEntry* activatedEntry = nullptr;
        // PRODUCTION_SUBMIT
        if (isOpen && activatedEntry) { activated.push_back(activatedEntry->label); CloseNodeBrowser(); }
        bool closeBrowser = false;
        // PRODUCTION_ESCAPE
        if (closeBrowser) CloseNodeBrowser();
        ImGui::EndDisabled();
    }
};
bool check(bool value, const char* message) { if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }
int main() {
    ImGui::CreateContext(); auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = {600, 400}; io.DeltaTime = 1.f / 60;
    unsigned char* pixels; int width, height; io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    Fixture fixture; bool passed = true;
    auto frame = [&](bool enabled = true, bool open = true) {
        ImGui::NewFrame(); ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize({600,400});
        ImGui::Begin("Browser", nullptr, ImGuiWindowFlags_NoNav); fixture.draw(enabled, open);
        ImGui::End(); ImGui::Render();
    };
    auto focus = [&](const char* query) {
        fixture.CloseNodeBrowser(); frame(false, false); frame(false, false);
        fixture.m_DrawerMode = Fixture::DrawerMode::NodeBrowser;
        std::strcpy(fixture.m_NodeBrowserSearchBuffer, query); fixture.m_NodeBrowserFocusSearch = true;
        frame(); frame(); frame();
    };
    auto selectAll = [&] {
        io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddKeyEvent(ImGuiKey_A, true); frame();
        io.AddKeyEvent(ImGuiKey_A, false); io.AddKeyEvent(ImGuiMod_Ctrl, false); frame();
    };
    focus("Gaussian"); selectAll();
    io.AddKeyEvent(ImGuiKey_Backspace, true); io.AddKeyEvent(ImGuiKey_Enter, true); frame();
    passed = check(fixture.activated == std::vector<std::string>{"Brightness"}, "clear+Enter uses current empty query's first entry") && passed;
    io.AddKeyEvent(ImGuiKey_Backspace, false); io.AddKeyEvent(ImGuiKey_Enter, false); frame(false, false);
    focus("Gaussian"); selectAll();
    io.AddInputCharactersUTF8("Brightness"); io.AddKeyEvent(ImGuiKey_Enter, true); frame(); frame();
    passed = check(fixture.activated.size() == 2 && fixture.activated.back() == "Brightness", "replacement+Enter uses current query") && passed;
    io.AddKeyEvent(ImGuiKey_Enter, false); frame(false, false);
    focus("GaussianX");
    // Select the suffix only by placing the caret at End before backspace.
    io.AddKeyEvent(ImGuiKey_End, true); frame(); io.AddKeyEvent(ImGuiKey_End, false); frame();
    io.AddKeyEvent(ImGuiKey_Backspace, true); io.AddKeyEvent(ImGuiKey_Enter, true); frame();
    passed = check(fixture.activated.size() == 3 && fixture.activated.back() == "Gaussian Blur", "backspace+Enter resolves newly matching query") && passed;
    io.AddKeyEvent(ImGuiKey_Backspace, false); io.AddKeyEvent(ImGuiKey_Enter, false); frame(false, false);
    focus("no matching node"); const auto count = fixture.activated.size();
    io.AddKeyEvent(ImGuiKey_Enter, true); frame();
    passed = check(fixture.activated.size() == count && fixture.m_NodeBrowserFocusSearch, "no-match Enter adds nothing and restores search focus") && passed;
    io.AddKeyEvent(ImGuiKey_Enter, false); frame(); frame();
    const int rebuilds = fixture.rebuilds, queries = fixture.ownedEditor.queries;
    frame(); frame();
    passed = check(fixture.rebuilds == rebuilds && fixture.ownedEditor.queries == queries, "unchanged query reuses filter and availability") && passed;
    io.AddKeyEvent(ImGuiKey_Escape, true); frame();
    passed = check(!fixture.m_NodeBrowserFilterCacheValid && fixture.m_NodeBrowserFilteredEntryIndices.empty(), "Escape clears search owner caches") && passed;
    io.AddKeyEvent(ImGuiKey_Escape, false); frame(false, false);
    focus("Gaussian");
    passed = check(fixture.m_NodeBrowserFilteredEntryIndices == std::vector<std::size_t>{1}, "reopen refreshes query results") && passed;
    io.AddKeyEvent(ImGuiKey_Enter, true); frame(false);
    passed = check(fixture.activated.size() == count, "disabled owner cannot activate") && passed;
    io.AddKeyEvent(ImGuiKey_Enter, false); frame(false, false);
    // Animated hosted collapse: Close invalidates, then shrinking visible frames
    // are still isOpen and rebuild the cache before the fully-closed early return.
    focus("Gaussian"); fixture.CloseNodeBrowser();
    frame(false, true); frame(false, true);
    passed = check(fixture.m_NodeBrowserFilterCacheValid && fixture.m_NodeBrowserVisibleCacheOpen,
        "shrinking host still refreshes open catalog state") && passed;
    frame(false, false); frame(false, false);
    passed = check(fixture.m_NodeBrowserEntryAlpha.empty() && !fixture.m_NodeBrowserVisibleCacheOpen,
        "fully closed host invalidates visible animation ownership") && passed;
    frame(true, true);
    passed = check(fixture.m_NodeBrowserVisibleKeys.count("Gaussian Blur") &&
        fixture.m_NodeBrowserEntryAlpha.count("Gaussian Blur"),
        "same-query docked reopen rebuilds visible cards") && passed;
    ImGui::DestroyContext();
    if (!passed) return 1;
    std::cout << "Node browser current-frame search/Enter/cache/close checks passed.\n";
}
