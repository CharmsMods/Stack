#pragma once
#include <algorithm>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
struct GalleryScrollMotion {
    float current = -1.f, lastApplied = -1.f, overshoot = 0.f, rebound = 0.f;
    double lastWheel = -100.;
    float run = 0.f, direction = 0.f;
    int frame = -2;
    void Observe(float native, float& target, int nextFrame) {
        if (frame != nextFrame-1 || current < 0.f || std::abs(native-lastApplied)>1.1f) {
            current = target = native; overshoot = rebound = 0.f;
        }
        frame = nextFrame;
    }
    void Wheel(float wheel, float step, float maximum, double now, float& target) {
        if (now-lastWheel>.26 || direction*wheel<0.f) run=0.f;
        run=std::min(8.f,run+std::abs(wheel)); direction=wheel; lastWheel=now;
        const float delta=-wheel*step*(1.f+.055f*run);
        if ((target-current)*delta<0.f) target=current;
        const float requested=target+delta;
        target=std::clamp(requested,0.f,maximum);
        if (requested!=target) overshoot=std::clamp(overshoot+requested-target,-700.f,700.f);
        else if (overshoot*delta<0.f) overshoot=0.f;
    }
    void Step(float& target, float maximum, float dt, double now, bool reduced) {
        target=std::clamp(target,0.f,maximum);
        const float response=1.f-std::exp(-std::clamp(dt,0.f,.05f)/.095f);
        current=reduced ? target : current+(target-current)*response;
        if(std::abs(target-current)<.08f)current=target;
        current=std::clamp(current,0.f,maximum);
        lastApplied=current;
        if(now-lastWheel>.10) {
            overshoot*=std::exp(-std::clamp(dt,0.f,.05f)*22.f);
            if(std::abs(overshoot)<.08f)overshoot=0.f;
        }
        rebound=reduced ? 0.f : -64.f*overshoot/(64.f+std::abs(overshoot));
    }
};
}
