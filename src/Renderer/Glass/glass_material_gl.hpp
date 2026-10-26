#pragma once

// Loader-independent GL 3.3 uniform binding for shaders/glass_material.frag.
// This owns neither a context nor a GL program/texture. There are no pixel reads.
// Shader linking, sampler binding, immutable source capture, draw submission,
// GL state restoration and resource lifetime remain the host's responsibility.

#include "glass_frame_contract.hpp"
#include "glass_profile.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace stack::glass::gl {

// Use small adapter functions around the host's actual GL loader. A user pointer
// avoids global loader state and avoids assuming a platform GL calling convention.
struct UniformDispatch {
    void* user = nullptr;
    std::int32_t (*getUniformLocation)(void*, std::uint32_t, const char*) = nullptr;
    void (*useProgram)(void*, std::uint32_t) = nullptr;
    void (*uniform1i)(void*, std::int32_t, std::int32_t) = nullptr;
    void (*uniform1f)(void*, std::int32_t, float) = nullptr;
    void (*uniform2f)(void*, std::int32_t, float, float) = nullptr;
    void (*uniform3f)(void*, std::int32_t, float, float, float) = nullptr;
    void (*uniform4fv)(void*, std::int32_t, std::int32_t, const float*) = nullptr;
    bool valid() const {
        return getUniformLocation && useProgram && uniform1i && uniform1f &&
               uniform2f && uniform3f && uniform4fv;
    }
};

// Supply queried limits from the current context; do not assume a device class.
struct ContextLimits {
    int fragmentTextureUnits = 0;
    int maximumTextureSize = 0;
    bool valid() const { return fragmentTextureUnits > 0 && maximumTextureSize > 0; }
};

struct RoundedShape {
    Vec2 centerLocalPx{};
    Vec2 halfSizePx{1, 1};
    float radiusPx = 1;
};
struct Geometry {
    // gl_FragCoord and this origin are bottom-left physical framebuffer pixels.
    // Shape centers/extents are GROUP-LOCAL; keep desktop positions out of them.
    Vec2 localOriginFramebufferPx{};
    std::array<RoundedShape, 4> shapes{};
    int shapeCount = 1;
    float mergePx = 0;
};

// These are safe example values, not a named fitted preset or private Apple
// constants. Load the selected versioned preset explicitly in the host. Geometry,
// source mapping and DPI remain per draw; a fitted presentation-pixel preset
// must not be blindly interpreted as native logical-point dimensions.
struct MaterialParameters {
    float materialize = 1;
    float bodyRadiusPx = 100;
    float crownHeightPx = 24;
    float crownAnisotropy = 0.35f;
    EdgeProfile edge{22, 27, 0.12f, EdgeProfileKind::Monotone, 0.15696f};
    float bevelWidthPx = 7;
    float bevelHeightPx = 3;
    Vec2 tilt{};
    float ior = 1.46f;
    float bodyReflection = 0.65f;
    float rimReflection = 0.58f;
    float innerShade = 0.32f;
    float innerShadeWidthPx = 6;
    float innerShadeInsetPx = 2;
    float lipOffsetPx = 2;
    float lipWidthPx = 1.25f;
    float lipGain = 0.35f;
    float outerContour = 0.18f;
    float contentReflection = 0.90f;
    float neutralContrast = 0.30f;
    float chromaGamma = 0.92f;
    float adaptation = 1.0f;
    float environmentGain = 1;
    // upper center/width and lower center/width in reflected direction.y
    std::array<float, 4> lightShape{0.72f, 0.30f, -0.55f, 0.40f};
    Vec2 lightGain{1.35f, 1.30f};
    bool useLightProfile = false;
    std::array<float, 16> lightProfile{0.1f, 0.3f, 0.8f, 1.2f, 1.4f, 1.2f, 0.5f, 0.15f,
                                       0.15f, 0.4f, 0.8f, 1.2f, 1.3f, 1.4f, 1.2f, 0.8f};
    float ambient = 0.025f;
    float bodyPower = 1.4f;
    float roughnessLod = 0;
    float tintAmount = 0;
    Vec3 tintLinear{1, 1, 1};
    float shadowOpacity = 0.20f;
    float shadowSigmaPx = 27;
    Vec2 shadowOffsetPx{0, -43};
    float causticGain = 0.14f;
    float causticWidthPx = 26;
    float contactGain = 0;
    Vec2 contactLocalPx{};
    float contactRadiusPx = 40;
};

