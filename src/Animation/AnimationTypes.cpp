// SPDX-License-Identifier: GPL-3.0-only
#include "AnimationTypes.h"

namespace Stack::Animation {
MotionSpec MotionSpec::Tween(float duration, Easing easing) {
    MotionSpec spec;
    spec.kind = MotionKind::Tween;
    spec.durationSeconds = duration;
    spec.easing = easing;
    return spec;
}
MotionSpec MotionSpec::Exponential(float rate, float epsilon) {
    MotionSpec spec;
    spec.kind = MotionKind::Exponential;
    spec.rate = rate;
    spec.settleEpsilon = epsilon;
    return spec;
}
MotionSpec MotionSpec::Spring(float frequency, float damping, float epsilon) {
    MotionSpec spec;
    spec.kind = MotionKind::Spring;
    spec.frequencyHz = frequency;
    spec.dampingRatio = damping;
    spec.settleEpsilon = epsilon;
    return spec;
}
} // namespace Stack::Animation
