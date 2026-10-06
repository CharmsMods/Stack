#include "NodeMath/DescriptorSerialization.h"
#include "NodeMath/SemanticSpine.h"
#include "NodeMath/PngMetadataWriter.h"
#include "NodeMath/SourceColorMetadata.h"
#include "NodeMath/TechnicalImageMath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace Stack::NodeMath;

class Tests {
public:
    void Check(bool condition, const std::string& name) {
        ++m_Total;
        if (condition) return;
        ++m_Failed;
        std::cerr << "FAIL: " << name << '\n';
    }
    int Finish() const {
        std::cout << "Stack Node Math Phase 2: " << (m_Total - m_Failed)
            << "/" << m_Total << " checks passed\n";
        return m_Failed == 0 ? 0 : 1;
    }
private:
    int m_Total = 0;
    int m_Failed = 0;
};

bool Near(float left, float right, float tolerance = 2.0e-5f) {
    return std::abs(left - right) <= tolerance;
}

void AppendBigEndian32(std::vector<unsigned char>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<unsigned char>((value >> 24) & 0xff));
    bytes.push_back(static_cast<unsigned char>((value >> 16) & 0xff));
    bytes.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
    bytes.push_back(static_cast<unsigned char>(value & 0xff));
}

void AppendPngChunk(
    std::vector<unsigned char>& bytes,
    const char type[5],
    const std::vector<unsigned char>& payload) {
    AppendBigEndian32(bytes, static_cast<std::uint32_t>(payload.size()));
    bytes.insert(bytes.end(), type, type + 4);
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    AppendBigEndian32(bytes, 0); // Parser tests bounds/meaning, not CRC validation.
}

std::vector<unsigned char> PngWithChunk(
    const char type[5],
    const std::vector<unsigned char>& payload) {
    std::vector<unsigned char> bytes = { 0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a };
    AppendPngChunk(bytes, type, payload);
    AppendPngChunk(bytes, "IEND", {});
    return bytes;
}

std::vector<unsigned char> MinimalPng() {
    std::vector<unsigned char> bytes = { 0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a };
    AppendPngChunk(bytes, "IHDR", {
        0,0,0,1, 0,0,0,1, 8,6,0,0,0
    });
    AppendPngChunk(bytes, "IEND", {});
    return bytes;
}

ValueDescriptor Tagged(
    const std::string& color,
    TransferKind transfer,
    AlphaMode alpha = AlphaMode::Straight) {
    SpatialDescriptor spatial;
    spatial.kind = SpatialExtentKind::Finite;
    spatial.fullWindow = { 0, 0, 16, 8 };
    spatial.dataWindow = spatial.fullWindow;
    SamplingDescriptor sampling;
    ValueDescriptor result = MakeTaggedColorImageDescriptor(
        color, {}, { transfer, 0.0, {} }, ReferenceState::Display,
        alpha, spatial, sampling, LogicalPrecision::Float32, "test-source");
    result.range = SemanticField<NumericRange>::Known({
        0.0, 1.0, true, true, NonFinitePolicy::Preserve
    });
    return result;
}

bool HasRule(const std::vector<Diagnostic>& diagnostics, const std::string& rule) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
        [&](const Diagnostic& diagnostic) { return diagnostic.ruleId == rule; });
}

