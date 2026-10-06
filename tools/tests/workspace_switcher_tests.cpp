#include "App/WorkspaceSwitcher.h"
#include <stdexcept>
#include <iostream>

static void Check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main() {
    using namespace Stack::Workspace;
    try {
        for(float scale : {.5f,1.f,1.5f,2.f}) {
            Switcher s; s.config.pointerSensitivity=1; s.scale=scale;
            for(const auto& d : Destinations) {
                s.Begin(0); s.Move(d.x*70*scale,d.y*70*scale);
                Check(s.preview==static_cast<int>(d.id),"direction or DPI mismatch");
                Check(s.End(true)==static_cast<int>(d.id),"commit mismatch");
                s.Begin(1); s.Move(d.x*10000*scale,d.y*10000*scale);
                Check(std::abs(std::hypot(s.x,s.y)-100)<.001f,"unbounded cursor");
                Check(s.End(false)==1,"cancel changed destination");
            }
        }
        HoldInput input;
        input.leftAlt=true;
        Check(ResolveHoldAction(input,false,false)==HoldAction::Open,"left Alt failed");
        Check(ResolveHoldAction(input,false,true)==HoldAction::None,"inhibited Alt reopened");
        input.rightAlt=true; input.leftAlt=false;
        Check(ResolveHoldAction(input,true,false)==HoldAction::None,"released before last Alt");
        Check(ResolveHoldAction(input,false,false)==HoldAction::Open,"right Alt failed");
        input.leftControl=true;
        Check(ResolveHoldAction(input,false,false)==HoldAction::None,"AltGr opened switcher");
        input.leftControl=false; input.tab=true;
        Check(ResolveHoldAction(input,true,false)==HoldAction::CancelWithoutFocus,"Alt Tab committed");
        input.tab=false; input.f4=true;
        Check(ResolveHoldAction(input,true,false)==HoldAction::CancelWithoutFocus,"Alt F4 committed");
        input.f4=false; input.escape=true;
        Check(ResolveHoldAction(input,true,false)==HoldAction::Cancel,"Escape failed");
        input.escape=false; input.focused=false;
        Check(ResolveHoldAction(input,true,false)==HoldAction::CancelWithoutFocus,"focus loss committed");
        input.focused=true; input.rightAlt=false;
        Check(ResolveHoldAction(input,true,false)==HoldAction::Commit,"last Alt release failed");
        Switcher s;
        s.Begin(0); s.Move(100, 0);
        Check(s.x==25 && s.selected==-1,"pointer sensitivity ignored");
        s.config.pointerSensitivity=1;
        s.Begin(5); s.Move(0,-60); s.Move(0,40);
        Check(s.preview==5 && s.selected==-1,"dead zone did not restore original");
        Check(s.End(true)==5,"dead zone committed hover");
        s.Begin(0); s.Move(65,-60); int selected=s.selected;
        s.Move(-3,0); Check(s.selected==selected,"sector boundary chatters");
        s.Move(-30,0); Check(s.selected!=selected,"hysteresis traps selection");
        s.Begin(0); s.Tick(.13f); Check(std::abs(s.Amount()-.5f)<.001f,"animation timing");
        s.End(false); s.Tick(.065f); Check(s.progress>.24f && s.progress<.26f,"exit did not reverse");
        s.Begin(1); s.Tick(.065f); Check(std::abs(s.progress-.5f)<.001f,"reopen jumped");
        s.End(false); s.Tick(1); Check(!s.Visible(),"animation did not end");
        Check(static_cast<int>(Destinations[0].id)==0 && static_cast<int>(Destinations[1].id)==1 &&
              static_cast<int>(Destinations[2].id)==7 && static_cast<int>(Destinations[3].id)==5 && Destinations.size()==4,"legacy IDs changed");
        std::cout << "Workspace direction, DPI, clamping, dead zone, hysteresis, commit, cancellation and animation tests passed.\n";
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
