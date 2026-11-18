#include "Hdr/CompatibleTransferPolicy.h"
#include "Hdr/WindowSurfaceExtent.h"
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
void Require(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
}

int main() {
    using namespace Stack::Hdr;
    using Phase=CompatibleTransferPhase;
    try {
        int width=2560,height=1440;
        for(const auto& size:{std::pair{0,0},std::pair{0,1440},std::pair{2560,0}}) {
            Require(UpdateWindowSurfaceExtent(size.first,size.second,width,height)==WindowSurfaceChange::Suspended &&
                    width==2560 && height==1440,
                "A missing drawable must preserve the last presentation extent");
        }
        Require(UpdateWindowSurfaceExtent(2560,1440,width,height)==WindowSurfaceChange::Unchanged,
            "Restoring an unchanged window must reuse its surface");
        Require(UpdateWindowSurfaceExtent(1920,1080,width,height)==WindowSurfaceChange::Resized &&
                width==1920 && height==1080,
            "A genuine positive resize must still replace the presentation extent");
        int otherWidth=800,otherHeight=600;
        Require(UpdateWindowSurfaceExtent(0,0,otherWidth,otherHeight)==WindowSurfaceChange::Suspended &&
                width==1920 && height==1080 && otherWidth==800 && otherHeight==600,
            "Suspending another window must preserve each window's independent extent");
        const auto active=BeginCompatibleTransfer(true);
        Require(active==Phase::ComparingAndReading && CompatibleTransferNeedsReadback(active),
            "Interaction must enqueue comparison and PBO readback under one fence");
        const auto changed=CompleteCompatibleTransfer(active,true);
        Require(changed.next==Phase::Ready && changed.copyPixels,
            "A changed interactive frame must copy after one completed fence");
        const auto unchanged=CompleteCompatibleTransfer(active,false);
        Require(unchanged.next==Phase::Ready && !unchanged.copyPixels,
            "An unchanged interactive frame must skip CPU and D3D pixel copies");

        const auto idle=BeginCompatibleTransfer(false);
        Require(idle==Phase::Comparing && !CompatibleTransferNeedsReadback(idle),
            "Idle comparison must not read the whole window speculatively");
        const auto idleUnchanged=CompleteCompatibleTransfer(idle,false);
        Require(idleUnchanged.next==Phase::Ready && !idleUnchanged.copyPixels,
            "An unchanged idle frame must return without any pixel readback");
        const auto idleChanged=CompleteCompatibleTransfer(idle,true);
        Require(idleChanged.next==Phase::Reading && !idleChanged.copyPixels &&
                CompatibleTransferNeedsReadback(idleChanged.next),
            "Changed idle pixels must wait for their separate readback fence");
        const auto idleRead=CompleteCompatibleTransfer(idleChanged.next,false);
        Require(idleRead.next==Phase::Ready && idleRead.copyPixels,
            "A completed idle readback must preserve its previously accepted change flag");
        const auto noWork=CompleteCompatibleTransfer(Phase::Ready,true);
        Require(noWork.next==Phase::Ready && !noWork.copyPixels,
            "An empty phase cannot copy pixels from an earlier transfer");
        std::cout<<"HDR surface suspension, interaction cadence and unchanged-copy suppression passed\n";
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