void DescriptorAndSourceTests(Tests& tests) {
    const ValueDescriptor descriptor = Tagged("srgb-d65", TransferKind::Srgb);
    const nlohmann::json encoded = SerializeValueDescriptor(descriptor);
    const DescriptorParseResult decoded = ParseValueDescriptor(encoded);
    tests.Check(decoded.descriptor && *decoded.descriptor == descriptor,
        "descriptor JSON round trip is exact");
    tests.Check(DescriptorContentIdentity(descriptor) == DescriptorContentIdentity(descriptor),
        "descriptor content identity is stable");
    nlohmann::json malformed = encoded;
    malformed["channels"]["value"]["roles"] = nlohmann::json::array({ 3 });
    tests.Check(!ParseValueDescriptor(malformed).descriptor,
        "malformed channel roles are rejected without throwing");

    const SourceColorMetadata p3 = InspectSourceColorMetadata(
        PngWithChunk("cICP", { 12, 13, 0, 1 }), 64, 32, 4,
        LogicalPrecision::UInt8, "p3.png");
    tests.Check(p3.descriptor.color.state == KnowledgeState::Known &&
        p3.descriptor.color.value.identity == "display-p3-d65" &&
        p3.descriptor.transfer.value.kind == TransferKind::Srgb,
        "PNG Display-P3 cICP is descriptive and known");
    tests.Check(p3.retainedPayload == std::vector<unsigned char>({ 12, 13, 0, 1 }) &&
        IsValidContentHash(p3.dependencyIdentity),
        "PNG cICP bytes and dependency identity are retained");

    const SourceColorMetadata untagged = InspectSourceColorMetadata(
        { 0xff, 0xd8, 0xff, 0xd9 }, 10, 10, 3,
        LogicalPrecision::UInt8, "untagged.jpg");
    tests.Check(untagged.descriptor.color.state == KnowledgeState::Unknown &&
        untagged.descriptor.transfer.state == KnowledgeState::Unknown &&
        untagged.descriptor.provenance.value.kind == ProvenanceKind::Untagged,
        "untagged sources remain visibly Unknown");

    const std::vector<unsigned char> iccp = { 'T','e','s','t',0,0,1,2,3,4 };
    const SourceColorMetadata embedded = InspectSourceColorMetadata(
        PngWithChunk("iCCP", iccp), 4, 4, 4,
        LogicalPrecision::UInt8, "icc.png");
    tests.Check(embedded.payloadKind == EmbeddedColorPayloadKind::PngIccpChunk &&
        embedded.label == "Test" && embedded.retainedPayload == iccp,
        "PNG iCCP payload is retained without pixel conversion");
    const nlohmann::json sourceJson = SerializeSourceColorMetadata(embedded);
    SourceColorMetadata parsed;
    std::vector<ContractIssue> issues;
    tests.Check(ParseSourceColorMetadata(sourceJson, parsed, issues) &&
        parsed.retainedPayload == embedded.retainedPayload &&
        parsed.descriptor == embedded.descriptor,
        "source color metadata serialization retains descriptor and bytes");

    std::vector<ContractIssue> pngIssues;
    const std::vector<unsigned char> p3Png = InsertPngColorMetadataChunks(
        MinimalPng(), { false, true, {} }, pngIssues);
    const SourceColorMetadata p3Output = InspectSourceColorMetadata(
        p3Png, 1, 1, 4, LogicalPrecision::UInt8, "output.png");
    tests.Check(pngIssues.empty() &&
        p3Output.descriptor.color.state == KnowledgeState::Known &&
        p3Output.descriptor.color.value.identity == "display-p3-d65",
        "PNG output writer emits a valid Display-P3 cICP signal");
    pngIssues.clear();
    const std::vector<unsigned char> srgbPng = InsertPngColorMetadataChunks(
        MinimalPng(), { true, false, {} }, pngIssues);
    const SourceColorMetadata srgbOutput = InspectSourceColorMetadata(
        srgbPng, 1, 1, 4, LogicalPrecision::UInt8, "output.png");
    tests.Check(pngIssues.empty() &&
        srgbOutput.descriptor.color.state == KnowledgeState::Known &&
        srgbOutput.descriptor.color.value.identity == "srgb-d65",
        "PNG output writer emits an sRGB chunk without changing pixels");
    pngIssues.clear();
    tests.Check(InsertPngColorMetadataChunks(
        MinimalPng(), { true, true, {} }, pngIssues).empty() && !pngIssues.empty(),
        "PNG output writer rejects conflicting hidden metadata choices");
}

