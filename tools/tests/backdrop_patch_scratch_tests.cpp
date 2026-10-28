#include "Renderer/RawImageBackdrop.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <new>
#include <tuple>
using BackdropPatch = Stack::Renderer::RawImageBackdropFrame::Patch;
static bool counting=false;
static std::size_t allocations=0;
void* operator new(std::size_t n) {
    if(counting)++allocations;
    if(void* p=std::malloc(n))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
// PRODUCTION_HELPERS

// Original straightforward algorithm is the output/order and allocation oracle.
static void Reference(std::vector<BackdropPatch>& patches) {
    if(patches.size()<2)return;
    std::vector<BackdropPatch> accepted;
    for(const auto& incoming:patches) {
        std::vector<BackdropPatch> retained;
        for(const auto& prior:accepted) {
            ImVec2 low(std::max(prior.minimum.x,incoming.minimum.x),std::max(prior.minimum.y,incoming.minimum.y));
            ImVec2 high(std::min(prior.maximum.x,incoming.maximum.x),std::min(prior.maximum.y,incoming.maximum.y));
            if(low.x>=high.x||low.y>=high.y) {retained.push_back(prior);continue;}
            auto keep=[&](ImVec2 a,ImVec2 b){if(a.x<b.x&&a.y<b.y)retained.push_back(ClipBackdropPatch(prior,a,b));};
            keep(prior.minimum,{prior.maximum.x,low.y});keep({prior.minimum.x,high.y},prior.maximum);
            keep({prior.minimum.x,low.y},{low.x,high.y});keep({high.x,low.y},{prior.maximum.x,high.y});
        }
        retained.push_back(incoming);accepted=std::move(retained);
    }
    patches=std::move(accepted);
}
static auto Fields(const BackdropPatch& p) {
    // Compare every metadata field, not object padding bytes.
    return std::make_tuple(p.texture,p.minimum.x,p.minimum.y,p.maximum.x,p.maximum.y,
        p.uvMinimum.x,p.uvMinimum.y,p.uvMaximum.x,p.uvMaximum.y,
        p.extendEdges.x,p.extendEdges.y,p.extendEdges.z,p.extendEdges.w,p.previousTexture,
        p.previousOffset.x,p.previousOffset.y,p.previousScale.x,p.previousScale.y,
        p.fadeAmount,p.encodedSrgb,p.imageBounds.x,p.imageBounds.y,p.imageBounds.z,p.imageBounds.w);
}
static BackdropPatch Patch(unsigned id,ImVec2 a,ImVec2 b) {
    BackdropPatch p{id,a,b};p.previousTexture=id+100;p.previousOffset={.1f,.2f};
    p.previousScale={.7f,.8f};p.fadeAmount=.3f;p.encodedSrgb=id%2;
    p.uvMinimum={.15f,.9f};p.uvMaximum={.75f,.05f};p.imageBounds={.1f,.2f,.8f,.9f};return p;
}
int main() {
    // Repeated increasing/decreasing inputs cannot preserve stale prior metadata.
    for(int n:{0,1,2,16,64,3,32,1,0})for(int mode=0;mode<3;++mode) {
        std::vector<BackdropPatch> input;
        if(mode==1)input.push_back(Patch(1,{-1,-1},{20,20}));
        for(int i=0;i<n;++i) {
            const float x=float(i%8),y=float(i/8);
            input.push_back(Patch(unsigned(i+2),{x,y},{x+1,y+1}));
        }
        if(mode==2&&n>1) {
            input.push_back(Patch(90,{.25f,.25f},{4.5f,3.75f}));
            input.push_back(Patch(91,{1.5f,-.5f},{3.5f,5.5f}));
            input.push_back(Patch(92,{2,1},{3,2}));
        }
        auto expected=input,actual=input;
        allocations=0;counting=true;Reference(expected);counting=false;const auto baseline=allocations;
        allocations=0;counting=true;MakeBackdropCoverageDisjoint(actual);counting=false;const auto reused=allocations;
        if(actual.size()!=expected.size())return 1;
        for(std::size_t i=0;i<actual.size();++i)if(Fields(actual[i])!=Fields(expected[i]))return 2;
        if(n>=16&&reused>=baseline) {std::cerr<<"Scratch allocation reduction missing: "<<reused<<" >= "<<baseline<<'\n';return 3;}
    }
    std::cout<<"Backdrop coverage metadata/order parity and per-call scratch allocation checks passed.\n";
}
