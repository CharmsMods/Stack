#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "App/settings/AppearanceTheme.h"
#include "Async/TaskSystem.h"
#include "Renderer/GLLoader.h"
#include "ThirdParty/stb_image_write.h"
#include "Utils/ImGuiExtras.h"

#include <GLFW/glfw3.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace Stack::Validation {
namespace {
constexpr int kWidth = 1280, kHeight = 800;

void RequireNotes(bool result, const char* message) {
    if (!result) throw std::runtime_error(message);
    std::cout << "PASS " << message << '\n';
}

void CaptureNotes(const std::filesystem::path& path) {
    std::vector<unsigned char> pixels(std::size_t(kWidth) * kHeight * 4);
    glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (int y = 0; y < kHeight / 2; ++y)
        for (int x = 0; x < kWidth * 4; ++x)
            std::swap(pixels[std::size_t(y) * kWidth * 4 + x],
                pixels[std::size_t(kHeight - y - 1) * kWidth * 4 + x]);
    RequireNotes(stbi_write_png(path.string().c_str(), kWidth, kHeight, 4,
        pixels.data(), kWidth * 4) != 0, "Native notes capture saved");
}

struct Canvas {
    EditorModule& editor;
    EditorNodeGraphUI ui;
    ImVec2 origin;
    explicit Canvas(EditorModule& owner) : editor(owner) {
        ui.Initialize();
        editor.CancelGraphAutoFocusTracking();
        ImGui::GetIO().AddMousePosEvent(-100, -100);
    }
    ~Canvas() { ui.Shutdown(); }
    void Frame() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        ImGuiExtras::BeginFrameInputRouting();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Graph notes validation", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        origin = ImGui::GetCursorScreenPos();
        ui.Render(&editor);
        editor.FinishRawLayerGraphFrame();
        ImGui::End();
        ImGui::Render();
        glViewport(0, 0, kWidth, kHeight);
        glClearColor(.12f, .12f, .12f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    void Settle(int count = 4) { for (int i = 0; i < count; ++i) Frame(); }
    ImVec2 At(int nodeId, float x = 24, float y = 24) {
        const auto* node = editor.GetNodeGraph().FindNode(nodeId);
        if (!node) throw std::runtime_error("Notes fixture node is missing");
        const auto view = ui.GetGraphCaptureViewportSnapshot();
        return {origin.x + view.panX + (node->position.x + x) * view.zoom,
            origin.y + view.panY + (node->position.y + y) * view.zoom};
    }
    void Point(ImVec2 point) { ImGui::GetIO().AddMousePosEvent(point.x, point.y); Frame(); }
    void Click(ImVec2 point) {
        Point(point);
        auto& io = ImGui::GetIO();
        io.AddMouseButtonEvent(0, true); Frame();
        io.AddMouseButtonEvent(0, false); Frame();
    }
    void Key(ImGuiKey key, bool down) { ImGui::GetIO().AddKeyEvent(key, down); Frame(); }
    void ReplaceText(const char* text) {
        Key(ImGuiMod_Ctrl, true); Key(ImGuiKey_A, true);
        Key(ImGuiKey_A, false); Key(ImGuiMod_Ctrl, false);
        ImGui::GetIO().AddInputCharactersUTF8(text); Frame();
    }
    void Save() {
        Key(ImGuiMod_Ctrl, true); Key(ImGuiKey_Enter, true);
        Key(ImGuiKey_Enter, false); Key(ImGuiMod_Ctrl, false);
        Settle();
    }
};

void ExerciseNotes(EditorModule& editor, bool raw) {
    using namespace EditorNodeGraph;
    if (raw) {
        auto& sourceGraph = editor.GetNodeGraph();
        sourceGraph.Clear();
        RawDevelopmentPayload source;
        source.recipe = Stack::RawRecipe::MakeDefaultRecipe({});
        const int sourceId = sourceGraph.AddRawDevelopmentNode(std::move(source), {})->id;
        const int outputId = sourceGraph.AddOutputNode({300, 0}, true)->id;
        RequireNotes(sourceGraph.TryConnect(sourceId, outputId), "RAW notes fixture source connected");
        editor.DeserializePipeline(editor.SerializePipeline());
        editor.OpenRawLayerMaskGraph({});
        editor.AddDataMathNodeAt(DataMathMode::Multiply, {320, 100});
    } else {
        auto& graph = editor.GetNodeGraph();
        graph.Clear(); graph.SetAllowNoOutput(true);
        editor.AddDataMathNodeAt(DataMathMode::Multiply, {160, 100});
    }
    const int id = editor.GetNodeGraph().GetSelectedNodeId();
    const auto initialPosition = editor.GetNodeGraph().FindNode(id)->position;
    const auto initialNote = editor.GetNodeGraph().FindNode(id)->notes;
    Canvas canvas(editor);
    canvas.Settle(8);
    auto& io = ImGui::GetIO();
    const auto beforeSize = canvas.ui.MeasureNodeForLayout(editor.GetNodeGraph(), *editor.GetNodeGraph().FindNode(id));
    const auto renderRevision = editor.GetRenderRevision();
    canvas.Point(canvas.At(id));
    canvas.Key(ImGuiKey_F, true);
    canvas.Settle();
    const auto revealSize = canvas.ui.MeasureNodeForLayout(editor.GetNodeGraph(), *editor.GetNodeGraph().FindNode(id));
    RequireNotes(beforeSize.min.x == revealSize.min.x && beforeSize.min.y == revealSize.min.y &&
        beforeSize.max.x == revealSize.max.x && beforeSize.max.y == revealSize.max.y,
        "F notes reveal retains node geometry");
    canvas.Click(canvas.At(id));
    canvas.Key(ImGuiKey_F, false);
    canvas.Settle();
    RequireNotes(io.WantTextInput, "Clicking notes retains text editing after F release");
    canvas.ReplaceText("Study ");
    canvas.Key(ImGuiKey_F, true);
    io.AddInputCharacter('f'); canvas.Frame();
    canvas.Key(ImGuiKey_F, false);
    io.AddInputCharactersUTF8(" note"); canvas.Frame();
    RequireNotes(editor.GetNodeGraph().FindNode(id)->notes == initialNote,
        "Typing remains a draft until commit");
    canvas.Save();
    RequireNotes(editor.GetNodeGraph().FindNode(id)->notes == "Study f note",
        "F types normally and Ctrl+Enter commits notes");
    RequireNotes(editor.GetRenderRevision() == renderRevision,
        "Notes commit does not invalidate image processing");
    RequireNotes(editor.UndoProjectGraphEdit() && editor.GetNodeGraph().FindNode(id)->notes == initialNote &&
        editor.RedoProjectGraphEdit() && editor.GetNodeGraph().FindNode(id)->notes == "Study f note",
        "One undo and redo restore the complete note edit");
    RequireNotes(editor.GetRenderRevision() == renderRevision,
        "Notes undo and redo do not invalidate image processing");
    canvas.Settle();
    canvas.Key(ImGuiKey_F, true); canvas.Click(canvas.At(id)); canvas.Key(ImGuiKey_F, false);
    canvas.ReplaceText("Discard this draft");
    canvas.Key(ImGuiKey_Escape, true); canvas.Key(ImGuiKey_Escape, false); canvas.Settle();
    RequireNotes(editor.GetNodeGraph().FindNode(id)->notes == "Study f note",
        "Escape cancels the note draft");
    canvas.Key(ImGuiKey_F, true); canvas.Click(canvas.At(id)); canvas.Key(ImGuiKey_F, false);
    canvas.ReplaceText("Committed by clicking outside.");
    canvas.Click({1100, 720}); canvas.Settle();
    RequireNotes(editor.GetNodeGraph().FindNode(id)->notes == "Committed by clicking outside.",
        "Clicking outside commits the note once");
    const auto* node = editor.GetNodeGraph().FindNode(id);
    RequireNotes(node->position.x == initialPosition.x && node->position.y == initialPosition.y,
        "Notes editing does not drag the node");
    canvas.Key(ImGuiKey_F, true); canvas.Point(canvas.At(id));
    const float zoom = canvas.ui.GetGraphCaptureViewportSnapshot().zoom;
    io.AddMouseWheelEvent(0, -1); canvas.Frame(); canvas.Settle();
    RequireNotes(canvas.ui.GetGraphCaptureViewportSnapshot().zoom == zoom,
        "Wheel over notes does not zoom the graph");
    canvas.Key(ImGuiKey_F, false);
    std::cout << (raw ? "RAW" : "Ordinary") << " native notes editing passed.\n";
}

void CaptureHalationNotes(EditorModule& editor, const std::filesystem::path& folder) {
    using namespace EditorNodeGraph;
    // A bounded presentation sample uses real authored Halation children.
    // It does not execute the image graph or alter the decompile layout algorithm.
    const auto& catalog = EditorNodeGraphDefinitions::GetShippedCompoundTemplates();
    const auto found = std::find_if(catalog.begin(), catalog.end(), [](const auto& definition) {
        return definition.identity.id == "stack:compound/halation";
    });
    if (found == catalog.end()) throw std::runtime_error("Halation template missing");
    Graph children;
    DeserializeGraphPayload(found->canonicalGraph, children, 0);
    auto& graph = editor.GetNodeGraph();
    graph.Clear(); graph.SetAllowNoOutput(true);
    const std::vector<NodeKind> kinds{NodeKind::Compound, NodeKind::DataMath, NodeKind::ChannelSplit,
        NodeKind::ConstantChannel, NodeKind::Layer, NodeKind::ChannelCombine};
    int count = 0;
    for (auto kind : kinds) {
        const auto child = std::find_if(children.GetNodes().begin(), children.GetNodes().end(),
            [&](const auto& node) { return node.kind == kind && !node.notes.empty(); });
        if (child == children.GetNodes().end()) continue;
        auto node = *child;
        node.id = ++count;
        node.position = {60.f + float((count - 1) % 3) * 360.f, 60.f + float((count - 1) / 3) * 330.f};
        graph.EditNodes().push_back(std::move(node));
    }
    graph.SetNextNodeId(count + 1);
    for (const auto& definition : children.GetCompoundDefinitions()) graph.GetCompoundDefinitions().push_back(definition);
    Canvas canvas(editor); canvas.Settle(8);
    canvas.Point({1100, 720}); canvas.Key(ImGuiKey_F, true); canvas.Settle(12);
    CaptureNotes(folder / "halation-notes.png");
    const auto before = canvas.ui.GetGraphCaptureViewportSnapshot();
    ImGui::GetIO().AddMouseWheelEvent(0, -1); canvas.Frame(); canvas.Settle(45);
    const auto after = canvas.ui.GetGraphCaptureViewportSnapshot();
    RequireNotes(after.zoom < before.zoom, "Wheel outside notes retains graph zoom");
    CaptureNotes(folder / "halation-notes-zoomed.png");
    canvas.Key(ImGuiKey_F, false);
}
} // namespace

bool ValidateGraphNodeNotes(const std::filesystem::path& folder) {
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    auto* window = glfwCreateWindow(kWidth, kHeight, "Graph node notes validation", nullptr, nullptr);
    if (!window) { glfwTerminate(); return false; }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) { glfwDestroyWindow(window); glfwTerminate(); return false; }
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.DisplaySize = {kWidth, kHeight}; io.DeltaTime = 1.f / 60.f;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    StackAppearance::AppearanceManager appearance;
    appearance.Load(); appearance.SetupFonts(io); appearance.ApplyCurrentTheme(io, ImGui::GetStyle());
    ImGui_ImplOpenGL3_Init("#version 430");
    ImGui_ImplOpenGL3_NewFrame(); ImGui::NewFrame(); ImGui::EndFrame();
    Async::TaskSystem::Get().Initialize();
    auto editor = std::make_unique<EditorModule>();
    editor->SetDocumentPersistenceEnabled(false); editor->Initialize(window, &appearance, false);
    bool success = false;
    try {
        std::filesystem::create_directories(folder);
        ExerciseNotes(*editor, false);
        CaptureHalationNotes(*editor, folder);
        ExerciseNotes(*editor, true);
        std::ofstream(folder / "result.json") << "{\"passed\":true,\"scope\":\"native node notes input and captures\"}\n";
        success = true;
    } catch (const std::exception& error) {
        std::cerr << "Graph notes validation: " << error.what() << '\n';
    }
    editor->RequestWorkerShutdownForAppClose(); Async::TaskSystem::Get().Shutdown(); editor.reset();
    ImGui_ImplOpenGL3_Shutdown(); ImGui::DestroyContext(); glfwDestroyWindow(window); glfwTerminate();
    return success;
}
} // namespace Stack::Validation