void MathTests(Tests& tests) {
    for (float value : { -0.4f, -0.02f, 0.0f, 0.02f, 0.18f, 1.0f, 1.4f }) {
        tests.Check(Near(DecodeSrgbExtended(EncodeSrgbExtended(value)), value, 4.0e-5f),
            "extended signed sRGB encode/decode round trip");
    }
    const Rgba32f pixel = { -0.1f, 0.4f, 1.3f, 0.25f };
    const Rgba32f p3 = ApplyTechnicalImageOperation(
        TechnicalImageOperation::LinearSrgbToDisplayP3, pixel);
    const Rgba32f restored = ApplyTechnicalImageOperation(
        TechnicalImageOperation::LinearDisplayP3ToSrgb, p3);
    tests.Check(Near(restored[0], pixel[0], 3.0e-4f) &&
        Near(restored[1], pixel[1], 3.0e-4f) &&
        Near(restored[2], pixel[2], 3.0e-4f) && restored[3] == pixel[3],
        "linear sRGB and Display-P3 matrices preserve extended values and alpha");
    const Rgba32f exposed = ApplyTechnicalImageOperation(
        TechnicalImageOperation::Exposure, pixel, 2.0f);
    tests.Check(Near(exposed[0], -0.4f) && Near(exposed[1], 1.6f) &&
        Near(exposed[2], 5.2f) && exposed[3] == pixel[3],
        "Exposure is ordered RGB multiplication and preserves alpha");
    const Rgba32f premult = ApplyTechnicalImageOperation(
        TechnicalImageOperation::Premultiply, pixel);
    const Rgba32f straight = ApplyTechnicalImageOperation(
        TechnicalImageOperation::Unpremultiply, premult);
    tests.Check(Near(straight[0], pixel[0]) && Near(straight[1], pixel[1]) &&
        Near(straight[2], pixel[2]) && straight[3] == pixel[3],
        "premultiply and guarded unpremultiply round trip");
    const Rgba32f zeroAlpha = ApplyTechnicalImageOperation(
        TechnicalImageOperation::Unpremultiply, { 7.0f, -3.0f, 2.0f, 0.0f });
    tests.Check(zeroAlpha == Rgba32f({ 0.0f, 0.0f, 0.0f, 0.0f }),
        "unpremultiply alpha at epsilon becomes transparent black");

    const Rgba32f source = { 1.0f, 0.0f, 0.0f, 0.5f };
    const Rgba32f backdrop = { 0.0f, 0.0f, 1.0f, 0.5f };
    const Rgba32f over = CompositeSourceOver(
        CompositeAlphaFormula::StraightSourceOver, source, backdrop);
    tests.Check(Near(over[0], 2.0f / 3.0f) && Near(over[2], 1.0f / 3.0f) &&
        Near(over[3], 0.75f), "straight Source Over follows the declared equation");
    const Rgba32f premultOver = CompositeSourceOver(
        CompositeAlphaFormula::PremultipliedSourceOver,
        { 0.5f, 0.0f, 0.0f, 0.5f }, { 0.0f, 0.0f, 0.5f, 0.5f });
    tests.Check(Near(premultOver[0], 0.5f) && Near(premultOver[2], 0.25f) &&
        Near(premultOver[3], 0.75f), "premultiplied Source Over follows the declared equation");
}