enum class FilterQuality : int { Isotropic = 0, AnisotropicFourTap = 1 };
enum class OutputMode : int { OpaqueReference = 0, PremultipliedLayer = 1 };
enum class DebugView : int { Result = 0, Normal = 1, Displacement = 2, Reflection = 3,
                             FilterFootprint = 4, Shadow = 5 };
struct RenderControls {
    FilterQuality filter = FilterQuality::AnisotropicFourTap;
    OutputMode output = OutputMode::PremultipliedLayer;
    DebugView debug = DebugView::Result;
};

struct SourceBinding {
    int textureUnit = 0; // host binds the immutable sampler2D on this unit
    int availableMipLevels = 1;
    float maxMip = 0;
    PixelExtent framebufferExtent{}; // destination viewport; converts TL contract to GL BL
    // BORROWED references. They and the texture allocation must outlive the draw.
    // Metadata constness cannot stop another GPU pass from writing the texture;
    // the host scheduler must enforce immutable captured contents and fences.
    const BackdropSnapshot* immutableSource = nullptr;
    const GlassPassContract* pass = nullptr;
};

struct UniformPacket {
    Geometry geometry;
    MaterialParameters material;
    SourceBinding source;
    RenderControls controls;
};

enum class UploadError {
    None, NotInitialized, InvalidDispatch, InvalidProgram, InvalidContextLimits,
    InactiveRequiredUniform, InvalidGeometry, InvalidMaterial, InvalidSource,
    SourceContractViolation, UnsupportedSourceMapping, InvalidRenderControls
};
struct UploadResult {
    UploadError error = UploadError::None;
    const char* field = "";
    ContractError contractError = ContractError::None;
    explicit operator bool() const { return error == UploadError::None; }
};

inline bool within(float v, float low, float high) {
    return std::isfinite(v) && v >= low && v <= high;
}
inline bool lengthValue(float v, float minimum = 0) {
    // Numerical safety guard for this screen-space shader, not a style rule.
    return within(v, minimum, 1048576.0f);
}
inline bool localPoint(Vec2 p) {
    return finite(p) && std::abs(p.x) <= 1048576.0f && std::abs(p.y) <= 1048576.0f;
}
inline bool unitInterval(float v) { return within(v, 0, 1); }

