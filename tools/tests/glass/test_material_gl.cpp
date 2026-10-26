#include "Renderer/Glass/glass_material_gl.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <vector>

using namespace stack::glass;
namespace material_gl = stack::glass::gl;

namespace {
int checks = 0, failures = 0;
void expect(bool condition, const char* message) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}

struct Declaration { std::string type; int count = 1; int location = -1; };
struct Upload { std::string name; int components = 0, count = 0; bool integer = false; std::vector<float> values; };
struct MockGl {
    std::map<std::string, Declaration> declarations;
    std::set<std::string> inactive;
    std::vector<Upload> uploads;
    int lookups = 0, useCalls = 0;
    std::uint32_t lastProgram = 0;
    bool unexpectedLookup = false;

    const Declaration* findLocation(int location, std::string& name) const {
        for (const auto& d : declarations) if (d.second.location == location) { name = d.first; return &d.second; }
        return nullptr;
    }
    void record(int location, int components, int count, bool integer, const float* values) {
        std::string name;
        const auto* d = findLocation(location, name);
        expect(d != nullptr, "uploaded location came from actual shader declaration lookup");
        if (!d) return;
        expect(inactive.count(name) == 0, "inactive location never uploaded");
        const int expectedComponents = d->type == "vec4" ? 4 : d->type == "vec3" ? 3 : d->type == "vec2" ? 2 : 1;
        const bool expectedInteger = d->type == "int" || d->type == "sampler2D";
        expect(components == expectedComponents && integer == expectedInteger,
               "GL upload type agrees with parsed GLSL declaration");
        expect(count == d->count, "uniform array element count agrees with GLSL declaration");
        uploads.push_back({name, components, count, integer, std::vector<float>(values, values + components * count)});
    }
    const Upload* findUpload(const char* name) const {
        for (auto i = uploads.rbegin(); i != uploads.rend(); ++i) if (i->name == name) return &*i;
        return nullptr;
    }
    material_gl::UniformDispatch dispatch() {
        return {this,
            [](void* user, std::uint32_t program, const char* name) -> std::int32_t {
                auto& m = *static_cast<MockGl*>(user); ++m.lookups;
                expect(program != 0, "lookup receives linked program identity");
                auto found = m.declarations.find(name);
                if (found == m.declarations.end()) { m.unexpectedLookup = true; return -1; }
                return m.inactive.count(name) ? -1 : found->second.location;
            },
            [](void* user, std::uint32_t program) { auto& m = *static_cast<MockGl*>(user); ++m.useCalls; m.lastProgram = program; },
            [](void* user, std::int32_t location, std::int32_t value) {
                const float v = static_cast<float>(value); static_cast<MockGl*>(user)->record(location, 1, 1, true, &v);
            },
            [](void* user, std::int32_t location, float value) { static_cast<MockGl*>(user)->record(location, 1, 1, false, &value); },
            [](void* user, std::int32_t location, float x, float y) {
                const float v[]{x, y}; static_cast<MockGl*>(user)->record(location, 2, 1, false, v);
            },
            [](void* user, std::int32_t location, float x, float y, float z) {
                const float v[]{x, y, z}; static_cast<MockGl*>(user)->record(location, 3, 1, false, v);
            },
            [](void* user, std::int32_t location, std::int32_t count, const float* v) {
                static_cast<MockGl*>(user)->record(location, 4, count, false, v);
            }};
    }
};