void SemanticTests(Tests& tests) {
    const ValueDescriptor encoded = Tagged("srgb-d65", TransferKind::Srgb);
    const std::vector<SemanticImageNode> nodes = {
        { "source", SemanticImageNodeKind::Source, encoded },
        { "exposure", SemanticImageNodeKind::TechnicalOperation, {}, TechnicalImageOperation::Exposure, 1.0f },
        { "output", SemanticImageNodeKind::DirectOutput }
    };
    const std::vector<SemanticImageEdge> edges = {
        { "edge-1", "source", "exposure", "image" },
        { "edge-2", "exposure", "output", "image" }
    };
    const SemanticAnalysisResult first = AnalyzeSemanticImageGraph(nodes, edges);
    const SemanticAnalysisResult second = AnalyzeSemanticImageGraph(nodes, edges);
    tests.Check(first.executable && HasRule(first.diagnostics, "nmr.semantic.transfer-nonlinear"),
        "encoded Exposure executes and warns instead of constraining the graph");
    tests.Check(first.semanticFingerprint == second.semanticFingerprint &&
        first.edges.size() == 2 && first.edges[1].descriptorIdentity ==
            FindSemanticNodeOutput(first, "exposure")->descriptorIdentity,
        "semantic analysis and edge descriptor fingerprints are deterministic");

    const ValueDescriptor unknown = MakeUntaggedColorImageDescriptor(
        AlphaMode::Opaque, encoded.spatial.value, encoded.sampling.value,
        LogicalPrecision::UInt8, "unknown");
    const SemanticAnalysisResult unknownResult = AnalyzeSemanticImageGraph({
        { "unknown-source", SemanticImageNodeKind::Source, unknown },
        { "unknown-output", SemanticImageNodeKind::DirectOutput }
    }, { { "unknown-edge", "unknown-source", "unknown-output", "image" } });
    tests.Check(unknownResult.executable &&
        HasRule(unknownResult.diagnostics, "nmr.semantic.source-color-unknown") &&
        !HasRule(unknownResult.diagnostics, "nmr.output.color-unknown"),
        "Unknown source is visible while viewport Output remains independent of file-export policy");

    const ValueDescriptor straight = Tagged("srgb-d65", TransferKind::Linear, AlphaMode::Straight);
    const ValueDescriptor premult = Tagged("srgb-d65", TransferKind::Linear, AlphaMode::Premultiplied);
    const SemanticAnalysisResult mismatch = AnalyzeSemanticImageGraph({
        { "a", SemanticImageNodeKind::Source, straight },
        { "b", SemanticImageNodeKind::Source, premult },
        { "over", SemanticImageNodeKind::StraightSourceOver }
    }, {
        { "a-over", "a", "over", "source" },
        { "b-over", "b", "over", "backdrop" }
    });
    tests.Check(mismatch.executable &&
        HasRule(mismatch.diagnostics, "nmr.semantic.alpha-formula-mismatch"),
        "explicit alpha formula mismatch warns while remaining executable");

    SemanticImageNode viewTransform;
    viewTransform.identity = "view-transform";
    viewTransform.kind = SemanticImageNodeKind::DeclaredColorOutput;
    viewTransform.declaredColor = { "srgb-d65", {}, ColorRelation::Standard };
    viewTransform.declaredTransfer = { TransferKind::Srgb, 0.0, {} };
    viewTransform.declaredReference = ReferenceState::Display;
    viewTransform.declaredOperationIdentity = "view-transform.display-output.v1";
    const SemanticAnalysisResult viewResult = AnalyzeSemanticImageGraph({
        { "view-source", SemanticImageNodeKind::Source, straight },
        viewTransform,
        { "view-output", SemanticImageNodeKind::DirectOutput }
    }, {
        { "view-input", "view-source", "view-transform", "image" },
        { "view-export", "view-transform", "view-output", "image" }
    });
    const SemanticNodeOutput* viewOutput =
        FindSemanticNodeOutput(viewResult, "view-transform");
    tests.Check(
        viewResult.executable && viewOutput != nullptr &&
        viewOutput->descriptor.color.state == KnowledgeState::Known &&
        viewOutput->descriptor.color.value.identity == "srgb-d65" &&
        viewOutput->descriptor.transfer.state == KnowledgeState::Known &&
        viewOutput->descriptor.transfer.value.kind == TransferKind::Srgb &&
        viewOutput->descriptor.reference.state == KnowledgeState::Known &&
        viewOutput->descriptor.reference.value == ReferenceState::Display &&
        !HasRule(viewResult.diagnostics, "nmr.output.color-unknown"),
        "declared View Transform output propagates explicit display color and transfer");

    const DirectOutputPolicy p3Policy = EvaluateDirectPngOutputPolicy(
        Tagged("display-p3-d65", TransferKind::Srgb));
    tests.Check(p3Policy.executable && p3Policy.writeDisplayP3Cicp &&
        !p3Policy.writeSrgbChunk,
        "direct P3 PNG output policy requests cICP without a hidden transform");
    const DirectOutputPolicy premultPolicy = EvaluateDirectPngOutputPolicy(premult);
    tests.Check(!premultPolicy.executable &&
        HasRule(premultPolicy.diagnostics, "nmr.output.png-premultiplied"),
        "premultiplied PNG output requires an explicit graph conversion");
}

} // namespace

int main() {
    Tests tests;
    DescriptorAndSourceTests(tests);
    MathTests(tests);
    SemanticTests(tests);
    return tests.Finish();
}
