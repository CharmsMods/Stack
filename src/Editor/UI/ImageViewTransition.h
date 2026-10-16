#pragma once
#include <imgui.h>
#include <algorithm>
#include <string>

namespace Stack::Editor {
// Geometry only. The editor retains textures and its independent RAW camera.
class ImageViewTransition {
public:
    struct Bounds { ImVec2 minimum{}, maximum{}; };
    enum class View { Raw, Graph };
    Bounds Present(View next, const std::string& source, Bounds target, double now, bool reducedMotion) {
        if (!valid || identity != source) {
            valid = true;
            identity = source;
            view = next;
            displayed = destination = target;
            active = false;
            return displayed;
        }
        const bool resizedGraph = next == View::Graph &&
            (target.minimum.x != destination.minimum.x || target.minimum.y != destination.minimum.y ||
             target.maximum.x != destination.maximum.x || target.maximum.y != destination.maximum.y);
        if (view != next || resizedGraph) {
            origin = displayed;
            destination = target;
            started = now;
            active = true;
            view = next;
        }
        if (reducedMotion) active = false;
        if (!active) return displayed = destination = target;
        destination = target;
        const float t = float(std::clamp((now - started) / .24, 0.0, 1.0));
        const float a = t * t * (3 - 2 * t);
        const auto mix = [a](ImVec2 from, ImVec2 to) {
            return ImVec2(from.x + (to.x - from.x) * a, from.y + (to.y - from.y) * a);
        };
        displayed = {mix(origin.minimum, destination.minimum), mix(origin.maximum, destination.maximum)};
        if (t >= 1) active = false;
        return displayed;
    }
    bool Active() const { return active; }
    void Reset() { *this = {}; }
private:
    bool valid = false, active = false;
    std::string identity;
    View view = View::Raw;
    Bounds origin{},destination{},displayed{};
    double started = 0;
};
}
