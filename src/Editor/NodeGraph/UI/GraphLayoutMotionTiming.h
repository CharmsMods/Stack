#pragma once
#include <algorithm>
namespace Stack::Editor::GraphLayoutMotionTiming {
constexpr double Room=.22, Reveal=.20, Sweep=.16, Trace=.20;
constexpr double Wires=Room+Reveal, Total=Wires+Sweep+Trace;
// A finite spring-like settle: a small pull past the destination, then rest.
// Cap the excess in callers so large layout changes never produce large swings.
inline float ElasticProgress(double elapsed, double duration) {
    const float t=float(std::clamp(elapsed/duration,0.0,1.0));
    const float u=t-1.f;
    return 1.f+1.65f*u*u*u+.65f*u*u;
}
inline float TraceProgress(double elapsed,float column) {
    const float t=float(std::clamp((elapsed-Wires-column*Sweep)/Trace,0.0,1.0));
    return t*t*(3-2*t);
}
}