inline UploadResult validate(const UniformPacket& p, ContextLimits limits) {
    if (!limits.valid()) return {UploadError::InvalidContextLimits, "context limits"};
    const auto& g = p.geometry;
    if (g.shapeCount < 1 || g.shapeCount > 4 || !localPoint(g.localOriginFramebufferPx) ||
        !lengthValue(g.mergePx)) return {UploadError::InvalidGeometry, "geometry count/origin/merge"};
    // All four records are uploaded, even though only shapeCount are evaluated.
    for (const auto& s : g.shapes)
        if (!localPoint(s.centerLocalPx) || !lengthValue(s.halfSizePx.x, 0.01f) ||
            !lengthValue(s.halfSizePx.y, 0.01f) || !lengthValue(s.radiusPx) ||
            s.radiusPx > std::min(s.halfSizePx.x, s.halfSizePx.y))
            return {UploadError::InvalidGeometry, "shape center/size/radius"};

    const auto& m = p.material;
    if (!unitInterval(m.materialize) || !lengthValue(m.bodyRadiusPx, 0.01f) ||
        !lengthValue(m.crownHeightPx) || !unitInterval(m.crownAnisotropy) ||
        !m.edge.valid() || !lengthValue(m.edge.widthPx, 0.01f) ||
        !lengthValue(m.edge.reachPx) || !within(m.edge.peakFraction, 0.02f, 0.98f) ||
        !within(m.edge.circularRegularization, 0.01f, 0.5f) ||
        !lengthValue(m.bevelWidthPx, 0.01f) || !lengthValue(m.bevelHeightPx) ||
        !within(m.tilt.x, -64, 64) || !within(m.tilt.y, -64, 64) || !within(m.ior, 1, 10))
        return {UploadError::InvalidMaterial, "geometry/optical material controls"};
    if (!within(m.bodyReflection, 0, 64) || !within(m.rimReflection, 0, 64) ||
        !unitInterval(m.innerShade) || !lengthValue(m.innerShadeWidthPx, 0.01f) ||
        !lengthValue(m.innerShadeInsetPx) ||
        !lengthValue(m.lipOffsetPx) || !lengthValue(m.lipWidthPx, 0.01f) ||
        !within(m.lipGain, 0, 64) || !unitInterval(m.outerContour) ||
        !unitInterval(m.contentReflection) || !unitInterval(m.neutralContrast) ||
        !within(m.chromaGamma, 0.5f, 2.0f) || !unitInterval(m.adaptation) ||
        !within(m.environmentGain, 0, 64) ||
        !within(m.lightShape[0], -1, 1) || !within(m.lightShape[1], 0.01f, 4) ||
        !within(m.lightShape[2], -1, 1) || !within(m.lightShape[3], 0.01f, 4) ||
        !within(m.lightGain.x, 0, 64) || !within(m.lightGain.y, 0, 64) ||
        !within(m.ambient, 0, 64) || !within(m.bodyPower, 0.05f, 64))
        return {UploadError::InvalidMaterial, "reflection/light controls"};
    for (const float control : m.lightProfile)
        if (!within(control, 0, 8)) return {UploadError::InvalidMaterial, "nonnegative light-profile controls"};
    if (!within(m.roughnessLod, 0, p.source.maxMip) || !unitInterval(m.tintAmount) ||
        !within(m.tintLinear.x, 0, 64) || !within(m.tintLinear.y, 0, 64) ||
        !within(m.tintLinear.z, 0, 64) || !unitInterval(m.shadowOpacity) ||
        !lengthValue(m.shadowSigmaPx, 0.25f) || !localPoint(m.shadowOffsetPx) ||
        !within(m.causticGain, 0, 64) || !lengthValue(m.causticWidthPx, 0.01f) ||
        !within(m.contactGain, 0, 64) || !localPoint(m.contactLocalPx) ||
        !lengthValue(m.contactRadiusPx, 0.01f))
        return {UploadError::InvalidMaterial, "source/shadow/contact controls"};

    const auto& c = p.controls;
    if ((c.filter != FilterQuality::Isotropic && c.filter != FilterQuality::AnisotropicFourTap) ||
        (c.output != OutputMode::OpaqueReference && c.output != OutputMode::PremultipliedLayer) ||
        static_cast<int>(c.debug) < 0 || static_cast<int>(c.debug) > 5 ||
        (c.debug != DebugView::Result && c.output != OutputMode::OpaqueReference))
        return {UploadError::InvalidRenderControls, "filter/output/debug"};

    const auto& b = p.source;
    if (!b.immutableSource || !b.pass || !b.framebufferExtent.valid() ||
        b.framebufferExtent.width > 1048576 || b.framebufferExtent.height > 1048576 ||
        b.textureUnit < 0 || b.textureUnit >= limits.fragmentTextureUnits || b.availableMipLevels < 1 ||
        !within(b.maxMip, 0, static_cast<float>(b.availableMipLevels - 1)))
        return {UploadError::InvalidSource, "source pointer/unit/mips/framebuffer"};
    if (const auto e = validatePassSource(*b.immutableSource, *b.pass); e != ContractError::None)
        return {UploadError::SourceContractViolation, "immutable source contract", e};
    const auto& source = b.immutableSource->description();
    const auto& mapping = source.mapping;
    const auto& region = mapping.texelRegionTopLeft;
    const auto& capture = mapping.capturedFramebufferPxTL;
    if (!localPoint(capture.minimum) || !localPoint(capture.maximum) ||
        !lengthValue(capture.maximum.x - capture.minimum.x, 0.01f) ||
        !lengthValue(capture.maximum.y - capture.minimum.y, 0.01f))
        return {UploadError::InvalidSource, "capture coordinates exceed shader numeric domain"};
    // The anisotropic filter squares its 2x2 covariance again to find the
    // eigenvalues. Merely checking each input for finiteness does not prevent
    // overflow there. Adjacent source positions differ by at most 1+2*reach;
    // bound the corresponding texel-space Jacobian with substantial float margin.
    const double texelScale = std::max(
        static_cast<double>(mapping.textureExtent.width) / (capture.maximum.x - capture.minimum.x),
        static_cast<double>(mapping.textureExtent.height) / (capture.maximum.y - capture.minimum.y));
    if ((1.0 + 2.0 * m.edge.reachPx) * texelScale > 1.0e8)
        return {UploadError::InvalidMaterial, "warp/source scale exceeds filter numeric domain"};
    // This exact shader maps one rectangular framebuffer ROI over a whole texture.
    // Subrect atlases, top-left UV textures, array layers and base-mip aliases need
    // another shader transform; silently treating them as full textures is wrong.
    if (mapping.uvZeroOrigin != PixelOrigin::BottomLeft || region.x0 != 0 || region.y0 != 0 ||
        region.x1 != mapping.textureExtent.width || region.y1 != mapping.textureExtent.height ||
        source.image.mipLevel != 0 || source.image.arrayLayer != 0)
        return {UploadError::UnsupportedSourceMapping, "shader requires full-texture ROI, bottom-left UV, base 2D image"};
    if (mapping.textureExtent.width > limits.maximumTextureSize ||
        mapping.textureExtent.height > limits.maximumTextureSize)
        return {UploadError::InvalidSource, "texture exceeds current context limit"};
    int maximumMipLevels = 1;
    for (int side = std::max(mapping.textureExtent.width, mapping.textureExtent.height); side > 1; side /= 2)
        ++maximumMipLevels;
    if (b.availableMipLevels > maximumMipLevels)
        return {UploadError::InvalidSource, "declared mip count exceeds texture dimensions"};
    const auto expectedOutput = c.output == OutputMode::OpaqueReference ?
        CompositeRepresentation::OpaqueReplacement : CompositeRepresentation::PremultipliedSurface;
    if (b.pass->output != expectedOutput)
        return {UploadError::InvalidRenderControls, "pass representation differs from shader output"};
    return {};
}