MockGl parseShader(const char* path) {
    std::ifstream input(path);
    expect(static_cast<bool>(input), "actual material GLSL file opens");
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::regex pattern(R"(uniform\s+(sampler2D|float|int|vec2|vec3|vec4)\s+(\w+)(?:\[(\d+)\])?\s*;)");
    MockGl mock;
    int location = 100;
    for (auto i = std::sregex_iterator(text.begin(), text.end(), pattern); i != std::sregex_iterator(); ++i) {
        const auto& match = *i;
        const int count = match[3].matched ? std::stoi(match[3].str()) : 1;
        const std::string name = match[2].str() + (match[3].matched ? "[0]" : "");
        mock.declarations.emplace(name, Declaration{match[1].str(), count, location});
        location += 13; // deliberately noncontiguous and unrelated to C++ slot order
    }
    // The shared v2 typed binding is retained. MaterialRenderer owns/cache-binds
    // v0.3 and graph-compound additions, exercised through native GL uploads.
    const std::map<std::string,Declaration> extensions{{"uLayerOpacity",{"float"}},{"uRestFillOpacity",{"float"}},
        {"uRestFillLinear",{"vec3"}},{"uWaterMode",{"float"}},{"uLogStrain",{"vec2"}},
        {"uJoinedCount",{"int"}},{"uJoinedShapes[0]",{"vec4",64}},{"uJoinedMetrics[0]",{"vec4",64}}};
    for(const auto& extension:extensions) {
        const auto found=mock.declarations.find(extension.first);
        expect(found!=mock.declarations.end() && found->second.type==extension.second.type && found->second.count==extension.second.count,
            "v0.3 extension has the expected scalar/vector type");
    }
    expect(mock.declarations.size() == material_gl::MaterialUniforms::Count+extensions.size(),
           "typed binding plus renderer extensions cover the current shader");
    return mock;
}

BackdropDescription validSource() {
    const ContextKey context{11, {1, 1}};
    BackdropDescription b;
    b.image = {44, {5, 1}, 0, 0}; b.viewport = {context, {2, 1}};
    b.captureScope = {8, 1}; b.frameSerial = 12; b.contentRevision = 2;
    b.mapping = {{{10, 20}, {810, 420}}, {0, 0, 400, 200}, {400, 200}, PixelOrigin::BottomLeft};
    b.provenance.complete = true; b.provenance.containsSemanticUnderlay = true;
    return b;
}
struct Fixture {
    BackdropSnapshot snapshot{validSource()};
    GlassPassContract pass;
    material_gl::UniformPacket packet;
    Fixture() {
        const auto& b = snapshot.description();
        pass.viewport = b.viewport; pass.group = {b.viewport.owner, {20, 1}};
        pass.frameSerial = b.frameSerial; pass.destination = {44, {6, 1}, 0, 0};
        pass.requiredSampleDomainPxTL = {{50, 50}, {760, 390}};
        pass.output = CompositeRepresentation::PremultipliedSurface;
        packet.source = {3, 9, 8, {1000, 700}, &snapshot, &pass};
        packet.geometry.localOriginFramebufferPx = {300.25f, 450.75f};
        packet.geometry.shapeCount = 4; packet.geometry.mergePx = 13;
        for (int i = 0; i < 4; ++i)
            packet.geometry.shapes[static_cast<std::size_t>(i)] = {{i * 40.25f, i * -15.75f}, {80.5f + i, 45.25f + i}, 20.5f + i};
        packet.material.roughnessLod = 1.25f;
        packet.material.lipGain = 0.37f; packet.material.contentReflection = 0.83f;
        packet.material.useLightProfile = true;
        for (std::size_t i = 0; i < packet.material.lightProfile.size(); ++i)
            packet.material.lightProfile[i] = static_cast<float>(i) * 0.125f;
        packet.material.tintLinear = {0.13f, 0.27f, 0.61f};
        packet.material.edge.kind = EdgeProfileKind::Fold;
    }
};

