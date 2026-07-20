#include "NodeMath/DescriptorPropagation.h"

#include <cmath>
#include <set>

namespace Stack::NodeMath {

namespace {

Diagnostic MakeDiagnostic(
    const char* ruleId,
    DiagnosticSeverity severity,
    const std::string& source,
    const std::string& fingerprint,
    const char* message,
    const char* repair) {
    Diagnostic diagnostic;
    diagnostic.ruleId = ruleId;
    diagnostic.stage = DiagnosticStage::Semantic;
    for (const DiagnosticRule& rule : BuiltInDiagnosticRules()) {
        if (rule.id == ruleId) {
            diagnostic.stage = rule.stage;
            break;
        }
    }
    diagnostic.severity = severity;
    diagnostic.authoredSourceIdentity = source;
    diagnostic.affectedIdentity = "output";
    diagnostic.semanticFingerprint = Sha256ContentIdentity(
        std::string(ruleId) + "|" + source + "|" + fingerprint);
    diagnostic.message = message;
    diagnostic.suggestedRepair = repair;
    return diagnostic;
}

void MarkDerivedProvenance(ValueDescriptor& descriptor, const std::string& operationIdentity) {
    descriptor.provenance = SemanticField<ProvenanceDescriptor>::Known({
        ProvenanceKind::Derived, {}, operationIdentity });
}

void MarkArithmeticDerived(ValueDescriptor& descriptor, const std::string& operationIdentity) {
    if (descriptor.color.state == KnowledgeState::Known) {
        descriptor.color.value.relation = ColorRelation::Derived;
    }
    MarkDerivedProvenance(descriptor, operationIdentity);
}

bool IsLinear(const SemanticField<TransferDescriptor>& transfer) {
    return transfer.state == KnowledgeState::Known &&
        transfer.value.kind == TransferKind::Linear;
}

template <typename T>
void InvalidateField(SemanticField<T>& field) {
    if (field.state != KnowledgeState::NotApplicable) {
        field = SemanticField<T>::Unknown();
    }
}

template <typename T>
void ApplyDisposition(
    SemanticField<T>& output,
    const SemanticField<T>& input,
    FieldDisposition disposition) {
    switch (disposition) {
    case FieldDisposition::Preserve:
        output = input;
        break;
    case FieldDisposition::Invalidate:
        InvalidateField(output);
        break;
    case FieldDisposition::Consume:
    case FieldDisposition::Drop:
        output = SemanticField<T>::NotApplicable();
        break;
    case FieldDisposition::Replace:
    case FieldDisposition::Generate:
        // The declared output already carries the replacement or generated field.
        break;
    }
}

void ApplyFieldRule(
    ValueDescriptor& output,
    const ValueDescriptor& input,
    const FieldDispositionRule& rule) {
    switch (rule.field) {
    case DescriptorField::Channels: ApplyDisposition(output.channels, input.channels, rule.disposition); break;
    case DescriptorField::Color: ApplyDisposition(output.color, input.color, rule.disposition); break;
    case DescriptorField::Transfer: ApplyDisposition(output.transfer, input.transfer, rule.disposition); break;
    case DescriptorField::Reference: ApplyDisposition(output.reference, input.reference, rule.disposition); break;
    case DescriptorField::Alpha: ApplyDisposition(output.alpha, input.alpha, rule.disposition); break;
    case DescriptorField::Range: ApplyDisposition(output.range, input.range, rule.disposition); break;
    case DescriptorField::Precision: ApplyDisposition(output.precision, input.precision, rule.disposition); break;
    case DescriptorField::Spatial: ApplyDisposition(output.spatial, input.spatial, rule.disposition); break;
    case DescriptorField::Sampling: ApplyDisposition(output.sampling, input.sampling, rule.disposition); break;
    case DescriptorField::Units: ApplyDisposition(output.units, input.units, rule.disposition); break;
    case DescriptorField::Provenance: ApplyDisposition(output.provenance, input.provenance, rule.disposition); break;
    }
}

} // namespace

PropagationResult PropagateIdentity(const ValueDescriptor& input) {
    return { input, {}, ValidateDescriptor(input).empty() };
}

PropagationResult PropagateGenericArithmetic(
    const ValueDescriptor& input,
    const std::string& operationIdentity) {
    PropagationResult result;
    result.descriptor = input;
    MarkArithmeticDerived(result.descriptor, operationIdentity);
    result.executable = ValidateDescriptor(result.descriptor).empty();
    return result;
}

PropagationResult PropagateExposure(
    const ValueDescriptor& input,
    double exposureValue,
    const std::string& operationIdentity) {
    PropagationResult result;
    result.descriptor = input;
    if (input.logicalType != LogicalValueType::ColorImage || !std::isfinite(exposureValue)) {
        result.executable = false;
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.connection.type-mismatch", DiagnosticSeverity::HardError,
            operationIdentity, "exposure-invalid-input",
            "Exposure requires a ColorImage and a finite EV value.",
            "Connect a ColorImage and provide a finite exposure value."));
        return result;
    }

    if (input.transfer.state == KnowledgeState::Unknown) {
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.semantic.transfer-unknown", DiagnosticSeverity::Warning,
            operationIdentity, "exposure-transfer-unknown",
            "Exposure is numerically executable, but the input transfer is unknown.",
            "Assign or convert the color state if a transfer-aware result is intended."));
    } else if (!IsLinear(input.transfer)) {
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.semantic.transfer-nonlinear", DiagnosticSeverity::Warning,
            operationIdentity, "exposure-transfer-nonlinear",
            "Exposure is being applied to a known non-linear signal.",
            "Keep this authored numeric order or add an explicit transform before Exposure."));
    }

    if (result.descriptor.range.state == KnowledgeState::Known) {
        const double scale = std::pow(2.0, exposureValue);
        result.descriptor.range.value.nominalMinimum *= scale;
        result.descriptor.range.value.nominalMaximum *= scale;
        result.descriptor.range.value.allowsBelowNominal = true;
        result.descriptor.range.value.allowsAboveNominal = true;
    }
    MarkDerivedProvenance(result.descriptor, operationIdentity);
    result.executable = ValidateDescriptor(result.descriptor).empty();
    return result;
}

