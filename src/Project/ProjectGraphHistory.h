#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/Timeline/TimelineAnimation.h"
#include <deque>
#include <optional>
#include <unordered_map>
#include "Utils/SharedPixelBuffer.h"

namespace Stack::Project {
// History owns authored CPU state. Runtime effects and GPU resources are
// reconstructed only after a restore passes the ordinary graph edit boundary.
struct ProjectGraphState {
    struct ImageStorage {
        SharedPixelBuffer pixels;
        std::shared_ptr<const std::vector<unsigned char>> png, preview;
        int previewWidth=0, previewHeight=0, previewChannels=4;
    };
    EditorNodeGraph::Graph graph;
    Timeline::TimelineAnimationState animation;
    std::unordered_map<std::string,ImageStorage> images;
};

class ProjectGraphHistory {
public:
    void Reset(ProjectGraphState state) {
        m_ImageBytes.clear();
        for (const auto& image:state.images) {
            if (image.second.png) m_ImageBytes[image.first+"/png"]=image.second.png;
            if (image.second.preview) m_ImageBytes[image.first+"/preview"]=image.second.preview;
        }
        m_Current = std::move(state); m_Undo.clear(); m_Redo.clear(); m_Gesture.reset();
    }
    void Begin(ProjectGraphState state) { if (!m_Current) m_Current = std::move(state); }
    bool HasBaseline() const { return m_Current.has_value(); }
    // Reconcile a presentation-only restore with current accepted async state
    // without dropping the redo entry created by AcceptRestore.
    void RefreshCurrent(ProjectGraphState state) { m_Current=std::move(state); }
    std::shared_ptr<const std::vector<unsigned char>> RetainImageBytes(const std::string& key,
        const std::vector<unsigned char>& bytes) {
        if (bytes.empty()) return {};
        auto& stored=m_ImageBytes[key];
        auto retained=stored.lock();
        if (!retained || *retained!=bytes) {
            retained=std::make_shared<const std::vector<unsigned char>>(bytes);
            stored=retained;
        }
        return retained;
    }
    void Observe(ProjectGraphState state, bool gesture, bool affectsPixels = true) {
        if (!m_Current) { Reset(std::move(state)); return; }
        if (gesture) {
            if (!m_Gesture) {m_Gesture = *m_Current;m_GestureAffectsPixels=false;}
            m_GestureAffectsPixels |= affectsPixels;
        } else if (m_Gesture) EndGesture();
        else { PushUndo(*m_Current,affectsPixels); m_Redo.clear(); }
        // A live gesture is provisional. Preserve the redo branch until it
        // commits, so Escape can restore both the document and its history.
        m_Current = std::move(state);
        PruneImageBytes();
    }
    // A result completing the current owned request replaces its pending
    // state within the initiating action, without another undo entry.
    void CompleteAsyncEdit(ProjectGraphState state) {
        if (!m_Current) { Reset(std::move(state)); return; }
        m_Current=std::move(state);m_Redo.clear();PruneImageBytes();
    }
    // Async source resolution belongs to the existing import action, including
    // snapshots made while it was pending. Never create an undo step or erase
    // a redo branch merely because its owned source finished loading.
    template<class Reconcile> void ReconcileImageImport(Reconcile reconcile) {
        if (m_Current) reconcile(*m_Current);
        if (m_Gesture) reconcile(*m_Gesture);
        for (auto& entry:m_Undo) reconcile(entry.state);
        for (auto& entry:m_Redo) reconcile(entry.state);
        PruneImageBytes();
    }
    void EndGesture() {
        if (m_Gesture) {
            PushUndo(std::move(*m_Gesture),m_GestureAffectsPixels);
            m_Gesture.reset(); m_Redo.clear();
        }
    }
    bool CanUndo() const { return m_Gesture.has_value() || !m_Undo.empty(); }
    bool CanRedo() const { return !m_Gesture && !m_Redo.empty(); }
    bool GestureActive() const { return m_Gesture.has_value(); }
    const ProjectGraphState* CancelCandidate() const { return m_Gesture ? &*m_Gesture : nullptr; }
    bool CancelAffectsPixels() const { return m_GestureAffectsPixels; }
    void AcceptCancel() {
        if (m_Gesture) { m_Current = std::move(m_Gesture); m_Gesture.reset(); }
    }
    const ProjectGraphState* Candidate(bool redo) {
        if (redo && m_Gesture) return nullptr;
        EndGesture();
        const auto& stack = redo ? m_Redo : m_Undo;
        return stack.empty() ? nullptr : &stack.back().state;
    }
    bool CandidateAffectsPixels(bool redo) const {
        const auto& stack = redo ? m_Redo : m_Undo;
        return !stack.empty() && stack.back().affectsPixels;
    }
    void AcceptRestore(bool redo) {
        auto& from = redo ? m_Redo : m_Undo;
        auto& to = redo ? m_Undo : m_Redo;
        to.push_back({std::move(*m_Current),from.back().affectsPixels});
        m_Current = std::move(from.back().state); from.pop_back();
    }
private:
    struct Entry { ProjectGraphState state; bool affectsPixels; };
    void PushUndo(ProjectGraphState state, bool affectsPixels = true) {
        if (m_Undo.size() == 64) m_Undo.pop_front();
        m_Undo.push_back({std::move(state),affectsPixels});
    }
    void PruneImageBytes() {
        for (auto entry=m_ImageBytes.begin();entry!=m_ImageBytes.end();)
            if (entry->second.expired()) entry=m_ImageBytes.erase(entry); else ++entry;
    }
    std::optional<ProjectGraphState> m_Current;
    std::optional<ProjectGraphState> m_Gesture;
    bool m_GestureAffectsPixels = false;
    std::deque<Entry> m_Undo, m_Redo;
    std::unordered_map<std::string,std::weak_ptr<const std::vector<unsigned char>>> m_ImageBytes;
};
}