inline std::array<float, 4> sourceRectBottomLeft(const SourceBinding& s) {
    // Preconditions: validate(packet, limits) succeeded for this same immutable packet.
    const auto& r = s.immutableSource->description().mapping.capturedFramebufferPxTL;
    return {r.minimum.x, s.framebufferExtent.height - r.maximum.y,
            r.maximum.x - r.minimum.x, r.maximum.y - r.minimum.y};
}

enum class InactiveUniformPolicy { Allow, RequireAll };

class MaterialUniforms {
public:
    enum Slot : std::size_t {
        Backdrop, SourceRectPx, LocalOriginPx, ShapeCount, Shapes, Radii, MergePx,
        Materialize, BodyRadiusPx, CrownHeightPx, CrownAnisotropy, EdgeWidthPx, EdgeReachPx, EdgePeak, EdgeRegularization,
        ProfileKind, BevelWidthPx, BevelHeightPx, Tilt, Ior, BodyReflection, RimReflection,
        InnerShade, InnerShadeWidthPx, InnerShadeInsetPx, LipOffsetPx, LipWidthPx, LipGain, OuterContour,
        ContentReflection, NeutralContrast, ChromaGamma, Adaptation,
        EnvironmentGain, LightShape, LightGain, UseLightProfile, LightProfile, Ambient, BodyPower,
        RoughnessLod, TintAmount, TintLinear, ShadowOpacity, ShadowSigmaPx, ShadowOffsetPx,
        CausticGain, CausticWidthPx, ContactGain, ContactLocalPx, ContactRadiusPx, MaxMip,
        Filter, Output, Debug, Count
    };

