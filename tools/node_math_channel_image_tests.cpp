#include "NodeMath/ChannelImageSemantics.h"
#include "NodeMath/DescriptorPropagation.h"
#include "NodeMath/DescriptorSerialization.h"
#include "NodeMath/OutputInspection.h"
#include "NodeMath/TechnicalImageMath.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace Stack::NodeMath;

class Tests {
public:
    void Check(bool condition, const std::string& name) {
        ++m_Total;
        if (condition) {
            return;
        }
        ++m_Failed;
        std::cerr << "FAIL: " << name << '\n';
    }

    int Finish() const {
        std::cout << "Stack Node Math Channel Image: "
                  << (m_Total - m_Failed) << "/" << m_Total
                  << " checks passed\n";
        return m_Failed == 0 ? 0 : 1;
    }

private:
    int m_Total = 0;
    int m_Failed = 0;
};

SpatialDescriptor TestSpatial(
    std::int64_t width = 32,
    std::int64_t height = 20) {
    SpatialDescriptor spatial;
    spatial.kind = SpatialExtentKind::Finite;
    spatial.fullWindow = { 0, 0, width, height };
    spatial.dataWindow = spatial.fullWindow;
    spatial.rasterOrigin = RasterOrigin::BottomLeft;
    spatial.pixelAspect = 1.0;
    return spatial;
}

SamplingDescriptor TestSampling(
    ReconstructionFilter filter = ReconstructionFilter::Linear) {
    SamplingDescriptor sampling;
    sampling.coordinates = CoordinateConvention::PixelCenters;
    sampling.filter = filter;
    sampling.border = BorderPolicy::Transparent;
    return sampling;
}

ImageComponentSet ComponentsFromBits(std::uint8_t bits) {
    return { bits };
}

ValueDescriptor PartialImage(
    std::uint8_t bits,
    const std::string& operation = "test.partial-image") {
    ValueDescriptor descriptor =
        MakePartialColorImageDescriptor(ComponentsFromBits(bits), operation);
    descriptor.range = SemanticField<NumericRange>::Known({
        0.0, 1.0, true, true, NonFinitePolicy::Preserve
    });
    descriptor.precision =
        SemanticField<LogicalPrecision>::Known(LogicalPrecision::Float32);
    descriptor.spatial =
        SemanticField<SpatialDescriptor>::Known(TestSpatial());
    descriptor.sampling =
        SemanticField<SamplingDescriptor>::Known(TestSampling());
    return descriptor;
}

ValueDescriptor Channel(
    const std::string& role,
    const SpatialDescriptor& spatial = TestSpatial(),
    const SamplingDescriptor& sampling = TestSampling()) {
    ValueDescriptor descriptor = MakeUnknownDescriptor(LogicalValueType::Channel);
    descriptor.channels = SemanticField<ChannelDescriptor>::Known({
        ChannelLayout::Gray, { role }
    });
    descriptor.range = SemanticField<NumericRange>::Known({
        0.0, 1.0, true, true, NonFinitePolicy::Preserve
    });
    descriptor.precision =
        SemanticField<LogicalPrecision>::Known(LogicalPrecision::Float32);
    descriptor.spatial =
        SemanticField<SpatialDescriptor>::Known(spatial);
    descriptor.sampling =
        SemanticField<SamplingDescriptor>::Known(sampling);
    descriptor.units = SemanticField<UnitDescriptor>::Known({
        UnitKind::Unitless, {}
    });
    descriptor.provenance =
        SemanticField<ProvenanceDescriptor>::Known({
            ProvenanceKind::Generated, {}, "test.channel." + role
        });
    return descriptor;
}

bool HasRule(
    const std::vector<Diagnostic>& diagnostics,
    const std::string& rule,
    DiagnosticSeverity severity = DiagnosticSeverity::HardError) {
    return std::any_of(
        diagnostics.begin(),
        diagnostics.end(),
        [&](const Diagnostic& diagnostic) {
            return diagnostic.ruleId == rule &&
                diagnostic.severity == severity;
        });
}

