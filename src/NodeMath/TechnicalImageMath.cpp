#include "NodeMath/TechnicalImageMath.h"

#include "NodeMath/DescriptorSerialization.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace Stack::NodeMath {
namespace {

Diagnostic MakeDiagnostic(
    const char* rule,
    DiagnosticSeverity severity,
    const std::string& fingerprint,
    std::string message,
    std::string repair = {}) {
    Diagnostic result;
    result.ruleId = rule;
    result.stage = DiagnosticStage::Semantic;
    result.severity = severity;
    result.affectedIdentity = fingerprint;
    result.semanticFingerprint = Sha256ContentIdentity(
        std::string(rule) + "\n" + fingerprint + "\n" + message);
    result.message = std::move(message);
    result.suggestedRepair = std::move(repair);
    return result;
}

bool IsKnownTransfer(const ValueDescriptor& descriptor, TransferKind kind) {
    return descriptor.transfer.state == KnowledgeState::Known &&
        descriptor.transfer.value.kind == kind;
}

void SetDerivedProvenance(ValueDescriptor& descriptor, const std::string& operation) {
    const std::string source = descriptor.provenance.state == KnowledgeState::Known
        ? descriptor.provenance.value.sourceIdentity : std::string();
    descriptor.provenance = SemanticField<ProvenanceDescriptor>::Known({
        operation.rfind("assign.", 0) == 0 ? ProvenanceKind::Assigned : ProvenanceKind::Converted,
        source,
        operation
    });
}

void WarnTransferIfNotLinear(
    const ValueDescriptor& input,
    const std::string& fingerprint,
    std::vector<Diagnostic>& diagnostics,
    const char* operationName) {
    if (input.transfer.state == KnowledgeState::Unknown) {
        diagnostics.push_back(MakeDiagnostic(
            "nmr.semantic.transfer-unknown", DiagnosticSeverity::Warning, fingerprint,
            std::string(operationName) + " will execute numerically, but the input transfer is Unknown.",
            "Assign or convert the transfer explicitly if linear-light behavior is intended."));
    } else if (!IsKnownTransfer(input, TransferKind::Linear)) {
        diagnostics.push_back(MakeDiagnostic(
            "nmr.semantic.transfer-nonlinear", DiagnosticSeverity::Warning, fingerprint,
            std::string(operationName) + " will execute in the declared encoded transfer.",
            "Add an explicit decode node first if linear-light behavior is intended."));
    }
}

const char* TransferLabel(const ValueDescriptor& descriptor) {
    if (descriptor.transfer.state == KnowledgeState::Unknown) return "Unknown transfer";
    if (descriptor.transfer.state != KnowledgeState::Known) return "N/A transfer";
    switch (descriptor.transfer.value.kind) {
    case TransferKind::Linear: return "Linear";
    case TransferKind::Srgb: return "sRGB transfer";
    case TransferKind::Gamma: return "Gamma";
    case TransferKind::Log: return "Log";
    case TransferKind::Pq: return "PQ";
    case TransferKind::Hlg: return "HLG";
    case TransferKind::Custom: return "Custom transfer";
    }
    return "Unknown transfer";
}

const char* AlphaLabel(const ValueDescriptor& descriptor) {
    if (descriptor.alpha.state == KnowledgeState::Unknown) return "Unknown alpha";
    if (descriptor.alpha.state != KnowledgeState::Known) return "N/A alpha";
    switch (descriptor.alpha.value) {
    case AlphaMode::Absent: return "No alpha";
    case AlphaMode::Opaque: return "Opaque";
    case AlphaMode::Straight: return "Straight alpha";
    case AlphaMode::Premultiplied: return "Premultiplied alpha";
    }
    return "Unknown alpha";
}

} // namespace

float DecodeSrgbExtended(float value) {
    const float magnitude = std::abs(value);
    if (magnitude <= 0.04045f) return value / 12.92f;
    return std::copysign(std::pow((magnitude + 0.055f) / 1.055f, 2.4f), value);
}

float EncodeSrgbExtended(float value) {
    const float magnitude = std::abs(value);
    if (magnitude <= 0.0031308f) return 12.92f * value;
    return std::copysign(1.055f * std::pow(magnitude, 1.0f / 2.4f) - 0.055f, value);
}

