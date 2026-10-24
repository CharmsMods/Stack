#include "Renderer/GraphTargetPrecision.h"
#include <iostream>
#include <stdexcept>

void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() try {
    using Stack::Renderer::ReconfigureGraphTargetPrecision;
    bool full = false;
    int releases = 0;
    auto release = [&] { ++releases; };
    Require(!ReconfigureGraphTargetPrecision(full,false,false,release)&&releases==0,"ordinary cache retention");
    Require(ReconfigureGraphTargetPrecision(full,true,false,[&]{Require(!full,"old format during upgrade release");release();})&&full,"upgrade");
    Require(!ReconfigureGraphTargetPrecision(full,true,false,release)&&releases==1,"full precision cache retention");
    Require(!ReconfigureGraphTargetPrecision(full,false,true,release)&&full&&releases==1,"seeded precision floor");
    Require(ReconfigureGraphTargetPrecision(full,false,false,[&]{Require(full,"old format during downgrade release");release();})&&!full&&releases==2,"released dependency permits downgrade");
    bool caught = false;
    try {
        ReconfigureGraphTargetPrecision(full,true,false,[]{throw std::runtime_error("invalidation failed");});
    } catch(const std::runtime_error&) { caught = true; }
    Require(caught && !full,"failed transition propagates and keeps old format");
    Require(ReconfigureGraphTargetPrecision(full,false,true,release)&&full,"seed alone selects full precision");
    Require(!ReconfigureGraphTargetPrecision(full,true,true,release),"RAW graph with seeded dependency retains caches");
    std::cout<<"Graph target precision tests passed\n";
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