void ComponentSetAndSerializationTests(Tests& tests) {
    std::set<std::string> identities;
    for (std::uint8_t bits = 1; bits <= 0x0f; ++bits) {
        const ValueDescriptor descriptor = PartialImage(bits);
        tests.Check(
            ValidateDescriptor(descriptor).empty(),
            "every nonempty Image component subset validates: " +
                std::to_string(bits));

        const nlohmann::json encoded = SerializeValueDescriptor(descriptor);
        const DescriptorParseResult decoded = ParseValueDescriptor(encoded);
        tests.Check(
            decoded.descriptor.has_value() &&
                decoded.issues.empty() &&
                *decoded.descriptor == descriptor,
            "every Image component subset round-trips: " +
                std::to_string(bits));
        tests.Check(
            identities.insert(DescriptorContentIdentity(descriptor)).second,
            "component subsets have distinct semantic identities: " +
                std::to_string(bits));

        const std::vector<ImageComponent> ordered =
            OrderedImageComponents(ComponentsFromBits(bits));
        const nlohmann::json& serializedComponents =
            encoded["presentImageComponents"]["value"];
        bool canonicalOrder =
            serializedComponents.is_array() &&
            serializedComponents.size() == ordered.size();
        for (std::size_t index = 0;
             canonicalOrder && index < ordered.size();
             ++index) {
            canonicalOrder =
                serializedComponents[index].is_string() &&
                serializedComponents[index].get<std::string>() ==
                    ImageComponentToken(ordered[index]);
        }
        tests.Check(
            canonicalOrder,
            "component serialization uses canonical R/G/B/A order: " +
                std::to_string(bits));
    }

    ValueDescriptor inconsistentChannels = PartialImage(
        static_cast<std::uint8_t>(ImageComponent::Red));
    inconsistentChannels.channels =
        SemanticField<ChannelDescriptor>::Known({
            ChannelLayout::RGB, { "red", "green", "blue" }
        });
    tests.Check(
        !ValidateDescriptor(inconsistentChannels).empty(),
        "component presence cannot contradict channel layout");

    ValueDescriptor inconsistentAlpha = PartialImage(
        static_cast<std::uint8_t>(ImageComponent::Red) |
        static_cast<std::uint8_t>(ImageComponent::Alpha));
    inconsistentAlpha.alpha =
        SemanticField<AlphaMode>::Known(AlphaMode::Opaque);
    tests.Check(
        !ValidateDescriptor(inconsistentAlpha).empty(),
        "stored alpha presence cannot contradict opaque alpha mode");

    ValueDescriptor rgb = PartialImage(0x07, "test.legacy-rgb");
    nlohmann::json legacyRgb = SerializeValueDescriptor(rgb);
    legacyRgb["schemaVersion"] = 2;
    legacyRgb.erase("presentImageComponents");
    const DescriptorParseResult migratedRgb =
        ParseValueDescriptor(legacyRgb);
    tests.Check(
        migratedRgb.descriptor.has_value() &&
            migratedRgb.issues.empty() &&
            migratedRgb.descriptor->schemaVersion ==
                kSemanticDescriptorSchemaVersion &&
            migratedRgb.descriptor->presentImageComponents ==
                rgb.presentImageComponents,
        "schema v2 RGB descriptors migrate to explicit RGB presence");

    ValueDescriptor partial = PartialImage(
        static_cast<std::uint8_t>(ImageComponent::Red) |
        static_cast<std::uint8_t>(ImageComponent::Blue),
        "test.legacy-partial");
    nlohmann::json legacyPartial = SerializeValueDescriptor(partial);
    legacyPartial["schemaVersion"] = 2;
    legacyPartial.erase("presentImageComponents");
    const DescriptorParseResult migratedPartial =
        ParseValueDescriptor(legacyPartial);
    tests.Check(
        migratedPartial.descriptor.has_value() &&
            migratedPartial.issues.empty() &&
            migratedPartial.descriptor->presentImageComponents ==
                partial.presentImageComponents,
        "schema v2 named R+B descriptors migrate without inventing G or A");

    nlohmann::json duplicate = SerializeValueDescriptor(partial);
    duplicate["presentImageComponents"]["value"] =
        nlohmann::json::array({ "R", "R" });
    tests.Check(
        !ParseValueDescriptor(duplicate).descriptor.has_value(),
        "schema v3 rejects duplicate component tokens");

    nlohmann::json empty = SerializeValueDescriptor(partial);
    empty["presentImageComponents"]["value"] =
        nlohmann::json::array();
    tests.Check(
        !ParseValueDescriptor(empty).descriptor.has_value(),
        "schema v3 rejects an empty known component set");
}

void PreservationTests(Tests& tests) {
    const ValueDescriptor source = PartialImage(
        static_cast<std::uint8_t>(ImageComponent::Red) |
        static_cast<std::uint8_t>(ImageComponent::Blue));

    const PropagationResult identity = PropagateIdentity(source);
    tests.Check(
        identity.executable &&
            identity.descriptor.presentImageComponents ==
                source.presentImageComponents,
        "identity preserves exact Image component presence");

    const PropagationResult arithmetic =
        PropagateGenericArithmetic(source, "test.arithmetic");
    tests.Check(
        arithmetic.executable &&
            arithmetic.descriptor.presentImageComponents ==
                source.presentImageComponents,
        "generic arithmetic preserves exact Image component presence");

    const SpatialDescriptor resized = TestSpatial(64, 40);
    const PropagationResult reformatted = PropagateGeometry(
        source,
        resized,
        TestSampling(ReconstructionFilter::Cubic),
        "test.reformat");
    tests.Check(
        reformatted.executable &&
            reformatted.descriptor.presentImageComponents ==
                source.presentImageComponents &&
            reformatted.descriptor.spatial.value == resized,
        "Reformat changes geometry without inventing Image components");

    tests.Check(
        CompactDescriptorLabel(source).find("Image \xC2\xB7 R, B |") == 0,
        "compact labels expose exact partial-Image presence");
}

