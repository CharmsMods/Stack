#include "NodeMath/ContractTypes.h"
#include "NodeMath/DescriptorPropagation.h"
#include "NodeMath/NodeDefinition.h"
#include "NodeMath/ProjectSchema.h"

#include <algorithm>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace Stack::NodeMath;

class TestContext {
public:
    void Check(bool condition, const std::string& message) {
        ++m_Checks;
        if (!condition) {
            ++m_Failures;
            std::cerr << "FAIL: " << message << "\n";
        }
    }

    int Checks() const { return m_Checks; }
    int Failures() const { return m_Failures; }

private:
    int m_Checks = 0;
    int m_Failures = 0;
};

SpatialDescriptor TestSpatial(std::int64_t width = 16, std::int64_t height = 12) {
    return {
        SpatialExtentKind::Finite,
        { 0, 0, width, height },
        { 0, 0, width, height },
        RasterOrigin::BottomLeft,
        1.0
    };
}

SamplingDescriptor TestSampling() {
    return {
        CoordinateConvention::PixelCenters,
        ReconstructionFilter::Linear,
        BorderPolicy::Transparent,
        {}
    };
}

ValueDescriptor TaggedImage(
    const std::string& identity = "profile:test-linear",
    TransferKind transferKind = TransferKind::Linear,
    AlphaMode alpha = AlphaMode::Straight) {
    ValueDescriptor descriptor = MakeTaggedColorImageDescriptor(
        identity,
        "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        { transferKind, transferKind == TransferKind::Gamma ? 2.2 : 0.0, {} },
        ReferenceState::Scene,
        alpha,
        TestSpatial(),
        TestSampling(),
        LogicalPrecision::Float32,
        "fixture:tagged-source");
    descriptor.range = SemanticField<NumericRange>::Known({
        0.0, 1.0, false, true, NonFinitePolicy::Preserve });
    return descriptor;
}

bool HasDiagnostic(
    const PropagationResult& result,
    const std::string& ruleId,
    DiagnosticSeverity severity) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [&](const Diagnostic& diagnostic) {
            return diagnostic.ruleId == ruleId && diagnostic.severity == severity;
        });
}

const NodeDefinition* FindDefinition(
    const std::vector<NodeDefinition>& definitions,
    const std::string& id) {
    const auto found = std::find_if(definitions.begin(), definitions.end(),
        [&](const NodeDefinition& definition) { return definition.identity.id == id; });
    return found == definitions.end() ? nullptr : &(*found);
}

void TestIdentityGrammar(TestContext& test) {
    test.Check(IsValidDefinitionId("stack:image/identity"), "valid namespaced definition ID");
    test.Check(IsValidDefinitionId("vendor.name:color/transform-v2"), "valid multi-segment definition ID");
    test.Check(!IsValidDefinitionId("Stack:image/identity"), "uppercase definition ID rejected");
    test.Check(!IsValidDefinitionId("stack:image//identity"), "empty definition path segment rejected");
    test.Check(!IsValidDefinitionId("stack:image/-identity"), "punctuation-led path segment rejected");
    test.Check(!IsValidDefinitionId("identity"), "unnamespaced definition ID rejected");

    test.Check(IsValidScopedId("input.image-1"), "valid scoped ID");
    test.Check(!IsValidScopedId("1input"), "scoped ID must start with a letter");
    test.Check(!IsValidScopedId("Input"), "uppercase scoped ID rejected");

    const std::string validHash =
        "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    test.Check(IsValidContentHash(validHash), "valid exact SHA-256 identity");
    test.Check(!IsValidContentHash("sha256:ABCDEF"), "short/uppercase hash rejected");
    test.Check(Sha256ContentIdentity("abc") ==
        "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 content identity matches the standard abc vector");

    const auto version = ParseSemanticVersion("12.3.45");
    test.Check(version.has_value() && ToString(*version) == "12.3.45", "semantic version round trip");
    test.Check(!ParseSemanticVersion("01.2.3"), "semantic version leading zero rejected");
    test.Check(!ParseSemanticVersion("1.2"), "incomplete semantic version rejected");
    test.Check(!ParseSemanticVersion("1.2.3-beta"), "prerelease syntax outside v1 rejected");

    test.Check(IsValidCanonicalUuid("123e4567-e89b-12d3-a456-426614174000"), "canonical lowercase UUID accepted");
    test.Check(!IsValidCanonicalUuid("123E4567-e89b-12d3-a456-426614174000"), "uppercase UUID rejected");
    test.Check(!IsValidCanonicalUuid("123e4567e89b12d3a456426614174000"), "unhyphenated UUID rejected");
}

