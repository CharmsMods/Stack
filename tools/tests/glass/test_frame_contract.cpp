#include "Renderer/Glass/glass_frame_contract.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <type_traits>

using namespace stack::glass;

namespace {
int checks = 0, failures = 0;
void expect(bool result, const char* label) {
    ++checks;
    if (!result) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}
bool near(float a, float b) { return std::abs(a-b) < 0.00002f; }
bool near(Vec2 a, Vec2 b) { return near(a.x,b.x) && near(a.y,b.y); }

void coordinatesAndClipping() {
    const ViewportTransform v{{120,-80}, {1.5f,1.5f}, {900,600}};
    expect(v.valid(), "nonzero detached viewport origin at 1.5 DPI");
    expect(near(logicalToFramebufferTopLeft({130,-60},v), {15,30}),
           "subtract desktop DisplayPos before physical scale");
    expect(near(logicalToFramebuffer({130,-60},v,PixelOrigin::BottomLeft), {15,570}),
           "bottom-left conversion uses physical framebuffer height");
    expect(near(framebufferTopLeftToLogical({15,30},v), {130,-60}),
           "logical/framebuffer round trip with negative desktop y");

    const Bounds2 fractional{{119,-79.8f},{140.2f,-49.8f}};
    const auto r=physicalClipTopLeft(fractional,v);
    expect(r.x0==0 && r.y0==0 && r.x1==31 && r.y1==46,
           "fractional clip clamps and rounds outward without losing AA pixels");
    const auto gl=scissorForLogicalClip(fractional,v);
    expect(gl.x==0 && gl.y==554 && gl.width==31 && gl.height==46,
           "OpenGL lower-left half-open scissor");
    const auto upper=scissorForLogicalClip(fractional,v,PixelOrigin::TopLeft);
    expect(upper.x==0 && upper.y==0 && upper.width==31 && upper.height==46,
           "upper-left clip-origin scissor");
    const auto exact=scissorForLogicalClip({{130,-60},{140,-50}},v);
    expect(exact.x==15 && exact.y==555 && exact.width==15 && exact.height==15,
           "integer boundaries do not gain an extra half-open pixel");
    expect(scissorForLogicalClip({{800,-70},{810,-50}},v).empty(), "offscreen clip is empty");
    expect(scissorForLogicalClip({{130,-60},{130,-50}},v).empty(), "zero-width clip is empty");
    ViewportTransform minimized=v; minimized.framebuffer.height=0;
    expect(scissorForLogicalClip(fractional,minimized).empty(), "minimized viewport issues no draw");

    SourceMapping s{{{10,20},{210,120}}, {32,16,132,66}, {256,128}, PixelOrigin::TopLeft};
    expect(s.valid(), "padded capture mapped into a downsampled atlas region");
    expect(near(framebufferTopLeftToSourceUV({11,21},s), {32.5f/256,16.5f/128}),
           "framebuffer position maps to atlas texel center without an extra half texel");
    const Vec2 logicalPoint=framebufferTopLeftToLogical({11,21},v);
    expect(near(logicalToSourceUV(logicalPoint,v,s), {32.5f/256,16.5f/128}),
           "logical input reaches cropped atlas at 1.5 DPI");
    s.uvZeroOrigin=PixelOrigin::BottomLeft;
    expect(near(framebufferTopLeftToSourceUV({11,21},s), {32.5f/256,1-16.5f/128}),
           "source UV orientation flips once independently of output scissor");
    expect(near(framebufferTopLeftToSourceUV({210,120},s), {132.0f/256,1-66.0f/128}),
           "continuous half-open maximum maps to source region edge");
}

void provenanceAndMaterials() {
    const ContextKey context{11,{1,1}};
    const ViewportKey viewport{context,{2,1}};
    const GlassGroupKey active{context,{20,1}}, lower{context,{21,1}};
    BackdropDescription b;
    b.image={44,{5,1},0,0}; b.viewport=viewport; b.captureScope={8,1};
    b.frameSerial=12; b.contentRevision=2;
    b.mapping={{{10,20},{210,120}}, {32,16,132,66}, {256,128}, PixelOrigin::BottomLeft};
    b.provenance.complete=true; b.provenance.containsSemanticUnderlay=true;
    GlassPassContract p;
    p.viewport=viewport; p.group=active; p.opticalLayer=1; p.frameSerial=12;
    p.destination={44,{6,1},0,0}; p.requiredSampleDomainPxTL={{12,22},{208,118}};
    expect(validatePassSource(BackdropSnapshot(b),p)==ContractError::None,
           "semantic-underlay source is permitted without recursive glass");

    static_assert(!std::is_copy_assignable<BackdropSnapshot>::value,
                  "published snapshot metadata must remain immutable");
    const BackdropSnapshot frozen(b);
    b.frameSerial=13;
    expect(frozen.description().frameSerial==12, "snapshot owns a frozen value copy");
    b.frameSerial=12;
    auto wrong=p; wrong.frameSerial=13;
    expect(validatePassSource(frozen,wrong)==ContractError::FrameMismatch, "stale frame rejected");
    wrong=p; ++wrong.viewport.viewport.generation;
    expect(validatePassSource(frozen,wrong)==ContractError::ViewportMismatch,
           "recreated platform viewport cannot use stale source ownership");
    wrong=p; ++wrong.destination.shareGroupNamespace;
    expect(validatePassSource(frozen,wrong)==ContractError::ResourceContextMismatch,
           "textures from unrelated GL share groups need an explicit transfer");
    wrong=p; wrong.destination=b.image; wrong.destination.mipLevel=2;
    expect(validatePassSource(frozen,wrong)==ContractError::TextureFeedback,
           "same texture allocation rejected even with another mip");
    wrong=p; wrong.requiredSampleDomainPxTL.maximum.x=211;
    expect(validatePassSource(frozen,wrong)==ContractError::InsufficientSourcePadding,
           "declared warp/filter reach must fit capture padding");

    b.provenance.glass={{active,0}};
    expect(validatePassSource(BackdropSnapshot(b),p)==ContractError::RecursiveGlass,
           "same merged group excluded from its optical source");
    b.provenance.glass={{lower,0}};
    expect(validatePassSource(BackdropSnapshot(b),p)==ContractError::UnexpectedGlass,
           "default source policy excludes previous glass composites");
    p.sourcePolicy=SourceGlassPolicy::AllowExplicitLowerLayers;
    expect(validatePassSource(BackdropSnapshot(b),p)==ContractError::None,
           "deliberate lower optical layer is allowed explicitly");
    b.provenance.glass.front().opticalLayer=1;
    expect(validatePassSource(BackdropSnapshot(b),p)==ContractError::NonLowerGlass,
           "peer/future optical layer dependency rejected");
    b.provenance.glass.clear(); b.provenance.complete=false;
    expect(validateBackdrop(BackdropSnapshot(b))==ContractError::UnknownGlassProvenance,
           "unknown source composition rejected");
    b.provenance.complete=true; b.sampleCount=4;
    expect(validateBackdrop(BackdropSnapshot(b))==ContractError::UnresolvedSource,
           "multisample attachment requires GPU resolve");
    b.sampleCount=1; b.color.encoding=ColorEncoding::SRGBEncoded;
    expect(validateBackdrop(BackdropSnapshot(b))==ContractError::InvalidColor,
           "material stage requires declared display-linear sampling");
    b.color.encoding=ColorEncoding::DisplayLinear; b.color.alpha=SourceAlpha::Premultiplied;
    expect(validatePassSource(BackdropSnapshot(b),p)==ContractError::UnsupportedSourceAlpha,
           "opaque replacement reference cannot consume an unresolved transparent backdrop");

    OpticalMaterialContract m;
    expect(validateMaterial(m)==ContractError::None, "default material contract is valid");
    m.ior=1.0f; m.materialize=0;
    expect(validateMaterial(m)==ContractError::None, "identity optical limit is valid");
    m.profile.centerZPx=1;
    expect(validateMaterial(m)==ContractError::InvalidMaterial, "closed glass may not cross source plane");
    m=OpticalMaterialContract{}; m.absorptionPerPx.y=-0.1f;
    expect(validateMaterial(m)==ContractError::InvalidMaterial, "negative absorption rejected");
    m=OpticalMaterialContract{}; m.dispersionPx=std::numeric_limits<float>::quiet_NaN();
    expect(validateMaterial(m)==ContractError::InvalidMaterial, "nonfinite material rejected");
}
}

int main() {
    coordinatesAndClipping(); provenanceAndMaterials();
    std::printf("Frame-contract checks: %d, failures: %d\n",checks,failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
