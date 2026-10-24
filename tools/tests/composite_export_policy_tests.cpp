#include "Editor/CompositeExportPolicy.h"
#include "NodeMath/DescriptorSerialization.h"

#include <iostream>
#include <string>

namespace {
int failures = 0;
void Check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}
}

int main() {
    using namespace Stack::NodeMath;
    using Stack::EditorRendering::ValidateCompositePngInput;
    SpatialDescriptor spatial;
    spatial.kind = SpatialExtentKind::Finite;
    spatial.fullWindow = spatial.dataWindow = {0, 0, 1, 1};
    const auto straight = MakeTaggedColorImageDescriptor(
        "srgb-d65", {}, {TransferKind::Srgb, 0.0, {}}, ReferenceState::Display,
        AlphaMode::Straight, spatial, {}, LogicalPrecision::Float32, "composite-fixture");
    std::vector<Diagnostic> diagnostics;
    const auto premultiplied = DescribeTechnicalImageOutput(
        TechnicalImageOperation::Premultiply, straight, 0.0f, diagnostics);
    const auto restored = DescribeTechnicalImageOutput(
        TechnicalImageOperation::Unpremultiply, premultiplied, 0.0f, diagnostics);
    const auto fingerprint = DescriptorContentIdentity(premultiplied);
    std::string error = "stale failure";
    Check(ValidateCompositePngInput(1, &straight, true, error) && error.empty(),
        "straight input passes and clears stale error");
    Check(!ValidateCompositePngInput(42, &premultiplied, true, error),
        "explicit Premultiply output is rejected before straight-alpha composition");
    Check(error.find("42") != std::string::npos &&
        error.find("PNG stores straight alpha") != std::string::npos &&
        error.find("Add an explicit Unpremultiply node before Output.") != std::string::npos,
        "rejection identifies the output and preserves the existing conversion repair");
    Check(DescriptorContentIdentity(premultiplied) == fingerprint,
        "preflight does not silently alter input alpha semantics");
    Check(!ValidateCompositePngInput(42, &restored, false, error) &&
        error.find("finish rendering") != std::string::npos,
        "current Straight descriptor cannot approve stale premultiplied pixels");
    Check(ValidateCompositePngInput(42, &restored, true, error) && error.empty(),
        "explicit Unpremultiply makes the output exportable");
    auto unknown = straight;
    unknown.alpha = SemanticField<AlphaMode>::Unknown();
    Check(ValidateCompositePngInput(2, &unknown, true, error),
        "unknown alpha keeps the direct policy instead of guessing a conversion");
    auto opaque = straight;
    opaque.alpha = SemanticField<AlphaMode>::Known(AlphaMode::Opaque);
    Check(ValidateCompositePngInput(3, &opaque, true, error), "opaque input remains exportable");
    auto channel = straight;
    channel.logicalType = LogicalValueType::Channel;
    Check(!ValidateCompositePngInput(4, &channel, true, error) &&
        error.find("Image Combine") != std::string::npos,
        "channel inspection reuses direct PNG type policy");
    Check(!ValidateCompositePngInput(5, nullptr, true, error) &&
        error.find("could not be validated") != std::string::npos,
        "missing metadata fails closed for an actual composite input");
    const Rgba32f red{1.0f, 0.0f, 0.0f, 128.0f / 255.0f};
    const auto premultipliedRed = ApplyTechnicalImageOperation(TechnicalImageOperation::Premultiply, red);
    Check(premultipliedRed[0] == red[3] && premultipliedRed[3] == red[3],
        "fixture contains genuinely premultiplied samples");
    Check(ApplyTechnicalImageOperation(TechnicalImageOperation::Unpremultiply, premultipliedRed) == red,
        "the authored conversion restores straight samples without a hidden export transform");
    if (failures) return 1;
    std::cout << "Composite PNG input policy checks passed.\n";
}
