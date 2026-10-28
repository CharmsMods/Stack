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
    EditorNodeGraph::Node second;
    second.id = 8; second.instanceUuid = "second-note"; second.notes = "Second";
    bool showSecond = false, secondFirst = false, secondExists = true, drawNotes = true;
    bool revealHeld = true;
    bool acknowledge = false, reject = false, resetOnReject = false, deleteSuccessor = false;
    int attempts = 0, accepted = 0;
    std::uint64_t noteRevision = 1;
    const auto frame = [&] {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Notes", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        if (drawNotes) {
            notes.BeginFrame(revealHeld, [&](const auto& note) {
                return note.documentId == owner.documentId && note.graphId == owner.graphId &&
                    ((note.nodeId == node.id && note.instanceUuid == node.instanceUuid) ||
                     (secondExists && note.nodeId == second.id && note.instanceUuid == second.instanceUuid));
            });
            const auto drawSecond = [&] {
                if (showSecond && secondExists)
                    notes.Draw(second, owner, noteRevision, {220,20}, {395,180}, 1,
                        {1,1,1,1}, {.5f,.5f,.5f,1}, true, 1);
            };
            if (secondFirst) drawSecond();
            notes.Draw(node, owner, noteRevision, {20, 20}, {200, 180}, 1,
                {1, 1, 1, 1}, {.5f, .5f, .5f, 1}, true, 1);
            if (!secondFirst) drawSecond();
            if (acknowledge) notes.CommitPending([&](const auto& note) {
                ++attempts;
                require(notes.EditingNode() != second.id,
                    "A changed-note successor activated before commit acknowledgement.");
                require(note.documentId == owner.documentId && note.graphId == owner.graphId,
                    "A handoff changed the outgoing document identity.");
                if (reject) { if (resetOnReject) notes.Reset(); return false; }
                auto& target = note.nodeId == node.id ? node : second;
                require(note.instanceUuid == target.instanceUuid, "A handoff changed the outgoing node identity.");
                target.notes = note.text;
                ++accepted;
                if (deleteSuccessor) secondExists = false;
                return true;
            });
        }
        ImGui::End();
        ImGui::Render();
    };
    const auto click = [&](float x, float y) {
        io.AddMousePosEvent(x, y); frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true); frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false); frame(); frame();
    };
    const auto edit = [&](const char* text) {
        revealHeld = true;
        click(50, 50);
        revealHeld = false;
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
    require(!notes.Editing() && !notes.HasPendingCommit(), "Escape queued a canceled note.");
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
    require(!notes.HasPendingCommit(), "A successful transition left a duplicate queued commit.");
    // Handoff is independent of node z-order, and each accepted draft reaches
    // the document/history callback exactly once.
    for (bool earlier : {false, true}) {
        notes.Reset(); node.notes = "Original"; second.notes = "Second";
        showSecond = true; secondFirst = earlier; acknowledge = true;
        attempts = accepted = 0;
        edit("Handoff text");
        click(250,50);
        require(notes.EditingNode() == second.id && node.notes == "Handoff text" &&
            attempts == 1 && accepted == 1, "One-click note handoff depends on draw order or duplicated its commit.");
        frame(); frame();
        require(attempts == 1, "A settled handoff repeated its history callback.");
        // Switching an unchanged note also retains this frame's reveal after F
        // release, including when the outgoing widget is drawn first.
        click(50,50);
        require(notes.EditingNode() == node.id && attempts == 1,
            "An unchanged note handoff lost reveal after F release or created history.");
        click(250,50);
        require(notes.EditingNode() == second.id && attempts == 1,
            "The reverse unchanged handoff depends on draw order.");
        io.AddKeyEvent(ImGuiKey_Escape,true);frame();io.AddKeyEvent(ImGuiKey_Escape,false);frame();
        require(!notes.Editing() && attempts == 1, "Canceling the successor revived the outgoing commit.");

        edit("Keep rejected draft");
        reject = resetOnReject = true;
        click(250,50);
        require(notes.EditingNode() == node.id && ImGui::GetActiveID() != 0 &&
            node.notes == "Handoff text" && attempts == 2 && accepted == 1,
            "A rejected handoff lost the outgoing draft/focus or activated the successor.");
        reject = resetOnReject = false;
        click(250,50);
        require(notes.EditingNode() == second.id && node.notes == "Keep rejected draft" &&
            attempts == 3 && accepted == 2, "Retry failed to retain the outgoing text exactly once.");
        // Model undo/external edits invalidate a live draft rather than writing
        // its superseded original back into the document on the next frame.
        second.notes = "Restored by history";
        frame();
        require(!notes.Editing() && attempts == 3,
            "An external history change revived or committed a superseded draft.");
    }

    notes.Reset(); node.notes = "Original"; second.notes = "Second";
    attempts = accepted = 0;
    edit("Survives target deletion");
    deleteSuccessor = true;
    click(250,50);
    require(!notes.Editing() && node.notes == "Survives target deletion" &&
        attempts == 1 && accepted == 1, "A deleted successor captured focus or discarded the accepted outgoing note.");
    deleteSuccessor = false; secondExists = true;

    edit("Survives focus loss and hiding");
    io.AddFocusEvent(false); frame(); io.AddFocusEvent(true); frame();
    drawNotes = false; frame(); frame(); frame(); drawNotes = true; frame();
    require(notes.EditingNode() == node.id, "Focus loss or hidden frames discarded an uncommitted draft.");
    require(notes.FinishEditing([&](const auto& note) {
        require(note.text == "Survives focus loss and hiding", "Hidden draft text was lost.");
        node.notes = note.text; return true;
    }), "A hidden/focus-lost note could not be committed.");
    frame(); frame();

    // A same-owner topology revision may remove an asynchronous import
    // placeholder without removing the node whose note is being edited.
    edit("Survives unrelated topology");
    secondExists = false; ++noteRevision; notes.InvalidateLayouts(); frame();
    require(notes.EditingNode() == node.id,
        "Removing an unrelated node discarded the live note draft.");
    secondExists = true; showSecond = true; ++noteRevision; notes.InvalidateLayouts(); frame();
    require(notes.EditingNode() == node.id,
        "Adding an unrelated node discarded the live note draft.");
    require(notes.FinishEditing([&](const auto& note) {
        require(note.text == "Survives unrelated topology", "Topology change lost draft text.");
        node.notes = note.text; return true;
    }), "A topology-surviving draft could not be committed.");
    frame(); frame();

    acknowledge = false;
    edit("Pending note survives unrelated topology");
    click(390, 220);
    require(notes.HasPendingCommit(), "Outside click did not queue the topology test note.");
    secondExists = false; ++noteRevision; notes.InvalidateLayouts(); frame();
    require(notes.HasPendingCommit(), "Unrelated removal discarded a queued note.");
    require(notes.CommitPending([&](const auto& note) {
        require(note.text == "Pending note survives unrelated topology", "Queued topology draft text was lost.");
        node.notes = note.text; return true;
    }), "A topology-surviving queued note could not be acknowledged.");
    secondExists = true; acknowledge = true; frame(); frame();

    edit("Must not enter replacement");
    node.instanceUuid = "replacement-node";
    frame();
    require(!notes.Editing() && !notes.HasPendingCommit() && node.notes == "Pending note survives unrelated topology",
        "A removed/replaced target received the old node's draft.");
    edit("Must not survive explicit owner reset");
    notes.Reset();
    require(!notes.Editing() && !notes.HasPendingCommit() && !notes.Revealed(),
        "Explicit owner teardown retained an uncommitted note.");
    std::cout << "Node notes transition interactions passed\n";
}
