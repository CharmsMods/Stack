#pragma once

// Timestamped, retained motion for selected Stack UI surfaces. This is an
// authored model informed by public references, not Apple's implementation.
#include "velocity.hpp"
#include "StackSpring.h"
#include <array>
#include <cstdint>

namespace stack::glass::motion {

enum class InputModality { Mouse, Touch, Pen, Keyboard };
enum class EventKind { Update, Cancel, Discontinuity };
enum class SubmitStatus { Accepted, CoalescedTimestamp, OutOfOrder, InvalidInput };

struct MotionInput {
    // UI-space, top-left origin, logical units. center is the functional anchor;
    // it is never passed through a spring. Renderer converts to framebuffer px.
    Vec2 center{};
    Vec2 halfSize{12, 12};
    Vec2 pointer{};
    bool hasPointer = false;
    bool hovered = false;
    bool pressed = false;
    bool dragging = false;
    bool focused = false;
    bool enabled = true;
    bool visible = true;
    bool reduceMotion = false;
    InputModality modality = InputModality::Mouse;
};

struct MotionTuning {
    VelocityTuning velocity{};
    SpringTuning hoverSpring{9.0, 1.0};
    SpringTuning pressSpring{12.0, 0.84};
    SpringTuning releaseSpring{7.5, 0.78};
    SpringTuning deformationSpring{9.5, 0.78};
    SpringTuning contactSpring{11.0, 1.0};
    SpringTuning materialSpring{14.0, 1.0};
    SpringTuning visibilitySpring{10.0, 1.0};
    SpringTuning focusSpring{8.0, 1.0};
    double controlHz = 240.0;
    float hoverGrowth = 0.018f;
    float pressGrowth = 0.23f;
    float maximumLogStrain = 0.18f;
    float strainSpeed = 800.0f;
    float strainAcceleration = 18000.0f;
    float accelerationStrainShare = 0.12f;
    float complianceUnits = 0.80f;
    float maximumTilt = 0.060f;
    float pointerTilt = 0.018f;
    float hoverContactGain = 0.012f;
    float pressContactGain = 0.075f;
    float hoverElevation = 0.10f;
    float pressElevation = 0.55f;
    float idleMaterialActivation = 1.0f;
    float hoverMaterialActivation = 1.0f;
    float pressMaterialActivation = 1.0f;
    float disabledOpacity = 0.48f;
    float mouseIntensity = 1.0f;
    float touchIntensity = 1.12f;
    float penIntensity = 1.05f;
    float keyboardIntensity = 0.55f;

    bool valid() const {
        const auto spring = [](SpringTuning s) {
            return std::isfinite(s.frequency_hz) && s.frequency_hz >= 0.5 && s.frequency_hz <= 100 &&
                   std::isfinite(s.damping_ratio) && s.damping_ratio >= 0.45 && s.damping_ratio <= 5;
        };
        const auto range = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
        return velocity.valid() && spring(hoverSpring) && spring(pressSpring) && spring(releaseSpring) &&
               spring(deformationSpring) && spring(contactSpring) && spring(materialSpring) && spring(visibilitySpring) && spring(focusSpring) &&
               std::isfinite(controlHz) && controlHz >= 60 && controlHz <= 1000 &&
               range(hoverGrowth, 0, 0.20f) && range(pressGrowth, -0.20f, 0.30f) &&
               range(maximumLogStrain, 0, 0.30f) && range(strainSpeed, 1, 100000) &&
               range(strainAcceleration, 1, 1000000) && range(accelerationStrainShare, 0, 1) &&
               range(complianceUnits, 0, 8) && range(maximumTilt, 0, 0.5f) && range(pointerTilt, 0, 0.25f) &&
               range(hoverContactGain, 0, 1) && range(pressContactGain, 0, 1) &&
               range(hoverElevation, 0, 2) && range(pressElevation, 0, 2) &&
               range(idleMaterialActivation, 0, 1) && range(hoverMaterialActivation, 0, 1) &&
               range(pressMaterialActivation, 0, 1) && range(disabledOpacity, 0, 1) &&
               range(mouseIntensity, 0, 2) && range(touchIntensity, 0, 2) &&
               range(penIntensity, 0, 2) && range(keyboardIntensity, 0, 2);
    }
};

struct MotionOutput {
    bool valid = false;
    double timestamp = 0;
    Vec2 center{}, halfSize{12, 12};
    Vec2 centerOffset{}, scale{1, 1}, tilt{}, contactLocal{};
    Vec2 velocity{}, acceleration{};
    // Trace-free symmetric log-deformation S = [[x,y],[y,-x]]. The renderer
    // applies exp(S), whose determinant is one. Diagonal drags retain stretch.
    Vec2 strainTensor{};
    Vec2 releaseVelocity{};
    double releaseTimestamp = -1;
    float logStrain = 0;
    float uniformScale = 1;
    float materialActivation = 1;
    float contactGain = 0;
    float elevation = 0;
    float opacity = 1;
    float hover = 0, press = 0, focus = 0;
    bool settled = true;
};

class MotionState {
public:
    explicit MotionState(MotionTuning tuning = {}) : tuning_(tuning), velocity_(tuning.velocity) {
        if (!tuning_.valid()) throw std::invalid_argument("invalid motion tuning");
    }

