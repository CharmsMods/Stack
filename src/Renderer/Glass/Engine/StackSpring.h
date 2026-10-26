// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Animation/ScalarChannel.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stack::glass {
struct SpringTuning { double frequency_hz=8.0, damping_ratio=1.0; };

// Adapt the v0.3 retained motion model to Stack's existing spring solver.
// No second integration runtime or clock. Copies remain pure sample state.
class ScalarSpring {
public:
    explicit ScalarSpring(double initial=0) { reset(initial); }
    double value() const noexcept { return channel_.Value(); }
    double velocity() const noexcept { return pending_?initialVelocity_:channel_.Velocity(); }
    double target() const noexcept { return target_; }
    void set_target(double target) { require(target);target_=float(target); }
    void reset(double value,double velocity=0) {
        require(value);require(velocity);channel_.Reset(float(value));target_=float(value);
        initialVelocity_=float(velocity);pending_=velocity!=0;configured_=false;
    }
    void snap_to_target() noexcept { channel_.Snap(target_);pending_=false; }
    double step(double seconds,SpringTuning tuning={},bool reduced=false) {
        require(seconds);require(tuning.frequency_hz);require(tuning.damping_ratio);
        if(seconds<0 || tuning.frequency_hz<=0 || tuning.damping_ratio<0)
            throw std::invalid_argument("invalid glass spring step");
        if(reduced){snap_to_target();return value();}
        if(seconds==0 || (!pending_ && !channel_.IsActive() && target_==channel_.Value()))return value();
        if(!configured_ || target_!=channel_.Target() || pending_ ||
            tuning.frequency_hz!=tuning_.frequency_hz || tuning.damping_ratio!=tuning_.damping_ratio) {
            auto spec=Stack::Animation::MotionSpec::Spring(float(tuning.frequency_hz),float(tuning.damping_ratio),1.e-6f);
            spec.velocityEpsilon=1.e-6f;
            channel_.SpringTo(target_,float(velocity()),spec);
            tuning_=tuning;configured_=true;pending_=false;
        }
        // ScalarChannel bounds one advance to .25s. The retained model owns
        // stall handling; consume its accepted interval without dropping time.
        while(seconds>0 && channel_.IsActive()) {
            const double dt=std::min(seconds,.25);channel_.Advance(dt);seconds-=dt;
        }
        return value();
    }
    bool settled(double distance,double speed) const noexcept {
        return distance>=0 && speed>=0 && std::abs(value()-target_)<=distance && std::abs(velocity())<=speed;
    }
private:
    static void require(double v) { if(!std::isfinite(v))throw std::invalid_argument("nonfinite glass spring input"); }
    Stack::Animation::ScalarChannel channel_;
    SpringTuning tuning_{};
    float target_=0,initialVelocity_=0;
    bool pending_=false,configured_=false;
};
}