void testMappingAndCache(MockGl mock) {
    material_gl::MaterialUniforms cache;
    Fixture fixture;
    expect(static_cast<bool>(cache.initialize(mock.dispatch(), 71, {16, 8192}, material_gl::InactiveUniformPolicy::RequireAll)),
           "strict cache initializes against actual shader schema");
    expect(!mock.unexpectedLookup && cache.inactiveCount() == 0 && mock.lookups == material_gl::MaterialUniforms::Count,
           "all uniform names resolved exactly once");
    expect(static_cast<bool>(cache.upload(fixture.packet)), "valid typed packet uploads");
    expect(mock.lastProgram == 71 && mock.useCalls == 1, "uploader binds its linked program");
    expect(mock.uploads.size() == material_gl::MaterialUniforms::Count, "exactly one upload per active uniform");
    const auto check = [&mock](const char* name, std::initializer_list<float> values) {
        const auto* u = mock.findUpload(name);
        expect(u != nullptr, "important shader uniform was uploaded");
        if (u) expect(u->values == std::vector<float>(values), "uploaded semantic value matches typed input");
    };
    check("uBackdrop", {3}); check("uSourceRectPx", {10, 280, 800, 400});
    check("uLocalOriginPx", {300.25f, 450.75f}); check("uShapeCount", {4});
    check("uRadii", {20.5f, 21.5f, 22.5f, 23.5f}); check("uMergePx", {13});
    check("uProfileKind", {1}); check("uMaxMip", {8}); check("uRoughnessLod", {1.25f});
    check("uEdgeRegularization", {0.15696f});
    check("uTintLinear", {0.13f, 0.27f, 0.61f}); check("uLipGain", {0.37f}); check("uContentReflection", {0.83f});
    check("uNeutralContrast", {0.30f}); check("uChromaGamma", {0.92f}); check("uAdaptation", {1.0f});
    check("uInnerShadeInsetPx", {2}); check("uUseLightProfile", {1});
    const auto* lightProfile = mock.findUpload("uLightProfile[0]");
    expect(lightProfile && lightProfile->values == std::vector<float>(fixture.packet.material.lightProfile.begin(),
                                                                     fixture.packet.material.lightProfile.end()),
           "all sixteen light-profile controls uploaded in declared vec4 array order");
    check("uFilterQuality", {1}); check("uOutputMode", {1}); check("uDebug", {0});
    const auto* shapes = mock.findUpload("uShapes[0]");
    expect(shapes && shapes->values.size() == 16, "all four shape records uploaded in one vec4 array");
    if (shapes) for (int n = 0; n < 4; ++n) {
        const auto& s = fixture.packet.geometry.shapes[static_cast<std::size_t>(n)];
        const std::size_t offset = static_cast<std::size_t>(n) * 4;
        expect(shapes->values[offset] == s.centerLocalPx.x && shapes->values[offset + 1] == s.centerLocalPx.y &&
               shapes->values[offset + 2] == s.halfSizePx.x && shapes->values[offset + 3] == s.halfSizePx.y,
               "shape center/half-size layout matches GLSL xy/zw contract");
    }
    const int previousLookups = mock.lookups;
    expect(static_cast<bool>(cache.upload(fixture.packet)) && mock.lookups == previousLookups,
           "subsequent draws reuse cached locations without GL lookups");
    auto circular = fixture.packet;
    circular.material.edge = {38.4f, 55.13f, 0.12f, EdgeProfileKind::Circular, 0.15696f};
    expect(static_cast<bool>(cache.upload(circular)), "circular profile packet uploads");
    check("uProfileKind", {2}); check("uEdgeRegularization", {0.15696f});
    for (auto& d : mock.declarations) d.second.location += 1000;
    expect(static_cast<bool>(cache.initialize(mock.dispatch(), 72, {16, 8192})), "cache reinitializes after program relink");
    expect(static_cast<bool>(cache.upload(fixture.packet)) && mock.lastProgram == 72,
           "relinked program uses refreshed locations");
}