    // Events own time. Rendering only calls sample(), which evaluates a copy.
    // Equal event logs therefore give equal trajectories at equal timestamps,
    // whether display sampling runs at 30, 60, 120, 144 Hz, or is irregular.
    SubmitStatus submit(double timestamp, MotionInput input, EventKind kind = EventKind::Update) {
        if (!validInput(timestamp, input) || (kind != EventKind::Update && kind != EventKind::Cancel &&
                                             kind != EventKind::Discontinuity)) return SubmitStatus::InvalidInput;
        if (initialized_ && timestamp < lastSubmitTime_) return SubmitStatus::OutOfOrder;
        const bool duplicate = initialized_ && timestamp == lastSubmitTime_;
        if (kind == EventKind::Cancel || !input.enabled || !input.visible) {
            input.pressed = input.dragging = input.hovered = false;
            if (!input.enabled || !input.visible) input.focused = false;
        }
        if (!input.pressed) input.dragging = false;
        if (input.modality == InputModality::Keyboard) input.hasPointer = false;
        // Immediate-mode UI commonly resubmits unchanged state every frame.
        // Do not introduce extra integration boundaries for those submissions.
        // Still retain stationary pointer sample times for the next derivative.
        if (initialized_ && kind == EventKind::Update && sameInput(input, input_)) {
            if (input.hasPointer) velocity_.submit(timestamp, input.pointer);
            lastSubmitTime_ = timestamp;
            return duplicate ? SubmitStatus::CoalescedTimestamp : SubmitStatus::Accepted;
        }
        if (initialized_) advance(timestamp);
        const bool newContact = input.pressed && (!initialized_ || !input_.pressed);
        const bool handoff = initialized_ && input_.pressed && !input.pressed && input_.dragging &&
            kind == EventKind::Update && input.enabled && input.visible && input.modality == input_.modality;
        const Vec2 velocityBeforeReleaseSample = velocity_.sample(timestamp).velocity;
        if (newContact || kind != EventKind::Update || !input.enabled || !input.visible ||
            (initialized_ && input.modality != input_.modality)) {
            releaseVelocity_ = {};
            releaseTimestamp_ = -1;
        }
        const bool pointerReset = !initialized_ || kind != EventKind::Update || !input.hasPointer ||
                                  !input.enabled || !input.visible || newContact ||
                                  input.modality != input_.modality || !input_.hasPointer;
        if (pointerReset) velocity_.reset(timestamp, input.pointer);
        else velocity_.submit(timestamp, input.pointer);
        if (handoff) {
            // Include a valid final pointer-up sample. If the release has no
            // pointer coordinate, retain the last actual contact estimate.
            releaseVelocity_ = input.hasPointer ? velocity_.sample(timestamp).velocity : velocityBeforeReleaseSample;
            releaseTimestamp_ = timestamp;
        }

        input_ = input;
        lastSubmitTime_ = timestamp;
        if (!initialized_) {
            initialized_ = true;
            epoch_ = time_ = timestamp;
            refreshTargets(timestamp);
            // First appearance is fully formed. Use an explicit visible=false
            // creation followed by visible=true for an authored fade-in.
            for (auto& s : springs_) s.snap_to_target();
        } else {
            refreshTargets(timestamp);
        }
        if (input_.reduceMotion) resetSpatial();
        return duplicate ? SubmitStatus::CoalescedTimestamp : SubmitStatus::Accepted;
    }