void TestDescriptorContract(TestContext& test) {
    const std::vector<LogicalValueType> validTypes = {
        LogicalValueType::Boolean,
        LogicalValueType::Integer,
        LogicalValueType::Scalar,
        LogicalValueType::Vector2,
        LogicalValueType::Vector3,
        LogicalValueType::Vector4,
        LogicalValueType::Matrix3,
        LogicalValueType::Matrix4,
        LogicalValueType::Curve1D,
        LogicalValueType::Lut,
        LogicalValueType::ColorImage,
        LogicalValueType::Mask,
        LogicalValueType::Channel,
        LogicalValueType::DataImage,
        LogicalValueType::ComplexSpectrum,
        LogicalValueType::FrequencyResponse,
        LogicalValueType::SpectrumMagnitude,
        LogicalValueType::SpectrumPhase,
        LogicalValueType::Histogram,
        LogicalValueType::Statistics,
        LogicalValueType::Metadata,
        LogicalValueType::Raw,
        LogicalValueType::Analysis,
        LogicalValueType::Failure
    };
    for (LogicalValueType type : validTypes) {
        const ValueDescriptor descriptor = MakeUnknownDescriptor(type);
        test.Check(ValidateDescriptor(descriptor).empty(), "unknown descriptor is representable for every v1 type");
    }
    test.Check(!ValidateDescriptor(MakeUnknownDescriptor(LogicalValueType::Invalid)).empty(),
        "Invalid logical type is rejected");

    const ValueDescriptor tagged = TaggedImage();
    test.Check(ValidateDescriptor(tagged).empty(), "tagged color image descriptor validates");
    test.Check(tagged.color.state == KnowledgeState::Known &&
        tagged.provenance.state == KnowledgeState::Known &&
        tagged.provenance.value.kind == ProvenanceKind::Embedded,
        "embedded profile is descriptive known metadata");

    const ValueDescriptor untagged = MakeUntaggedColorImageDescriptor(
        AlphaMode::Straight, TestSpatial(), TestSampling(),
        LogicalPrecision::UInt8, "fixture:untagged-source");
    test.Check(ValidateDescriptor(untagged).empty(), "untagged color image descriptor validates");
    test.Check(untagged.color.state == KnowledgeState::Unknown &&
        untagged.transfer.state == KnowledgeState::Unknown &&
        untagged.reference.state == KnowledgeState::Unknown &&
        untagged.provenance.state == KnowledgeState::Known &&
        untagged.provenance.value.kind == ProvenanceKind::Untagged,
        "untagged input remains explicitly Unknown rather than assumed sRGB");
    test.Check(!StrictSemanticDescriptorMatch(untagged, untagged),
        "Unknown does not count as a strict semantic match, even against Unknown");
    test.Check(StrictSemanticDescriptorMatch(tagged, tagged),
        "fully known equivalent descriptors strictly match");

    ValueDescriptor invalidMask = MakeUnknownDescriptor(LogicalValueType::Mask);
    invalidMask.color = SemanticField<ColorIdentity>::Unknown();
    test.Check(!ValidateDescriptor(invalidMask).empty(), "mask cannot silently acquire color meaning");

    ValueDescriptor invalidRange = tagged;
    invalidRange.range = SemanticField<NumericRange>::Known({
        2.0, -1.0, false, false, NonFinitePolicy::Forbidden });
    test.Check(!ValidateDescriptor(invalidRange).empty(), "reversed nominal range rejected");

    ValueDescriptor invalidSpatial = tagged;
    SpatialDescriptor outside = TestSpatial();
    outside.dataWindow = { -1, 0, 16, 12 };
    invalidSpatial.spatial = SemanticField<SpatialDescriptor>::Known(outside);
    test.Check(!ValidateDescriptor(invalidSpatial).empty(), "data window outside full window rejected");
}