Rgba32f ApplyTechnicalImageOperation(
    TechnicalImageOperation operation,
    const Rgba32f& input,
    float exposureValue) {
    Rgba32f output = input;
    switch (operation) {
    case TechnicalImageOperation::AssignSrgb:
    case TechnicalImageOperation::AssignLinearSrgb:
    case TechnicalImageOperation::AssignLinearDisplayP3:
        return input;
    case TechnicalImageOperation::SrgbDecode:
        for (int channel = 0; channel < 3; ++channel) output[channel] = DecodeSrgbExtended(input[channel]);
        break;
    case TechnicalImageOperation::SrgbEncode:
        for (int channel = 0; channel < 3; ++channel) output[channel] = EncodeSrgbExtended(input[channel]);
        break;
    case TechnicalImageOperation::LinearSrgbToDisplayP3:
        output[0] = 0.82259287f * input[0] + 0.17753395f * input[1];
        output[1] = 0.03319951f * input[0] + 0.96678350f * input[1];
        output[2] = 0.01708535f * input[0] + 0.07239572f * input[1] + 0.91030148f * input[2];
        break;
    case TechnicalImageOperation::LinearDisplayP3ToSrgb:
        output[0] = 1.22474527f * input[0] - 0.22490472f * input[1];
        output[1] = -0.04205797f * input[0] + 1.04208100f * input[1];
        output[2] = -0.01964227f * input[0] - 0.07865400f * input[1] + 1.09853700f * input[2];
        break;
    case TechnicalImageOperation::Exposure: {
        const float scale = std::exp2(exposureValue);
        for (int channel = 0; channel < 3; ++channel) output[channel] = input[channel] * scale;
        break;
    }
    case TechnicalImageOperation::Premultiply:
        for (int channel = 0; channel < 3; ++channel) output[channel] = input[channel] * input[3];
        break;
    case TechnicalImageOperation::Unpremultiply:
        if (input[3] <= 1.0e-6f) {
            output[0] = output[1] = output[2] = 0.0f;
        } else {
            for (int channel = 0; channel < 3; ++channel) output[channel] = input[channel] / input[3];
        }
        break;
    }
    return output;
}

Rgba32f CompositeSourceOver(
    CompositeAlphaFormula formula,
    const Rgba32f& source,
    const Rgba32f& backdrop,
    float sourceOpacity) {
    const float opacity = std::clamp(sourceOpacity, 0.0f, 1.0f);
    const float sourceAlpha = source[3] * opacity;
    const float outputAlpha = sourceAlpha + backdrop[3] * (1.0f - sourceAlpha);
    Rgba32f output{};
    output[3] = outputAlpha;
    if (formula == CompositeAlphaFormula::PremultipliedSourceOver) {
        for (int channel = 0; channel < 3; ++channel) {
            output[channel] = source[channel] * opacity + backdrop[channel] * (1.0f - sourceAlpha);
        }
    } else if (outputAlpha > 1.0e-6f) {
        for (int channel = 0; channel < 3; ++channel) {
            const float premultiplied = source[channel] * sourceAlpha +
                backdrop[channel] * backdrop[3] * (1.0f - sourceAlpha);
            output[channel] = premultiplied / outputAlpha;
        }
    }
    return output;
}

std::string TechnicalOperationIdentity(TechnicalImageOperation operation) {
    switch (operation) {
    case TechnicalImageOperation::AssignSrgb: return "assign.srgb-d65.srgb-transfer.v1";
    case TechnicalImageOperation::AssignLinearSrgb: return "assign.srgb-d65.linear.v1";
    case TechnicalImageOperation::AssignLinearDisplayP3: return "assign.display-p3-d65.linear.v1";
    case TechnicalImageOperation::SrgbDecode: return "convert.srgb-transfer-to-linear.extended.v1";
    case TechnicalImageOperation::SrgbEncode: return "convert.linear-to-srgb-transfer.extended.v1";
    case TechnicalImageOperation::LinearSrgbToDisplayP3: return "convert.linear-srgb-to-linear-display-p3.v1";
    case TechnicalImageOperation::LinearDisplayP3ToSrgb: return "convert.linear-display-p3-to-linear-srgb.v1";
    case TechnicalImageOperation::Exposure: return "math.exposure-ev.v1";
    case TechnicalImageOperation::Premultiply: return "alpha.premultiply.v1";
    case TechnicalImageOperation::Unpremultiply: return "alpha.unpremultiply-epsilon-1e-6.v1";
    }
    return "technical-image.unknown";
}

