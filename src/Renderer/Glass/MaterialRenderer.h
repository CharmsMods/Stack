// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "glass_material_gl.hpp"
#include "JoinedGeometry.h"
#include <deque>
#include <string>

namespace Stack::Renderer::Glass {
struct MaterialEffects {
    float opacity=1,restFillOpacity=0;
    stack::glass::Vec3 restFillLinear{};
    stack::glass::Vec2 logStrain{};
    bool clearWater=false;
    const JoinedGeometry* joined=nullptr; // Borrowed only for the synchronous upload.
};
// Context-bound GPU storage. A slot is leased to one scheduled capture until
// BeginFrame; the immutable source is never used as a render destination.
class MaterialRenderer {
public:
    static constexpr std::size_t BudgetBytes=24u*1024u*1024u;
    struct Target {
        unsigned source=0, encoded=0, composite=0;
        unsigned sourceFbo=0, encodedFbo=0, compositeFbo=0;
        int width=0,height=0,mips=0;
        std::uint64_t allocation=0, revision=0;
        int captureX=0,captureY=0, hostFramebuffer=0,hostBuffer=0;
        bool hostSrgb=false,ready=false,leased=false,composing=false;
        int outputX=0,outputY=0,outputWidth=0,outputHeight=0;
        std::size_t bytes=0;
    };
    struct Counters { std::uint64_t allocations=0,captures=0,mipBuilds=0,draws=0,cacheHits=0,compositions=0,encodes=0; };
    void BeginFrame();
    Target* Acquire(int width,int height);
    void ReleaseLease(Target& target) {target.leased=false;}
    bool Capture(Target&,int x,int y,std::uint64_t contentRevision=0);
    // A group starts from its captured base, blends peers in linear light, then
    // encodes once before the caller submits that group's sharp foreground.
    bool BeginComposition(Target&,int x,int y,int width,int height);
    bool Composite(Target&,const stack::glass::gl::UniformPacket&,int x,int y,int width,int height,MaterialEffects effects={});
    bool EndComposition(Target&);
    bool Draw(Target&,const stack::glass::gl::UniformPacket&,int x,int y,int width,int height,MaterialEffects effects={});
    const stack::glass::gl::ContextLimits& Limits() const { return limits_; }
    const Counters& Diagnostics() const { return counters_; }
    const std::string& Error() const { return error_; }
    void Shutdown();
    static std::size_t RequiredBytes(int width,int height);
private:
    bool Initialize();
    void Release(Target&);
    std::deque<Target> targets_;
    std::size_t used_=0,bytes_=0;
    unsigned material_=0,transfer_=0,vao_=0;
    stack::glass::gl::MaterialUniforms uniforms_;
    stack::glass::gl::ContextLimits limits_;
    int opacityLocation_=-1,restOpacityLocation_=-1,restColorLocation_=-1,waterLocation_=-1,strainLocation_=-1;
    int joinedCountLocation_=-1,joinedShapesLocation_=-1,joinedMetricsLocation_=-1;
    Counters counters_;
    std::string error_;
};
}
