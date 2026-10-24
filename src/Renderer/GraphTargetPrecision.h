#pragma once

namespace Stack::Renderer {
// Invalidate resources while the old format still describes their allocation.
// A successfully retained external dependency supplies a conservative floor.
template <class Invalidate>
bool ReconfigureGraphTargetPrecision(bool& currentFloat32, bool requiredFloat32,
    bool seededFloat32, Invalidate&& invalidate) {
    const bool nextFloat32 = requiredFloat32 || seededFloat32;
    if (nextFloat32 == currentFloat32) return false;
    invalidate();
    currentFloat32 = nextFloat32;
    return true;
}
}
