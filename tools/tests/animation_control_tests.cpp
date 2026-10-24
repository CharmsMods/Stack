// SPDX-License-Identifier: GPL-3.0-only
#include "Animation/ControlMotion.h"
#include "Animation/SliderMotion.h"
#include <stdexcept>
#include <cmath>
#include <limits>
#include <iostream>
using namespace Stack::Animation;
static void Require(bool condition) { if (!condition) throw std::runtime_error("animation control contract failed"); }
int main() {
    ControlMotion motion;
    ControlInput in; in.identity=42; in.frame=1; in.deltaSeconds=1./60.;
    in.selected=true;
    auto s=motion.Update(in); Require(s.selection==1.f && s.press==0.f);
    in.frame++; in.hovered=true; in.held=true;
    s=motion.Update(in); Require(s.press==1.f && s.hover>0.f && s.Elevation()==1.f-.8f);
    const auto stable=s; in.held=false; in.hovered=false;
    s=motion.Update(in); Require(s.press==stable.press && s.hover==stable.hover);
    in.frame++; s=motion.Update(in); Require(s.press>0.f && s.press<1.f);
    in.frame++; in.held=true; s=motion.Update(in); Require(s.press==1.f);
    in.frame++; in.held=false; in.reducedMotion=true;
    s=motion.Update(in); Require(s.press==0.f && s.hover==0.f && s.selection==1.f);
    in.frame++; in.hovered=true; in.held=true; in.enabled=false;
    s=motion.Update(in); Require(s.press==0.f && s.hover==0.f);
    in.frame++; in.enabled=true; in.visible=false; s=motion.Update(in);
    Require(s.press==0.f && s.selection==0.f);
    in.frame++; in.visible=true; in.hovered=false; in.held=false;
    s=motion.Update(in); Require(s.selection==1.f && s.press==0.f);
    in.frame++; in.held=true; motion.Update(in);
    in.frame+=2; in.held=false; in.reducedMotion=false;
    s=motion.Update(in); Require(s.press==0.f); // skipped owner cannot replay pressure
    in.frame++; in.held=true; motion.Update(in);
    in.identity=43; in.frame++; in.held=false;
    s=motion.Update(in); Require(s.press==0.f);
    motion.Reset(); Require(motion.Snapshot().press==0.f);
    // Historical exponential response and .05s resume bound remain intentional.
    in={}; in.identity=9; in.frame=1; in.deltaSeconds=100.; in.hovered=true;
    s=motion.Update(in); Require(std::abs(s.hover-(1.f-std::exp(-.05f*26.f)))<1e-6f);
    in.frame++; in.deltaSeconds=std::numeric_limits<double>::quiet_NaN();
    const float before=s.hover; s=motion.Update(in); Require(s.hover==before);
    in.frame++; in.reducedMotion=true; s=motion.Update(in); Require(s.hover==1.f);
    SliderMotion slider;
    const double authored=.25;
    auto p=slider.Update(in,authored,0.,1.); Require(p.validRange&&p.position==.25f&&authored==.25);
    in.frame++; in.held=true; p=slider.Update(in,.75,0.,1.); Require(p.position==.75f);
    in.frame++; p=slider.Update(in,2.,0.,1.); Require(p.position==1.f);
    in.frame++; p=slider.Update(in,-1.,0.,1.); Require(p.position==0.f);
    in.frame++; p=slider.Update(in,.25,1.,0.); Require(p.position==.75f);
    in.frame++; p=slider.Update(in,.25,1.,1.); Require(!p.validRange&&p.position==0.f);
    in.frame++; p=slider.Update(in,std::numeric_limits<double>::infinity(),0.,1.);
    Require(!p.validRange&&p.position==0.f);
    slider.Reset(); Require(slider.Snapshot().position==0.f);
    std::cout<<"PASS owned control snapshots, interruption, visibility, reduced motion and authoritative slider mapping\n";
}
