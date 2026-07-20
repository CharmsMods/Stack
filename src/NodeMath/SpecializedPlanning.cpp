#include "NodeMath/SpecializedPlanning.h"

#include <algorithm>
#include <cmath>

namespace Stack::NodeMath {

const char* SpecializedStageKindName(SpecializedStageKind kind) {
    switch (kind) {
        case SpecializedStageKind::RawDecode: return "RAW decode";
        case SpecializedStageKind::RawDevelopment: return "RAW development";
        case SpecializedStageKind::NeuralOrExternal: return "CPU/external process";
        case SpecializedStageKind::MultiFrameMerge: return "multi-frame merge";
        case SpecializedStageKind::FrequencyTransform: return "frequency transform";
        case SpecializedStageKind::FrequencyInverseTransform: return "inverse frequency transform";
        case SpecializedStageKind::FrequencyOperation: return "frequency operation";
        case SpecializedStageKind::ScopeAnalysis: return "scope analysis";
        case SpecializedStageKind::PreviewReadback: return "preview readback";
        case SpecializedStageKind::ExportReadback: return "export readback";
        case SpecializedStageKind::None:
        default: return "none";
    }
}

SpecializedStagePlan PlanSpecializedStage(SpecializedStageKind kind) {
    SpecializedStagePlan plan;
    plan.kind = kind;
    plan.valid = kind != SpecializedStageKind::None;
    switch (kind) {
        case SpecializedStageKind::RawDecode:
            plan.inputType = LogicalValueType::Raw;
            plan.outputType = LogicalValueType::DataImage;
            plan.scalePolicy = RenderScalePolicy::ProxyAllowed;
            plan.cancellation = CancellationPolicy::BetweenStages;
            plan.reason = "RAW decode is an opaque metadata-aware full-frame stage.";
            break;
        case SpecializedStageKind::RawDevelopment:
            plan.inputType = LogicalValueType::Raw;
            plan.outputType = LogicalValueType::ColorImage;
            plan.scalePolicy = RenderScalePolicy::ProxyAllowed;
            plan.cancellation = CancellationPolicy::BetweenStages;
            plan.reason = "RAW development remains opaque and publishes a declared image boundary.";
            break;
        case SpecializedStageKind::NeuralOrExternal:
            plan.inputType = LogicalValueType::Raw;
            plan.outputType = LogicalValueType::Raw;
            plan.scalePolicy = RenderScalePolicy::ExactRequested;
            plan.cancellation = CancellationPolicy::CooperativeExternal;
            plan.reason = "RAW neural/external work must publish exact RAW metadata or an explicit failure.";
            break;
        case SpecializedStageKind::MultiFrameMerge:
            plan.capability = CapabilityClass::MultipassIterative;
            plan.inputType = LogicalValueType::ColorImage;
            plan.outputType = LogicalValueType::ColorImage;
            plan.scalePolicy = RenderScalePolicy::FullQualityOnly;
            plan.cancellation = CancellationPolicy::BetweenStages;
            plan.reason = "Multi-frame alignment and reconstruction require the complete declared frames.";
            break;
        case SpecializedStageKind::FrequencyTransform:
            plan.capability = CapabilityClass::MultipassIterative;
            plan.inputType = LogicalValueType::DataImage;
            plan.outputType = LogicalValueType::ComplexSpectrum;
            plan.scalePolicy = RenderScalePolicy::ExactRequested;
            plan.cancellation = CancellationPolicy::BetweenStages;
            plan.reason = "FFT/IFFT are global multipass transforms and cannot consume independent image tiles.";
            break;
        case SpecializedStageKind::FrequencyInverseTransform:
            plan.capability = CapabilityClass::MultipassIterative;
            plan.inputType = LogicalValueType::ComplexSpectrum;
            plan.outputType = LogicalValueType::DataImage;
            plan.scalePolicy = RenderScalePolicy::ExactRequested;
            plan.cancellation = CancellationPolicy::BetweenStages;
            plan.reason = "Inverse FFT is a global multipass transform and cannot consume independent tiles.";
            break;
        case SpecializedStageKind::FrequencyOperation:
            plan.capability = CapabilityClass::MultipassIterative;
            plan.inputType = LogicalValueType::ComplexSpectrum;
            plan.outputType = LogicalValueType::ComplexSpectrum;
            plan.scalePolicy = RenderScalePolicy::ExactRequested;
            plan.cancellation = CancellationPolicy::BetweenStages;
            plan.reason = "Frequency-domain work retains the complete spectrum extent.";
            break;
        case SpecializedStageKind::ScopeAnalysis:
            plan.capability = CapabilityClass::Reduction;
            plan.inputType = LogicalValueType::ColorImage;
            plan.outputType = LogicalValueType::Analysis;
            plan.regionRequirement = RegionRequirement::GlobalPopulation;
            plan.scalePolicy = RenderScalePolicy::ProxyAllowed;
            plan.opaque = false;
            plan.reason = "Scopes read a declared bounded proxy without changing graph pixels.";
            break;
        case SpecializedStageKind::PreviewReadback:
            plan.inputType = LogicalValueType::ColorImage;
            plan.outputType = LogicalValueType::ColorImage;
            plan.scalePolicy = RenderScalePolicy::ProxyAllowed;
            plan.opaque = false;
            plan.reason = "Preview readback may resize for presentation and never changes the graph result.";
            break;
        case SpecializedStageKind::ExportReadback:
            plan.inputType = LogicalValueType::ColorImage;
            plan.outputType = LogicalValueType::ColorImage;
            plan.scalePolicy = RenderScalePolicy::FullQualityOnly;
            plan.opaque = false;
            plan.reason = "Export consumes the full graph output; quantization is an explicit output boundary.";
            break;
        case SpecializedStageKind::None:
        default:
            plan.issues.push_back({ "kind", "A specialized stage kind is required." });
            plan.reason = "No specialized stage was selected.";
            break;
    }
    return plan;
}

ConsumerBoundaryPlan PlanConsumerBoundary(
    SpecializedStageKind kind,
    const SpatialDescriptor& inputSpatial,
    int maximumDimension) {
    ConsumerBoundaryPlan result;
    result.kind = kind;
    result.inputSpatial = inputSpatial;
    const SpecializedStagePlan stage = PlanSpecializedStage(kind);
    result.regionRequirement = stage.regionRequirement;
    result.scalePolicy = stage.scalePolicy;
    result.cancellation = stage.cancellation;
    result.reason = stage.reason;
    if (!stage.valid ||
        (kind != SpecializedStageKind::ScopeAnalysis &&
         kind != SpecializedStageKind::PreviewReadback &&
         kind != SpecializedStageKind::ExportReadback)) {
        result.issues.push_back({ "kind", "This stage is not a graph consumer boundary." });
    }
    if (inputSpatial.kind != SpatialExtentKind::Finite ||
        inputSpatial.fullWindow.width <= 0 || inputSpatial.fullWindow.height <= 0) {
        result.issues.push_back({ "inputSpatial", "The consumer requires a finite graph output." });
    }
    result.outputSpatial = inputSpatial;
    if (result.issues.empty() && kind != SpecializedStageKind::ExportReadback) {
        const int boundedMaximum = maximumDimension > 0
            ? maximumDimension
            : (kind == SpecializedStageKind::ScopeAnalysis ? 256 : 1);
        const double sourceMaximum = static_cast<double>(std::max(
            inputSpatial.fullWindow.width, inputSpatial.fullWindow.height));
        const double scale = std::min(1.0, static_cast<double>(boundedMaximum) / sourceMaximum);
        result.outputSpatial.fullWindow.width = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(std::round(inputSpatial.fullWindow.width * scale)));
        result.outputSpatial.fullWindow.height = std::max<std::int64_t>(
            1, static_cast<std::int64_t>(std::round(inputSpatial.fullWindow.height * scale)));
        result.outputSpatial.dataWindow = result.outputSpatial.fullWindow;
    }
    result.changesGraphResult = false;
    result.valid = result.issues.empty();
    return result;
}

} // namespace Stack::NodeMath
