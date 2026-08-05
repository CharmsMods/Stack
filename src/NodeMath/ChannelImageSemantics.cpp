#include "NodeMath/ChannelImageSemantics.h"

#include "NodeMath/DescriptorSerialization.h"

#include <algorithm>
#include <utility>

namespace Stack::NodeMath {
namespace {

constexpr std::array<ImageComponent, 4> kComponents = {
    ImageComponent::Red,
    ImageComponent::Green,
    ImageComponent::Blue,
    ImageComponent::Alpha
};

Diagnostic MakeDiagnostic(
    const char* ruleId,
    DiagnosticSeverity severity,
    const std::string& operationIdentity,
    std::string discriminator,
    std::string message,
    std::string repair = {}) {
    Diagnostic diagnostic;
    diagnostic.ruleId = ruleId;
    diagnostic.stage = DiagnosticStage::Semantic;
    diagnostic.severity = severity;
    diagnostic.authoredSourceIdentity = operationIdentity;
    diagnostic.affectedIdentity = operationIdentity;
    diagnostic.semanticFingerprint = Sha256ContentIdentity(
        std::string(ruleId) + "\n" + operationIdentity + "\n" + discriminator);
    diagnostic.message = std::move(message);
    diagnostic.suggestedRepair = std::move(repair);
    return diagnostic;
}

ValueDescriptor FailureDescriptor() {
    return MakeUnknownDescriptor(LogicalValueType::Failure);
}

bool SameKnownSpatial(
    const SemanticField<SpatialDescriptor>& left,
    const SemanticField<SpatialDescriptor>& right) {
    return left.state == KnowledgeState::Known &&
        right.state == KnowledgeState::Known &&
        left.value.kind == SpatialExtentKind::Finite &&
        right.value.kind == SpatialExtentKind::Finite &&
        left.value == right.value;
}

template <typename T>
SemanticField<T> PreserveWhenAllEqual(
    const std::array<const ValueDescriptor*, 4>& inputs,
    SemanticField<T> ValueDescriptor::* member) {
    const SemanticField<T>* first = nullptr;
    for (const ValueDescriptor* input : inputs) {
        if (!input) {
            continue;
        }
        const SemanticField<T>& field = input->*member;
        if (!first) {
            first = &field;
        } else if (*first != field) {
            return SemanticField<T>::Unknown();
        }
    }
    if (!first || first->state == KnowledgeState::NotApplicable) {
        return SemanticField<T>::Unknown();
    }
    return *first;
}

} // namespace

ValueDescriptor MakePartialColorImageDescriptor(
    const ImageComponentSet& components,
    std::string operationIdentity) {
    ValueDescriptor descriptor = MakeUnknownDescriptor(LogicalValueType::ColorImage);
    descriptor.channels = SemanticField<ChannelDescriptor>::Known(
        MakeImageChannelDescriptor(components));
    descriptor.presentImageComponents =
        SemanticField<ImageComponentSet>::Known(components);
    descriptor.alpha = HasImageComponent(components, ImageComponent::Alpha)
        ? SemanticField<AlphaMode>::Unknown()
        : SemanticField<AlphaMode>::Known(AlphaMode::Absent);
    descriptor.provenance = SemanticField<ProvenanceDescriptor>::Known({
        ProvenanceKind::Derived,
        {},
        std::move(operationIdentity)
    });
    return descriptor;
}

ChannelImageDescription DescribeImageComponentExtraction(
    const ValueDescriptor& image,
    ImageComponent component,
    const std::string& operationIdentity) {
    ChannelImageDescription result;
    result.descriptor = FailureDescriptor();
    if (image.logicalType != LogicalValueType::ColorImage) {
        result.executable = false;
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.connection.type-mismatch",
            DiagnosticSeverity::HardError,
            operationIdentity,
            "component-extraction-type",
            "Channel Split requires a Color Image.",
            "Connect an Image or use the matching typed operation."));
        return result;
    }
    if (image.presentImageComponents.state != KnowledgeState::Known) {
        result.executable = false;
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.semantic.image-component-set-invalid",
            DiagnosticSeverity::HardError,
            operationIdentity,
            "component-presence-unknown",
            "Channel Split cannot prove which Image components are present.",
            "Repair or reconstruct the Image with explicit Channel inputs."));
        return result;
    }
    if (!HasImageComponent(image.presentImageComponents.value, component)) {
        result.executable = false;
        const std::string token = ImageComponentToken(component);
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.semantic.image-component-missing",
            DiagnosticSeverity::HardError,
            operationIdentity,
            "missing-" + token,
            "Image component " + token + " is absent.",
            "Connect a present component or construct it explicitly before splitting."));
        return result;
    }

    result.descriptor = MakeUnknownDescriptor(LogicalValueType::Channel);
    result.descriptor.channels = SemanticField<ChannelDescriptor>::Known({
        ChannelLayout::Gray,
        { ImageComponentToken(component) }
    });
    result.descriptor.range = image.range;
    result.descriptor.precision = image.precision;
    result.descriptor.spatial = image.spatial;
    result.descriptor.sampling = image.sampling;
    result.descriptor.units = SemanticField<UnitDescriptor>::Known({
        UnitKind::Unitless,
        {}
    });
    result.descriptor.provenance = SemanticField<ProvenanceDescriptor>::Known({
        ProvenanceKind::Derived,
        DescriptorContentIdentity(image),
        operationIdentity + "." + ImageComponentToken(component)
    });
    if (!ValidateDescriptor(result.descriptor).empty()) {
        result.descriptor = FailureDescriptor();
        result.executable = false;
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.semantic.image-component-set-invalid",
            DiagnosticSeverity::HardError,
            operationIdentity,
            "component-output-invalid",
            "The extracted Channel descriptor is invalid."));
    }
    return result;
}

