#include "Editor/NodeGraph/UI/NodeNotesView.h"
#include "Editor/NodeGraph/GraphEditorContext.h"
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>

void ValidateNodeNotesInteractions() {
    using Stack::Editor::NodeGraphUI::NodeNotesView;
    const auto require = [](bool result, const char* message) {
        if (!result) throw std::runtime_error(message);
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
    io.DisplaySize = {400, 240};
    io.DeltaTime = 1.f / 60.f;
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    NodeNotesView notes, otherProject;
    EditorNodeGraph::Node node;
    node.id = 7;
    node.instanceUuid = "note-node";
    node.notes = "Original";
    Stack::Editor::GraphEditorContext owner;
    owner.documentId = "project-a";
    owner.graphId = "graph-a";
    const auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Notes", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        notes.BeginFrame(true);
        notes.Draw(node, owner, 1, {20, 20}, {300, 180}, 1,
            {1, 1, 1, 1}, {.5f, .5f, .5f, 1}, true, 1);
        ImGui::End();
        ImGui::Render();
    };
    const auto click = [&](float x, float y) {
        io.AddMousePosEvent(x, y); frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true); frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false); frame(); frame();
    };
    const auto edit = [&](const char* text) {
        click(50, 50);
        require(notes.Editing(), "Click did not open a note draft.");
        io.AddKeyEvent(ImGuiMod_Ctrl, true); io.AddKeyEvent(ImGuiKey_A, true); frame();
        io.AddKeyEvent(ImGuiKey_A, false); io.AddKeyEvent(ImGuiMod_Ctrl, false); frame();
        io.AddInputCharactersUTF8(text); frame();
    };
    frame(); frame();
    edit("Unsaved note");
    require(node.notes == "Original", "Editing prematurely changed authored notes.");
    const auto active = ImGui::GetActiveID();
    require(active != 0, "The note test did not retain text focus.");
    int commits = 0;
    require(otherProject.FinishEditing([&](const auto&) { ++commits; return true; }),
        "An unrelated clean project failed to finish.");
    require(commits == 0 && notes.Editing() && ImGui::GetActiveID() == active,
        "Finishing another project consumed the note draft or focus.");
    require(notes.FinishEditing([&](const auto& note) {
        ++commits;
        require(note.documentId == owner.documentId && note.graphId == owner.graphId &&
            note.nodeId == node.id && note.instanceUuid == node.instanceUuid,
            "Transition commit lost its project or node identity.");
        require(note.text == "Unsaved note", "Transition did not commit current typed text.");
        node.notes = note.text;
        return true;
    }), "An explicit transition failed to finish the note.");
    require(commits == 1 && !notes.Editing() && ImGui::GetActiveID() == active,
        "Transition duplicated its commit or changed global input ownership.");
    frame(); frame();

    edit("Retry this note");
    require(!notes.FinishEditing([&](const auto&) {
        notes.Reset(); // A rejected RAW graph can rebuild its canvas.
        return false;
    }), "A rejected note commit reported success.");
    require(notes.Editing(), "A rejected transition discarded its draft.");
    require(notes.FinishEditing([&](const auto& note) {
        require(note.text == "Retry this note", "Retry lost the rejected draft text.");
        node.notes = note.text;
        return true;
    }), "The retained draft could not be retried.");
    frame(); frame();

    click(50, 50);
    require(notes.FinishEditing([&](const auto&) {
        throw std::runtime_error("An unchanged note created a commit.");
        return true;
    }), "An unchanged draft blocked a transition.");
    frame(); frame();

    edit("Cancel this note");
    io.AddKeyEvent(ImGuiKey_Escape, true); frame();
    io.AddKeyEvent(ImGuiKey_Escape, false); frame();
    require(!notes.Editing() && !notes.TakeCommit(), "Escape queued a canceled note.");
    require(notes.FinishEditing([&](const auto&) {
        throw std::runtime_error("Transition revived a canceled note.");
        return true;
    }), "A canceled draft blocked a transition.");
    require(node.notes == "Retry this note", "Cancel changed authored notes.");

    edit("Queued note");
    click(350, 210);
    require(!notes.Editing(), "Outside click did not finish the note.");
    require(!notes.FinishEditing([](const auto&) { return false; }),
        "A rejected queued note reported success.");
    require(notes.FinishEditing([&](const auto& note) {
        require(note.text == "Queued note", "Rejected queued note was lost.");
        node.notes = note.text;
        return true;
    }), "A queued note could not finish before transition.");
    require(!notes.TakeCommit(), "A successful transition left a duplicate queued commit.");
    std::cout << "Node notes transition interactions passed\n";
}