void testInactiveAndSetup(MockGl mock) {
    material_gl::MaterialUniforms cache;
    Fixture fixture;
    expect(cache.upload(fixture.packet).error == material_gl::UploadError::NotInitialized && mock.useCalls == 0,
           "uninitialized upload performs no GL calls");
    auto invalid = mock.dispatch(); invalid.uniform2f = nullptr;
    expect(cache.initialize(invalid, 71, {16, 8192}).error == material_gl::UploadError::InvalidDispatch && mock.lookups == 0,
           "incomplete GL dispatch rejected before lookup");
    expect(cache.initialize(mock.dispatch(), 0, {16, 8192}).error == material_gl::UploadError::InvalidProgram && mock.lookups == 0,
           "zero program rejected before lookup");
    expect(cache.initialize(mock.dispatch(), 71, {0, 8192}).error == material_gl::UploadError::InvalidContextLimits && mock.lookups == 0,
           "unqueried context limits rejected before lookup");
    mock.inactive = {"uContactGain", "uLipGain"};
    expect(static_cast<bool>(cache.initialize(mock.dispatch(), 71, {16, 8192})) && cache.inactiveCount() == 2,
           "optimized-out uniforms are counted and allowed by normal policy");
    expect(static_cast<bool>(cache.upload(fixture.packet)) && mock.uploads.size() == material_gl::MaterialUniforms::Count - 2,
           "inactive locations skipped safely");
    expect(cache.initialize(mock.dispatch(), 71, {16, 8192}, material_gl::InactiveUniformPolicy::RequireAll).error ==
               material_gl::UploadError::InactiveRequiredUniform && !cache.ready(),
           "strict shader-schema check rejects absent uniforms and invalidates cache");
}

