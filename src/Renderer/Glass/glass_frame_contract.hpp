#pragma once

// Original C++17 handoff scaffold. These are metadata and pure functions, not a
// GL backend. The host must enforce resource lifetime and immutable GPU content.
#include "glass_shape.hpp"
#include <cstdint>
#include <utility>
#include <vector>

namespace stack::glass {

struct GenerationalId {
    std::uint64_t value = 0;
    std::uint32_t generation = 0; // start at 1; increment before reusing value
    bool valid() const { return value != 0 && generation != 0; }
};
inline bool operator==(GenerationalId a, GenerationalId b) {
    return a.value == b.value && a.generation == b.generation;
}
inline bool operator!=(GenerationalId a, GenerationalId b) { return !(a == b); }

struct ContextKey {
    std::uint64_t nameSpace = 0; // application/session namespace, not an address
    GenerationalId context;
    bool valid() const { return nameSpace != 0 && context.valid(); }
};
inline bool operator==(ContextKey a, ContextKey b) {
    return a.nameSpace == b.nameSpace && a.context == b.context;
}
inline bool operator!=(ContextKey a, ContextKey b) { return !(a == b); }

struct ViewportKey {
    ContextKey owner;
    GenerationalId viewport; // recreate generation on platform-window replacement
    bool valid() const { return owner.valid() && viewport.valid(); }
};
inline bool operator==(ViewportKey a, ViewportKey b) {
    return a.owner == b.owner && a.viewport == b.viewport;
}
inline bool operator!=(ViewportKey a, ViewportKey b) { return !(a == b); }

struct VisualKey {
    ContextKey owner;
    std::uint64_t itemNamespace = 0; // distinguishes independent editor views
    std::uint32_t imguiId = 0;
    std::uint32_t generation = 1;
    std::uint16_t role = 0;
    // Deliberately no viewport: undocking should not restart a live spring.
    bool valid() const {
        return owner.valid() && itemNamespace != 0 && imguiId != 0 && generation != 0;
    }
};
inline bool operator==(const VisualKey& a, const VisualKey& b) {
    return a.owner == b.owner && a.itemNamespace == b.itemNamespace &&
           a.imguiId == b.imguiId && a.generation == b.generation && a.role == b.role;
}

struct GlassGroupKey {
    ContextKey owner;
    GenerationalId group;
    bool valid() const { return owner.valid() && group.valid(); }
};
inline bool operator==(GlassGroupKey a, GlassGroupKey b) {
    return a.owner == b.owner && a.group == b.group;
}

enum class PixelOrigin : std::uint8_t { TopLeft, BottomLeft };
struct PixelExtent {
    int width = 0, height = 0;
    bool valid() const { return width > 0 && height > 0; }
};

// All rectangles use half-open bounds [minimum, maximum). A point is a continuous
// position; framebuffer pixel centers are n+0.5. No helper adds a half texel.
struct PixelRect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty() const { return x1 <= x0 || y1 <= y0; }
    int width() const { return empty() ? 0 : x1 - x0; }
    int height() const { return empty() ? 0 : y1 - y0; }
};
struct ScissorRect {
    int x = 0, y = 0, width = 0, height = 0;
    bool empty() const { return width <= 0 || height <= 0; }
};
inline bool nonemptyFinite(Bounds2 r) {
    return finite(r.minimum) && finite(r.maximum) &&
           r.maximum.x > r.minimum.x && r.maximum.y > r.minimum.y;
}
inline bool containsBounds(Bounds2 outer, Bounds2 inner) {
    return nonemptyFinite(outer) && nonemptyFinite(inner) &&
           inner.minimum.x >= outer.minimum.x && inner.minimum.y >= outer.minimum.y &&
           inner.maximum.x <= outer.maximum.x && inner.maximum.y <= outer.maximum.y;
}

struct ViewportTransform {
    Vec2 displayPos{};                  // ImDrawData::DisplayPos, logical desktop
    Vec2 framebufferScale{1.0f, 1.0f};  // per-viewport ImDrawData::FramebufferScale
    PixelExtent framebuffer;           // actual allocated physical attachment
    bool valid() const {
        return finite(displayPos) && finite(framebufferScale) && framebufferScale.x > 0 &&
               framebufferScale.y > 0 && framebuffer.valid();
    }
};

