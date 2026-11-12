#include "Animation/FrameTween.h"
#include "App/GalleryNavigationTransition.h"
#include "Animation/FilmstripMotion.h"
#include "Async/UiInteraction.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

static int checks=0;
static void Check(bool condition,const char* what) {
    ++checks;
    if(!condition) {std::cerr<<"FAIL: "<<what<<'\n';std::exit(1);}
}
int main() {
    using namespace Stack::Animation;
    FrameTween tween;float amount=0;
    tween.Update(amount,0,.28f,0,0);
    tween.Update(amount,1,.28f,5,1);
    Check(amount==0 && tween.Active(),"new trigger excludes preceding stall");
    tween.Update(amount,1,.28f,5.01,2);
    Check(amount>0 && amount<1,"intermediate geometry");
    float displayed=amount;
    tween.Update(amount,1,.28f,5.02,2);
    Check(amount==displayed,"one advance per frame");
    tween.Update(amount,0,.28f,5.03,3);
    Check(amount==displayed,"reversal preserves displayed position");
    tween.Update(amount,0,.28f,5.32,4);
    Check(amount>0 && amount<displayed,"post-trigger stall still submits intermediate geometry");
    tween.Update(amount,0,.28f,5.33,5);
    Check(amount==0 && !tween.Active(),"finite exact completion");
    tween.Update(amount,1,.28f,5.34,6,true);
    Check(amount==1 && !tween.Active(),"reduced motion exact completion");

    {
        Stack::Navigation::GalleryNavigationTransition opening;
        Stack::Renderer::GalleryTransitionOrigin origin;
        origin.minimum={20,500};origin.maximum={140,580};
        const ImRect destination{{300,40},{1200,700}};
        opening.Tick(0,0,false);opening.BeginImage(origin,7,destination);
        opening.Tick(3,1,false);
        Check(opening.Waiting(7) && opening.SceneWeight()==0 && opening.ImageControls(7)==0,
            "slow load preserves Gallery and hides Controls");
        auto rect=opening.PresentImage(7,destination,123);
        Check(rect.Min.x==20 && rect.Max.y==580,"ready image starts at thumbnail geometry");
        opening.Tick(3.10,2,false);rect=opening.PresentImage(7,destination,123);
        Check(rect.Min.x==20 && opening.ImageControls(7)==0,"ready image fades before expansion");
        opening.Tick(3.23,3,false);opening.PresentImage(7,destination,123);
        opening.Tick(3.38,4,false);rect=opening.PresentImage(7,destination,123);
        Check(rect.Min.x>20 && rect.Min.x<300 && opening.ImageControls(7)==0,"image expands before Controls");
        opening.Tick(3.8,5,false);rect=opening.PresentImage(7,destination,123);
        Check(rect.Min.x==300 && rect.Max.y==700 && opening.ImageControls(7)==1 && !opening.Active(),
            "opening finishes at exact fitted geometry without residual work");
        opening.Retire(7);Check(!opening.HasImage(),"closing owner releases opening state");
        opening.Tick(4,6,true);opening.BeginImage(origin,8,destination);
        rect=opening.PresentImage(8,destination,123);
        Check(rect.Min.x==300 && opening.ImageControls(8)==1 && !opening.Active(),"reduced motion completes immediately");
    }
    const std::vector<float> stops{0,80,230,350,445,610,720,900,1040,1180,1320,1500,1680,1800,2000};
    for(float hz:{60.f,120.f,144.f,240.f}) for(float wheel:{-.18f,-1.f,-4.f,.18f,1.f,4.f}) {
        FilmstripMotion m;float target=900;double now=10;m.Reset(target,now);
        const int direction=wheel<0?1:-1;
        m.Wheel(wheel,stops,stops.size()-1,now,false);target=m.Position();
        Check(target==900,"wheel input does not jump on acceptance");
        float previous=target;bool completed=false;
        for(int frame=0;frame<600;++frame) {
            now+=1./hz;m.Step(target,stops,stops.size()-1,now,false);
            Check((target-previous)*direction>=-.0001f,"no unrequested direction reversal");
            if(!m.Active()) {completed=true;break;}
            if(now>10.03) Check((target-previous)*direction>0,"smooth progress after the short input delay");
            previous=target;
        }
        Check(completed,"settles in bounded time");
        Check(now-10<.3,"wheel tail ends within 300 ms");
        Check(std::abs(target-900)<=2.f*FilmstripMotion::Spacing(stops,stops.size()-1,900,direction),"travel is bounded to two cards");
        const float settled=target;now+=1;m.Step(target,stops,stops.size()-1,now,false);
        Check(target==settled && m.Velocity()==0,"settled has no residual work");
    }
    FilmstripMotion m;float target=900;double now=10;m.Reset(target,now);
    m.Wheel(-.18f,stops,stops.size()-1,now,false);target=m.Position();
    now+=.03;m.Step(target,stops,stops.size()-1,now,false);
    const float before=target;
    m.Wheel(.18f,stops,stops.size()-1,now,false);target=m.Position();
    Check(target==before && m.Direction()==-1,"reversal preserves displayed position");
    now+=.08;m.Step(target,stops,stops.size()-1,now,false);
    Check(target<before,"opposite input replaces pending travel");
    m.Drag(570,2000,now);target=570;
    Check(m.Position()==570 && !m.Active(),"drag directly follows pointer");
    now+=.01;m.Step(target,stops,stops.size()-1,now,false);
    Check(!m.Active() && target==570,"drag release stops without a forced snap");
    m.Reset(500,now);target=500;m.Wheel(-.18f,stops,stops.size()-1,now,true);target=m.Position();
    float reduced=target;now+=1;m.Step(target,stops,stops.size()-1,now,true);
    Check(target==reduced && !m.Active(),"reduced motion has no inertial tail");
    m.Reset(0,now);target=0;m.Wheel(1,stops,stops.size()-1,now,false);target=m.Position();
    m.Step(target,stops,stops.size()-1,now+.1,false);
    Check(target==0 && !m.Active(),"boundary has no rebound");
    m.Reset(400,now);target=400;m.Wheel(-1,stops,stops.size()-1,now,false);
    const float speed=m.Velocity();m.Reframe(600,1.5f,3000);
    Check(m.Position()==600 && m.Velocity()==speed*1.5f,"geometry preserves anchor and normalized speed");
    target=230;m.Step(target,stops,stops.size()-1,now+.01,false);
    Check(target==230 && !m.Active(),"explicit navigation replaces momentum");
    {
        Async::UiInteraction::Lease lease;lease.Update(true,false,1);
        Check(Async::UiInteraction::Active(),"input owns scheduling lease");
        lease.Update(false,true,2);lease.Update(false,false,2.149);
        Check(Async::UiInteraction::Active(),"150 ms quiet grace");
        lease.Update(false,false,2.151);
        Check(!Async::UiInteraction::Active(),"background admission resumes");
        lease.Update(true,false,3);
    }
    Check(!Async::UiInteraction::Active(),"owner teardown releases lease");
    std::cout<<checks<<" UI motion checks passed\n";
}
