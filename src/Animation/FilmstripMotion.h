#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Stack::Animation {
// A value-owned, allocation-free gesture. Stops are supplied by the layout
// owner; no catalog, image, clock or ImGui work belongs in this class.
class FilmstripMotion {
public:
    enum class Phase { Idle, Glide, Settle };
    float Position() const { return position_; }
    float Velocity() const { return velocity_; }
    int Direction() const { return direction_; }
    bool Active() const { return phase_ != Phase::Idle; }
    void Reset(float position, double now = 0.) {
        position_ = published_ = std::max(0.f, position);
        velocity_ = 0; direction_ = 0; phase_ = Phase::Idle; lastTime_ = now;
        dragging_ = false;
    }
    static std::size_t Ahead(const std::vector<float>& stops, std::size_t last, float position, int direction) {
        if (stops.empty()) return 0;
        last = std::min(last, stops.size()-1);
        if (direction > 0) {
            auto i = std::upper_bound(stops.begin(), stops.begin()+last+1, position+.001f);
            return std::min(last, std::size_t(i-stops.begin()));
        }
        auto i = std::lower_bound(stops.begin(), stops.begin()+last+1, position-.001f);
        return i == stops.begin() ? 0 : std::size_t(i-stops.begin()-1);
    }
    static float Spacing(const std::vector<float>& stops, std::size_t last, float position, int direction) {
        if (stops.size()<2 || !last) return 1.f;
        auto i = Ahead(stops,last,position,direction);
        if (direction < 0) i = std::min(i+1,last);
        i = std::max(std::size_t(1),i);
        return std::max(1.f,stops[i]-stops[i-1]);
    }
    void Wheel(float wheel, const std::vector<float>& stops, std::size_t last, double now, bool reduced) {
        if (!std::isfinite(wheel) || wheel == 0 || stops.empty()) return;
        const int direction = wheel < 0 ? 1 : -1;
        const float spacing = Spacing(stops,last,position_,direction);
        const bool reversed = direction != direction_;
        if (reversed) velocity_ = 0;
        direction_ = direction;
        // Coalesce wheel ticks into bounded travel, then ease from the displayed
        // pose. No immediate position jump and no accumulated inertial tail.
        const bool continuing = !reversed && phase_ == Phase::Settle && now - lastWheel_ < .14;
        const float base = continuing ? destination_ : position_;
        const float destination = std::clamp(base - wheel * spacing, 0.f, stops[last]);
        const float bounded = std::clamp(destination, position_ - 2.f*spacing, position_ + 2.f*spacing);
        if (reduced) Reset(bounded, now);
        else BeginSettle(bounded, now + (continuing ? 0. : .025), .24f);
        lastWheel_ = now; lastTime_ = now; published_ = position_; dragging_ = false;
    }
    void Drag(float position, float maximum, double now) {
        position = std::clamp(position,0.f,maximum);
        if (position != position_) direction_ = position>position_ ? 1 : -1;
        position_ = published_ = position; velocity_ = 0;
        phase_ = Phase::Idle; lastTime_ = now; dragging_ = true;
    }
    // Geometry updates preserve the fractional leading-item anchor and scale
    // velocity with its spacing. They do not restart the gesture or snap it.
    void Reframe(float position, float scale, float maximum) {
        const float publishedBefore_ = position_;
        position_ = published_ = std::clamp(position,0.f,maximum);
        velocity_ *= scale;
        if (phase_ == Phase::Settle) {
            destination_ = std::clamp(position_ + (destination_ - publishedBefore_) * scale, 0.f, maximum);
            start_ = position_; started_ = lastTime_;
        }
        Clamp(maximum);
    }
    void Step(float& requested, const std::vector<float>& stops, std::size_t last, double now, bool reduced) {
        if (stops.empty()) { Reset(0,now); requested=0; return; }
        const float maximum=stops[last];
        // Explicit selection/preview navigation is independent of momentum.
        if (std::abs(requested-published_)>.01f) {
            Reset(std::clamp(requested,0.f,maximum),now);
        }
        if (reduced) { velocity_=0; phase_=Phase::Idle; dragging_=false; }
        dragging_=false;
        lastTime_=now;
        if (phase_==Phase::Settle) {
            const float t=std::clamp(float((now-started_)/duration_),0.f,1.f);
            const float t2=t*t,t3=t2*t;
            const float distance=destination_-start_;
            // Cubic Hermite with zero terminal speed. A normalized initial
            // slope in [0,3] is monotone for the complete segment.
            position_=start_+distance*((-2*t3+3*t2)+slope_*(t3-2*t2+t));
            velocity_=distance*((-6*t2+6*t)+slope_*(3*t2-4*t+1))/duration_;
            if(t>=1) {position_=destination_;velocity_=0;phase_=Phase::Idle;}
        }
        Clamp(maximum);requested=published_=position_;
    }
private:
    void BeginSettle(float destination,double now,float duration) {
        start_=position_;destination_=destination;started_=now;duration_=duration;
        const float distance=destination-start_;
        if(std::abs(distance)<.001f) {position_=destination;velocity_=0;phase_=Phase::Idle;return;}
        slope_=std::clamp(velocity_*duration/distance,0.f,3.f);
        // Zero initial speed rounds off an isolated wheel notch.
        phase_=Phase::Settle;
    }
    void Clamp(float maximum) {
        position_=std::clamp(position_,0.f,maximum);
        if((position_<=0 && direction_<0) || (position_>=maximum && direction_>0)) {
            velocity_=0;phase_=Phase::Idle;
        }
    }
    float position_=0,published_=0,velocity_=0,start_=0,destination_=0,duration_=.2f,slope_=0;
    double lastTime_=0,started_=0,lastWheel_=-1;
    int direction_=0;
    Phase phase_=Phase::Idle;
    bool dragging_=false;
};
}