PropagationResult PropagateMask(
    const ValueDescriptor& input,
    const std::string& operationIdentity) {
    PropagationResult result;
    result.descriptor = MakeUnknownDescriptor(LogicalValueType::Mask);
    if (!IsImageLike(input.logicalType)) {
        result.executable = false;
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.connection.type-mismatch", DiagnosticSeverity::HardError,
            operationIdentity, "mask-input-type",
            "Mask extraction requires an image-like input.",
            "Connect a color or data image."));
        return result;
    }
    result.descriptor.channels = SemanticField<ChannelDescriptor>::Known({
        ChannelLayout::Gray, { "coverage" } });
    result.descriptor.range = SemanticField<NumericRange>::Known({
        0.0, 1.0, false, false, NonFinitePolicy::Forbidden });
    result.descriptor.precision = input.precision;
    result.descriptor.spatial = input.spatial;
    result.descriptor.sampling = input.sampling;
    result.descriptor.provenance = SemanticField<ProvenanceDescriptor>::Known({
        ProvenanceKind::Generated, {}, operationIdentity });
    result.executable = ValidateDescriptor(result.descriptor).empty();
    return result;
}

PropagationResult PropagateGeometry(
    const ValueDescriptor& input,
    const SpatialDescriptor& outputSpatial,
    const SamplingDescriptor& outputSampling,
    const std::string& operationIdentity) {
    PropagationResult result;
    result.descriptor = input;
    if (!IsImageLike(input.logicalType)) {
        result.executable = false;
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.connection.type-mismatch", DiagnosticSeverity::HardError,
            operationIdentity, "geometry-input-type",
            "Geometry requires an image-like input.", "Connect an image-like value."));
        return result;
    }
    result.descriptor.spatial = SemanticField<SpatialDescriptor>::Known(outputSpatial);
    result.descriptor.sampling = SemanticField<SamplingDescriptor>::Known(outputSampling);
    MarkDerivedProvenance(result.descriptor, operationIdentity);
    result.executable = ValidateDescriptor(result.descriptor).empty();
    return result;
}

