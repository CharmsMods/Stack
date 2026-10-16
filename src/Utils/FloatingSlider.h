#pragma once
#include "Utils/MorphingLabel.h"
#include <unordered_map>
#include <cstdint>

namespace ImGuiExtras {

// Owned by the editing surface. No parameter pointers are retained between frames.
struct FloatingSliderState {
    struct Readout { MorphingLabel label; int lastFrame = 0; };
    std::unordered_map<ImGuiID, Readout> readouts;
};

// Opt-in presentation for RAW. Existing NodeSlider callers keep their graph style.
class FloatingSliderScope {
public:
    explicit FloatingSliderScope(FloatingSliderState& state);
    ~FloatingSliderScope();
    FloatingSliderScope(const FloatingSliderScope&) = delete;
    FloatingSliderScope& operator=(const FloatingSliderScope&) = delete;
private:
    FloatingSliderState* previous_;
};

bool HasFloatingSliderScope();
bool FloatingSliderFloat(const char* label, const char* id, float* value,
    float minimum, float maximum, const char* format, float width, bool bounded = true);
bool FloatingSliderInt(const char* label, const char* id, int* value,
    int minimum, int maximum, const char* format, float width);
bool FloatingSliderDouble(const char* label, const char* id, double* value,
    double minimum, double maximum, const char* format, float width);
bool FloatingSliderInteger(const char* label, const char* id, std::int64_t* value,
    std::int64_t minimum, std::int64_t maximum, const char* format, float width);

} // namespace ImGuiExtras
