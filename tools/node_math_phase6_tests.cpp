#include "NodeMath/DescriptorSerialization.h"
#include "NodeMath/RegionPlanning.h"
#include "NodeMath/ReductionMath.h"
#include "NodeMath/GeometryMath.h"
#include "NodeMath/SpecializedPlanning.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

namespace {

using namespace Stack::NodeMath;

int gChecks = 0;

bool Check(bool condition, const std::string& message) {
    ++gChecks;
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

bool HasIssue(const std::vector<ContractIssue>& issues, const std::string& field) {
    return std::any_of(issues.begin(), issues.end(), [&](const ContractIssue& issue) {
        return issue.field == field;
    });
}

SpatialDescriptor TestSpatial(RasterOrigin origin = RasterOrigin::BottomLeft) {
    SpatialDescriptor spatial;
    spatial.kind = SpatialExtentKind::Finite;
    spatial.fullWindow = { 10, -4, 11, 9 };
    spatial.dataWindow = { 12, -2, 6, 4 };
    spatial.rasterOrigin = origin;
    spatial.pixelAspect = 1.25;
    return spatial;
}

SamplingDescriptor TestSampling() {
    SamplingDescriptor sampling;
    sampling.coordinates = CoordinateConvention::PixelCenters;
    sampling.filter = ReconstructionFilter::Linear;
    sampling.border = BorderPolicy::Clamp;
    return sampling;
}

bool TestSpatialDescriptorV2() {
    bool ok = true;
    const ValueDescriptor source = MakeUntaggedColorImageDescriptor(
        AlphaMode::Straight,
        TestSpatial(RasterOrigin::TopLeft),
        TestSampling(),
        LogicalPrecision::UInt8,
        "phase6-spatial-source");
    ok &= Check(
        source.schemaVersion == kSemanticDescriptorSchemaVersion,
        "new descriptors use the current semantic schema");
    const nlohmann::json serialized = SerializeValueDescriptor(source);
    ok &= Check(
        serialized["spatial"]["value"].value("rasterOrigin", std::string()) == "top-left",
        "serialized spatial descriptors carry raster origin");
    const DescriptorParseResult parsed = ParseValueDescriptor(serialized);
    ok &= Check(parsed.descriptor.has_value() && parsed.issues.empty() && *parsed.descriptor == source,
        "spatial schema v2 round-trips exactly");

    nlohmann::json legacy = serialized;
    legacy["schemaVersion"] = 1;
    legacy["spatial"]["value"].erase("rasterOrigin");
    const DescriptorParseResult migrated = ParseValueDescriptor(legacy);
    ok &= Check(
        migrated.descriptor.has_value() && migrated.issues.empty() &&
        migrated.descriptor->schemaVersion == kSemanticDescriptorSchemaVersion &&
        migrated.descriptor->spatial.value.rasterOrigin == RasterOrigin::BottomLeft,
        "schema v1 migrates to the established bottom-left live graph origin");

    ValueDescriptor invalid = source;
    invalid.spatial.value.pixelAspect = 0.0;
    ok &= Check(HasIssue(ValidateDescriptor(invalid), "spatial"),
        "zero pixel aspect remains structurally invalid");
    invalid = source;
    invalid.spatial.value.rasterOrigin = static_cast<RasterOrigin>(99);
    ok &= Check(HasIssue(ValidateDescriptor(invalid), "spatial"),
        "unknown raster origin is rejected rather than guessed");
    return ok;
}

bool TestRegionsAndMappings() {
    bool ok = true;
    const SpatialDescriptor spatial = TestSpatial();
    const RenderRegion request = MakeFiniteRegion({ 13, -1, 2, 2 }, 0, 4);
    const RegionMapping pointwise = MapPointwiseRegion(request, spatial);
    ok &= Check(pointwise.executable && pointwise.input == request,
        "pointwise mapping preserves the exact requested finite region");

    const NeighborhoodSupport support { 3, 3, 3, 3 };
    const RegionMapping neighborhood = MapNeighborhoodRegion(
        request, spatial, support, BorderPolicy::Clamp);
    ok &= Check(
        neighborhood.executable &&
        neighborhood.input.bounds == Rect{ 12, -2, 6, 4 } &&
        neighborhood.samplesOutsideDataWindow &&
        neighborhood.border == BorderPolicy::Clamp,
        "neighborhood mapping expands support, clips fetches to data, and retains clamp policy");

    const RegionMapping reduction = MapReductionRegion(request, spatial);
    ok &= Check(
        reduction.executable && reduction.requiresFullInput &&
        reduction.input.bounds == spatial.dataWindow,
        "reduction mapping requests the complete declared data population");

    const RegionMapping invalidSupport = MapNeighborhoodRegion(
        request, spatial, { -1, 0, 0, 0 }, BorderPolicy::Clamp);
    ok &= Check(!invalidSupport.executable && HasIssue(invalidSupport.issues, "support"),
        "negative support is rejected");
    ok &= Check(HasIssue(ValidateRenderRegion(MakeFiniteRegion({ 0, 0, 4, 4 }, 0, 0)), "channels"),
        "empty channel spans are rejected");
    ok &= Check(!ValidateRenderScale({ 1.0, 0.5 }).size(),
        "positive anisotropic render scale is representable");
    ok &= Check(HasIssue(ValidateRenderScale({ 1.0, 0.0 }), "renderScale"),
        "zero render scale is rejected");
    return ok;
}

bool TestGaussianSupport() {
    bool ok = true;
    ok &= Check(GaussianBlurRadiusAtScale(3.9, 1.0) == 3,
        "scale-1 support matches the live integer truncation rule");
    ok &= Check(GaussianBlurRadiusAtScale(3.0, 0.5) == 1,
        "full-resolution pixel support scales down for a proxy");
    ok &= Check(GaussianBlurRadiusAtScale(3.0, 2.0) == 6,
        "full-resolution pixel support scales up at render scale 2");
    ok &= Check(
        GaussianBlurSupport(4.0, { 0.5, 1.5 }) == NeighborhoodSupport{ 2, 2, 6, 6 },
        "anisotropic render scales produce explicit per-axis support");
    ok &= Check(GaussianBlurRadiusAtScale(-2.0, 1.0) == 1,
        "the support helper preserves the live minimum radius behavior");
    ok &= Check(GaussianBlurRadiusAtScale(2.0, 0.0) < 0,
        "invalid scale does not produce a guessed support");
    return ok;
}

bool TestFieldMeanReduction() {
    bool ok = true;
    const FieldMeanResult ordinary = ComputeFieldMean({ 0.0f, 0.25f, 0.75f, 1.0f });
    ok &= Check(ordinary.valid && ordinary.sampleCount == 4 &&
        std::abs(ordinary.value - 0.5) <= 1e-12,
        "Field Mean computes the exact ordinary arithmetic mean");

    const FieldMeanResult compensated = ComputeFieldMean({ 100000000.0f, 1.0f, -100000000.0f });
    ok &= Check(compensated.valid && std::abs(compensated.value - (1.0 / 3.0)) <= 1e-12,
        "Field Mean uses compensated float64 accumulation in authored sample order");

    const FieldMeanResult empty = ComputeFieldMean({});
    ok &= Check(!empty.valid && empty.sampleCount == 0 && !empty.error.empty(),
        "Field Mean rejects an empty population");
    const FieldMeanResult nan = ComputeFieldMean({ 1.0f, std::numeric_limits<float>::quiet_NaN() });
    ok &= Check(!nan.valid && nan.sampleCount == 1 && !nan.error.empty(),
        "Field Mean fails instead of silently removing NaN");
    const FieldMeanResult infinity = ComputeFieldMean({ std::numeric_limits<float>::infinity() });
    ok &= Check(!infinity.valid && infinity.sampleCount == 0 && !infinity.error.empty(),
        "Field Mean fails instead of clamping infinity");
    return ok;
}

bool TestReformatGeometry() {
    bool ok = true;
    ReformatSettings nearest;
    nearest.width = 4;
    nearest.height = 2;
    nearest.filter = ReconstructionFilter::Nearest;
    nearest.border = BorderPolicy::Clamp;
    ok &= Check(ValidateReformatSettings(nearest).empty(),
        "valid Reformat settings satisfy the explicit geometry contract");

    const SpatialDescriptor input = TestSpatial();
    const SpatialDescriptor output = ReformatOutputSpatial(input, nearest);
    ok &= Check(
        output.kind == SpatialExtentKind::Finite &&
        output.fullWindow == Rect{ 10, -4, 4, 2 } &&
        output.dataWindow == output.fullWindow &&
        output.rasterOrigin == input.rasterOrigin &&
        output.pixelAspect == input.pixelAspect,
        "Reformat propagates origin, raster convention, and pixel aspect while declaring a new extent");

    ReformatSettings linear = nearest;
    linear.width = 8;
    linear.height = 6;
    linear.filter = ReconstructionFilter::Linear;
    const RegionMapping mapping = MapReformatRegion(
        MakeFiniteRegion({ 11, -3, 2, 2 }), input, linear);
    ok &= Check(
        mapping.executable && mapping.border == BorderPolicy::Clamp &&
        mapping.support == NeighborhoodSupport{ 1, 1, 1, 1 } &&
        mapping.input.kind == RenderRegionKind::Finite,
        "Reformat maps output ROI through inverse pixel-center sampling with named linear support and clamp border");

    ReformatSettings invalid = nearest;
    invalid.width = 0;
    ok &= Check(HasIssue(ValidateReformatSettings(invalid), "width"),
        "Reformat rejects an empty output extent instead of guessing");
    invalid = nearest;
    invalid.filter = ReconstructionFilter::Cubic;
    ok &= Check(HasIssue(ValidateReformatSettings(invalid), "filter"),
        "unsupported reconstruction filters are explicit contract failures");

    std::vector<float> source {
        0,0,0,1, 1,0,0,0.5f,
        0,1,0,0.25f, 1,1,1,1
    };
    ReformatSettings upsample;
    upsample.width = 4;
    upsample.height = 4;
    upsample.filter = ReconstructionFilter::Nearest;
    const auto nearestOutput = ReformatRgbaReference(source, 2, 2, upsample);
    ok &= Check(nearestOutput.size() == 4u * 4u * 4u &&
        nearestOutput[3] == 1.0f && nearestOutput[15] == 0.5f &&
        nearestOutput[35] == 0.25f && nearestOutput[63] == 1.0f,
        "nearest Reformat preserves independently controlled RGBA samples");
    upsample.filter = ReconstructionFilter::Linear;
    const auto linearOutput = ReformatRgbaReference(source, 2, 2, upsample);
    ok &= Check(linearOutput.size() == nearestOutput.size() &&
        std::abs(linearOutput[(1u * 4u + 1u) * 4u + 3u] - 0.765625f) < 1e-6f,
        "linear Reformat uses the declared pixel-center interpolation for alpha as data");

    ok &= Check(
        std::abs(
            (NormalizedPixelCenter(0, 4) + NormalizedPixelCenter(1, 4)) * 0.5 -
            NormalizedPixelCenter(0, 2)) < 1e-12,
        "a half-resolution proxy center matches the center of its full-resolution pixel footprint");
    return ok;
}

bool TestSpecializedBoundaries() {
    bool ok = true;
    const SpecializedStagePlan raw = PlanSpecializedStage(SpecializedStageKind::RawDevelopment);
    ok &= Check(raw.valid && raw.opaque && raw.regionRequirement == RegionRequirement::FullFrame &&
        raw.inputType == LogicalValueType::Raw && raw.outputType == LogicalValueType::ColorImage,
        "RAW development is an opaque typed full-frame boundary");
    const SpecializedStagePlan frequency = PlanSpecializedStage(SpecializedStageKind::FrequencyTransform);
    ok &= Check(frequency.valid && frequency.capability == CapabilityClass::MultipassIterative &&
        frequency.regionRequirement == RegionRequirement::FullFrame &&
        frequency.inputType == LogicalValueType::Channel &&
        frequency.outputType == LogicalValueType::ComplexSpectrum,
        "frequency transforms are typed global multipass boundaries");
    const SpecializedStagePlan inverse =
        PlanSpecializedStage(SpecializedStageKind::FrequencyInverseTransform);
    ok &= Check(inverse.valid && inverse.inputType == LogicalValueType::ComplexSpectrum &&
        inverse.outputType == LogicalValueType::Channel &&
        inverse.capability == CapabilityClass::MultipassIterative,
        "inverse frequency transforms declare the opposite typed boundary explicitly");
    const SpecializedStagePlan external =
        PlanSpecializedStage(SpecializedStageKind::NeuralOrExternal);
    ok &= Check(external.valid && external.inputType == LogicalValueType::Raw &&
        external.outputType == LogicalValueType::Raw &&
        external.cancellation == CancellationPolicy::CooperativeExternal,
        "the current neural/external boundary preserves RAW data and requires cooperative cancellation");

    SpatialDescriptor spatial;
    spatial.kind = SpatialExtentKind::Finite;
    spatial.fullWindow = { 0, 0, 800, 400 };
    spatial.dataWindow = spatial.fullWindow;
    spatial.rasterOrigin = RasterOrigin::BottomLeft;
    spatial.pixelAspect = 1.0;
    const ConsumerBoundaryPlan scope = PlanConsumerBoundary(
        SpecializedStageKind::ScopeAnalysis, spatial, 256);
    ok &= Check(scope.valid && !scope.changesGraphResult &&
        scope.outputSpatial.fullWindow == Rect{ 0, 0, 256, 128 } &&
        scope.scalePolicy == RenderScalePolicy::ProxyAllowed,
        "scope analysis declares a bounded proxy consumer without changing graph pixels");
    const ConsumerBoundaryPlan preview = PlanConsumerBoundary(
        SpecializedStageKind::PreviewReadback, spatial, 200);
    ok &= Check(preview.valid && preview.outputSpatial.fullWindow == Rect{ 0, 0, 200, 100 } &&
        !preview.changesGraphResult,
        "preview readback declares its proxy extent and remains outside graph math");
    const ConsumerBoundaryPlan exportPlan = PlanConsumerBoundary(
        SpecializedStageKind::ExportReadback, spatial);
    ok &= Check(exportPlan.valid && exportPlan.outputSpatial == spatial &&
        exportPlan.scalePolicy == RenderScalePolicy::FullQualityOnly &&
        !exportPlan.changesGraphResult,
        "export consumes the exact full graph result through a typed full-quality boundary");
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    ok &= TestSpatialDescriptorV2();
    ok &= TestRegionsAndMappings();
    ok &= TestGaussianSupport();
    ok &= TestFieldMeanReduction();
    ok &= TestReformatGeometry();
    ok &= TestSpecializedBoundaries();
    if (!ok) {
        std::cerr << "Node Math Phase 6 tests failed after " << gChecks << " checks.\n";
        return 1;
    }
    std::cout << "Node Math Phase 6 tests passed (" << gChecks << " checks).\n";
    return 0;
}
