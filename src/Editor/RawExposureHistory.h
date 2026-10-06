#pragma once

#include <deque>
#include <string>

namespace Stack::Editor {

// Exposure-only history leaves later curve, camera and area edits intact.
// A held pointer contributes one entry when the gesture completes.
class RawExposureHistory {
public:
    bool SetSource(const std::string& source, float value) {
        if (m_Source == source && m_Initialized) return false;
        m_Source = source;
        m_Current = value;
        m_Initialized = true;
        m_Gesture = false;
        m_Undo.clear();
        m_Redo.clear();
        return true;
    }
    void Observe(float value, bool active) {
        if (active) { m_Gesture = true; return; }
        if (value != m_Current) {
            m_Undo.push_back(m_Current);
            if (m_Undo.size() > 32) m_Undo.pop_front();
            m_Redo.clear();
            m_Current = value;
        }
        m_Gesture = false;
    }
    bool CanUndo() const { return !m_Gesture && !m_Undo.empty(); }
    bool CanRedo() const { return !m_Gesture && !m_Redo.empty(); }
    float Undo() {
        if (CanUndo()) {
            m_Redo.push_back(m_Current);
            m_Current = m_Undo.back();
            m_Undo.pop_back();
        }
        return m_Current;
    }
    float Redo() {
        if (CanRedo()) {
            m_Undo.push_back(m_Current);
            m_Current = m_Redo.back();
            m_Redo.pop_back();
        }
        return m_Current;
    }
private:
    std::string m_Source;
    bool m_Initialized = false;
    bool m_Gesture = false;
    float m_Current = 0.0f;
    std::deque<float> m_Undo;
    std::deque<float> m_Redo;
};

} // namespace Stack::Editor
