// SPDX-License-Identifier: GPL-3.0-only
#include "Renderer/GlassChrome.h"
#include "Renderer/GlassChromeShaders.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include <algorithm>

namespace Stack::Renderer {
namespace {
namespace sg=stack::glass;
// Only main-window callbacks enter this owner. Contributions are ordered by
// actual draw execution, including surfaces belonging to other UI owners.
thread_local int provenanceFrame=-1;
thread_local std::vector<sg::GlassContribution> completedGroups;
constexpr sg::ContextKey MainContext{0x535441434b,{1,1}};
}
void GlassChrome::BackdropCallback(const ImDrawList*,const ImDrawCmd* cmd){auto* b=static_cast<Backdrop*>(cmd->UserCallbackData);b->owner->CaptureBackdrop(*b);}
void GlassChrome::ObjectCallback(const ImDrawList*,const ImDrawCmd* cmd){auto* o=static_cast<Object*>(cmd->UserCallbackData);o->owner->DrawObject(*o);}
void GlassChrome::InkCallback(const ImDrawList*,const ImDrawCmd* cmd){const auto* i=static_cast<Ink*>(cmd->UserCallbackData);i->owner->DrawInk(*i);}
void GlassChrome::CaptureBackdrop(Backdrop& b){
    if(!b.remainingObjects)return;
    PrepareJoins(b);
    if(provenanceFrame!=ImGui::GetFrameCount()){provenanceFrame=ImGui::GetFrameCount();completedGroups.clear();}
    captureValid=inkReady=false;activeBackdrop=nullptr;
    const GLState::FramebufferState host(true);
    b.target=gpu.Acquire(b.layout.width,b.layout.height);host.Restore(true);
    if(!b.target||!gpu.Capture(*b.target,b.layout.x,b.layout.y,b.revision))return;
    if(!gpu.BeginComposition(*b.target,b.left,b.bottom,b.right-b.left,b.top-b.bottom))return;
    sg::BackdropDescription d;
    d.image={MainContext.nameSpace,{b.target->allocation,1}};
    d.viewport={MainContext,{ImGui::GetMainViewport()->ID,1}};d.captureScope={b.group,1};
    d.frameSerial=frame;d.contentRevision=b.revision;
    d.mapping={{{0,0},{float(b.layout.width),float(b.layout.height)}},{0,0,b.layout.width,b.layout.height},{b.layout.width,b.layout.height},sg::PixelOrigin::BottomLeft};
    d.provenance.complete=true;d.provenance.containsSemanticUnderlay=true;d.provenance.glass=completedGroups;
    b.layer=std::uint32_t(completedGroups.size()+1);b.snapshot.emplace(std::move(d));
    activeBackdrop=&b;captureValid=true;sourceTexture=b.target->source;
    width=b.layout.width;height=b.layout.height;
    captureOrigin={float(b.layout.x),float(b.layout.y)};captureSize=captureSoftSize={float(width),float(height)};
    captureDisplayPos=b.displayPos;captureDisplaySize=b.displaySize;captureScale=b.scale;
    if(!inks.empty() && EnsureInk())PrepareInk();host.Restore(true);
}
void GlassChrome::DrawObject(const Object& o){
    auto& b=*o.backdrop;if(!b.target||!b.snapshot||!Error().empty())return;
    sg::GlassPassContract pass;
    pass.viewport=b.snapshot->description().viewport;pass.group={MainContext,{b.group,1}};
    pass.opticalLayer=b.layer;pass.frameSerial=frame;pass.destination={MainContext.nameSpace,{b.target->allocation+1,1}};
    // Crop contains warp/filter support. Only the physical viewport boundary
    // clamps, matching the presentation sampler's defined edge extension.
    pass.requiredSampleDomainPxTL=b.snapshot->description().mapping.capturedFramebufferPxTL;
    pass.sourcePolicy=sg::SourceGlassPolicy::AllowExplicitLowerLayers;pass.output=sg::CompositeRepresentation::PremultipliedSurface;
    sg::gl::UniformPacket p;p.material=o.material;
    p.geometry.localOriginFramebufferPx={o.center.x-b.layout.x,o.center.y-b.layout.y};
    p.geometry.shapes[0]={{0,0},{o.halfSize.x,o.halfSize.y},o.radius};
    p.source={0,b.target->mips,float(b.target->mips-1),{b.layout.width,b.layout.height},&*b.snapshot,&pass};
    p.material.roughnessLod=std::min(p.material.roughnessLod,p.source.maxMip);
    p.material.materialize=1.f; // Presence stays on; layer opacity owns fades.
    bool drawn=false;
    if(o.compound<0)drawn=gpu.Composite(*b.target,p,o.left,o.bottom,o.width,o.height,o.effects);
    else if(o.joinLeader) {
        const auto& compound=compounds[o.compound];
        auto effects=o.effects;effects.logStrain={};effects.joined=&compound.geometry;
        p.geometry.mergePx=compound.width;
        drawn=gpu.Composite(*b.target,p,compound.left,compound.bottom,
            compound.right-compound.left,compound.top-compound.bottom,effects);
    }
    if(--b.remainingObjects==0){gpu.EndComposition(*b.target);gpu.ReleaseLease(*b.target);}
    if(!drawn)return;
    const sg::GlassContribution contribution{pass.group,pass.opticalLayer};
    if(std::none_of(completedGroups.begin(),completedGroups.end(),[&](const auto& c){return c.group==contribution.group;}))completedGroups.push_back(contribution);
}
bool GlassChrome::EnsureInk(){
    if(programs[2])return true;
    programs[2]=GLHelpers::CreateShaderProgram(GlassChromeShaders::InkVertex,GlassChromeShaders::InkFragment);
    programs[3]=GLHelpers::CreateComputeProgram(GlassChromeShaders::InkMeasure);
    glGenBuffers(2,inkBuffers);glBindBuffer(GL_SHADER_STORAGE_BUFFER,inkBuffers[0]);
    glBufferData(GL_SHADER_STORAGE_BUFFER,MaxInkItems*32,nullptr,GL_DYNAMIC_DRAW);
    std::array<std::uint32_t,MaxInkItems*4> empty{};glBindBuffer(GL_SHADER_STORAGE_BUFFER,inkBuffers[1]);
    glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(empty),empty.data(),GL_DYNAMIC_DRAW);glBindBuffer(GL_SHADER_STORAGE_BUFFER,0);
    return programs[2]&&programs[3];
}
void GlassChrome::Shutdown(){
    gpu.Shutdown();for(auto& p:programs){if(p)glDeleteProgram(p);p=0;}glDeleteBuffers(2,inkBuffers);inkBuffers[0]=inkBuffers[1]=0;
    inkIdentity.fill(0);inkLastUsed.fill(0);inkEpoch=0;backdrops.clear();objects.clear();inks.clear();activeBackdrop=nullptr;
    joins.Clear();compounds.clear();compoundCount=0;
    enabled=captureValid=inkReady=false;error.clear();
}
}