    inline static constexpr std::array<const char*, Count> names{
        "uBackdrop", "uSourceRectPx", "uLocalOriginPx", "uShapeCount", "uShapes[0]", "uRadii", "uMergePx",
        "uMaterialize", "uBodyRadiusPx", "uCrownHeightPx", "uCrownAnisotropy", "uEdgeWidthPx", "uEdgeReachPx", "uEdgePeak", "uEdgeRegularization",
        "uProfileKind", "uBevelWidthPx", "uBevelHeightPx", "uTilt", "uIor", "uBodyReflection", "uRimReflection",
        "uInnerShade", "uInnerShadeWidthPx", "uInnerShadeInsetPx", "uLipOffsetPx", "uLipWidthPx", "uLipGain", "uOuterContour",
        "uContentReflection", "uNeutralContrast", "uChromaGamma", "uAdaptation",
        "uEnvironmentGain", "uLightShape", "uLightGain", "uUseLightProfile", "uLightProfile[0]", "uAmbient", "uBodyPower",
        "uRoughnessLod", "uTintAmount", "uTintLinear", "uShadowOpacity", "uShadowSigmaPx", "uShadowOffsetPx",
        "uCausticGain", "uCausticWidthPx", "uContactGain", "uContactLocalPx", "uContactRadiusPx", "uMaxMip",
        "uFilterQuality", "uOutputMode", "uDebug"
    };

    UploadResult initialize(UniformDispatch dispatch, std::uint32_t linkedProgram,
                            ContextLimits limits, InactiveUniformPolicy policy = InactiveUniformPolicy::Allow) {
        ready_ = false;
        if (!dispatch.valid()) return {UploadError::InvalidDispatch, "uniform dispatch"};
        if (linkedProgram == 0) return {UploadError::InvalidProgram, "linked program"};
        if (!limits.valid()) return {UploadError::InvalidContextLimits, "context limits"};
        dispatch_ = dispatch; program_ = linkedProgram; limits_ = limits;
        inactiveCount_ = 0;
        const char* firstInactive = nullptr;
        for (std::size_t i = 0; i < Count; ++i) {
            locations_[i] = dispatch_.getUniformLocation(dispatch_.user, program_, names[i]);
            if (locations_[i] < 0) { ++inactiveCount_; if (!firstInactive) firstInactive = names[i]; }
        }
        if (inactiveCount_ && policy == InactiveUniformPolicy::RequireAll)
            return {UploadError::InactiveRequiredUniform, firstInactive};
        ready_ = true;
        return {};
    }

    // Reinitialize after relinking or changing the program's share-group context.
    // Cached uniform locations are invalidated by a successful program relink.
    bool ready() const { return ready_; }
    std::size_t inactiveCount() const { return inactiveCount_; }
    std::uint32_t program() const { return program_; }