void ExtractionTests(Tests& tests) {
    const ValueDescriptor source = PartialImage(
        static_cast<std::uint8_t>(ImageComponent::Red) |
        static_cast<std::uint8_t>(ImageComponent::Blue));
    const ChannelImageDescription blue =
        DescribeImageComponentExtraction(
            source,
            ImageComponent::Blue,
            "test.split");
    tests.Check(
        blue.executable &&
            blue.diagnostics.empty() &&
            blue.descriptor.logicalType == LogicalValueType::Channel &&
            blue.descriptor.spatial == source.spatial &&
            blue.descriptor.sampling == source.sampling &&
            blue.descriptor.range == source.range &&
            blue.descriptor.precision == source.precision &&
            ValidateDescriptor(blue.descriptor).empty(),
        "present Image components extract to valid Channels");

    const ChannelImageDescription missing =
        DescribeImageComponentExtraction(
            source,
            ImageComponent::Green,
            "test.split");
    tests.Check(
        !missing.executable &&
            HasRule(
                missing.diagnostics,
                "nmr.semantic.image-component-missing"),
        "absent Image components fail with an exact hard diagnostic");

    ValueDescriptor unknown = source;
    unknown.presentImageComponents =
        SemanticField<ImageComponentSet>::Unknown();
    const ChannelImageDescription unknownResult =
        DescribeImageComponentExtraction(
            unknown,
            ImageComponent::Red,
            "test.split");
    tests.Check(
        !unknownResult.executable &&
            HasRule(
                unknownResult.diagnostics,
                "nmr.semantic.image-component-set-invalid"),
        "unknown Image component presence cannot be split speculatively");
}

void CombineTests(Tests& tests) {
    const ValueDescriptor red = Channel("red");
    const ValueDescriptor blue = Channel("blue");
    const std::array<const ValueDescriptor*, 4> redBlue = {
        &red, nullptr, &blue, nullptr
    };
    const ChannelImageDescription combined =
        DescribeImageCombine(redBlue, "test.combine");
    const ImageComponentSet expected = MakeImageComponentSet({
        ImageComponent::Red,
        ImageComponent::Blue
    });
    tests.Check(
        combined.executable &&
            combined.diagnostics.empty() &&
            combined.descriptor.presentImageComponents.state ==
                KnowledgeState::Known &&
            combined.descriptor.presentImageComponents.value == expected &&
            combined.descriptor.alpha.state == KnowledgeState::Known &&
            combined.descriptor.alpha.value == AlphaMode::Absent &&
            combined.descriptor.spatial == red.spatial &&
            ValidateDescriptor(combined.descriptor).empty(),
        "Image Combine derives exact connected presence and does not fill missing channels semantically");

    const ValueDescriptor alpha = Channel("alpha");
    const std::array<const ValueDescriptor*, 4> redAlpha = {
        &red, nullptr, nullptr, &alpha
    };
    const ChannelImageDescription withAlpha =
        DescribeImageCombine(redAlpha, "test.combine-alpha");
    tests.Check(
        withAlpha.executable &&
            withAlpha.descriptor.alpha.state == KnowledgeState::Unknown &&
            HasImageComponent(
                withAlpha.descriptor.presentImageComponents.value,
                ImageComponent::Alpha),
        "connected alpha is present while its association remains explicitly unknown");

    const std::array<const ValueDescriptor*, 4> alphaOnly = {
        nullptr, nullptr, nullptr, &alpha
    };
    const ChannelImageDescription noColor =
        DescribeImageCombine(alphaOnly, "test.combine-alpha-only");
    tests.Check(
        !noColor.executable &&
            HasRule(
                noColor.diagnostics,
                "nmr.semantic.image-combine-color-missing"),
        "alpha-only Image Combine is rejected as a usable color Image");

    const ValueDescriptor mismatched =
        Channel("green", TestSpatial(31, 20));
    const std::array<const ValueDescriptor*, 4> mismatchInputs = {
        &red, &mismatched, nullptr, nullptr
    };
    const ChannelImageDescription mismatch =
        DescribeImageCombine(mismatchInputs, "test.combine-mismatch");
    tests.Check(
        !mismatch.executable &&
            HasRule(
                mismatch.diagnostics,
                "nmr.semantic.image-combine-extent-mismatch"),
        "Image Combine rejects mismatched extents instead of silently stretching");

    ValueDescriptor unknownExtent = Channel("green");
    unknownExtent.spatial =
        SemanticField<SpatialDescriptor>::Unknown();
    const std::array<const ValueDescriptor*, 4> unknownInputs = {
        &red, &unknownExtent, nullptr, nullptr
    };
    const ChannelImageDescription unknown =
        DescribeImageCombine(unknownInputs, "test.combine-unknown");
    tests.Check(
        !unknown.executable &&
            HasRule(
                unknown.diagnostics,
                "nmr.semantic.image-combine-extent-unknown"),
        "Image Combine rejects unknown extents instead of guessing");

    ValueDescriptor cubicBlue =
        Channel("blue", TestSpatial(), TestSampling(ReconstructionFilter::Cubic));
    const std::array<const ValueDescriptor*, 4> mixedSampling = {
        &red, nullptr, &cubicBlue, nullptr
    };
    const ChannelImageDescription mixed =
        DescribeImageCombine(mixedSampling, "test.combine-sampling");
    tests.Check(
        mixed.executable &&
            mixed.descriptor.sampling.state == KnowledgeState::Unknown,
        "disagreeing Channel sampling is preserved as explicit Unknown");

    const ValueDescriptor image = PartialImage(0x07);
    const std::array<const ValueDescriptor*, 4> wrongType = {
        &image, nullptr, nullptr, nullptr
    };
    const ChannelImageDescription wrong =
        DescribeImageCombine(wrongType, "test.combine-type");
    tests.Check(
        !wrong.executable &&
            HasRule(wrong.diagnostics, "nmr.connection.type-mismatch"),
        "Image Combine rejects non-Channel inputs");
}