ValueDescriptor DescribeTechnicalImageOutput(
    TechnicalImageOperation operation,
    const ValueDescriptor& input,
    float exposureValue,
    std::vector<Diagnostic>& diagnostics) {
    (void)exposureValue;
    ValueDescriptor output = input;
    const std::string identity = TechnicalOperationIdentity(operation);
    const std::string fingerprint = DescriptorContentIdentity(input);
    if (input.logicalType != LogicalValueType::ColorImage) {
        diagnostics.push_back(MakeDiagnostic(
            "nmr.connection.type-mismatch", DiagnosticSeverity::HardError, fingerprint,
            "Technical image operations require a color-image input."));
        return MakeUnknownDescriptor(LogicalValueType::Failure);
    }

    switch (operation) {
    case TechnicalImageOperation::AssignSrgb:
        output.color = SemanticField<ColorIdentity>::Known({ "srgb-d65", {}, ColorRelation::Standard });
        output.transfer = SemanticField<TransferDescriptor>::Known({ TransferKind::Srgb, 0.0, {} });
        output.reference = SemanticField<ReferenceState>::Known(ReferenceState::Display);
        break;
    case TechnicalImageOperation::AssignLinearSrgb:
        output.color = SemanticField<ColorIdentity>::Known({ "srgb-d65", {}, ColorRelation::Standard });
        output.transfer = SemanticField<TransferDescriptor>::Known({ TransferKind::Linear, 0.0, {} });
        break;
    case TechnicalImageOperation::AssignLinearDisplayP3:
        output.color = SemanticField<ColorIdentity>::Known({ "display-p3-d65", {}, ColorRelation::Standard });
        output.transfer = SemanticField<TransferDescriptor>::Known({ TransferKind::Linear, 0.0, {} });
        break;
    case TechnicalImageOperation::SrgbDecode:
        if (!IsKnownTransfer(input, TransferKind::Srgb)) {
            diagnostics.push_back(MakeDiagnostic(
                "nmr.semantic.transfer-unexpected", DiagnosticSeverity::Warning, fingerprint,
                "sRGB Decode will execute, but the input is not declared as sRGB-encoded.",
                "Assign the intended input transfer explicitly if this connection is intentional."));
        }
        output.transfer = SemanticField<TransferDescriptor>::Known({ TransferKind::Linear, 0.0, {} });
        break;
    case TechnicalImageOperation::SrgbEncode:
        if (!IsKnownTransfer(input, TransferKind::Linear)) {
            diagnostics.push_back(MakeDiagnostic(
                "nmr.semantic.transfer-unexpected", DiagnosticSeverity::Warning, fingerprint,
                "sRGB Encode will execute, but the input is not declared Linear.",
                "Decode or assign the intended input transfer explicitly if this connection is intentional."));
        }
        output.transfer = SemanticField<TransferDescriptor>::Known({ TransferKind::Srgb, 0.0, {} });
        output.reference = SemanticField<ReferenceState>::Known(ReferenceState::Display);
        break;
    case TechnicalImageOperation::LinearSrgbToDisplayP3:
        WarnTransferIfNotLinear(input, fingerprint, diagnostics, "Linear sRGB to Display-P3");
        if (input.color.state != KnowledgeState::Known || input.color.value.identity != "srgb-d65") {
            diagnostics.push_back(MakeDiagnostic(
                "nmr.semantic.color-unexpected", DiagnosticSeverity::Warning, fingerprint,
                "The sRGB-to-P3 matrix will execute, but the input is not declared sRGB D65."));
        }
        output.color = SemanticField<ColorIdentity>::Known({ "display-p3-d65", {}, ColorRelation::Standard });
        output.transfer = SemanticField<TransferDescriptor>::Known({ TransferKind::Linear, 0.0, {} });
        break;
    case TechnicalImageOperation::LinearDisplayP3ToSrgb:
        WarnTransferIfNotLinear(input, fingerprint, diagnostics, "Linear Display-P3 to sRGB");
        if (input.color.state != KnowledgeState::Known || input.color.value.identity != "display-p3-d65") {
            diagnostics.push_back(MakeDiagnostic(
                "nmr.semantic.color-unexpected", DiagnosticSeverity::Warning, fingerprint,
                "The P3-to-sRGB matrix will execute, but the input is not declared Display-P3 D65."));
        }
        output.color = SemanticField<ColorIdentity>::Known({ "srgb-d65", {}, ColorRelation::Standard });
        output.transfer = SemanticField<TransferDescriptor>::Known({ TransferKind::Linear, 0.0, {} });
        break;
    case TechnicalImageOperation::Exposure:
        WarnTransferIfNotLinear(input, fingerprint, diagnostics, "Exposure");
        if (output.range.state == KnowledgeState::Known) {
            output.range.value.allowsAboveNominal = true;
            output.range.value.allowsBelowNominal = true;
        }
        break;
    case TechnicalImageOperation::Premultiply:
        if (input.alpha.state != KnowledgeState::Known || input.alpha.value != AlphaMode::Straight) {
            diagnostics.push_back(MakeDiagnostic(
                "nmr.semantic.alpha-formula-mismatch", DiagnosticSeverity::Warning, fingerprint,
                "Premultiply will execute, but the input is not declared Straight alpha."));
        }
        output.alpha = SemanticField<AlphaMode>::Known(AlphaMode::Premultiplied);
        break;
    case TechnicalImageOperation::Unpremultiply:
        if (input.alpha.state != KnowledgeState::Known || input.alpha.value != AlphaMode::Premultiplied) {
            diagnostics.push_back(MakeDiagnostic(
                "nmr.semantic.alpha-formula-mismatch", DiagnosticSeverity::Warning, fingerprint,
                "Unpremultiply will execute, but the input is not declared Premultiplied alpha."));
        }
        output.alpha = SemanticField<AlphaMode>::Known(AlphaMode::Straight);
        break;
    }
    SetDerivedProvenance(output, identity);
    return output;
}

