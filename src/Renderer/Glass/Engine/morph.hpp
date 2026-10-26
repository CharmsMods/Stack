#pragma once
#include "StackSpring.h"
#include "../glass_math.hpp"
#include <array>

namespace stack::glass::engine {

struct Shape {
    Vec2 center{};
    Vec2 halfSize{30, 25};
    float radius = 25;
    bool valid() const {
        return finite(center) && std::abs(center.x) <= 1.0e6f && std::abs(center.y) <= 1.0e6f &&
            finite(halfSize) && halfSize.x >= 0.01f && halfSize.y >= 0.01f &&
            halfSize.x <= 1.0e5f && halfSize.y <= 1.0e5f && std::isfinite(radius) &&
            radius >= 0 && radius <= std::min(halfSize.x, halfSize.y);
    }
};
enum class Placement { Direct, Spring };

// Shape values are retained independently of input. Positions may be direct
// (slider/graph semantics) or spring-driven (selection/matched-shape transitions).
// Size is animated in log space so interrupted morphs never produce negative
// extents. Radius is an animated proportion of the smaller half extent.
class ShapeMotion {
public:
    void submit(double time, Shape target, Placement placement, Vec2 velocity,
                SpringTuning positionSpring, SpringTuning morphSpring, bool reduceMotion) {
        const auto validTuning=[](SpringTuning t){return std::isfinite(t.frequency_hz)&&t.frequency_hz>=.5&&t.frequency_hz<=100&&std::isfinite(t.damping_ratio)&&t.damping_ratio>=.45&&t.damping_ratio<=5;};
        if (!target.valid() || !std::isfinite(time) || time < 0 || time>1.0e9 ||
            !finite(velocity)||std::abs(velocity.x)>1.0e6f||std::abs(velocity.y)>1.0e6f||
            !validTuning(positionSpring)||!validTuning(morphSpring)||
            (placement!=Placement::Direct&&placement!=Placement::Spring)||
            (initialized_ && time < time_)) throw std::invalid_argument("invalid shape event");
        if (initialized_) advance(time);
        const bool wasDirect = placement_ == Placement::Direct;
        placement_ = placement; target_ = target; positionSpring_ = positionSpring;
        morphSpring_ = morphSpring; reduceMotion_ = reduceMotion;
        if (!initialized_) {
            x_.reset(target.center.x); y_.reset(target.center.y);
            width_.reset(std::log(target.halfSize.x)); height_.reset(std::log(target.halfSize.y));
            radius_.reset(target.radius / std::min(target.halfSize.x,target.halfSize.y));
            initialized_ = true; time_ = time;
        } else if (placement == Placement::Spring && wasDirect) {
            // The last direct position is the starting point; inherit the drag
            // velocity, not a restarted easing curve. Caller may choose zero.
            x_.reset(x_.value(), velocity.x); y_.reset(y_.value(), velocity.y);
        }
        x_.set_target(target.center.x); y_.set_target(target.center.y);
        width_.set_target(std::log(target.halfSize.x)); height_.set_target(std::log(target.halfSize.y));
        radius_.set_target(target.radius / std::min(target.halfSize.x,target.halfSize.y));
        if (placement == Placement::Direct) { x_.reset(target.center.x,velocity.x); y_.reset(target.center.y,velocity.y); }
        if (reduceMotion) snap();
    }
    Shape sample(double time) const {
        if (!initialized_ || time < time_ || !std::isfinite(time)) return {};
        auto copy = *this; copy.advance(time);
        Shape out;
        out.center = {static_cast<float>(copy.x_.value()),static_cast<float>(copy.y_.value())};
        out.halfSize = {std::max(.01f,static_cast<float>(std::exp(copy.width_.value()))),std::max(.01f,static_cast<float>(std::exp(copy.height_.value())))};
        out.radius = clamp(static_cast<float>(copy.radius_.value()),0,1)*std::min(out.halfSize.x,out.halfSize.y);
        return out;
    }
    bool settled(double time) const {
        auto c=*this; c.advance(time);
        return (placement_==Placement::Direct || (c.x_.settled(.01,.05)&&c.y_.settled(.01,.05))) &&
            c.width_.settled(.0001,.001)&&c.height_.settled(.0001,.001)&&c.radius_.settled(.0001,.001);
    }
private:
    void snap() { x_.snap_to_target();y_.snap_to_target();width_.snap_to_target();height_.snap_to_target();radius_.snap_to_target(); }
    void advance(double time) {
        if (!initialized_ || time <= time_) return;
        const double dt=time-time_;
        if (placement_==Placement::Spring) { x_.step(dt,positionSpring_,reduceMotion_);y_.step(dt,positionSpring_,reduceMotion_); }
        width_.step(dt,morphSpring_,reduceMotion_);height_.step(dt,morphSpring_,reduceMotion_);radius_.step(dt,morphSpring_,reduceMotion_);
        time_=time;
    }
    ScalarSpring x_,y_,width_,height_,radius_;
    double time_=0;
    Shape target_{};
    Placement placement_=Placement::Direct;
    SpringTuning positionSpring_{7,.82},morphSpring_{8,.84};
    bool initialized_=false,reduceMotion_=false;
};

} // namespace stack::glass::engine