// Preconditions: a validated transform and finite points. Canonical intermediate
// space is viewport-local top-left physical pixels, independent of GL clip origin.
inline Vec2 logicalToFramebufferTopLeft(Vec2 p, const ViewportTransform& t) {
    return {(p.x - t.displayPos.x) * t.framebufferScale.x,
            (p.y - t.displayPos.y) * t.framebufferScale.y};
}
inline Vec2 framebufferTopLeftToLogical(Vec2 p, const ViewportTransform& t) {
    return {p.x / t.framebufferScale.x + t.displayPos.x,
            p.y / t.framebufferScale.y + t.displayPos.y};
}
inline Vec2 logicalToFramebuffer(Vec2 p, const ViewportTransform& t, PixelOrigin origin) {
    const Vec2 f = logicalToFramebufferTopLeft(p, t);
    return origin == PixelOrigin::TopLeft ? f : Vec2{f.x, t.framebuffer.height - f.y};
}
inline Bounds2 logicalBoundsToFramebufferTopLeft(Bounds2 r, const ViewportTransform& t) {
    return {logicalToFramebufferTopLeft(r.minimum, t),
            logicalToFramebufferTopLeft(r.maximum, t)};
}

// Conservative rounded-out bounds preserve partially covered edge pixels.
// Clamp before integer conversion; an empty or invalid clip produces no draw.
inline PixelRect physicalClipTopLeft(Bounds2 logicalClip, const ViewportTransform& t) {
    if (!t.valid() || !nonemptyFinite(logicalClip)) return {};
    Bounds2 r = logicalBoundsToFramebufferTopLeft(logicalClip, t);
    if (!nonemptyFinite(r)) return {};
    const float w = static_cast<float>(t.framebuffer.width);
    const float h = static_cast<float>(t.framebuffer.height);
    r.minimum = {clamp(r.minimum.x, 0, w), clamp(r.minimum.y, 0, h)};
    r.maximum = {clamp(r.maximum.x, 0, w), clamp(r.maximum.y, 0, h)};
    if (!nonemptyFinite(r)) return {};
    return {static_cast<int>(std::floor(r.minimum.x)), static_cast<int>(std::floor(r.minimum.y)),
            static_cast<int>(std::ceil(r.maximum.x)), static_cast<int>(std::ceil(r.maximum.y))};
}
inline ScissorRect scissorForLogicalClip(Bounds2 logicalClip, const ViewportTransform& t,
                                       PixelOrigin scissorOrigin = PixelOrigin::BottomLeft) {
    const PixelRect r = physicalClipTopLeft(logicalClip, t);
    if (r.empty()) return {};
    return {r.x0, scissorOrigin == PixelOrigin::BottomLeft ? t.framebuffer.height - r.y1 : r.y0,
            r.width(), r.height()};
}

struct SourceMapping {
    Bounds2 capturedFramebufferPxTL{}; // source content domain, including required padding
    PixelRect texelRegionTopLeft{};    // region in an atlas, not necessarily the full texture
    PixelExtent textureExtent{};
    PixelOrigin uvZeroOrigin = PixelOrigin::BottomLeft;
    bool valid() const {
        const auto r = texelRegionTopLeft;
        return nonemptyFinite(capturedFramebufferPxTL) && textureExtent.valid() && !r.empty() &&
               r.x0 >= 0 && r.y0 >= 0 && r.x1 <= textureExtent.width && r.y1 <= textureExtent.height;
    }
};

// No clamping: insufficient padding is a capture-contract bug, not something to
// conceal by sticking samples to the ROI boundary. The host validates the planned
// sample domain, including optical reach and filter support, before drawing.
inline Vec2 framebufferTopLeftToSourceUV(Vec2 p, const SourceMapping& s) {
    const Vec2 span = s.capturedFramebufferPxTL.maximum - s.capturedFramebufferPxTL.minimum;
    const Vec2 q = p - s.capturedFramebufferPxTL.minimum;
    const float tx = s.texelRegionTopLeft.x0 + q.x / span.x * s.texelRegionTopLeft.width();
    const float ty = s.texelRegionTopLeft.y0 + q.y / span.y * s.texelRegionTopLeft.height();
    const float u = tx / s.textureExtent.width, v = ty / s.textureExtent.height;
    return {u, s.uvZeroOrigin == PixelOrigin::TopLeft ? v : 1.0f - v};
}
inline Vec2 logicalToSourceUV(Vec2 p, const ViewportTransform& t, const SourceMapping& s) {
    return framebufferTopLeftToSourceUV(logicalToFramebufferTopLeft(p, t), s);
}

