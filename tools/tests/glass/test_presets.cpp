#include "Renderer/Glass/glass_presets.hpp"
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
namespace preset = stack::glass::presets;

namespace {
int checks = 0, failures = 0;
void expect(bool condition, const char* message) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
using NumericObject = std::map<std::string, std::vector<float>>;

// Fixture reader for this flat object of numeric scalars/arrays only. This is not
// a production JSON parser or a new application dependency. The external frozen
// JSON is the test oracle; numbers are rounded once to the actual GPU float type.
NumericObject readNumericFixture(const char* path) {
    std::ifstream input(path);
    expect(static_cast<bool>(input), "frozen JSON preset opens");
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::regex keyPattern(R"json("(u\w+)"\s*:\s*)json");
    const std::regex numberPattern(R"([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)");
    NumericObject result;
    for (auto i = std::sregex_iterator(text.begin(), text.end(), keyPattern); i != std::sregex_iterator(); ++i) {
        const auto& match = *i;
        const std::size_t start = static_cast<std::size_t>(match.position() + match.length());
        std::size_t end = start;
        int depth = 0;
        for (; end < text.size(); ++end) {
            const char c = text[end];
            if (c == '[') ++depth;
            else if (c == ']') { --depth; if (depth == 0) { ++end; break; } }
            else if (depth == 0 && (c == ',' || c == '}')) break;
        }
        const std::string value = text.substr(start, end - start);
        std::vector<float> values;
        for (auto number = std::sregex_iterator(value.begin(), value.end(), numberPattern);
             number != std::sregex_iterator(); ++number)
            values.push_back(static_cast<float>(std::stod(number->str())));
        expect(!values.empty(), "preset fixture key has numeric content");
        result.emplace(match[1].str(), std::move(values));
    }
    expect(result.size() == material_gl::MaterialUniforms::Count, "frozen preset covers all current shader uniforms");
    return result;
}

struct Capture {
    const NumericObject* expected = nullptr;
    std::vector<std::string> locations;
    NumericObject values;
    void record(int location, const float* value, std::size_t count) {
        expect(location >= 0 && static_cast<std::size_t>(location) < locations.size(), "preset upload uses cached location");
        if (location < 0 || static_cast<std::size_t>(location) >= locations.size()) return;
        values[locations[static_cast<std::size_t>(location)]] = std::vector<float>(value, value + count);
    }
    material_gl::UniformDispatch dispatch() {
        return {this,
            [](void* user, std::uint32_t, const char* name) -> std::int32_t {
                auto& c = *static_cast<Capture*>(user);
                std::string key(name); const auto bracket = key.find('[');
                if (bracket != std::string::npos) key.resize(bracket);
                if (c.expected->count(key) == 0) return -1;
                c.locations.push_back(key); return static_cast<std::int32_t>(c.locations.size() - 1);
            },
            [](void*, std::uint32_t) {},
            [](void* user, std::int32_t location, std::int32_t value) {
                const float f = static_cast<float>(value); static_cast<Capture*>(user)->record(location, &f, 1);
            },
            [](void* user, std::int32_t location, float value) { static_cast<Capture*>(user)->record(location, &value, 1); },
            [](void* user, std::int32_t location, float x, float y) {
                const float v[]{x, y}; static_cast<Capture*>(user)->record(location, v, 2);
            },
            [](void* user, std::int32_t location, float x, float y, float z) {
                const float v[]{x, y, z}; static_cast<Capture*>(user)->record(location, v, 3);
            },
            [](void* user, std::int32_t location, std::int32_t count, const float* value) {
                static_cast<Capture*>(user)->record(location, value, static_cast<std::size_t>(count) * 4);
            }};
    }
};

BackdropDescription sourceFor(const NumericObject& json) {
    const auto& rect = json.at("uSourceRectPx");
    const int w = static_cast<int>(rect[2]), h = static_cast<int>(rect[3]);
    const ContextKey context{11, {1, 1}};
    BackdropDescription b;
    b.image = {44, {5, 1}, 0, 0}; b.viewport = {context, {2, 1}};
    b.captureScope = {8, 1}; b.frameSerial = 12; b.contentRevision = 2;
    b.mapping = {{{rect[0], h - rect[1] - rect[3]}, {rect[0] + rect[2], h - rect[1]}},
                 {0, 0, w, h}, {w, h}, PixelOrigin::BottomLeft};
    b.provenance.complete = true; b.provenance.containsSemanticUnderlay = true;
    return b;
}

NumericObject capturedUniforms(const material_gl::MaterialParameters& material, const NumericObject& json) {
    const BackdropSnapshot snapshot(sourceFor(json));
    GlassPassContract pass;
    pass.viewport = snapshot.description().viewport; pass.group = {pass.viewport.owner, {20, 1}};
    pass.frameSerial = 12; pass.destination = {44, {6, 1}, 0, 0};
    pass.requiredSampleDomainPxTL = snapshot.description().mapping.capturedFramebufferPxTL;
    pass.output = CompositeRepresentation::OpaqueReplacement;
    material_gl::UniformPacket packet;
    packet.material = material;
    packet.source = {static_cast<int>(json.at("uBackdrop")[0]), static_cast<int>(json.at("uMaxMip")[0]) + 1,
                     json.at("uMaxMip")[0], snapshot.description().mapping.textureExtent, &snapshot, &pass};
    const auto& origin = json.at("uLocalOriginPx");
    packet.geometry.localOriginFramebufferPx = {origin[0], origin[1]};
    packet.geometry.shapeCount = static_cast<int>(json.at("uShapeCount")[0]);
    packet.geometry.mergePx = json.at("uMergePx")[0];
    const auto& shapes = json.at("uShapes"); const auto& radii = json.at("uRadii");
    for (std::size_t i = 0; i < 4; ++i)
        packet.geometry.shapes[i] = {{shapes[i * 4], shapes[i * 4 + 1]},
                                     {shapes[i * 4 + 2], shapes[i * 4 + 3]}, radii[i]};
    packet.controls.filter = static_cast<material_gl::FilterQuality>(static_cast<int>(json.at("uFilterQuality")[0]));
    packet.controls.output = static_cast<material_gl::OutputMode>(static_cast<int>(json.at("uOutputMode")[0]));
    packet.controls.debug = static_cast<material_gl::DebugView>(static_cast<int>(json.at("uDebug")[0]));
    Capture capture; capture.expected = &json;
    material_gl::MaterialUniforms uniforms;
    expect(static_cast<bool>(uniforms.initialize(capture.dispatch(), 71, {16, 8192}, material_gl::InactiveUniformPolicy::RequireAll)),
           "preset fixture initializes typed uploader");
    expect(static_cast<bool>(uniforms.upload(packet)), "named preset passes material/source upload validation");
    return capture.values;
}

void testFrozenParity(const NumericObject& json) {
    const auto material = preset::clearControlV2(preset::clearControlV2ReferenceHalfHeightPx);
    expect(material.has_value(), "reference-sized named preset exists");
    if (!material) return;
    const auto actual = capturedUniforms(*material, json);
    expect(actual.size() == json.size(), "named preset and caller packet upload all JSON uniforms");
    for (const auto& uniform : json) {
        const auto found = actual.find(uniform.first);
        expect(found != actual.end(), "frozen JSON uniform represented in C++ upload");
        if (found == actual.end()) continue;
        if (found->second != uniform.second) std::fprintf(stderr, "MISMATCH: %s\n", uniform.first.c_str());
        expect(found->second == uniform.second, "exact GPU-float parity with frozen JSON payload");
    }
    expect(material->edge.kind == EdgeProfileKind::Circular && material->useLightProfile,
           "selected preset uses measured circular profile and calibrated light controls");
    const auto& referenceShape = json.at("uShapes");
    const double aspect = static_cast<double>(referenceShape[2]) / referenceShape[3];
    expect(std::abs(preset::clearControlV2AspectRatio - aspect) < 1.0e-7,
           "recommended aspect matches reference geometry without owning placement");
}

void testScaling(const NumericObject& json) {
    const std::set<std::string> physicalMaterialFields{
        "uBodyRadiusPx", "uCrownHeightPx", "uEdgeWidthPx", "uEdgeReachPx", "uBevelWidthPx", "uBevelHeightPx",
        "uInnerShadeWidthPx", "uInnerShadeInsetPx", "uLipOffsetPx", "uLipWidthPx", "uShadowSigmaPx",
        "uShadowOffsetPx", "uCausticWidthPx", "uContactLocalPx", "uContactRadiusPx"
    };
    const auto reference = preset::clearControlV2(preset::clearControlV2ReferenceHalfHeightPx);
    for (const float halfHeight : {3.0f, 12.0f, 24.0f, 48.0f, 180.0f, 999.0f}) {
        const auto scaled = preset::clearControlV2(halfHeight);
        expect(scaled.has_value(), "practical control size creates valid scaled preset");
        if (!scaled || !reference) continue;
        const double factor = static_cast<double>(halfHeight) / preset::clearControlV2ReferenceHalfHeightPx;
        const auto actual = capturedUniforms(*scaled, json);
        for (const auto& uniform : json) {
            const auto found = actual.find(uniform.first);
            expect(found != actual.end() && found->second.size() == uniform.second.size(), "scaled uniform retains its schema");
            if (found == actual.end() || found->second.size() != uniform.second.size()) continue;
            for (std::size_t i = 0; i < uniform.second.size(); ++i) {
                if (physicalMaterialFields.count(uniform.first)) {
                    const double expected = uniform.second[i] * factor;
                    expect(std::abs(found->second[i] - expected) <= 2.0e-6 * std::max(1.0, std::abs(expected)),
                           "every physical material length scales proportionally");
                } else {
                    expect(found->second[i] == uniform.second[i],
                           "unitless material and caller-owned geometry/source/render controls unchanged");
                }
            }
        }
        const auto a = sampleEdgeProfile(-reference->edge.widthPx * 0.31f, reference->edge);
        const auto b = sampleEdgeProfile(-scaled->edge.widthPx * 0.31f, scaled->edge);
        expect(std::abs(a.displacementPx / reference->edge.reachPx - b.displacementPx / scaled->edge.reachPx) < 2.0e-7,
               "normalized optical shape is invariant under preset scaling");
        expect(std::abs(a.dDisplacementDDepth - b.dDisplacementDDepth) < 2.0e-6,
               "normal texture stretch is invariant under preset scaling");
    }
}

void testInvalidSizes() {
    for (const float size : {0.0f, -1.0f, 1.0f, 1.0e-30f, 1048577.0f,
                             std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::max()})
        expect(!preset::clearControlV2(size).has_value(), "invalid/unrepresentable size rejected without clamping proportions");
}
}

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "Usage: test_presets path/to/clear-control-refined.json\n"); return EXIT_FAILURE; }
    const auto json = readNumericFixture(argv[1]);
    testFrozenParity(json); testScaling(json); testInvalidSizes();
    std::printf("Named preset checks: %d, failures: %d; frozen uniform payloads: %zu\n", checks, failures, json.size());
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