void TestDiagnostics(TestContext& test) {
    const std::vector<DiagnosticRule>& rules = BuiltInDiagnosticRules();
    test.Check(rules.size() >= 16, "v1 registry covers all four diagnostic stages");
    test.Check(ValidateDiagnosticRules(rules).empty(), "built-in diagnostic rules are valid and unique");
    std::set<DiagnosticStage> stages;
    for (const DiagnosticRule& rule : rules) {
        stages.insert(rule.stage);
    }
    test.Check(stages.size() == 4, "diagnostic registry includes connection, semantic, lowering, and runtime");
    test.Check(IsDiagnosticAcknowledgementAllowed(DiagnosticSeverity::Warning), "warnings can be acknowledged");
    test.Check(IsDiagnosticAcknowledgementAllowed(DiagnosticSeverity::Information), "information can be acknowledged");
    test.Check(!IsDiagnosticAcknowledgementAllowed(DiagnosticSeverity::HardError), "hard errors cannot be acknowledged away");
    test.Check(!IsDiagnosticAcknowledgementAllowed(DiagnosticSeverity::RuntimeFault), "runtime faults cannot be acknowledged away");

    std::vector<DiagnosticRule> duplicate = rules;
    duplicate.push_back(rules.front());
    test.Check(!ValidateDiagnosticRules(duplicate).empty(), "duplicate diagnostic rule rejected");
}

void TestDefinitions(TestContext& test) {
    const std::vector<NodeDefinition> definitions = BuildRepresentativeDefinitions();
    test.Check(definitions.size() >= 15, "representative catalog covers all current major paths");
    std::set<std::string> identities;
    std::set<std::string> implementationHashes;
    std::set<DescriptorPropagationKind> propagationKinds;
    for (const NodeDefinition& definition : definitions) {
        const std::vector<ContractIssue> issues = ValidateNodeDefinition(definition);
        if (!issues.empty()) {
            std::cerr << "Definition issue in " << definition.identity.id << ": "
                      << issues.front().field << " " << issues.front().message << "\n";
        }
        test.Check(issues.empty(), "representative definition validates: " + definition.identity.id);
        test.Check(identities.insert(definition.identity.id).second,
            "representative definition identity is unique");
        propagationKinds.insert(definition.propagation);
        implementationHashes.insert(definition.implementations.front().contentHash);
    }
    test.Check(propagationKinds.size() == 13, "every v1 propagation family has a representative definition");
    test.Check(implementationHashes.size() == definitions.size(),
        "representative implementation identities include their definition binding");

    const char* requiredIds[] = {
        "stack:image/source", "stack:raw/source", "stack:raw/develop",
        "stack:image/identity", "stack:math/multiply", "stack:color/exposure",
        "stack:mask/luminance", "stack:geometry/resample", "stack:color/transform",
        "stack:composite/source-over", "stack:analysis/mean", "stack:output/direct",
        "stack:frequency/fft", "stack:image/merge-many", "stack:external/model-process"
    };
    for (const char* id : requiredIds) {
        test.Check(FindDefinition(definitions, id) != nullptr,
            std::string("major-path definition exists: ") + id);
    }

    NodeDefinition invalid = definitions.front();
    invalid.policies.operationOrder.clear();
    invalid.identity.contentHash = "bad";
    test.Check(!ValidateNodeDefinition(invalid).empty(), "definition with unspecified order and bad hash rejected");

    NodeDefinition changedWithoutRehash = definitions[3];
    changedWithoutRehash.semanticDescription += " changed";
    test.Check(!ValidateNodeDefinition(changedWithoutRehash).empty(),
        "semantic definition change invalidates its exact content identity");
    RefreshNodeDefinitionContentHash(changedWithoutRehash);
    test.Check(ValidateNodeDefinition(changedWithoutRehash).empty(),
        "canonical rehash establishes a new exact content identity");

    NodeDefinition changedImplementation = definitions[3];
    changedImplementation.implementations.front().absoluteTolerance = 0.01;
    RefreshNodeDefinitionContentHash(changedImplementation);
    test.Check(!ValidateNodeDefinition(changedImplementation).empty(),
        "implementation contract change invalidates its own exact identity");
    RefreshImplementationContentHash(changedImplementation.implementations.front());
    RefreshNodeDefinitionContentHash(changedImplementation);
    test.Check(ValidateNodeDefinition(changedImplementation).empty(),
        "deliberately rehashed implementation and definition validate together");

    const NodeDefinition* external = FindDefinition(definitions, "stack:external/model-process");
    test.Check(external != nullptr && external->externalFieldPolicy.size() == 12,
        "external definition declares every descriptor field");
    if (external) {
        NodeDefinition incomplete = *external;
        incomplete.externalFieldPolicy.pop_back();
        test.Check(!ValidateNodeDefinition(incomplete).empty(), "incomplete external field policy rejected");
    }

    NodeInstance instance;
    instance.instanceUuid = "123e4567-e89b-12d3-a456-426614174000";
    instance.definition = definitions.front().identity;
    test.Check(ValidateNodeInstance(instance).empty(), "instance pins exact reference and canonical UUID");
    Connection connection = {
        "123e4567-e89b-12d3-a456-426614174000", "image",
        "123e4567-e89b-12d3-a456-426614174001", "image"
    };
    test.Check(ValidateConnection(connection).empty(), "connection saves UUID and stable port IDs");
}

