#include "Renderer/Glass/Engine/motion_presets.hpp"
#include "Renderer/Glass/Engine/morph.hpp"
#include "Renderer/Glass/ProximityJoins.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

using namespace stack::glass;
using namespace stack::glass::motion;
namespace {
int checks = 0, failures = 0;
void check(bool ok, const char* what) {
    ++checks;
    if (!ok) { ++failures; if (failures < 20) std::cerr << "FAIL " << what << '\n'; }
}
bool near(double a, double b, double eps = 1.0e-6) { return std::abs(a - b) <= eps; }
bool finiteOutput(const MotionOutput& o) {
    return o.valid && finite(o.center) && finite(o.halfSize) && finite(o.scale) && finite(o.tilt) &&
        finite(o.centerOffset) && finite(o.contactLocal) && finite(o.velocity) && finite(o.acceleration) &&
        finite(o.strainTensor) && finite(o.releaseVelocity) && std::isfinite(o.releaseTimestamp) &&
        std::isfinite(o.logStrain) && std::isfinite(o.uniformScale) &&
        std::isfinite(o.materialActivation) && std::isfinite(o.contactGain) &&
        std::isfinite(o.elevation) && std::isfinite(o.opacity) && o.scale.x > 0 && o.scale.y > 0 &&
        o.materialActivation >= 0 && o.materialActivation <= 1 && o.opacity >= 0 && o.opacity <= 1;
}
MotionInput script(double t) {
    MotionInput i;
    i.center = {180, 150}; i.halfSize = {24, 20};
    i.hasPointer = true; i.pointer = {180, 145};
    i.hovered = t >= 0.1 && t < 2.5;
    i.pressed = t >= 0.3 && t < 2.1;
    i.dragging = t >= 0.5 && t < 1.8;
    i.focused = t >= 2.5 && t < 2.8;
    if (t >= 0.5 && t < 1.05) i.center.x = static_cast<float>(180 + (t - 0.5) * 520);
    else if (t >= 1.05 && t < 1.8) i.center.x = static_cast<float>(466 - (t - 1.05) * 350);
    else if (t >= 1.8) i.center.x = 203.5f;
    i.pointer = i.center + Vec2{0, -5};
    return i;
}
double outputDistance(const MotionOutput& a, const MotionOutput& b) {
    const double values[] = {std::abs(a.scale.x-b.scale.x), std::abs(a.scale.y-b.scale.y),
        std::abs(a.tilt.x-b.tilt.x), std::abs(a.tilt.y-b.tilt.y), std::abs(a.press-b.press),
        std::abs(a.contactGain-b.contactGain), std::abs(a.opacity-b.opacity),
        std::abs(a.centerOffset.x-b.centerOffset.x), std::abs(a.centerOffset.y-b.centerOffset.y),
        std::abs(a.strainTensor.x-b.strainTensor.x),std::abs(a.strainTensor.y-b.strainTensor.y),
        std::abs(a.uniformScale-b.uniformScale)};
    return *std::max_element(std::begin(values), std::end(values));
}
void velocityTests() {
    VelocityEstimator v;
    check(v.submit(0, {0,0}) == SampleStatus::Accepted, "velocity initial sample");
    for (int k=1;k<=240;++k) v.submit(k/240.0, {static_cast<float>(1000.0*k/240),0});
    check(near(v.sample(1).velocity.x, 1000, 0.01), "constant speed converges");
    const auto before = v.sample(1);
    check(v.submit(1, {1002,0}) == SampleStatus::CoalescedTimestamp, "duplicate coalesced");
    check(near(v.sample(1).velocity.x, before.velocity.x), "duplicate has no derivative");
    check(v.submit(0.9, {0,0}) == SampleStatus::OutOfOrder, "out of order rejected");
    check(near(v.sample(1).velocity.x,before.velocity.x), "rejection leaves estimate unchanged");
    for (int k=1;k<=120;++k) v.submit(1+k/240.0, {1002,0});
    check(length(v.sample(1.5).velocity) < 0.08f, "identical submissions do not refresh velocity");
    check(length(v.sample(2).acceleration) < 0.001f, "acceleration drains at rest");
    check(v.submit(3, {4000,0}) == SampleStatus::Accepted, "long gap accepted");
    check(length(v.sample(3).velocity) == 0, "long gap rebase prevents velocity kick");
    check(v.submit(3.0000001,{1.0e6f,-1.0e6f}) == SampleStatus::Accepted, "tiny dt accepted safely");
    check(finite(v.sample(3.0000001).velocity) && length(v.sample(3.0000001).velocity) <= 6000.01f, "speed bound");
    check(v.submit(std::numeric_limits<double>::quiet_NaN(),{}) == SampleStatus::Invalid, "nan time rejected");
    check(v.submit(4,{INFINITY,0}) == SampleStatus::Invalid, "invalid position rejected");
    VelocityEstimator tiny;
    tiny.submit(0,{});tiny.submit(1e-320,{100000,100000});
    check(finite(tiny.sample(1e-320).velocity) && finite(tiny.sample(1e-320).acceleration),"subnormal dt cannot form inf times zero");
}
void motionTests() {
    MotionState state;
    check(!state.sample(0).valid,"uninitialized sample invalid");
    MotionInput i;
    i.center={100,100}; i.halfSize={24,20}; i.hasPointer=true; i.pointer={100,95};
    check(state.submit(0,i)==SubmitStatus::Accepted,"first input accepted");
    const auto initial=state.sample(0);
    check(near(initial.scale.x,1)&&near(initial.scale.y,1),"rest has no deformation");
    i.hovered=true;
    state.submit(.1,i);
    const auto hover=state.sample(.3);
    i.pressed=true;
    state.submit(.3,i);
    const auto contact=state.sample(.3);
    check(near(hover.scale.x,contact.scale.x),"press retarget preserves value");
    const auto held=state.sample(.6);
    check(held.uniformScale>1.24f && held.uniformScale<1.27f,"expressive press footprint");
    check(held.contactGain>0.07f,"press optical cue");
    i.dragging=true;
    state.submit(.6,i);
    for(int k=1;k<=60;++k) {
        i.center.x=100+static_cast<float>(k*5);i.pointer.x=i.center.x;
        state.submit(.6+k/120.0,i);
        const auto o=state.sample(.6+k/120.0);
        check(o.center.x==i.center.x,"drag anchor exact");
        check(near(o.scale.x*o.scale.y,o.uniformScale*o.uniformScale,3e-6),"log strain conserves projected area");
        check(length(o.centerOffset)<=1.801f,"visual compliance bounded");
    }
    const auto drag=state.sample(1.1);
    check(drag.scale.x>drag.scale.y*1.12f,"velocity makes visible directional stretch");
    const auto interruption=state.sample(1.1);
    i.center.x-=6;i.pointer.x=i.center.x;
    check(state.submit(1.1,i)==SubmitStatus::CoalescedTimestamp,"motion duplicate coalesced");
    const auto coalesced=state.sample(1.1);
    check(outputDistance(interruption,coalesced)<1e-6,"same-time retarget has no property jump");
    i.pressed=false;i.dragging=false;
    state.submit(1.12,i);
    const auto release=state.sample(1.12);
    check(release.scale.x>1.2f,"release retains spring state");
    i.pressed=true;state.submit(1.16,i);
    check(finiteOutput(state.sample(1.17)),"release interrupted by re-press");
    state.submit(1.2,i,EventKind::Cancel);
    const auto cancel=state.sample(1.5);
    check(length(cancel.velocity)==0 && cancel.press<0.003f,"cancel clears kinematics and relaxes press");
    i.enabled=false;state.submit(1.6,i);
    check(!state.input().pressed && !state.input().dragging && !state.input().hovered,"disabled clears active states");
    check(near(state.sample(1.9).opacity,.48,1e-5),"disabled opacity settles");
    check(state.submit(1.5,i)==SubmitStatus::OutOfOrder,"motion out of order rejected");
    MotionInput bad=i;bad.halfSize.x=-1;
    check(state.submit(2,bad)==SubmitStatus::InvalidInput,"invalid extent rejected");
    check(state.lastEventTime()==1.6,"rejection does not mutate event time");
    check(!state.sample(1).valid,"sample before most recent input rejected");
    check(finiteOutput(state.sample(1000000)),"large pause bounded and finite");
    check(state.sample(1000000).settled,"resume boundary settles state");
    check(state.lastEventTime()==1.6,"sample never advances authoritative time");
}
void reducedMotionTests() {
    MotionState s;
    MotionInput i; i.hasPointer=true;i.hovered=i.pressed=i.dragging=true;
    s.submit(0,i);
    for(int k=1;k<30;++k){i.pointer.x=k*8.0f;s.submit(k/120.0,i);}
    check(s.sample(.25).scale.x>1.2f,"normal movement present before accessibility change");
    i.reduceMotion=true;s.submit(.25,i);
    const auto reduced=s.sample(.25);
    check(reduced.scale.x==1 && reduced.scale.y==1 && length(reduced.tilt)==0 && length(reduced.centerOffset)==0,"reduce motion removes spatial effects immediately");
    check(reduced.contactGain>0 && reduced.opacity==1,"reduce motion retains interaction cue");
    i.visible=false;s.submit(.3,i);
    check(s.sample(.3).opacity>0.9f && s.sample(.4).opacity<0.1f,"reduced motion preserves short opacity fade");
    check(s.sample(.7).opacity<1e-7f,"hidden settles transparent");
}
void rateTests() {
    std::vector<MotionOutput> reference;
    double maximum=0;
    for (const int fps : {30,60,120,144}) {
        MotionState state;
        int next=0;
        std::vector<MotionOutput> common;
        for(int f=0;f<=fps*3;++f){
            const double t=static_cast<double>(f)/fps;
            while(next<=360 && next/120.0<=t){
                state.submit(next/120.0,script(next/120.0));++next;
            }
            const auto o=state.sample(t);
            check(finiteOutput(o),"display-rate sample finite");
            if(f%(fps/6)==0)common.push_back(o);
            // Extra peeks cannot change later results.
            if(f%7==0) state.sample(t+0.001);
        }
        if(reference.empty())reference=common;
        else for(std::size_t k=0;k<common.size();++k){
            const double d=outputDistance(reference[k],common[k]);maximum=std::max(maximum,d);
            check(d==0,"same event trajectory independent of display sampling");
        }
    }
    std::cout<<"maximum display-rate trajectory difference="<<maximum<<'\n';
    // Identical immediate-mode UI calls must not alter the tail after a flick.
    MotionState sparse,frequent;
    auto i=script(.8);i.center={100,100};i.pointer={100,100};
    sparse.submit(0,i);frequent.submit(0,i);
    for(int k=1;k<=24;++k){i.pointer.x=k*8.0f; sparse.submit(k/240.0,i);frequent.submit(k/240.0,i);}
    for(int k=1;k<=100;++k)frequent.submit(.1+k/240.0,i);
    check(outputDistance(sparse.sample(.6),frequent.sample(.6))==0,"identical UI submits do not alter integration");
    MotionState oneStationary=sparse;
    oneStationary.submit(.5,i);
    check(outputDistance(sparse.sample(.6),oneStationary.sample(.6))==0,"sparse stationary submission preserves earlier decay forcing");
}
void presetAndStressTests() {
    for (const auto& t : {presets::clearControl(),presets::clearControlMaterialize(),presets::precision(),
                        presets::graphPoint(),presets::button(),presets::selection(),presets::panel()})
        check(t.valid(),"named preset valid");
    MotionState mouse, touch;
    MotionInput i;i.pressed=true;i.modality=InputModality::Mouse;mouse.submit(0,i);
    i.modality=InputModality::Touch;touch.submit(0,i);
    check(touch.sample(0).scale.x>mouse.sample(0).scale.x,"touch expresses greater spatial amplitude");
    MotionState panel(presets::panel());panel.submit(0,i);
    check(panel.sample(.1).scale.x==1 && panel.sample(.1).scale.y==1,"panel has no wobble");
    MotionState material(presets::clearControlMaterialize());
    i.pressed=false;material.submit(0,i);
    check(material.sample(0).materialActivation==0,"materialize rest value");
    i.pressed=true;material.submit(.1,i);
    check(material.sample(.4).materialActivation>.999f,"materialize active value");
    std::mt19937 rng(9917);
    std::uniform_real_distribution<float> p(-3000,3000);
    MotionState stress;
    MotionInput x;
    double t=0;
    for(int k=0;k<10000;++k){
        t+=(k%29==0 ? .3 : 1.0/1000);
        x.center={p(rng),p(rng)};x.pointer={p(rng),p(rng)};
        x.halfSize={0.01f+std::abs(p(rng)),0.01f+std::abs(p(rng))};
        x.hasPointer=true;x.hovered=k%3!=0;x.pressed=k%7!=0;x.dragging=k%5!=0;
        x.enabled=k%31!=0;x.visible=k%37!=0;x.reduceMotion=k%41==0;
        x.modality=static_cast<InputModality>(k%4);
        stress.submit(t,x,k%97==0 ? EventKind::Cancel : EventKind::Update);
        const auto o=stress.sample(t+.0001);
        check(finiteOutput(o),"seeded input stress finite and bounded");
        check(near(o.scale.x*o.scale.y,o.uniformScale*o.uniformScale,3e-6),"stress area constraint");
        const double q=std::hypot(o.strainTensor.x,o.strainTensor.y);
        const double sincHyperbolic=q>1e-8?std::sinh(q)/q:1;
        const double coshQ=std::cosh(q);
        const double xx=coshQ+sincHyperbolic*o.strainTensor.x;
        const double xy=sincHyperbolic*o.strainTensor.y;
        const double yy=coshQ-sincHyperbolic*o.strainTensor.x;
        check(q<=.300001 && near(xx*yy-xy*xy,1,1e-12),"stress full tensor area and singular-value bounds");
    }
    bool threw=false;
    try {MotionTuning invalid;invalid.pressGrowth=-1;MotionState bad(invalid);}
    catch(const std::invalid_argument&){threw=true;}
    check(threw,"invalid tuning rejected before use");
}
void tensorAndEditingTests() {
    MotionState s(presets::graphPoint());
    MotionInput i;i.pressed=i.dragging=i.hasPointer=true;
    s.submit(0,i);
    for(int k=1;k<=120;++k){i.pointer={k*5.0f,k*5.0f};s.submit(k/240.0,i);}
    const auto diagonal=s.sample(.5);
    check(std::abs(diagonal.strainTensor.x)<1e-6f && diagonal.strainTensor.y>.14f,"diagonal motion uses offdiagonal tensor");
    const double q=std::hypot(diagonal.strainTensor.x,diagonal.strainTensor.y);
    const double a=q>1e-8?std::sinh(q)/q:1;
    const double c=std::cosh(q);
    const double xx=c+a*diagonal.strainTensor.x,xy=a*diagonal.strainTensor.y,yy=c-a*diagonal.strainTensor.x;
    check(near(xx*yy-xy*xy,1,1e-12),"tensor exponential determinant one");
    check(q<=.300001,"tensor magnitude hard bound");
    MotionState reversal=s;
    MotionInput r=i;
    for(int k=1;k<=120;++k){r.pointer={600-k*5.0f,600-k*5.0f};reversal.submit(.5+k/240.0,r);}
    check(reversal.sample(1).strainTensor.y>.14f && std::abs(reversal.sample(1).strainTensor.x)<1e-6f,
          "velocity reversal retains the same diagonal deformation axis");
    i.pressed=i.dragging=false;
    s.submit(.5,i);
    check(length(s.sample(.5).releaseVelocity)>1400 && s.sample(.5).releaseTimestamp==.5,"release exposes velocity for optional animation handoff");
    const auto before=s.sample(.55);
    const MotionState oldTuning=s;
    check(s.setTuning(.55,presets::precision()),"valid live tuning accepted");
    const auto after=s.sample(.55);
    check(outputDistance(before,after)==0 && before.uniformScale==after.uniformScale &&
        before.strainTensor.y==after.strainTensor.y,"live tuning preserves property values");
    const auto later=s.sample(.56);
    check(finiteOutput(later) && later.uniformScale>1.0001f,"live tuning retains motion rather than snapping");
    check(outputDistance(s.sample(.550001),oldTuning.sample(.550001))<1e-7,"live tuning preserves first derivative to numeric tolerance");
    MotionTuning invalid;invalid.controlHz=0;
    check(!s.setTuning(.6,invalid) && s.lastEventTime()==.55,"invalid tuning leaves timeline unchanged");
    check(!s.setTuning(.1,presets::button()),"out of order tuning rejected");
    s.submit(.6,i,EventKind::Cancel);
    check(length(s.sample(.6).releaseVelocity)==0 && s.sample(.6).releaseTimestamp==-1,"cancel suppresses handoff velocity");
    MotionState finalSample;
    MotionInput f;f.hasPointer=f.pressed=f.dragging=true;
    finalSample.submit(0,f);f.pointer.x=10;finalSample.submit(.01,f);
    f.pointer.x=30;f.pressed=f.dragging=false;finalSample.submit(.02,f);
    check(finalSample.sample(.02).releaseVelocity.x>600,"handoff includes final pointer-up movement sample");
}
}
void placementAndJoinTests() {
    engine::ShapeMotion selection;
    selection.submit(0,{{0,0},{20,20},20},engine::Placement::Direct,{}, {7,.82},{8,.84},false);
    selection.submit(.01,{{1.6f,0},{20,20},20},engine::Placement::Direct,{4,0}, {7,.82},{8,.84},false);
    selection.submit(.02,{{2,0},{20,20},20},engine::Placement::Spring,{4,0}, {7,.82},{8,.84},false);
    check(near(selection.sample(.02).center.x,1.6f),"release handoff retains direct position");
    const auto before=selection.sample(.025).center.x;
    const auto expected=selection.sample(.025001).center.x;
    selection.submit(.025,{{0,0},{20,20},20},engine::Placement::Spring,{}, {7,.82},{8,.84},false);
    check(near(selection.sample(.025).center.x,before) && near(selection.sample(.025001).center.x,expected,1.e-5),
        "interrupted selection preserves position and velocity");
    selection.submit(.03,{{2,0},{20,20},20},engine::Placement::Spring,{}, {7,.82},{8,.84},true);
    check(selection.sample(.03).center.x==2 && selection.settled(.03),"reduced-motion placement snaps");
    using namespace Stack::Renderer::Glass;
    ProximityJoins joins;
    std::array<JoinShape,3> points{};
    for(int i=0;i<3;++i)points[i]={unsigned(i+1),{float(i*31),0},{16,16},{},16,8,{0,0,100,100}};
    auto* groups=joins.Resolve(1,points.data(),3,1);
    check(groups && (*groups)[0]==(*groups)[2],"touching chain forms one visual compound");
    const auto builds=joins.BuildCount();joins.Resolve(1,points.data(),3,2);
    check(joins.BuildCount()==builds,"idle proximity grouping reuses retained geometry");
    points[1].center.y=80;groups=joins.Resolve(1,points.data(),3,3);
    check(groups && (*groups)[0]!=(*groups)[1] && (*groups)[0]!=(*groups)[2],"moved points separate immediately");
    points[1].center={31,0};points[1].clip[0]=1;groups=joins.Resolve(1,points.data(),3,4);
    check(groups && (*groups)[0]!=(*groups)[1],"different clip contracts cannot join");
    joins.Resolve(2,points.data(),3,4);
    check(joins.BuildCount()==builds+3,"separate graphs retain independent proximity caches");
}
int main(){
    velocityTests();motionTests();reducedMotionTests();rateTests();presetAndStressTests();tensorAndEditingTests();
    placementAndJoinTests();
    std::cout<<"motion checks="<<checks<<" failures="<<failures<<'\n';
    return failures==0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