    MotionOutput sample(double timestamp) const {
        if (!initialized_ || !VelocityEstimator::validTimestamp(timestamp) || timestamp < lastSubmitTime_) return {};
        MotionState copy = *this;
        copy.advance(timestamp);
        return copy.output(timestamp);
    }

    bool initialized() const { return initialized_; }
    double lastEventTime() const { return lastSubmitTime_; }
    const MotionInput& input() const { return input_; }
    const MotionTuning& tuning() const { return tuning_; }
    // Apply editable tuning at an explicit event boundary. Current spring values
    // and velocities survive, so changing a preset does not restart animation.
    bool setTuning(double timestamp, const MotionTuning& tuning) {
        if (!tuning.valid() || !VelocityEstimator::validTimestamp(timestamp) ||
            (initialized_ && timestamp < lastSubmitTime_)) return false;
        if (initialized_) advance(timestamp);
        tuning_ = tuning;
        velocity_.setTuning(tuning.velocity);
        lastSubmitTime_ = timestamp;
        if (initialized_) refreshTargets(timestamp);
        return true;
    }
    bool setTuning(const MotionTuning& tuning) { return setTuning(lastSubmitTime_, tuning); }

private:
    enum Property : std::size_t { Hover, Press, Focus, Opacity, Strain, StrainXY, Growth,
                                 TiltX, TiltY, OffsetX, OffsetY, ContactX, ContactY,
                                 Material, ContactGain, Elevation, Count };
    static bool sameInput(const MotionInput& a, const MotionInput& b) {
        return a.center.x == b.center.x && a.center.y == b.center.y &&
            a.halfSize.x == b.halfSize.x && a.halfSize.y == b.halfSize.y &&
            a.pointer.x == b.pointer.x && a.pointer.y == b.pointer.y &&
            a.hasPointer == b.hasPointer && a.hovered == b.hovered &&
            a.pressed == b.pressed && a.dragging == b.dragging &&
            a.focused == b.focused && a.enabled == b.enabled &&
            a.visible == b.visible && a.reduceMotion == b.reduceMotion && a.modality == b.modality;
    }
    static bool validInput(double timestamp, const MotionInput& i) {
        return VelocityEstimator::validTimestamp(timestamp) && VelocityEstimator::validPosition(i.center) &&
               VelocityEstimator::validPosition(i.pointer) && finite(i.halfSize) &&
               i.halfSize.x >= 0.01f && i.halfSize.y >= 0.01f && i.halfSize.x <= 1.0e5f && i.halfSize.y <= 1.0e5f &&
               (i.modality == InputModality::Mouse || i.modality == InputModality::Touch ||
                i.modality == InputModality::Pen || i.modality == InputModality::Keyboard);
    }
    float intensity() const {
        switch (input_.modality) {
        case InputModality::Mouse: return tuning_.mouseIntensity;
        case InputModality::Touch: return tuning_.touchIntensity;
        case InputModality::Pen: return tuning_.penIntensity;
        case InputModality::Keyboard: return tuning_.keyboardIntensity;
        }
        return 0;
    }
    void refreshTargets(double t) {
        springs_[Hover].set_target(input_.hovered ? 1 : 0);
        springs_[Press].set_target(input_.pressed ? 1 : 0);
        springs_[Focus].set_target(input_.focused ? 1 : 0);
        springs_[Opacity].set_target(input_.visible ? (input_.enabled ? 1 : tuning_.disabledOpacity) : 0);
        const float pressed = input_.pressed ? 1.0f : 0.0f;
        const float hovered = input_.hovered ? 1.0f : 0.0f;
        springs_[Material].set_target(input_.pressed ? tuning_.pressMaterialActivation :
            (input_.hovered ? tuning_.hoverMaterialActivation : tuning_.idleMaterialActivation));
        springs_[ContactGain].set_target(intensity() * (tuning_.hoverContactGain * hovered + tuning_.pressContactGain * pressed));
        springs_[Elevation].set_target(intensity() * (tuning_.hoverElevation * hovered + tuning_.pressElevation * pressed));

        const Kinematics k = velocity_.sample(t);
        const double vx = input_.dragging ? k.velocity.x : 0;
        const double vy = input_.dragging ? k.velocity.y : 0;
        const double speed = std::hypot(vx, vy);
        const double direction = (vx * vx - vy * vy) / (vx * vx + vy * vy + 16.0 * 16.0);
        const double offDiagonal = 2.0 * vx * vy / (vx * vx + vy * vy + 16.0 * 16.0);
        const double acc = input_.dragging ? std::hypot(k.acceleration.x, k.acceleration.y) : 0;
        const double response = (1 - tuning_.accelerationStrainShare) * std::tanh(speed / tuning_.strainSpeed) +
                                tuning_.accelerationStrainShare * std::tanh(acc / tuning_.strainAcceleration);
        const float amount = input_.reduceMotion ? 0 : intensity();
        const Vec2 strain = boundedVector({static_cast<float>(amount * tuning_.maximumLogStrain * direction * response),
            static_cast<float>(amount * tuning_.maximumLogStrain * offDiagonal * response)}, 0.30f);
        springs_[Strain].set_target(strain.x);
        springs_[StrainXY].set_target(strain.y);
        springs_[Growth].set_target(amount * (std::log1p(tuning_.hoverGrowth) * hovered + std::log1p(tuning_.pressGrowth) * pressed));

        Vec2 contact{};
        if (input_.hasPointer && (input_.hovered || input_.pressed)) {
            contact = input_.pointer - input_.center;
            contact.x = std::clamp(contact.x, -input_.halfSize.x, input_.halfSize.x);
            contact.y = std::clamp(contact.y, -input_.halfSize.y, input_.halfSize.y);
        }
        springs_[ContactX].set_target(contact.x);
        springs_[ContactY].set_target(contact.y);
        const double tx = std::tanh(vx / tuning_.strainSpeed);
        const double ty = std::tanh(vy / tuning_.strainSpeed);
        const Vec2 tilt = boundedVector({static_cast<float>(amount * (tuning_.maximumTilt * tx + tuning_.pointerTilt * contact.x / input_.halfSize.x)),
            static_cast<float>(amount * (tuning_.maximumTilt * ty + tuning_.pointerTilt * contact.y / input_.halfSize.y))}, 0.50f);
        springs_[TiltX].set_target(tilt.x);
        springs_[TiltY].set_target(tilt.y);
        // Small visual body compliance only. No effect on hit tests or value.
        const Vec2 offset = boundedVector({static_cast<float>(-amount * tuning_.complianceUnits * tx),
            static_cast<float>(-amount * tuning_.complianceUnits * ty)}, 8.0f);
        springs_[OffsetX].set_target(offset.x);
        springs_[OffsetY].set_target(offset.y);
    }
    SpringTuning springFor(std::size_t p) const {
        switch (p) {
        case Hover: return tuning_.hoverSpring;
        case Press: return input_.pressed ? tuning_.pressSpring : tuning_.releaseSpring;
        case Growth: return input_.pressed ? tuning_.pressSpring : tuning_.releaseSpring;
        case Focus: return tuning_.focusSpring;
        case Opacity: return tuning_.visibilitySpring;
        case ContactX: case ContactY: return tuning_.contactSpring;
        case ContactGain: case Elevation: return tuning_.contactSpring;
        case Material: return tuning_.materialSpring;
        default: return tuning_.deformationSpring;
        }
    }
    void resetSpatial() {
        for (std::size_t p : {std::size_t(Strain), std::size_t(StrainXY), std::size_t(Growth), std::size_t(TiltX), std::size_t(TiltY),
                              std::size_t(OffsetX), std::size_t(OffsetY)}) springs_[p].reset(0);
    }
    void advance(double t) {
        if (t <= time_) return;
        // One internal clock, anchored to object creation, not the display's
        // frame boundaries. Nonlinear target updates are bounded to 240 Hz by
        // default; each held-target spring interval is solved analytically.
        // A gap beyond 2 s is an explicit resume boundary: zero stale kinematics
        // and settle. There is no enormous catch-up loop after sleep/debugging.
        if (t - time_ > 2.0) {
            velocity_.reset(t, input_.pointer);
            refreshTargets(t);
            for (auto& s : springs_) s.snap_to_target();
            time_ = t;
            return;
        }
        // With no dynamic velocity target, the exact spring solution handles the
        // whole interval at once. Rest, hover, release and opacity fades do not
        // need hundreds of replayed fixed ticks when render sampling is pure.
        const auto k = velocity_.sample(time_);
        if (!input_.dragging || input_.reduceMotion ||
            (length(k.velocity) == 0 && length(k.acceleration) == 0)) {
            refreshTargets(time_);
            for (std::size_t p = 0; p < Count; ++p) springs_[p].step(t - time_, springFor(p));
            time_ = t;
            if (input_.reduceMotion) resetSpatial();
            return;
        }
        while (time_ < t) {
            const double relative = (time_ - epoch_) * tuning_.controlHz;
            const double tick = std::floor(relative + 1.0e-7) + 1.0;
            double next = std::min(t, epoch_ + tick / tuning_.controlHz);
            if (next <= time_) next = t;
            refreshTargets(time_);
            const double dt = next - time_;
            for (std::size_t p = 0; p < Count; ++p) springs_[p].step(dt, springFor(p));
            time_ = next;
        }
        if (input_.reduceMotion) resetSpatial();
    }
    MotionOutput output(double t) const {
        MotionOutput o;
        o.valid = true;
        o.timestamp = t;
        o.center = input_.center;
        o.halfSize = input_.halfSize;
        o.hover = static_cast<float>(std::clamp(springs_[Hover].value(), 0.0, 1.0));
        o.press = static_cast<float>(std::clamp(springs_[Press].value(), -0.12, 1.12));
        o.focus = static_cast<float>(std::clamp(springs_[Focus].value(), 0.0, 1.0));
        o.opacity = static_cast<float>(std::clamp(springs_[Opacity].value(), 0.0, 1.0));
        o.strainTensor = boundedVector({static_cast<float>(springs_[Strain].value()), static_cast<float>(springs_[StrainXY].value())}, 0.30f);
        o.logStrain = o.strainTensor.x; // horizontal-only legacy diagnostic alias
        o.uniformScale = static_cast<float>(std::exp(springs_[Growth].value()));
        o.scale = {o.uniformScale * std::exp(o.logStrain), o.uniformScale * std::exp(-o.logStrain)};
        o.tilt = boundedVector({static_cast<float>(springs_[TiltX].value()), static_cast<float>(springs_[TiltY].value())}, 0.60f);
        o.centerOffset = boundedVector({static_cast<float>(springs_[OffsetX].value()), static_cast<float>(springs_[OffsetY].value())}, 8.0f);
        o.contactLocal = {static_cast<float>(springs_[ContactX].value()), static_cast<float>(springs_[ContactY].value())};
        const auto k = velocity_.sample(t);
        o.velocity = input_.dragging ? k.velocity : Vec2{};
        o.acceleration = input_.dragging ? k.acceleration : Vec2{};
        o.releaseVelocity = releaseVelocity_;
        o.releaseTimestamp = releaseTimestamp_;
        o.contactGain = static_cast<float>(std::clamp(springs_[ContactGain].value(), 0.0, 4.0));
        o.elevation = static_cast<float>(std::clamp(springs_[Elevation].value(), 0.0, 8.0));
        o.materialActivation = static_cast<float>(std::clamp(springs_[Material].value(), 0.0, 1.0));
        o.settled = true;
        for (const auto& s : springs_) o.settled = o.settled && s.settled(0.0002, 0.002);
        o.settled = o.settled && length(k.velocity) < 0.02f && length(k.acceleration) < 0.1f;
        return o;
    }

    MotionTuning tuning_;
    VelocityEstimator velocity_;
    bool initialized_ = false;
    double epoch_ = 0, time_ = 0, lastSubmitTime_ = 0;
    MotionInput input_;
    Vec2 releaseVelocity_{};
    double releaseTimestamp_ = -1;
    std::array<ScalarSpring, Count> springs_;
};

} // namespace stack::glass::motion
