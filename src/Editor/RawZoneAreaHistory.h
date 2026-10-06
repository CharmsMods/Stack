#pragma once
#include "Raw/RawZoneArea.h"
#include <deque>

namespace Stack::Editor {
// One entry per completed gesture. Coverage and gain share the same history.
class RawZoneAreaHistory {
public:
    using Areas = std::vector<RawRecipe::RawZoneArea>;
    void Initialize(const Areas& areas) {
        if (!m_Initialized) {m_Current = areas; m_Initialized = true;}
    }
    void Observe(const Areas& areas, bool active) {
        Initialize(areas);
        if (active) {m_Gesture = true; return;}
        if (!RawRecipe::EqualZoneAreas(m_Current, areas)) {
            m_Undo.push_back(std::move(m_Current)); m_Redo.clear();
            if (m_Undo.size() > 32) m_Undo.pop_front();
            m_Current = areas;
        }
        m_Gesture = false;
    }
    Areas Cancel() {
        m_Gesture = false;
        return m_Current;
    }
    bool CanUndo() const {return !m_Gesture && !m_Undo.empty();}
    bool CanRedo() const {return !m_Gesture && !m_Redo.empty();}
    Areas Undo() {
        if (CanUndo()) {m_Redo.push_back(m_Current); m_Current = std::move(m_Undo.back()); m_Undo.pop_back();}
        return m_Current;
    }
    Areas Redo() {
        if (CanRedo()) {m_Undo.push_back(m_Current); m_Current = std::move(m_Redo.back()); m_Redo.pop_back();}
        return m_Current;
    }
private:
    bool m_Initialized = false;
    bool m_Gesture = false;
    Areas m_Current;
    std::deque<Areas> m_Undo, m_Redo;
};
}