void TestPropagation(TestContext& test) {
    const ValueDescriptor tagged = TaggedImage();
    const PropagationResult identity = PropagateIdentity(tagged);
    test.Check(identity.executable && identity.descriptor == tagged,
        "identity preserves every descriptor field exactly");

    const PropagationResult arithmetic = PropagateGenericArithmetic(tagged, "math.multiply");
    test.Check(arithmetic.executable &&
        arithmetic.descriptor.spatial == tagged.spatial &&
        arithmetic.descriptor.alpha == tagged.alpha &&
        arithmetic.descriptor.color.value.relation == ColorRelation::Derived,
        "generic arithmetic preserves structure and marks color relation derived");

    const PropagationResult linearExposure = PropagateExposure(tagged, 1.0, "color.exposure-ev");
    test.Check(linearExposure.executable && linearExposure.diagnostics.empty(),
        "linear exposure propagates without warning");
    test.Check(linearExposure.descriptor.range.value.nominalMaximum == 2.0,
        "exposure derives known range without clamping");

    const PropagationResult nonlinearExposure = PropagateExposure(
        TaggedImage("profile:test-srgb", TransferKind::Srgb), 0.5, "color.exposure-ev");
    test.Check(nonlinearExposure.executable && HasDiagnostic(nonlinearExposure,
        "nmr.semantic.transfer-nonlinear", DiagnosticSeverity::Warning),
        "known nonlinear exposure warns but remains executable");
    test.Check(!nonlinearExposure.diagnostics.empty() &&
        IsValidContentHash(nonlinearExposure.diagnostics.front().semanticFingerprint),
        "diagnostic acknowledgement fingerprint is a stable content identity");

    const ValueDescriptor untagged = MakeUntaggedColorImageDescriptor(
        AlphaMode::Straight, TestSpatial(), TestSampling(),
        LogicalPrecision::UInt8, "fixture:untagged");
    const PropagationResult unknownExposure = PropagateExposure(untagged, 0.0, "color.exposure-ev");
    test.Check(unknownExposure.executable && HasDiagnostic(unknownExposure,
        "nmr.semantic.transfer-unknown", DiagnosticSeverity::Warning),
        "unknown-transfer exposure warns without restricting plug-and-play numeric work");

    const PropagationResult mask = PropagateMask(tagged, "mask.luminance");
    test.Check(mask.executable && mask.descriptor.logicalType == LogicalValueType::Mask &&
        mask.descriptor.color.state == KnowledgeState::NotApplicable &&
        mask.descriptor.alpha.state == KnowledgeState::NotApplicable &&
        mask.descriptor.range.state == KnowledgeState::Known,
        "mask propagation removes color/alpha meaning and declares mask range");

    SpatialDescriptor resized = TestSpatial(32, 24);
    SamplingDescriptor cubic = TestSampling();
    cubic.filter = ReconstructionFilter::Cubic;
    const PropagationResult geometry = PropagateGeometry(tagged, resized, cubic, "geometry.resample");
    test.Check(geometry.executable && geometry.descriptor.spatial.value == resized &&
        geometry.descriptor.sampling.value == cubic && geometry.descriptor.color == tagged.color,
        "geometry replaces spatial/sampling state and preserves color meaning");

    const ColorIdentity destination = { "profile:display-p3-linear", {}, ColorRelation::Standard };
    const TransferDescriptor destinationTransfer = { TransferKind::Linear, 0.0, {} };
    const PropagationResult converted = PropagateColorTransform(
        tagged, destination, destinationTransfer, ReferenceState::Display, "color.transform");
    test.Check(converted.executable && converted.descriptor.color.value == destination &&
        converted.descriptor.reference.value == ReferenceState::Display,
        "explicit color transform produces declared destination state");
    const PropagationResult unknownConversion = PropagateColorTransform(
        untagged, destination, destinationTransfer, ReferenceState::Display, "color.transform");
    test.Check(!unknownConversion.executable && HasDiagnostic(unknownConversion,
        "nmr.semantic.metadata-missing", DiagnosticSeverity::HardError),
        "unknown execution-critical source identity blocks only explicit conversion");

    const ValueDescriptor otherColor = TaggedImage("profile:other-linear");
    const PropagationResult composite = PropagateComposite(
        tagged, otherColor, true, TestSpatial(), AlphaMode::Straight,
        AlphaMode::Straight, "composite.source-over");
    test.Check(composite.executable && HasDiagnostic(composite,
        "nmr.semantic.color-mismatch", DiagnosticSeverity::Warning),
        "declared numeric composite warns on color mismatch but remains executable");
    const PropagationResult badAlpha = PropagateComposite(
        tagged, TaggedImage("profile:test-linear", TransferKind::Linear, AlphaMode::Premultiplied),
        true, TestSpatial(), AlphaMode::Straight, AlphaMode::Straight,
        "composite.source-over");
    test.Check(!badAlpha.executable && HasDiagnostic(badAlpha,
        "nmr.semantic.alpha-mismatch", DiagnosticSeverity::HardError),
        "formula-incompatible alpha is a hard error rather than silently converted");
    const PropagationResult noExtent = PropagateComposite(
        tagged, tagged, false, TestSpatial(), AlphaMode::Straight,
        AlphaMode::Straight, "composite.source-over");
    test.Check(!noExtent.executable && HasDiagnostic(noExtent,
        "nmr.semantic.extent-policy-missing", DiagnosticSeverity::HardError),
        "multi-input extent policy must be explicit");

    const PropagationResult reduction = PropagateReduction(
        tagged, LogicalValueType::Vector3, { UnitKind::Unitless, {} }, "analysis.mean");
    test.Check(reduction.executable && reduction.descriptor.logicalType == LogicalValueType::Vector3 &&
        reduction.descriptor.spatial.state == KnowledgeState::NotApplicable &&
        reduction.descriptor.units.state == KnowledgeState::Known,
        "reduction produces a first-class typed nonimage value");

    const PropagationResult direct = PropagateDirectOutput(tagged);
    test.Check(direct.executable && direct.descriptor == tagged,
        "direct output preserves the graph result without a hidden preview transform");

    const std::vector<NodeDefinition> definitions = BuildRepresentativeDefinitions();
    const NodeDefinition* externalDefinition = FindDefinition(definitions, "stack:external/model-process");
    test.Check(externalDefinition != nullptr, "external propagation fixture exists");
    if (externalDefinition) {
        ValueDescriptor declared = tagged;
        declared.color = SemanticField<ColorIdentity>::Unknown();
        declared.transfer = SemanticField<TransferDescriptor>::Unknown();
        declared.reference = SemanticField<ReferenceState>::Unknown();
        const PropagationResult external = PropagateUnknownExternal(
            tagged, declared, externalDefinition->externalFieldPolicy, "external.model-process");
        test.Check(external.executable && external.descriptor.provenance.state == KnowledgeState::Known &&
            external.descriptor.provenance.value.kind == ProvenanceKind::External,
            "complete external policy produces an explicit boundary descriptor");
        std::vector<FieldDispositionRule> incomplete = externalDefinition->externalFieldPolicy;
        incomplete.pop_back();
        const PropagationResult rejected = PropagateUnknownExternal(
            tagged, declared, incomplete, "external.model-process");
        test.Check(!rejected.executable && HasDiagnostic(rejected,
            "nmr.semantic.external-policy-missing", DiagnosticSeverity::HardError),
            "unknown external operation cannot omit a field disposition");
    }
}

