#pragma once

#include "glass_material_gl.hpp"
#include <optional>

namespace stack::glass::presets {

// Public-video appearance fit, not native-device parity or private Apple constants.
// Exact source: presets/clear-control-refined.json and its provenance metadata.
// The reference is a scaled/compressed presentation capture with an estimated
// backdrop. Its dimensions are not native UI points or a prescribed control size.
inline constexpr float clearControlV2ReferenceHalfHeightPx = 99.47765145624747f;
inline constexpr float clearControlV2AspectRatio =
    static_cast<float>(120.12570417879074 / 99.47765145624747);

// halfHeightPx is the caller's intended PHYSICAL framebuffer half-height. The
// factory scales every physical material length from the reference size, while
// preserving direction-space lighting, IOR, tone transfer and all other unitless
// controls. Geometry, placement, source capture, output and filter mode stay with
// the caller. A typical capsule uses halfWidth=halfHeight*AspectRatio and
// cornerRadius=halfHeight; these are recommendations, not enforced geometry.
//
// Invalid/too-small/too-large sizes return nullopt instead of silently clamping
// individual lengths and changing the preset's proportions. The accepted range
// follows the current material shader's numeric/positive-size validation domain.
inline std::optional<gl::MaterialParameters> clearControlV2(float halfHeightPx) {
    if (!std::isfinite(halfHeightPx) || halfHeightPx <= 0) return std::nullopt;
    const double scale = static_cast<double>(halfHeightPx) / clearControlV2ReferenceHalfHeightPx;
    if (!std::isfinite(scale) || scale <= 0) return std::nullopt;

    gl::MaterialParameters m;
    m.materialize = 1.0f;
    m.bodyRadiusPx = 99.47765145624747f;
    m.crownHeightPx = 22.53249505249296f;
    m.crownAnisotropy = 0.9290498117897042f;
    m.edge = {38.4f, 55.13f, 0.12f, EdgeProfileKind::Circular, 0.15696f};
    m.bevelWidthPx = 5.00000000013012f;
    m.bevelHeightPx = 1.0203849734248769f;
    m.tilt = {0.0f, 0.0f};
    m.ior = 1.46f;
    m.bodyReflection = 0.45713253977603785f;
    m.rimReflection = 0.050004520697637396f;
    m.innerShade = 0.45f;
    m.innerShadeWidthPx = 4.459885338733843f;
    m.innerShadeInsetPx = 2.0f;
    m.lipOffsetPx = 6.0f;
    m.lipWidthPx = 3.3f;
    m.lipGain = 0.25f;
    m.outerContour = 0.35f;
    m.contentReflection = 0.9728647736748406f;
    m.neutralContrast = 0.45f;
    m.chromaGamma = 0.9435035070053983f;
    m.adaptation = 1.0f;
    m.environmentGain = 1.0f;
    m.lightShape = {0.6877784334165146f, 0.5499940848533063f,
                    -0.6040609371120137f, 0.6499998089057563f};
    m.lightGain = {1.1624956720735657f, 1.1452419566994338f};
    m.useLightProfile = true;
    m.lightProfile = {1.2909485404596106f, 1.178906930760831f, 1.0670891344155955f, 0.9896331273541603f,
                      0.9686263485040799f, 0.8495233488129209f, 0.7553015404128061f, 0.8045592497584309f,
                      0.73765129477536f, 0.6357424205709106f, 0.7270507017720689f, 0.9318106243415557f,
                      1.1087420493417681f, 1.0038864183186622f, 0.5440336806246712f, 0.08323051779066433f};
    m.ambient = 0.025f;
    m.bodyPower = 0.5000007939699588f;
    m.roughnessLod = 0.0f;
    m.tintAmount = 0.0f;
    m.tintLinear = {1.0f, 1.0f, 1.0f};
    m.shadowOpacity = 0.31472128184367726f;
    m.shadowSigmaPx = 21.05012555756283f;
    m.shadowOffsetPx = {0.0f, -58.511976216763244f};
    m.causticGain = 0.23707803707811845f;
    m.causticWidthPx = 42.74587962271549f;
    m.contactGain = 0.0f;
    m.contactLocalPx = {0.0f, 0.0f};
    m.contactRadiusPx = 40.0f;

    struct LengthToScale { float* value; float minimum; };
    const std::array<LengthToScale, 13> lengths{{
        {&m.bodyRadiusPx, 0.01f}, {&m.crownHeightPx, 0.0f},
        {&m.edge.widthPx, 0.01f}, {&m.edge.reachPx, 0.0f},
        {&m.bevelWidthPx, 0.01f}, {&m.bevelHeightPx, 0.0f},
        {&m.innerShadeWidthPx, 0.01f}, {&m.innerShadeInsetPx, 0.0f},
        {&m.lipOffsetPx, 0.0f}, {&m.lipWidthPx, 0.01f},
        {&m.shadowSigmaPx, 0.25f}, {&m.causticWidthPx, 0.01f},
        {&m.contactRadiusPx, 0.01f}
    }};
    for (const auto& length : lengths) {
        const double value = static_cast<double>(*length.value) * scale;
        if (!std::isfinite(value) || value < length.minimum || value > 1048576.0)
            return std::nullopt;
        *length.value = static_cast<float>(value);
    }
    const std::array<float*, 4> coordinates{&m.shadowOffsetPx.x, &m.shadowOffsetPx.y,
                                           &m.contactLocalPx.x, &m.contactLocalPx.y};
    for (float* coordinate : coordinates) {
        const double value = static_cast<double>(*coordinate) * scale;
        if (!std::isfinite(value) || std::abs(value) > 1048576.0) return std::nullopt;
        *coordinate = static_cast<float>(value);
    }
    return m;
}

} // namespace stack::glass::presets
