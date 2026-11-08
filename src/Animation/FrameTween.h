#pragma once
#include "ScalarChannel.h"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace Stack::Animation {
// Presentation-owner clock adapter. A newly accepted target cannot consume a
// stall from before its trigger. Multiple drawing consumers advance only once.
class FrameTween {
public:
    static double ClockSeconds() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    void Update(float& value,float target,float duration,double now,int frame,bool reduced=false) {
        if(!initialized_ || value!=channel_.Value()) {
            channel_.Reset(value);initialized_=true;lastTime_=now;
        }
        if(target!=channel_.Target()) {
            channel_.Retarget(target,MotionSpec::Tween(duration,Easing::EaseOut));
            started_=lastTime_=now;duration_=std::max(.001f,duration);
            intermediate_=false;
        }
        if(reduced)channel_.Finish();
        else if(channel_.IsActive() && frame!=frame_) {
            const double elapsed=std::max(0.,now-lastTime_);
            const bool hadIntermediate=intermediate_;
            channel_.Advance(intermediate_ ? elapsed : std::min(elapsed,duration_*.5));
            if(elapsed>0)intermediate_=true;
            // Even if the next frame stalls beyond the complete duration,
            // submit an intermediate pose before retiring this transition.
            if(hadIntermediate && now-started_>=duration_)channel_.Finish();
        }
        value=channel_.Value();lastTime_=now;frame_=frame;
    }
    bool Active() const {return channel_.IsActive();}
    void Reset(float value) {channel_.Reset(value);initialized_=false;frame_=-1;}
private:
    ScalarChannel channel_;
    double started_=0,lastTime_=0,duration_=.2;
    int frame_=-1;
    bool initialized_=false,intermediate_=false;
};
}