PropagationResult PropagateColorTransform(
    const ValueDescriptor& input,
    const ColorIdentity& destinationColor,
    const TransferDescriptor& destinationTransfer,
    ReferenceState destinationReference,
    const std::string& operationIdentity) {
    PropagationResult result;
    result.descriptor = input;
    if (input.logicalType != LogicalValueType::ColorImage) {
        result.executable = false;
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.connection.type-mismatch", DiagnosticSeverity::HardError,
            operationIdentity, "color-transform-input-type",
            "Color Transform requires a ColorImage.", "Connect a ColorImage."));
        return result;
    }
    if (input.color.state != KnowledgeState::Known ||
        input.transfer.state != KnowledgeState::Known ||
        input.reference.state != KnowledgeState::Known) {
        result.executable = false;
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.semantic.metadata-missing", DiagnosticSeverity::HardError,
            operationIdentity, "color-transform-source-unknown",
            "The explicit color transform cannot run because source color meaning is unknown.",
            "Assign the source color state explicitly before converting it."));
        return result;
    }
    if (destinationColor.identity.empty() ||
        (destinationTransfer.kind == TransferKind::Custom && destinationTransfer.key.empty())) {
        result.executable = false;
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.semantic.metadata-missing", DiagnosticSeverity::HardError,
            operationIdentity, "color-transform-destination-invalid",
            "The destination color transform is incomplete.",
            "Choose an explicit destination identity, transfer, and reference state."));
        return result;
    }
    result.descriptor.color = SemanticField<ColorIdentity>::Known(destinationColor);
    result.descriptor.transfer = SemanticField<TransferDescriptor>::Known(destinationTransfer);
    result.descriptor.reference = SemanticField<ReferenceState>::Known(destinationReference);
    result.descriptor.provenance = SemanticField<ProvenanceDescriptor>::Known({
        ProvenanceKind::Converted, {}, operationIdentity });
    result.executable = ValidateDescriptor(result.descriptor).empty();
    return result;
}

PropagationResult PropagateComposite(
    const ValueDescriptor& source,
    const ValueDescriptor& backdrop,
    bool extentPolicyDeclared,
    const SpatialDescriptor& outputSpatial,
    AlphaMode requiredInputAlpha,
    AlphaMode outputAlpha,
    const std::string& operationIdentity) {
    PropagationResult result;
    result.descriptor = source;
    if (source.logicalType != LogicalValueType::ColorImage ||
        backdrop.logicalType != LogicalValueType::ColorImage) {
        result.executable = false;
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.connection.type-mismatch", DiagnosticSeverity::HardError,
            operationIdentity, "composite-input-type",
            "Composite requires two ColorImage inputs.", "Connect two ColorImage values."));
        return result;
    }
    if (!extentPolicyDeclared) {
        result.executable = false;
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.semantic.extent-policy-missing", DiagnosticSeverity::HardError,
            operationIdentity, "composite-extent-policy",
            "Composite extent and alignment policy is not declared.",
            "Choose union, intersection, source, backdrop, or another explicit extent policy."));
    }
    const bool sourceAlphaMatches = source.alpha.state == KnowledgeState::Known &&
        source.alpha.value == requiredInputAlpha;
    const bool backdropAlphaMatches = backdrop.alpha.state == KnowledgeState::Known &&
        backdrop.alpha.value == requiredInputAlpha;
    if (!sourceAlphaMatches || !backdropAlphaMatches) {
        result.executable = false;
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.semantic.alpha-mismatch", DiagnosticSeverity::HardError,
            operationIdentity, "composite-alpha-formula",
            "Input alpha representation does not match the declared composite formula.",
            "Use a matching formula or add an explicit alpha conversion."));
    }
    if (source.color.state == KnowledgeState::Known &&
        backdrop.color.state == KnowledgeState::Known &&
        !(source.color.value == backdrop.color.value)) {
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.semantic.color-mismatch", DiagnosticSeverity::Warning,
            operationIdentity, "composite-color-mismatch",
            "Inputs have different color identities; numeric composition remains authored and visible.",
            "Keep the numeric combination or add explicit color transforms."));
    }
    result.descriptor.alpha = SemanticField<AlphaMode>::Known(outputAlpha);
    result.descriptor.spatial = SemanticField<SpatialDescriptor>::Known(outputSpatial);
    MarkDerivedProvenance(result.descriptor, operationIdentity);
    if (!ValidateDescriptor(result.descriptor).empty()) {
        result.executable = false;
    }
    return result;
}

