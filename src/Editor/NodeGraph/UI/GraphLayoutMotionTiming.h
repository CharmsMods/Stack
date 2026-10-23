#pragma once
#include <algorithm>
namespace Stack::Editor::GraphLayoutMotionTiming {
constexpr double Room=.22, Reveal=.20, Sweep=.16, Trace=.20;
constexpr double Wires=Room+Reveal, Total=Wires+Sweep+Trace;
inline float TraceProgress(double elapsed,float column) {
    const float t=float(std::clamp((elapsed-Wires-column*Sweep)/Trace,0.0,1.0));
    return t*t*(3-2*t);
}
}