    UploadResult upload(const UniformPacket& p) const {
        if (!ready_) return {UploadError::NotInitialized, "uniform cache"};
        if (const auto r = validate(p, limits_); !r) return r;
        // No GL calls above this point: rejected draw parameters cannot partially
        // mutate the current program or leave a mixture of old/new uniforms.
        dispatch_.useProgram(dispatch_.user, program_);
        const auto& g = p.geometry; const auto& m = p.material;
        i(Backdrop, p.source.textureUnit);
        const auto sourceRect = sourceRectBottomLeft(p.source); v4(SourceRectPx, sourceRect.data());
        v2(LocalOriginPx, g.localOriginFramebufferPx); i(ShapeCount, g.shapeCount);
        std::array<float, 16> shapes{}; std::array<float, 4> radii{};
        for (std::size_t n = 0; n < 4; ++n) {
            shapes[n * 4] = g.shapes[n].centerLocalPx.x; shapes[n * 4 + 1] = g.shapes[n].centerLocalPx.y;
            shapes[n * 4 + 2] = g.shapes[n].halfSizePx.x; shapes[n * 4 + 3] = g.shapes[n].halfSizePx.y;
            radii[n] = g.shapes[n].radiusPx;
        }
        v4(Shapes, shapes.data(), 4); v4(Radii, radii.data()); f(MergePx, g.mergePx);
        f(Materialize, m.materialize); f(BodyRadiusPx, m.bodyRadiusPx); f(CrownHeightPx, m.crownHeightPx);
        f(CrownAnisotropy, m.crownAnisotropy);
        f(EdgeWidthPx, m.edge.widthPx); f(EdgeReachPx, m.edge.reachPx); f(EdgePeak, m.edge.peakFraction);
        f(EdgeRegularization, m.edge.circularRegularization); i(ProfileKind, static_cast<int>(m.edge.kind));
        f(BevelWidthPx, m.bevelWidthPx); f(BevelHeightPx, m.bevelHeightPx); v2(Tilt, m.tilt); f(Ior, m.ior);
        f(BodyReflection, m.bodyReflection); f(RimReflection, m.rimReflection); f(InnerShade, m.innerShade);
        f(InnerShadeWidthPx, m.innerShadeWidthPx); f(InnerShadeInsetPx, m.innerShadeInsetPx);
        f(LipOffsetPx, m.lipOffsetPx); f(LipWidthPx, m.lipWidthPx);
        f(LipGain, m.lipGain); f(OuterContour, m.outerContour); f(ContentReflection, m.contentReflection);
        f(NeutralContrast, m.neutralContrast); f(ChromaGamma, m.chromaGamma); f(Adaptation, m.adaptation);
        f(EnvironmentGain, m.environmentGain); v4(LightShape, m.lightShape.data()); v2(LightGain, m.lightGain);
        i(UseLightProfile, m.useLightProfile ? 1 : 0); v4(LightProfile, m.lightProfile.data(), 4);
        f(Ambient, m.ambient); f(BodyPower, m.bodyPower); f(RoughnessLod, m.roughnessLod);
        f(TintAmount, m.tintAmount); v3(TintLinear, m.tintLinear); f(ShadowOpacity, m.shadowOpacity);
        f(ShadowSigmaPx, m.shadowSigmaPx); v2(ShadowOffsetPx, m.shadowOffsetPx);
        f(CausticGain, m.causticGain); f(CausticWidthPx, m.causticWidthPx); f(ContactGain, m.contactGain);
        v2(ContactLocalPx, m.contactLocalPx); f(ContactRadiusPx, m.contactRadiusPx); f(MaxMip, p.source.maxMip);
        i(Filter, static_cast<int>(p.controls.filter)); i(Output, static_cast<int>(p.controls.output));
        i(Debug, static_cast<int>(p.controls.debug));
        return {};
    }

private:
    void i(Slot s, int value) const { if (locations_[s] >= 0) dispatch_.uniform1i(dispatch_.user, locations_[s], value); }
    void f(Slot s, float value) const { if (locations_[s] >= 0) dispatch_.uniform1f(dispatch_.user, locations_[s], value); }
    void v2(Slot s, Vec2 value) const {
        if (locations_[s] >= 0) dispatch_.uniform2f(dispatch_.user, locations_[s], value.x, value.y);
    }
    void v3(Slot s, Vec3 value) const {
        if (locations_[s] >= 0) dispatch_.uniform3f(dispatch_.user, locations_[s], value.x, value.y, value.z);
    }
    void v4(Slot s, const float* value, int count = 1) const {
        if (locations_[s] >= 0) dispatch_.uniform4fv(dispatch_.user, locations_[s], count, value);
    }
    UniformDispatch dispatch_{};
    ContextLimits limits_{};
    std::uint32_t program_ = 0;
    std::array<std::int32_t, Count> locations_{};
    std::size_t inactiveCount_ = 0;
    bool ready_ = false;
};

} // namespace stack::glass::gl