ChannelImageDescription DescribeImageCombine(
    const std::array<const ValueDescriptor*, 4>& inputs,
    const std::string& operationIdentity) {
    ChannelImageDescription result;
    ImageComponentSet present;
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        if (inputs[index]) {
            AddImageComponent(present, kComponents[index]);
        }
    }
    result.descriptor =
        MakePartialColorImageDescriptor(present, operationIdentity);

    const bool hasColor =
        HasImageComponent(present, ImageComponent::Red) ||
        HasImageComponent(present, ImageComponent::Green) ||
        HasImageComponent(present, ImageComponent::Blue);
    if (!hasColor) {
        result.executable = false;
        result.diagnostics.push_back(MakeDiagnostic(
            "nmr.semantic.image-combine-color-missing",
            DiagnosticSeverity::HardError,
            operationIdentity,
            "color-missing",
            "Image Combine requires at least one connected R, G, or B Channel.",
            "Connect a color Channel before using the Image."));
    }

    const ValueDescriptor* firstInput = nullptr;
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const ValueDescriptor* input = inputs[index];
        if (!input) {
            continue;
        }
        if (input->logicalType != LogicalValueType::Channel) {
            result.executable = false;
            result.diagnostics.push_back(MakeDiagnostic(
                "nmr.connection.type-mismatch",
                DiagnosticSeverity::HardError,
                operationIdentity,
                "input-" + std::to_string(index),
                "Image Combine accepts Channel inputs only."));
            continue;
        }
        if (input->spatial.state != KnowledgeState::Known ||
            input->spatial.value.kind != SpatialExtentKind::Finite) {
            result.executable = false;
            result.diagnostics.push_back(MakeDiagnostic(
                "nmr.semantic.image-combine-extent-unknown",
                DiagnosticSeverity::HardError,
                operationIdentity,
                "extent-" + std::to_string(index),
                "Image Combine requires a known finite extent for every connected Channel.",
                "Connect Match Extent or another Channel with a known extent."));
            continue;
        }
        if (!firstInput) {
            firstInput = input;
        } else if (!SameKnownSpatial(firstInput->spatial, input->spatial)) {
            result.executable = false;
            result.diagnostics.push_back(MakeDiagnostic(
                "nmr.semantic.image-combine-extent-mismatch",
                DiagnosticSeverity::HardError,
                operationIdentity,
                "extent-" + std::to_string(index),
                "Image Combine inputs have different extents.",
                "Add an explicit Reformat to the mismatched Channel."));
        }
    }

    if (firstInput) {
        result.descriptor.spatial = firstInput->spatial;
    }
    result.descriptor.sampling =
        PreserveWhenAllEqual(inputs, &ValueDescriptor::sampling);
    result.descriptor.range =
        PreserveWhenAllEqual(inputs, &ValueDescriptor::range);
    result.descriptor.precision =
        PreserveWhenAllEqual(inputs, &ValueDescriptor::precision);

    if (!ValidateDescriptor(result.descriptor).empty()) {
        result.executable = false;
    }
    return result;
}

} // namespace Stack::NodeMath
