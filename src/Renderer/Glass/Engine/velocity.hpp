#pragma once

// Original Stack glass engine machinery. Units are logical UI units / seconds.
// No input polling, pixel reads, windowing, or renderer dependency.
#include "../glass_math.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stack::glass::motion {

enum class SampleStatus { Accepted, CoalescedTimestamp, OutOfOrder, Invalid };

struct VelocityTuning {
    double velocityHalfLife = 0.022;
    double accelerationHalfLife = 0.040;
    double staleDelay = 0.018;
    double staleHalfLife = 0.035;
    double accelerationDecayHalfLife = 0.030;
    double discontinuitySeconds = 0.200;
    float maximumSpeed = 6000.0f;
    float maximumAcceleration = 100000.0f;
    float stationaryEpsilon = 0.00001f;

    bool valid() const {
        const auto time = [](double v) { return std::isfinite(v) && v > 0 && v <= 10; };
        return time(velocityHalfLife) && time(accelerationHalfLife) &&
               std::isfinite(staleDelay) && staleDelay >= 0 && staleDelay <= 1 &&
               time(staleHalfLife) && time(accelerationDecayHalfLife) &&
               time(discontinuitySeconds) && std::isfinite(maximumSpeed) &&
               maximumSpeed > 0 && maximumSpeed <= 1.0e7f &&
               std::isfinite(maximumAcceleration) && maximumAcceleration > 0 &&
               maximumAcceleration <= 1.0e9f && std::isfinite(stationaryEpsilon) &&
               stationaryEpsilon >= 0 && stationaryEpsilon <= 1;
    }
};

struct Kinematics { Vec2 velocity{}, acceleration{}; };

inline Vec2 boundedVector(Vec2 v, float maximum) {
    const double magnitude = std::hypot(static_cast<double>(v.x), static_cast<double>(v.y));
    return magnitude > maximum ? v * static_cast<float>(maximum / magnitude) : v;
}

// A display does not have to sample at the pointer event rate. Queries are pure;
// the time since the last *movement*, not the last UI submission, drains stale
// velocity. Identical positions cannot hold a shape stretched indefinitely.
class VelocityEstimator {
public:
    explicit VelocityEstimator(VelocityTuning tuning = {}) : tuning_(tuning) {
        if (!tuning.valid()) throw std::invalid_argument("invalid velocity tuning");
    }

    static bool validTimestamp(double t) {
        return std::isfinite(t) && t >= 0 && t <= 1.0e9;
    }
    static bool validPosition(Vec2 p) {
        return finite(p) && std::abs(p.x) <= 1.0e7f && std::abs(p.y) <= 1.0e7f;
    }

    SampleStatus reset(double timestamp, Vec2 position) {
        if (!validTimestamp(timestamp) || !validPosition(position)) return SampleStatus::Invalid;
        initialized_ = true;
        lastSampleTime_ = lastMovementTime_ = timestamp;
        lastPosition_ = position;
        filtered_ = {};
        return SampleStatus::Accepted;
    }

    SampleStatus submit(double timestamp, Vec2 position) {
        if (!validTimestamp(timestamp) || !validPosition(position)) return SampleStatus::Invalid;
        if (!initialized_) return reset(timestamp, position);
        if (timestamp < lastSampleTime_) return SampleStatus::OutOfOrder;
        if (timestamp == lastSampleTime_) {
            // Last position wins, but zero dt never generates a derivative.
            lastPosition_ = position;
            return SampleStatus::CoalescedTimestamp;
        }
        const double dt = timestamp - lastSampleTime_;
        const Vec2 delta = position - lastPosition_;
        lastPosition_ = position;
        lastSampleTime_ = timestamp;
        if (std::hypot(delta.x, delta.y) <= tuning_.stationaryEpsilon) return SampleStatus::Accepted;
        // A repeated stationary UI submission must not rewrite the movement
        // history: MotionState may still evaluate its earlier decay interval.
        // Only an actual movement after a long sampling gap is a discontinuity.
        if (dt > tuning_.discontinuitySeconds) return reset(timestamp, position);

        const Kinematics old = sample(timestamp);
        const double deltaLength = std::hypot(static_cast<double>(delta.x), static_cast<double>(delta.y));
        // Clamp the scalar speed before reconstructing the vector. Forming
        // delta/dt first can overflow at a valid subnormal dt (inf * 0 -> NaN).
        const double rawSpeed = std::min(deltaLength / dt, static_cast<double>(tuning_.maximumSpeed));
        const Vec2 raw{static_cast<float>((delta.x / deltaLength) * rawSpeed),
                       static_cast<float>((delta.y / deltaLength) * rawSpeed)};
        const float av = static_cast<float>(-std::expm1(-ln2 * dt / tuning_.velocityHalfLife));
        const Vec2 next = old.velocity + (raw - old.velocity) * av;
        const double dvx = static_cast<double>(next.x) - old.velocity.x;
        const double dvy = static_cast<double>(next.y) - old.velocity.y;
        const double dvLength = std::hypot(dvx, dvy);
        const double acceleration = std::min(dvLength / dt, static_cast<double>(tuning_.maximumAcceleration));
        const Vec2 rawAcceleration = dvLength > 0 ? Vec2{static_cast<float>((dvx / dvLength) * acceleration),
            static_cast<float>((dvy / dvLength) * acceleration)} : Vec2{};
        const float aa = static_cast<float>(-std::expm1(-ln2 * dt / tuning_.accelerationHalfLife));
        filtered_.velocity = next;
        filtered_.acceleration = old.acceleration + (rawAcceleration - old.acceleration) * aa;
        lastMovementTime_ = timestamp;
        return SampleStatus::Accepted;
    }

    Kinematics sample(double timestamp) const {
        if (!initialized_ || !validTimestamp(timestamp) || timestamp < lastMovementTime_) return {};
        const double age = timestamp - lastMovementTime_;
        const double decayAge = std::max(0.0, age - tuning_.staleDelay);
        const float velocityDecay = static_cast<float>(std::exp(-ln2 * decayAge / tuning_.staleHalfLife));
        const float accelerationDecay = static_cast<float>(std::exp(-ln2 * age / tuning_.accelerationDecayHalfLife));
        return {filtered_.velocity * velocityDecay, filtered_.acceleration * accelerationDecay};
    }

    bool initialized() const { return initialized_; }
    bool setTuning(VelocityTuning tuning) {
        if (!tuning.valid()) return false;
        tuning_ = tuning;
        filtered_.velocity = boundedVector(filtered_.velocity, tuning.maximumSpeed);
        filtered_.acceleration = boundedVector(filtered_.acceleration, tuning.maximumAcceleration);
        return true;
    }
    double lastSampleTime() const { return lastSampleTime_; }
    double lastMovementTime() const { return lastMovementTime_; }

private:
    static constexpr double ln2 = 0.69314718055994530942;
    VelocityTuning tuning_;
    bool initialized_ = false;
    double lastSampleTime_ = 0;
    double lastMovementTime_ = 0;
    Vec2 lastPosition_{};
    Kinematics filtered_{};
};

} // namespace stack::glass::motion