void testValidationNoGlSideEffects(MockGl mock) {
    material_gl::MaterialUniforms cache;
    Fixture fixture;
    expect(static_cast<bool>(cache.initialize(mock.dispatch(), 71, {16, 8192})), "validation fixture initializes");
    const auto reject = [&cache, &mock](const material_gl::UniformPacket& packet, const char* name) {
        const auto uploads = mock.uploads.size(); const int uses = mock.useCalls, lookups = mock.lookups;
        const auto result = cache.upload(packet);
        expect(!result, name);
        expect(mock.uploads.size() == uploads && mock.useCalls == uses && mock.lookups == lookups,
               "invalid input cannot partially mutate any GL state");
    };
    auto p = fixture.packet; p.geometry.shapeCount = 0; reject(p, "zero shapes rejected");
    p = fixture.packet; p.geometry.shapeCount = 5; reject(p, "too many shapes rejected");
    p = fixture.packet; p.geometry.shapes[2].halfSizePx.x = 0; reject(p, "zero half extent rejected");
    p = fixture.packet; p.geometry.shapes[0].radiusPx = 100; reject(p, "oversized radius rejected rather than silently clamped");
    p = fixture.packet; p.material.edge.widthPx = 0; reject(p, "zero optical band rejected");
    p = fixture.packet; p.material.edge.peakFraction = 0.001f; reject(p, "peak outside shader policy rejected");
    p = fixture.packet; p.material.edge.circularRegularization = 0; reject(p, "zero circular regularization rejected");
    p = fixture.packet; p.material.materialize = 1.01f; reject(p, "invalid activation rejected");
    p = fixture.packet; p.material.ior = 0; reject(p, "invalid IOR rejected");
    p = fixture.packet; p.material.crownAnisotropy = 1.1f; reject(p, "invalid crown anisotropy rejected");
    p = fixture.packet; p.material.shadowOpacity = -0.1f; reject(p, "negative shadow opacity rejected");
    p = fixture.packet; p.material.contentReflection = 2; reject(p, "content reflection outside unit interval rejected");
    p = fixture.packet; p.material.innerShadeInsetPx = -1; reject(p, "negative inner-shade inset rejected");
    p = fixture.packet; p.material.lightProfile[15] = -0.1f; reject(p, "negative light-profile control rejected");
    p = fixture.packet; p.material.lightProfile[7] = 8.1f; reject(p, "light-profile control beyond bound rejected");
    p = fixture.packet; p.material.lightProfile[8] = std::numeric_limits<float>::quiet_NaN(); reject(p, "NaN light-profile control rejected");
    p = fixture.packet; p.material.neutralContrast = 1.1f; reject(p, "neutral contrast beyond unit interval rejected");
    p = fixture.packet; p.material.chromaGamma = 0; reject(p, "zero chroma gamma rejected");
    p = fixture.packet; p.material.adaptation = -1; reject(p, "negative adaptation rejected");
    p = fixture.packet; p.material.tintLinear.z = std::numeric_limits<float>::quiet_NaN(); reject(p, "NaN material rejected");
    p = fixture.packet; p.material.lipWidthPx = std::numeric_limits<float>::infinity(); reject(p, "infinite material rejected");
    p = fixture.packet; p.material.roughnessLod = 9; reject(p, "roughness beyond available mip budget rejected");
    p = fixture.packet; p.source.textureUnit = 16; reject(p, "sampler unit beyond queried context limit rejected");
    p = fixture.packet; p.source.immutableSource = nullptr; reject(p, "missing immutable source metadata rejected");
    p = fixture.packet; p.source.availableMipLevels = 20; reject(p, "impossible mip count rejected");
    p = fixture.packet; p.controls.filter = static_cast<material_gl::FilterQuality>(7); reject(p, "unknown filtering mode rejected");
    p = fixture.packet; p.controls.debug = material_gl::DebugView::Normal; reject(p, "opaque debug output cannot use premultiplied pass contract");
    p = fixture.packet; p.controls.output = material_gl::OutputMode::OpaqueReference; reject(p, "output representation mismatch rejected");
    auto pass = fixture.pass; pass.destination = fixture.snapshot.description().image;
    p = fixture.packet; p.source.pass = &pass; reject(p, "same texture feedback rejected through source contract");
    pass = fixture.pass; ++pass.frameSerial; reject(p, "stale source frame rejected");
    pass = fixture.pass; pass.requiredSampleDomainPxTL.maximum.x = 900; reject(p, "insufficient optical capture padding rejected");
    auto description = validSource(); description.mapping.texelRegionTopLeft = {20, 20, 200, 120};
    const BackdropSnapshot atlas(description); p = fixture.packet; p.source.immutableSource = &atlas;
    reject(p, "atlas subrect rejected because current shader only supports full-texture ROI");
    description = validSource(); description.mapping.uvZeroOrigin = PixelOrigin::TopLeft;
    const BackdropSnapshot upsideDown(description); p = fixture.packet; p.source.immutableSource = &upsideDown;
    reject(p, "unsupported UV orientation rejected");
    description = validSource(); description.color.encoding = ColorEncoding::SRGBEncoded;
    const BackdropSnapshot encoded(description); p = fixture.packet; p.source.immutableSource = &encoded;
    reject(p, "encoded source values rejected by display-linear source contract");
    description = validSource(); description.mapping.capturedFramebufferPxTL = {{0, 0}, {0.125f, 0.125f}};
    const BackdropSnapshot extremeDensity(description); pass = fixture.pass;
    pass.requiredSampleDomainPxTL = {{0.01f, 0.01f}, {0.12f, 0.12f}};
    p = fixture.packet; p.source.immutableSource = &extremeDensity; p.source.pass = &pass;
    p.material.edge.reachPx = 1000000;
    reject(p, "finite but overflowing anisotropic-filter Jacobian ratio rejected");
    expect(mock.uploads.empty() && mock.useCalls == 0, "entire rejection suite performed zero GL mutation calls");
}
}

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "Usage: test_material_gl path/to/glass_material.frag\n"); return EXIT_FAILURE; }
    const auto mock = parseShader(argv[1]);
    testMappingAndCache(mock); testInactiveAndSetup(mock); testValidationNoGlSideEffects(mock);
    std::printf("GL material binding checks: %d, failures: %d; parsed GLSL uniforms: %zu\n", checks, failures, mock.declarations.size());
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