void TestProjectEnvelope(TestContext& test) {
    const std::vector<NodeDefinition> definitions = BuildRepresentativeDefinitions();
    ProjectEnvelope envelope;
    envelope.projectUuid = "123e4567-e89b-12d3-a456-426614174000";
    envelope.definitionManifest = {
        { definitions[0].identity, DefinitionSourceKind::BuiltIn, {} },
        { definitions[3].identity, DefinitionSourceKind::Embedded, "definitions/identity.json" },
        { definitions[8].identity, DefinitionSourceKind::External, "ocio:studio-config" }
    };
    test.Check(ValidateProjectEnvelope(envelope).empty(), "forward project envelope validates");
    const nlohmann::json serialized = SerializeProjectEnvelope(envelope);
    const EnvelopeParseResult parsed = ParseProjectEnvelope(serialized);
    test.Check(parsed.status == EnvelopeParseStatus::Ready && parsed.envelope.has_value(),
        "forward project envelope JSON round trip parses");
    if (parsed.envelope) {
        test.Check(SerializeProjectEnvelope(*parsed.envelope) == serialized,
            "forward project envelope round trip is exact");
    }

    const nlohmann::json oldProject = { { "graphVersion", 3 }, { "nodes", nlohmann::json::array() } };
    test.Check(ParseProjectEnvelope(oldProject).status == EnvelopeParseStatus::UnsupportedPreRewrite,
        "pre-rewrite project is rejected explicitly without migration fallback");

    nlohmann::json future = serialized;
    future["generation"] = 2u;
    test.Check(ParseProjectEnvelope(future).status == EnvelopeParseStatus::UnsupportedGeneration,
        "unknown future generation is rejected explicitly");
    nlohmann::json malformed = serialized;
    malformed["projectUuid"] = 7;
    test.Check(ParseProjectEnvelope(malformed).status == EnvelopeParseStatus::Malformed,
        "malformed envelope is distinct from unsupported generation");

    std::vector<DefinitionReference> catalog;
    for (const NodeDefinition& definition : definitions) {
        catalog.push_back(definition.identity);
    }
    const DefinitionReference exact = definitions.front().identity;
    test.Check(ResolveExactDefinition(exact, catalog).status == DefinitionResolutionStatus::Exact,
        "exact ID/version/hash resolves");
    DefinitionReference missing = exact;
    missing.id = "vendor:missing/node";
    test.Check(ResolveExactDefinition(missing, catalog).status == DefinitionResolutionStatus::MissingId,
        "missing definition ID remains unresolved");
    DefinitionReference wrongVersion = exact;
    wrongVersion.version = { 2, 0, 0 };
    test.Check(ResolveExactDefinition(wrongVersion, catalog).status == DefinitionResolutionStatus::VersionMismatch,
        "version mismatch does not auto-upgrade");
    DefinitionReference wrongHash = exact;
    wrongHash.contentHash =
        "sha256:0000000000000000000000000000000000000000000000000000000000000000";
    test.Check(ResolveExactDefinition(wrongHash, catalog).status == DefinitionResolutionStatus::HashMismatch,
        "hash mismatch does not silently substitute an implementation");
}

} // namespace

int main() {
    TestContext test;
    TestIdentityGrammar(test);
    TestDescriptorContract(test);
    TestDiagnostics(test);
    TestDefinitions(test);
    TestPropagation(test);
    TestProjectEnvelope(test);

    if (test.Failures() != 0) {
        std::cerr << "FAIL: " << test.Failures() << " of " << test.Checks()
                  << " Phase 1 contract checks failed.\n";
        return 1;
    }
    std::cout << "PASS: " << test.Checks()
              << " Phase 1 semantic and node-definition contract checks.\n";
    return 0;
}