void OutputInspectionTests(Tests& tests) {
    const float sample = 0.25f;
    tests.Check(
        MapChannelForOutputInspection(
            sample,
            OutputChannelViewMode::Neutral) ==
            std::array<float, 4>{ sample, sample, sample, 1.0f },
        "neutral Output inspection replicates Channel to RGB with constant alpha");
    tests.Check(
        MapChannelForOutputInspection(
            sample,
            OutputChannelViewMode::Red) ==
            std::array<float, 4>{ sample, 0.0f, 0.0f, 1.0f } &&
        MapChannelForOutputInspection(
            sample,
            OutputChannelViewMode::Green) ==
            std::array<float, 4>{ 0.0f, sample, 0.0f, 1.0f } &&
        MapChannelForOutputInspection(
            sample,
            OutputChannelViewMode::Blue) ==
            std::array<float, 4>{ 0.0f, 0.0f, sample, 1.0f },
        "color Output inspection modes isolate the requested display component");

    OutputChannelViewMode parsed = OutputChannelViewMode::Blue;
    tests.Check(
        ParseOutputChannelViewMode("neutral", parsed) &&
            parsed == OutputChannelViewMode::Neutral &&
            !ParseOutputChannelViewMode("alpha", parsed),
        "Output inspection mode tokens are exact and reject unsupported alpha mode");

    const ValueDescriptor channel = Channel("red");
    const OutputInspectionPolicy channelPolicy =
        EvaluateOutputInspectionPolicy(channel);
    tests.Check(
        channelPolicy.executable &&
            channelPolicy.descriptor == channel &&
            channelPolicy.diagnostics.empty(),
        "Output inspection preserves the Channel descriptor exactly");

    const ValueDescriptor image = PartialImage(0x05);
    const OutputInspectionPolicy imagePolicy =
        EvaluateOutputInspectionPolicy(image);
    tests.Check(
        imagePolicy.executable &&
            imagePolicy.descriptor == image,
        "Output inspection preserves a partial Image descriptor exactly");

    const DirectOutputPolicy channelExport =
        EvaluateDirectPngOutputPolicy(channel);
    tests.Check(
        !channelExport.executable &&
            HasRule(
                channelExport.diagnostics,
                "nmr.output.channel-export-unsupported"),
        "Channel PNG export is hard-disabled with the explicit Image Combine repair");

    const ValueDescriptor mask =
        MakeUnknownDescriptor(LogicalValueType::Mask);
    const OutputInspectionPolicy maskPolicy =
        EvaluateOutputInspectionPolicy(mask);
    tests.Check(
        !maskPolicy.executable &&
            HasRule(maskPolicy.diagnostics, "nmr.output.type-mismatch"),
        "Output rejects values outside the exact Image-or-Channel union");
}

} // namespace

int main() {
    Tests tests;
    ComponentSetAndSerializationTests(tests);
    PreservationTests(tests);
    ExtractionTests(tests);
    CombineTests(tests);
    OutputInspectionTests(tests);
    return tests.Finish();
}