struct TextureImageKey {
    std::uint64_t shareGroupNamespace = 0;
    GenerationalId allocation; // host allocation identity, NOT a reusable GLuint
    std::uint32_t mipLevel = 0, arrayLayer = 0;
    bool valid() const { return shareGroupNamespace != 0 && allocation.valid(); }
};
inline bool sameTextureAllocation(TextureImageKey a, TextureImageKey b) {
    return a.shareGroupNamespace == b.shareGroupNamespace && a.allocation == b.allocation;
}

enum class ColorEncoding : std::uint8_t { DisplayLinear, SRGBEncoded, Unspecified };
enum class ColorPrimaries : std::uint8_t { Rec709, DisplayP3, Rec2020, HostDefined };
enum class SourceAlpha : std::uint8_t { Opaque, Premultiplied };
struct DisplayColorContract {
    // Encoding of values returned to the material by sampling, not necessarily
    // texture storage: an sRGB texture with hardware decode is DisplayLinear here.
    ColorEncoding encoding = ColorEncoding::DisplayLinear;
    ColorPrimaries primaries = ColorPrimaries::Rec709;
    std::uint64_t hostColorSpaceId = 0; // required for HostDefined
    float referenceWhiteNits = 100.0f; // 1.0 in this display-linear representation
    SourceAlpha alpha = SourceAlpha::Opaque;
    // Values >1 are allowed; gamut/output encoding and highlight limits belong
    // to the host display pipeline. This does not authorize RAW-scene sampling.
    bool valid() const {
        return encoding == ColorEncoding::DisplayLinear && std::isfinite(referenceWhiteNits) &&
               referenceWhiteNits > 0 &&
               (primaries != ColorPrimaries::HostDefined || hostColorSpaceId != 0);
    }
};

struct GlassContribution {
    GlassGroupKey group;
    std::uint32_t opticalLayer = 0;
};
struct BackdropProvenance {
    bool complete = false; // host has enumerated every glass contribution
    bool containsSemanticUnderlay = false; // tracks/peer icons/labels may refract
    std::vector<GlassContribution> glass;   // empty for the default nonrecursive source
};
struct BackdropDescription {
    TextureImageKey image;
    ViewportKey viewport;
    GenerationalId captureScope;
    std::uint64_t frameSerial = 0, contentRevision = 0;
    SourceMapping mapping;
    DisplayColorContract color;
    int sampleCount = 1; // ordinary sampler2D source must already be resolved
    BackdropProvenance provenance;
};

class BackdropSnapshot {
public:
    explicit BackdropSnapshot(BackdropDescription description) : description_(std::move(description)) {}
    const BackdropDescription& description() const { return description_; }
private:
    const BackdropDescription description_;
    // Metadata immutability is not a GL fence. The host must retain the texture
    // and prohibit writes to its captured content until all scheduled readers end.
};

enum class SourceGlassPolicy : std::uint8_t { ExcludeAllGlass, AllowExplicitLowerLayers };
enum class CompositeRepresentation : std::uint8_t { OpaqueReplacement, PremultipliedSurface };
struct GlassPassContract {
    ViewportKey viewport;
    GlassGroupKey group;
    std::uint32_t opticalLayer = 0;
    std::uint64_t frameSerial = 0;
    TextureImageKey destination;
    Bounds2 requiredSampleDomainPxTL{}; // bounds expanded for warp AND filter support
    SourceGlassPolicy sourcePolicy = SourceGlassPolicy::ExcludeAllGlass;
    CompositeRepresentation output = CompositeRepresentation::OpaqueReplacement;
};