PropagationResult PropagateReduction(
    const ValueDescriptor& input,
    LogicalValueType outputType,
    const UnitDescriptor& outputUnits,
    const std::string& operationIdentity) {
    PropagationResult result;
    const bool supported = outputType == LogicalValueType::Scalar ||
        outputType == LogicalValueType::Vector2 ||
        outputType == LogicalValueType::Vector3 ||
        outputType == LogicalValueType::Vector4 ||
        outputType == LogicalValueType::Histogram ||
        outputType == LogicalValueType::Statistics;
    result.descriptor = MakeUnknownDescriptor(outputType);
    if (!IsImageLike(input.logicalType) || !supported) {
        result.executable = false;
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.connection.type-mismatch", DiagnosticSeverity::HardError,
            operationIdentity, "reduction-types",
            "Reduction requires an image-like input and a declared reduction output type.",
            "Use Scalar, Vector, Histogram, or Statistics output."));
        return result;
    }
    if (result.descriptor.units.state != KnowledgeState::NotApplicable) {
        result.descriptor.units = SemanticField<UnitDescriptor>::Known(outputUnits);
    }
    result.descriptor.range = input.range.state == KnowledgeState::NotApplicable
        ? result.descriptor.range : input.range;
    result.descriptor.precision = SemanticField<LogicalPrecision>::Known(LogicalPrecision::Float64);
    result.descriptor.provenance = SemanticField<ProvenanceDescriptor>::Known({
        ProvenanceKind::Derived, {}, operationIdentity });
    result.executable = ValidateDescriptor(result.descriptor).empty();
    return result;
}

PropagationResult PropagateDirectOutput(const ValueDescriptor& input) {
    return PropagateIdentity(input);
}

PropagationResult PropagateUnknownExternal(
    const ValueDescriptor& input,
    const ValueDescriptor& declaredOutput,
    const std::vector<FieldDispositionRule>& fieldPolicy,
    const std::string& operationIdentity) {
    PropagationResult result;
    result.descriptor = declaredOutput;
    std::set<DescriptorField> seen;
    for (const FieldDispositionRule& rule : fieldPolicy) {
        if (!seen.insert(rule.field).second) {
            result.executable = false;
        }
        ApplyFieldRule(result.descriptor, input, rule);
        if (rule.field == DescriptorField::Provenance &&
            rule.disposition == FieldDisposition::Generate) {
            result.descriptor.provenance = SemanticField<ProvenanceDescriptor>::Known({
                ProvenanceKind::External, {}, operationIdentity });
        }
    }
    constexpr std::size_t kDescriptorFieldCount = 11;
    if (seen.size() != kDescriptorFieldCount) {
        result.executable = false;
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.semantic.external-policy-missing", DiagnosticSeverity::HardError,
            operationIdentity, "external-field-policy",
            "External operation does not declare every descriptor-field disposition.",
            "Declare Preserve, Replace, Invalidate, Consume, Generate, or Drop for every field."));
    }
    if (result.descriptor.provenance.state != KnowledgeState::NotApplicable &&
        result.descriptor.provenance.state != KnowledgeState::Known) {
        result.descriptor.provenance = SemanticField<ProvenanceDescriptor>::Known({
            ProvenanceKind::External, {}, operationIdentity });
    }
    if (!ValidateDescriptor(result.descriptor).empty()) {
        result.executable = false;
    }
    return result;
}

} // namespace Stack::NodeMath
