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
    void Observe(ProjectGraphState state, bool gesture) {
        if (!m_Current) { Reset(std::move(state)); return; }
        if (gesture) {
            if (!m_Gesture) m_Gesture = *m_Current;
        } else if (m_Gesture) EndGesture();
        else PushUndo(*m_Current);
        m_Current = std::move(state); m_Redo.clear();
        PruneImageBytes();
    }
    // A result completing the current owned request replaces its pending
    // state within the initiating action, without another undo entry.
    void CompleteAsyncEdit(ProjectGraphState state) {
        if (!m_Current) { Reset(std::move(state)); return; }
        m_Current=std::move(state);m_Redo.clear();PruneImageBytes();
    }
    void EndGesture() {
        if (m_Gesture) { PushUndo(std::move(*m_Gesture)); m_Gesture.reset(); }
    }
    bool CanUndo() const { return m_Gesture.has_value() || !m_Undo.empty(); }
    bool CanRedo() const { return !m_Redo.empty(); }
    bool GestureActive() const { return m_Gesture.has_value(); }
    const ProjectGraphState* CancelCandidate() const { return m_Gesture ? &*m_Gesture : nullptr; }
    void AcceptCancel() { m_Current = std::move(m_Gesture); m_Gesture.reset(); }
    const ProjectGraphState* Candidate(bool redo) {
        EndGesture();
        const auto& stack = redo ? m_Redo : m_Undo;
        return stack.empty() ? nullptr : &stack.back();
    }
    void AcceptRestore(bool redo) {
        auto& from = redo ? m_Redo : m_Undo;
        auto& to = redo ? m_Undo : m_Redo;
        to.push_back(std::move(*m_Current));
        m_Current = std::move(from.back()); from.pop_back();
    }
private:
    void PushUndo(ProjectGraphState state) {
        if (m_Undo.size() == 64) m_Undo.pop_front();
        m_Undo.push_back(std::move(state));
    }
    void PruneImageBytes() {
        for (auto entry=m_ImageBytes.begin();entry!=m_ImageBytes.end();)
            if (entry->second.expired()) entry=m_ImageBytes.erase(entry); else ++entry;
    }
    std::optional<ProjectGraphState> m_Current;
    std::optional<ProjectGraphState> m_Gesture;
    std::deque<ProjectGraphState> m_Undo, m_Redo;
    std::unordered_map<std::string,std::weak_ptr<const std::vector<unsigned char>>> m_ImageBytes;
};
}