DirectOutputPolicy EvaluateDirectPngOutputPolicy(const ValueDescriptor& descriptor) {
    DirectOutputPolicy result;
    const std::string fingerprint = DescriptorContentIdentity(descriptor);
    if (descriptor.logicalType != LogicalValueType::ColorImage) {
        result.executable = false;
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.output.type-mismatch", DiagnosticSeverity::HardError, fingerprint,
            "PNG output requires a color image."));
        return result;
    }
    if (descriptor.alpha.state == KnowledgeState::Known &&
        descriptor.alpha.value == AlphaMode::Premultiplied) {
        result.executable = false;
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.output.png-premultiplied", DiagnosticSeverity::HardError, fingerprint,
            "PNG stores straight alpha; the graph output is declared Premultiplied.",
            "Add an explicit Unpremultiply node before Output."));
    }
    const bool srgbTransfer = IsKnownTransfer(descriptor, TransferKind::Srgb);
    if (descriptor.color.state == KnowledgeState::Known) {
        if (descriptor.color.value.identity.rfind("icc:", 0) == 0 &&
            !descriptor.color.value.profileHash.empty()) {
            result.writeRetainedIccProfile = true;
            result.retainedProfileDependency = descriptor.color.value.profileHash;
        } else if (srgbTransfer && descriptor.color.value.identity == "srgb-d65") {
            result.writeSrgbChunk = true;
        } else if (srgbTransfer && descriptor.color.value.identity == "display-p3-d65") {
            result.writeDisplayP3Cicp = true;
        }
    }
    if (descriptor.color.state != KnowledgeState::Known ||
        (descriptor.transfer.state != KnowledgeState::Known &&
         !result.writeRetainedIccProfile)) {
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.output.color-unknown", DiagnosticSeverity::Warning, fingerprint,
            "The direct graph result will be exported without an assumed color profile because its color state is Unknown.",
            "Assign or convert color and transfer explicitly before Output if the file should be tagged."));
    } else if (!result.writeSrgbChunk && !result.writeDisplayP3Cicp &&
        !result.writeRetainedIccProfile) {
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.output.profile-unavailable", DiagnosticSeverity::Warning, fingerprint,
            "The declared output color state has no retained profile payload that PNG export can attach."));
    }
    result.diagnostics.push_back(MakeDiagnostic(
        "nmr.output.png-quantization", DiagnosticSeverity::Information, fingerprint,
        "PNG export quantizes the direct graph result to 8-bit values; it does not tone-map, normalize, gamut-map, or apply a hidden view transform."));
    return result;
}

std::string CompactDescriptorLabel(const ValueDescriptor& descriptor) {
    const std::string color = descriptor.color.state == KnowledgeState::Known
        ? descriptor.color.value.identity
        : (descriptor.color.state == KnowledgeState::Unknown ? "Unknown color" : "N/A color");
    return color + " | " + TransferLabel(descriptor) + " | " + AlphaLabel(descriptor);
}

} // namespace Stack::NodeMath
