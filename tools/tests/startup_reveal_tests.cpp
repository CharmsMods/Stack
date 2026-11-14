#include "App/StartupReveal.h"
#include <iostream>

int main() {
    using namespace Stack::StartupReveal;
    bool ok=true;
    auto check=[&](bool v,const char* text){if(!v){std::cerr<<text<<"\n";ok=false;}};
    const ImVec2 size(1920,1080);
    Controller c;c.Enable();
    check(!c.Update(500,size).pegVisible,"clock waits for first presentation");
    c.Start(9.6);
    check(c.Update(9.6,size).backdropAmount==0,"blur begins clear");
    const float intro=c.Update(9.8,size).backdropAmount;
    check(intro>0 && intro<1,"blur fades through intermediate amount");
    check(c.Update(10.01,size).backdropAmount==1,"blur reaches final amount");
    check(c.Update(10.84,size).phase==Phase::Throw,"throw lasts until contact");
    check(!c.Update(10.84,size).pegVisible,"peg invisible before contact");
    check(c.Update(10.86,size).pegVisible,"peg visible after contact");
    check(c.Update(11.5,size).phase==Phase::PaintPier,"pier follows contact");
    check(c.Update(12.4,size).phase==Phase::PaintOcean,"ocean follows pier");
    check(c.Update(13,size,false).phase==Phase::Hold,"readiness holds scene");
    check(!c.Update(13,size,false).AllowsInput(),"hold blocks input");
    check(c.Update(14,size,true).phase==Phase::RevealInterface,"ready begins handoff");
    check(c.Update(15.0,size,true,false,false).phase==Phase::RevealInterface,"held skip input cannot leak");
    check(c.Update(15.1,size).phase==Phase::Complete,"handoff completes after input release");
    check(c.Update(20,size).AllowsInput(),"completion permits input");
    check(!c.IsActive(),"completion stops timeline");
    c.Enable();c.Start(0);c.RequestSkip();
    check(c.Update(.1,size,false).phase==Phase::Throw,"skip does not bypass readiness");
    check(c.Update(.2,size).phase==Phase::RevealInterface,"pending skip honored when ready");
    check(c.Update(.4,size).phase==Phase::Complete,"skip uses short fade");
    c.Enable();c.Start(0);
    auto reduced=c.Update(0,size,true,true);
    check(reduced.sceneSeconds==2.9f && reduced.pegVisible,"reduced motion is settled composition");
    check(c.Update(.2,size,true,true).phase==Phase::Complete,"reduced motion has no cinematic delay");
    c.Disable();check(c.Update(0,size).AllowsInput(),"disabled permits input");
    c.Enable();c.Start(0);
    check(c.Update(3.29,size).phase==Phase::PaintOcean,"full scene is not shortened");
    c.Update(3.3,size);
    check(c.Update(4.21,size).phase==Phase::Complete,"normal scene ends after opening hold and cinematic");
    return ok?0:1;
}