enum class ContractError : std::uint8_t {
    None, InvalidIdentity, InvalidMapping, InvalidColor, UnresolvedSource,
    UnknownGlassProvenance, FrameMismatch, ViewportMismatch, ResourceContextMismatch, TextureFeedback,
    InsufficientSourcePadding, UnexpectedGlass, RecursiveGlass, NonLowerGlass,
    UnsupportedSourceAlpha, InvalidMaterial
};

inline ContractError validateBackdrop(const BackdropSnapshot& snapshot) {
    const auto& b = snapshot.description();
    if (!b.image.valid() || !b.viewport.valid() || !b.captureScope.valid() || b.frameSerial == 0)
        return ContractError::InvalidIdentity;
    if (!b.mapping.valid()) return ContractError::InvalidMapping;
    if (!b.color.valid()) return ContractError::InvalidColor;
    if (b.sampleCount != 1) return ContractError::UnresolvedSource;
    if (!b.provenance.complete) return ContractError::UnknownGlassProvenance;
    for (const auto& contribution : b.provenance.glass)
        if (!contribution.group.valid() || contribution.group.owner != b.viewport.owner)
            return ContractError::InvalidIdentity;
    return ContractError::None;
}

inline ContractError validatePassSource(const BackdropSnapshot& snapshot, const GlassPassContract& p) {
    if (const auto e = validateBackdrop(snapshot); e != ContractError::None) return e;
    const auto& b = snapshot.description();
    if (!p.viewport.valid() || !p.group.valid() || !p.destination.valid() || p.frameSerial == 0 ||
        p.group.owner != p.viewport.owner) return ContractError::InvalidIdentity;
    if (p.frameSerial != b.frameSerial) return ContractError::FrameMismatch;
    if (p.viewport != b.viewport) return ContractError::ViewportMismatch;
    if (p.destination.shareGroupNamespace != b.image.shareGroupNamespace)
        return ContractError::ResourceContextMismatch;
    // Conservative rule: reject the whole same allocation, even another mip or
    // layer. Relax only with an explicit, independently verified GL hazard policy.
    if (sameTextureAllocation(b.image, p.destination)) return ContractError::TextureFeedback;
    if (!containsBounds(b.mapping.capturedFramebufferPxTL, p.requiredSampleDomainPxTL))
        return ContractError::InsufficientSourcePadding;
    if (b.color.alpha != SourceAlpha::Opaque) return ContractError::UnsupportedSourceAlpha;
    for (const auto& contribution : b.provenance.glass) {
        if (contribution.group == p.group) return ContractError::RecursiveGlass;
        if (p.sourcePolicy == SourceGlassPolicy::ExcludeAllGlass) return ContractError::UnexpectedGlass;
        if (contribution.opticalLayer >= p.opticalLayer) return ContractError::NonLowerGlass;
    }
    return ContractError::None;
}

// A material's independent quantities must not be overloaded into one opacity.
// These are validity constraints, not a claim that these defaults match Apple.
struct OpticalMaterialContract {
    LensProfile profile;
    float backdropPlaneZPx = 0.0f;
    float ior = 1.46f;
    float materialize = 1.0f;
    float roughness = 0.0f;
    float maximumWarpPx = 64.0f;
    float dispersionPx = 0.0f;
    Vec3 absorptionPerPx{};
};
inline ContractError validateMaterial(const OpticalMaterialContract& m) {
    const bool valid = m.profile.valid() && std::isfinite(m.backdropPlaneZPx) &&
        m.profile.centerZPx - m.profile.halfThicknessPx >= m.backdropPlaneZPx &&
        std::isfinite(m.ior) && m.ior >= 1.0f &&
        std::isfinite(m.materialize) && m.materialize >= 0 && m.materialize <= 1 &&
        std::isfinite(m.roughness) && m.roughness >= 0 && m.roughness <= 1 &&
        std::isfinite(m.maximumWarpPx) && m.maximumWarpPx >= 0 &&
        std::isfinite(m.dispersionPx) && m.dispersionPx >= 0 &&
        finite(m.absorptionPerPx) && m.absorptionPerPx.x >= 0 &&
        m.absorptionPerPx.y >= 0 && m.absorptionPerPx.z >= 0;
    return valid ? ContractError::None : ContractError::InvalidMaterial;
}

} // namespace stack::glass
