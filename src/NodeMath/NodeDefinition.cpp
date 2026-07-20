#include "NodeMath/NodeDefinition.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <type_traits>

namespace Stack::NodeMath {

namespace {

DefinitionReference Reference(const char* id, const char* hash) {
    return { id, { 1, 0, 0 }, hash };
}

PortDefinition Input(
    const char* id,
    LogicalValueType type,
    RequirementPolicy requirement = RequirementPolicy::Agnostic,
    const char* requirementDescription = "numeric operation is descriptor-agnostic") {
    PortDefinition port;
    port.id = id;
    port.direction = PortDirection::Input;
    port.logicalType = type;
    port.minimumConnections = 1;
    port.maximumConnections = 1;
    port.semanticRequirement = requirement;
    port.requirementDescription = requirementDescription;
    return port;
}

PortDefinition Output(const char* id, LogicalValueType type) {
    PortDefinition port;
    port.id = id;
    port.direction = PortDirection::Output;
    port.logicalType = type;
    port.arity = PortArity::Variadic;
    port.minimumConnections = 0;
    port.maximumConnections = 0;
    port.optional = true;
    port.requirementDescription = "output descriptor is produced by the propagation rule";
    return port;
}

ImplementationContract Canonical(const char* definitionId) {
    ImplementationContract implementation = {
        "canonical.cpu",
        ImplementationKind::CanonicalCpu,
        definitionId,
        { 1, 0, 0 },
        {},
        "portable C++17 reference semantics",
        0.0,
        0.0,
        true,
        { "nmr.phase1.contract-validation" }
    };
    RefreshImplementationContentHash(implementation);
    return implementation;
}

SemanticPolicies Policies(
    RangeBehavior range,
    AlphaBehavior alpha,
    SpatialBehavior spatial,
    MetadataBehavior metadata,
    const char* order) {
    SemanticPolicies policies;
    policies.range = range;
    policies.alpha = alpha;
    policies.spatial = spatial;
    policies.metadata = metadata;
    policies.nonFinite = NonFinitePolicy::Preserve;
    policies.operationOrder = order;
    return policies;
}

NodeDefinition Base(
    const char* id,
    const char* technicalName,
    const char* operationId,
    const char* semantics,
    CapabilityClass capability,
    Inspectability inspectability,
    DescriptorPropagationKind propagation) {
    NodeDefinition definition;
    definition.identity = Reference(id, "");
    definition.technicalName = technicalName;
    definition.operationId = operationId;
    definition.semanticDescription = semantics;
    definition.capability = capability;
    definition.inspectability = inspectability;
    definition.propagation = propagation;
    definition.implementations = { Canonical(id) };
    definition.referenceTestIds = { "nmr.phase1.representative-definitions" };
    definition.knownLimits = "Phase 1 contract only; no live renderer binding.";
    definition.lifecycleNotes = "Exact identity remains pinned until an explicit migration.";
    return definition;
}

bool ParameterTypeMatches(ParameterType type, const ParameterValue& value) {
    switch (type) {
    case ParameterType::Boolean: return std::holds_alternative<bool>(value);
    case ParameterType::Integer: return std::holds_alternative<std::int64_t>(value);
    case ParameterType::Scalar: return std::holds_alternative<double>(value);
    case ParameterType::Vector2: return std::holds_alternative<std::array<double, 2>>(value);
    case ParameterType::Vector3: return std::holds_alternative<std::array<double, 3>>(value);
    case ParameterType::Vector4: return std::holds_alternative<std::array<double, 4>>(value);
    case ParameterType::Text:
    case ParameterType::ResourceReference:
        return std::holds_alternative<std::string>(value);
    }
    return false;
}

std::optional<double> NumericDefault(const ParameterDefinition& parameter) {
    if (const auto* integer = std::get_if<std::int64_t>(&parameter.defaultValue)) {
        return static_cast<double>(*integer);
    }
    if (const auto* scalar = std::get_if<double>(&parameter.defaultValue)) {
        return *scalar;
    }
    return std::nullopt;
}

std::vector<FieldDispositionRule> CompleteExternalPolicy() {
    return {
        { DescriptorField::Channels, FieldDisposition::Replace },
        { DescriptorField::Color, FieldDisposition::Invalidate },
        { DescriptorField::Transfer, FieldDisposition::Invalidate },
        { DescriptorField::Reference, FieldDisposition::Invalidate },
        { DescriptorField::Alpha, FieldDisposition::Replace },
        { DescriptorField::Range, FieldDisposition::Invalidate },
        { DescriptorField::Precision, FieldDisposition::Replace },
        { DescriptorField::Spatial, FieldDisposition::Preserve },
        { DescriptorField::Sampling, FieldDisposition::Preserve },
        { DescriptorField::Units, FieldDisposition::Drop },
        { DescriptorField::Provenance, FieldDisposition::Generate }
    };
}

void AppendText(std::string& output, const std::string& value) {
    output += std::to_string(value.size());
    output.push_back(':');
    output += value;
    output.push_back(';');
}

void AppendUnsigned(std::string& output, std::uint64_t value) {
    output += std::to_string(value);
    output.push_back(';');
}

void AppendSigned(std::string& output, std::int64_t value) {
    output += std::to_string(value);
    output.push_back(';');
}

void AppendBoolean(std::string& output, bool value) {
    output += value ? "1;" : "0;";
}

void AppendDouble(std::string& output, double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "double fingerprint requires 64-bit double");
    std::memcpy(&bits, &value, sizeof(bits));
    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << bits;
    output += stream.str();
    output.push_back(';');
}

template <typename T>
void AppendEnum(std::string& output, T value) {
    AppendUnsigned(output, static_cast<std::uint64_t>(value));
}

void AppendVersion(std::string& output, const SemanticVersion& version) {
    AppendUnsigned(output, version.major);
    AppendUnsigned(output, version.minor);
    AppendUnsigned(output, version.patch);
}

void AppendUnit(std::string& output, const SemanticField<UnitDescriptor>& unit) {
    AppendEnum(output, unit.state);
    if (unit.state == KnowledgeState::Known) {
        AppendEnum(output, unit.value.kind);
        AppendText(output, unit.value.customKey);
    }
}

void AppendParameterValue(std::string& output, const ParameterValue& value) {
    AppendUnsigned(output, value.index());
    std::visit([&](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, bool>) {
            AppendBoolean(output, item);
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
            AppendSigned(output, item);
        } else if constexpr (std::is_same_v<T, double>) {
            AppendDouble(output, item);
        } else if constexpr (std::is_same_v<T, std::string>) {
            AppendText(output, item);
        } else {
            for (double component : item) {
                AppendDouble(output, component);
            }
        }
    }, value);
}

} // namespace

bool operator==(const DefinitionReference& left, const DefinitionReference& right) {
    return left.id == right.id && left.version == right.version &&
        left.contentHash == right.contentHash;
}

bool operator!=(const DefinitionReference& left, const DefinitionReference& right) {
    return !(left == right);
}

std::string CanonicalImplementationContent(
    const ImplementationContract& implementation) {
    std::string content;
    content.reserve(512);
    AppendText(content, "stack.node-implementation.canonical.v1");
    AppendText(content, implementation.id);
    AppendEnum(content, implementation.kind);
    AppendText(content, implementation.definitionBinding);
    AppendVersion(content, implementation.version);
    AppendText(content, implementation.targetRequirement);
    AppendDouble(content, implementation.absoluteTolerance);
    AppendDouble(content, implementation.relativeTolerance);
    AppendBoolean(content, implementation.canonical);
    AppendUnsigned(content, implementation.evidenceIds.size());
    for (const std::string& evidence : implementation.evidenceIds) {
        AppendText(content, evidence);
    }
    return content;
}

std::string ComputeImplementationContentHash(
    const ImplementationContract& implementation) {
    return Sha256ContentIdentity(CanonicalImplementationContent(implementation));
}

void RefreshImplementationContentHash(ImplementationContract& implementation) {
    implementation.contentHash = ComputeImplementationContentHash(implementation);
}

std::string CanonicalNodeDefinitionContent(const NodeDefinition& definition) {
    std::string content;
    content.reserve(4096);
    AppendText(content, "stack.node-definition.canonical.v1");
    AppendText(content, definition.identity.id);
    AppendVersion(content, definition.identity.version);
    AppendText(content, definition.technicalName);
    AppendText(content, definition.operationId);
    AppendText(content, definition.semanticDescription);
    AppendEnum(content, definition.capability);
    AppendEnum(content, definition.inspectability);
    AppendEnum(content, definition.propagation);

    AppendUnsigned(content, definition.ports.size());
    for (const PortDefinition& port : definition.ports) {
        AppendText(content, port.id);
        AppendEnum(content, port.direction);
        AppendEnum(content, port.logicalType);
        AppendEnum(content, port.arity);
        AppendUnsigned(content, port.minimumConnections);
        AppendUnsigned(content, port.maximumConnections);
        AppendBoolean(content, port.optional);
        AppendEnum(content, port.broadcast);
        AppendUnit(content, port.units);
        AppendEnum(content, port.semanticRequirement);
        AppendText(content, port.requirementDescription);
    }

    AppendUnsigned(content, definition.parameters.size());
    for (const ParameterDefinition& parameter : definition.parameters) {
        AppendText(content, parameter.id);
        AppendEnum(content, parameter.type);
        AppendUnit(content, parameter.units);
        AppendParameterValue(content, parameter.defaultValue);
        AppendBoolean(content, parameter.hardDomain.applicable);
        AppendDouble(content, parameter.hardDomain.minimum);
        AppendDouble(content, parameter.hardDomain.maximum);
        AppendBoolean(content, parameter.hardDomain.minimumInclusive);
        AppendBoolean(content, parameter.hardDomain.maximumInclusive);
        AppendBoolean(content, parameter.serialized);
        AppendText(content, parameter.uiHint);
        AppendBoolean(content, parameter.graphInputCapable);
    }

    AppendEnum(content, definition.policies.range);
    AppendEnum(content, definition.policies.alpha);
    AppendEnum(content, definition.policies.spatial);
    AppendEnum(content, definition.policies.metadata);
    AppendEnum(content, definition.policies.nonFinite);
    AppendText(content, definition.policies.operationOrder);

    AppendUnsigned(content, definition.externalFieldPolicy.size());
    for (const FieldDispositionRule& rule : definition.externalFieldPolicy) {
        AppendEnum(content, rule.field);
        AppendEnum(content, rule.disposition);
    }

    AppendUnsigned(content, definition.diagnosticRuleIds.size());
    for (const std::string& diagnostic : definition.diagnosticRuleIds) {
        AppendText(content, diagnostic);
    }

    AppendUnsigned(content, definition.implementations.size());
    for (const ImplementationContract& implementation : definition.implementations) {
        AppendText(content, implementation.id);
        AppendEnum(content, implementation.kind);
        AppendText(content, implementation.definitionBinding);
        AppendVersion(content, implementation.version);
        AppendText(content, implementation.contentHash);
        AppendText(content, implementation.targetRequirement);
        AppendDouble(content, implementation.absoluteTolerance);
        AppendDouble(content, implementation.relativeTolerance);
        AppendBoolean(content, implementation.canonical);
        AppendUnsigned(content, implementation.evidenceIds.size());
        for (const std::string& evidence : implementation.evidenceIds) {
            AppendText(content, evidence);
        }
    }

    AppendUnsigned(content, definition.referenceTestIds.size());
    for (const std::string& testId : definition.referenceTestIds) {
        AppendText(content, testId);
    }
    AppendBoolean(content, definition.requiresUserVisualReview);
    AppendText(content, definition.knownLimits);
    AppendText(content, definition.lifecycleNotes);
    return content;
}

std::string ComputeNodeDefinitionContentHash(const NodeDefinition& definition) {
    return Sha256ContentIdentity(CanonicalNodeDefinitionContent(definition));
}

void RefreshNodeDefinitionContentHash(NodeDefinition& definition) {
    definition.identity.contentHash = ComputeNodeDefinitionContentHash(definition);
}

std::vector<ContractIssue> ValidateNodeDefinition(const NodeDefinition& definition) {
    std::vector<ContractIssue> issues;
    if (!IsValidDefinitionId(definition.identity.id)) {
        issues.push_back({ "identity.id", "malformed definition ID" });
    }
    if (!IsValidContentHash(definition.identity.contentHash)) {
        issues.push_back({ "identity.contentHash", "malformed SHA-256 content identity" });
    } else if (definition.identity.contentHash !=
        ComputeNodeDefinitionContentHash(definition)) {
        issues.push_back({ "identity.contentHash", "content identity does not match canonical definition content" });
    }
    if (!IsValidScopedId(definition.operationId)) {
        issues.push_back({ "operationId", "malformed semantic operation ID" });
    }
    if (definition.technicalName.empty()) {
        issues.push_back({ "technicalName", "technical name cannot be empty" });
    }
    if (definition.semanticDescription.empty()) {
        issues.push_back({ "semanticDescription", "formula, algorithm, or staged semantics are required" });
    }
    if (definition.policies.operationOrder.empty()) {
        issues.push_back({ "policies.operationOrder", "authored operation order/domain must be explicit" });
    }

    std::set<std::string> portIds;
    bool hasOutput = false;
    for (const PortDefinition& port : definition.ports) {
        if (!IsValidScopedId(port.id)) {
            issues.push_back({ "ports.id", "malformed port ID: " + port.id });
        }
        if (!portIds.insert(port.id).second) {
            issues.push_back({ "ports.id", "duplicate port ID: " + port.id });
        }
        if (port.logicalType == LogicalValueType::Invalid) {
            issues.push_back({ "ports.logicalType", "port type cannot be Invalid" });
        }
        if (port.arity == PortArity::Single && port.maximumConnections != 1) {
            issues.push_back({ "ports.arity", "single port must permit exactly one maximum connection" });
        }
        if (port.maximumConnections != 0 &&
            port.minimumConnections > port.maximumConnections) {
            issues.push_back({ "ports.connections", "minimum exceeds maximum connections" });
        }
        if (port.requirementDescription.empty()) {
            issues.push_back({ "ports.requirement", "semantic requirement must be explained" });
        }
        hasOutput = hasOutput || port.direction == PortDirection::Output;
    }
    if (!hasOutput && definition.propagation != DescriptorPropagationKind::DirectOutput) {
        issues.push_back({ "ports", "non-sink definition must declare an output" });
    }

    std::set<std::string> parameterIds;
    for (const ParameterDefinition& parameter : definition.parameters) {
        if (!IsValidScopedId(parameter.id)) {
            issues.push_back({ "parameters.id", "malformed parameter ID: " + parameter.id });
        }
        if (!parameterIds.insert(parameter.id).second) {
            issues.push_back({ "parameters.id", "duplicate parameter ID: " + parameter.id });
        }
        if (!ParameterTypeMatches(parameter.type, parameter.defaultValue)) {
            issues.push_back({ "parameters.default", "default type does not match parameter: " + parameter.id });
        }
        if (parameter.hardDomain.applicable) {
            if (!std::isfinite(parameter.hardDomain.minimum) ||
                !std::isfinite(parameter.hardDomain.maximum) ||
                parameter.hardDomain.minimum > parameter.hardDomain.maximum) {
                issues.push_back({ "parameters.domain", "numeric hard domain is invalid: " + parameter.id });
            }
            const std::optional<double> value = NumericDefault(parameter);
            if (!value) {
                issues.push_back({ "parameters.domain", "numeric domain applied to nonnumeric parameter: " + parameter.id });
            } else {
                const bool below = parameter.hardDomain.minimumInclusive
                    ? *value < parameter.hardDomain.minimum
                    : *value <= parameter.hardDomain.minimum;
                const bool above = parameter.hardDomain.maximumInclusive
                    ? *value > parameter.hardDomain.maximum
                    : *value >= parameter.hardDomain.maximum;
                if (below || above) {
                    issues.push_back({ "parameters.default", "default is outside hard domain: " + parameter.id });
                }
            }
        }
    }

    std::set<std::string> implementationIds;
    bool hasCanonical = false;
    for (const ImplementationContract& implementation : definition.implementations) {
        if (!IsValidScopedId(implementation.id) ||
            !implementationIds.insert(implementation.id).second) {
            issues.push_back({ "implementations.id", "malformed or duplicate implementation ID" });
        }
        if (implementation.definitionBinding != definition.identity.id) {
            issues.push_back({ "implementations.definitionBinding", "implementation must bind to its owning definition ID" });
        }
        if (!IsValidContentHash(implementation.contentHash)) {
            issues.push_back({ "implementations.contentHash", "malformed implementation hash" });
        } else if (implementation.contentHash !=
            ComputeImplementationContentHash(implementation)) {
            issues.push_back({ "implementations.contentHash", "implementation hash does not match canonical contract content" });
        }
        if (implementation.absoluteTolerance < 0.0 ||
            implementation.relativeTolerance < 0.0) {
            issues.push_back({ "implementations.tolerance", "equivalence tolerance cannot be negative" });
        }
        if (implementation.targetRequirement.empty() || implementation.evidenceIds.empty()) {
            issues.push_back({ "implementations.evidence", "implementation needs target and evidence" });
        }
        hasCanonical = hasCanonical || implementation.canonical;
    }
    if (!hasCanonical) {
        issues.push_back({ "implementations", "one canonical implementation is required" });
    }
    if (definition.referenceTestIds.empty()) {
        issues.push_back({ "referenceTestIds", "at least one reference/property test ID is required" });
    }

    const std::set<std::string> knownRules = [&]() {
        std::set<std::string> ids;
        for (const DiagnosticRule& rule : BuiltInDiagnosticRules()) {
            ids.insert(rule.id);
        }
        return ids;
    }();
    for (const std::string& ruleId : definition.diagnosticRuleIds) {
        if (knownRules.count(ruleId) == 0) {
            issues.push_back({ "diagnosticRuleIds", "unknown diagnostic rule: " + ruleId });
        }
    }

    if (definition.propagation == DescriptorPropagationKind::UnknownExternal) {
        std::set<DescriptorField> fields;
        for (const FieldDispositionRule& rule : definition.externalFieldPolicy) {
            if (!fields.insert(rule.field).second) {
                issues.push_back({ "externalFieldPolicy", "descriptor field has duplicate disposition" });
            }
        }
        constexpr std::size_t kDescriptorFieldCount = 11;
        if (fields.size() != kDescriptorFieldCount) {
            issues.push_back({ "externalFieldPolicy", "external definition must declare all descriptor fields" });
        }
    }
    return issues;
}

std::vector<ContractIssue> ValidateNodeInstance(const NodeInstance& instance) {
    std::vector<ContractIssue> issues;
    if (!IsValidCanonicalUuid(instance.instanceUuid)) {
        issues.push_back({ "instanceUuid", "instance UUID is not canonical lowercase form" });
    }
    if (!IsValidDefinitionId(instance.definition.id) ||
        !IsValidContentHash(instance.definition.contentHash)) {
        issues.push_back({ "definition", "instance requires an exact valid definition reference" });
    }
    std::set<std::string> overrides;
    for (const ParameterOverride& overrideValue : instance.parameterOverrides) {
        if (!IsValidScopedId(overrideValue.parameterId) ||
            !overrides.insert(overrideValue.parameterId).second) {
            issues.push_back({ "parameterOverrides", "override IDs must be valid and unique" });
        }
    }
    return issues;
}

std::vector<ContractIssue> ValidateConnection(const Connection& connection) {
    std::vector<ContractIssue> issues;
    if (!IsValidCanonicalUuid(connection.sourceInstanceUuid) ||
        !IsValidCanonicalUuid(connection.destinationInstanceUuid)) {
        issues.push_back({ "connection.instanceUuid", "connection endpoint UUID is malformed" });
    }
    if (!IsValidScopedId(connection.sourcePortId) ||
        !IsValidScopedId(connection.destinationPortId)) {
        issues.push_back({ "connection.portId", "connection port ID is malformed" });
    }
    return issues;
}

std::vector<NodeDefinition> BuildRepresentativeDefinitions() {
    std::vector<NodeDefinition> definitions;

    NodeDefinition source = Base(
        "stack:image/source", "Ordinary Image Source", "image.source",
        "Decode samples and attach embedded metadata; do not convert samples.",
        CapabilityClass::SpecializedExternal, Inspectability::OpaqueSpecialized,
        DescriptorPropagationKind::Source);
    source.ports = { Output("image", LogicalValueType::ColorImage) };
    source.policies = Policies(RangeBehavior::ExplicitOutput, AlphaBehavior::ExplicitFormula,
        SpatialBehavior::ExplicitOutput, MetadataBehavior::ConsumeAndGenerate, "decode then describe");
    source.diagnosticRuleIds = { "nmr.runtime.resource" };
    definitions.push_back(std::move(source));

    NodeDefinition rawSource = Base(
        "stack:raw/source", "RAW Sensor Source", "raw.source",
        "Decode sensor samples and calibration metadata without assigning color meaning.",
        CapabilityClass::SpecializedExternal, Inspectability::OpaqueSpecialized,
        DescriptorPropagationKind::Source);
    rawSource.ports = { Output("raw", LogicalValueType::Raw), Output("metadata", LogicalValueType::Metadata) };
    rawSource.policies = Policies(RangeBehavior::ExplicitOutput, AlphaBehavior::NotApplicable,
        SpatialBehavior::ExplicitOutput, MetadataBehavior::ConsumeAndGenerate, "sensor decode then calibration extraction");
    rawSource.diagnosticRuleIds = { "nmr.runtime.resource" };
    definitions.push_back(std::move(rawSource));

    NodeDefinition rawDevelop = Base(
        "stack:raw/develop", "RAW Development", "raw.develop",
        "Named staged RAW development produces a declared color image from sensor data and calibration.",
        CapabilityClass::SpecializedExternal, Inspectability::OpaqueSpecialized,
        DescriptorPropagationKind::RawDevelopment);
    rawDevelop.ports = { Input("raw", LogicalValueType::Raw, RequirementPolicy::Strict, "sensor data is required"),
        Input("metadata", LogicalValueType::Metadata, RequirementPolicy::Strict, "calibration metadata is execution-critical"),
        Output("image", LogicalValueType::ColorImage) };
    rawDevelop.policies = Policies(RangeBehavior::ExplicitOutput, AlphaBehavior::ExplicitFormula,
        SpatialBehavior::ExplicitOutput, MetadataBehavior::ConsumeAndGenerate, "decode, calibrate, demosaic, map to declared color output");
    rawDevelop.diagnosticRuleIds = { "nmr.semantic.metadata-missing", "nmr.runtime.resource" };
    definitions.push_back(std::move(rawDevelop));

    NodeDefinition identity = Base(
        "stack:image/identity", "Identity", "image.identity",
        "output(x) = input(x), preserving every sample and descriptor field.",
        CapabilityClass::Pointwise, Inspectability::TransparentGraph,
        DescriptorPropagationKind::Identity);
    identity.ports = { Input("image", LogicalValueType::ColorImage), Output("output", LogicalValueType::ColorImage) };
    identity.policies = Policies(RangeBehavior::Preserve, AlphaBehavior::Preserve,
        SpatialBehavior::Preserve, MetadataBehavior::Preserve, "identity");
    definitions.push_back(std::move(identity));

    NodeDefinition arithmetic = Base(
        "stack:math/multiply", "Multiply", "math.multiply",
        "output(x) = left(x) * right(x), evaluated in authored graph order.",
        CapabilityClass::Pointwise, Inspectability::TransparentGraph,
        DescriptorPropagationKind::GenericArithmetic);
    arithmetic.ports = { Input("left", LogicalValueType::ColorImage),
        Input("right", LogicalValueType::Scalar), Output("image", LogicalValueType::ColorImage) };
    arithmetic.ports[1].broadcast = BroadcastPolicy::UniformToImage;
    arithmetic.policies = Policies(RangeBehavior::Derived, AlphaBehavior::Preserve,
        SpatialBehavior::Preserve, MetadataBehavior::Derive, "left then right multiply at this graph position");
    definitions.push_back(std::move(arithmetic));

    NodeDefinition exposure = Base(
        "stack:color/exposure", "Exposure EV", "color.exposure-ev",
        "output.rgb = input.rgb * pow(2, ev); alpha is unchanged.",
        CapabilityClass::Pointwise, Inspectability::TransparentGraph,
        DescriptorPropagationKind::Exposure);
    exposure.ports = { Input("image", LogicalValueType::ColorImage, RequirementPolicy::Recommended,
        "linear transfer is recommended but numeric work remains allowed"), Output("output", LogicalValueType::ColorImage) };
    ParameterDefinition ev;
    ev.id = "ev";
    ev.type = ParameterType::Scalar;
    ev.units = SemanticField<UnitDescriptor>::Known({ UnitKind::ExposureValue, {} });
    ev.defaultValue = 0.0;
    ev.hardDomain = { true, -64.0, 64.0, true, true };
    ev.serialized = true;
    ev.uiHint = "exposure slider";
    ev.graphInputCapable = true;
    exposure.parameters = { ev };
    exposure.policies = Policies(RangeBehavior::Derived, AlphaBehavior::Preserve,
        SpatialBehavior::Preserve, MetadataBehavior::Derive, "multiply RGB by 2^EV at authored graph position");
    exposure.diagnosticRuleIds = { "nmr.semantic.transfer-nonlinear", "nmr.semantic.transfer-unknown" };
    definitions.push_back(std::move(exposure));

    NodeDefinition mask = Base(
        "stack:mask/luminance", "Luminance Mask", "mask.luminance",
        "Compute declared scalar luminance formula and emit a unitless Mask in [0,1].",
        CapabilityClass::Pointwise, Inspectability::TransparentGraph,
        DescriptorPropagationKind::Mask);
    mask.ports = { Input("image", LogicalValueType::ColorImage, RequirementPolicy::Recommended,
        "known transfer improves interpretation but does not block numeric extraction"), Output("mask", LogicalValueType::Mask) };
    mask.policies = Policies(RangeBehavior::ExplicitOutput, AlphaBehavior::Remove,
        SpatialBehavior::Preserve, MetadataBehavior::Derive, "declared luminance formula then explicit range declaration");
    definitions.push_back(std::move(mask));

    NodeDefinition geometry = Base(
        "stack:geometry/resample", "Resample", "geometry.resample",
        "Sample the input using declared coordinates, filter, border, and output windows.",
        CapabilityClass::SampleResample, Inspectability::TransparentGraph,
        DescriptorPropagationKind::Geometry);
    geometry.ports = { Input("image", LogicalValueType::ColorImage), Output("output", LogicalValueType::ColorImage) };
    geometry.policies = Policies(RangeBehavior::Derived, AlphaBehavior::Preserve,
        SpatialBehavior::ExplicitOutput, MetadataBehavior::Derive, "map output coordinates then sample once with declared policy");
    definitions.push_back(std::move(geometry));

    NodeDefinition color = Base(
        "stack:color/transform", "Color Transform", "color.transform",
        "Apply an explicit source-to-destination color transform; no identity is inferred.",
        CapabilityClass::Pointwise, Inspectability::GraphDefinedOptimizedEquivalent,
        DescriptorPropagationKind::ColorTransform);
    color.ports = { Input("image", LogicalValueType::ColorImage, RequirementPolicy::Strict,
        "known source color identity is execution-critical"), Input("transform", LogicalValueType::Metadata,
        RequirementPolicy::Strict, "explicit destination transform is required"), Output("output", LogicalValueType::ColorImage) };
    color.policies = Policies(RangeBehavior::Derived, AlphaBehavior::Preserve,
        SpatialBehavior::Preserve, MetadataBehavior::ConsumeAndGenerate, "decode source meaning, apply declared transform, declare destination");
    color.diagnosticRuleIds = { "nmr.semantic.metadata-missing" };
    definitions.push_back(std::move(color));

    NodeDefinition composite = Base(
        "stack:composite/source-over", "Source Over", "composite.source-over",
        "Straight-alpha source-over with declared union extent: Ao=As+Ab(1-As), Co=(CsAs+CbAb(1-As))/Ao when Ao>0.",
        CapabilityClass::Pointwise, Inspectability::TransparentGraph,
        DescriptorPropagationKind::Composite);
    composite.ports = { Input("source", LogicalValueType::ColorImage, RequirementPolicy::Strict,
        "straight alpha is required by this formula"), Input("backdrop", LogicalValueType::ColorImage,
        RequirementPolicy::Strict, "straight alpha is required by this formula"), Output("image", LogicalValueType::ColorImage) };
    composite.policies = Policies(RangeBehavior::Derived, AlphaBehavior::ExplicitFormula,
        SpatialBehavior::ExplicitOutput, MetadataBehavior::Derive, "align by declared origins, union extents, then source-over");
    composite.diagnosticRuleIds = { "nmr.semantic.color-mismatch", "nmr.semantic.alpha-mismatch", "nmr.semantic.extent-policy-missing" };
    definitions.push_back(std::move(composite));

    NodeDefinition reduction = Base(
        "stack:analysis/mean", "Image Mean", "analysis.mean",
        "Reduce the declared population to a Vector3 arithmetic mean.",
        CapabilityClass::Reduction, Inspectability::TransparentGraph,
        DescriptorPropagationKind::Reduction);
    reduction.ports = { Input("image", LogicalValueType::ColorImage), Output("mean", LogicalValueType::Vector3) };
    reduction.policies = Policies(RangeBehavior::Reduced, AlphaBehavior::NotApplicable,
        SpatialBehavior::ReduceAway, MetadataBehavior::Derive, "select declared population then sum and divide");
    definitions.push_back(std::move(reduction));

    NodeDefinition output = Base(
        "stack:output/direct", "Direct Output", "output.direct",
        "Expose the connected graph result without tone mapping, normalization, encoding, or repair.",
        CapabilityClass::SpecializedExternal, Inspectability::TransparentGraph,
        DescriptorPropagationKind::DirectOutput);
    output.ports = { Input("image", LogicalValueType::ColorImage) };
    output.policies = Policies(RangeBehavior::Preserve, AlphaBehavior::Preserve,
        SpatialBehavior::Preserve, MetadataBehavior::Preserve, "direct graph result");
    definitions.push_back(std::move(output));

    NodeDefinition frequency = Base(
        "stack:frequency/fft", "Forward FFT", "frequency.fft",
        "Transform declared image channels to complex frequency-domain samples.",
        CapabilityClass::MultipassIterative, Inspectability::GraphDefinedOptimizedEquivalent,
        DescriptorPropagationKind::FrequencyTransform);
    frequency.ports = { Input("image", LogicalValueType::DataImage), Output("spectrum", LogicalValueType::ComplexSpectrum) };
    frequency.policies = Policies(RangeBehavior::Derived, AlphaBehavior::NotApplicable,
        SpatialBehavior::ExplicitOutput, MetadataBehavior::Derive, "declared channel selection then forward FFT");
    definitions.push_back(std::move(frequency));

    NodeDefinition multi = Base(
        "stack:image/merge-many", "Merge Many Images", "image.merge-many",
        "Combine repeated typed images using a declared authored order and alignment policy.",
        CapabilityClass::MultipassIterative, Inspectability::TransparentGraph,
        DescriptorPropagationKind::Composite);
    PortDefinition images = Input("images", LogicalValueType::ColorImage, RequirementPolicy::Recommended,
        "color differences warn while declared numeric combination remains allowed");
    images.arity = PortArity::Variadic;
    images.minimumConnections = 1;
    images.maximumConnections = 0;
    multi.ports = { images, Output("image", LogicalValueType::ColorImage) };
    multi.policies = Policies(RangeBehavior::Derived, AlphaBehavior::ExplicitFormula,
        SpatialBehavior::ExplicitOutput, MetadataBehavior::Derive, "fold inputs in saved connection order using declared alignment");
    multi.diagnosticRuleIds = { "nmr.semantic.color-mismatch", "nmr.semantic.extent-policy-missing" };
    definitions.push_back(std::move(multi));

    NodeDefinition external = Base(
        "stack:external/model-process", "External Model Process", "external.model-process",
        "Opaque external processing with a complete descriptor-field disposition boundary.",
        CapabilityClass::SpecializedExternal, Inspectability::OpaqueSpecialized,
        DescriptorPropagationKind::UnknownExternal);
    external.ports = { Input("image", LogicalValueType::ColorImage), Output("output", LogicalValueType::ColorImage) };
    external.policies = Policies(RangeBehavior::ExplicitOutput, AlphaBehavior::ExplicitFormula,
        SpatialBehavior::Preserve, MetadataBehavior::ExplicitFieldPolicy, "invoke exact external implementation once");
    external.externalFieldPolicy = CompleteExternalPolicy();
    external.diagnosticRuleIds = { "nmr.semantic.external-policy-missing", "nmr.runtime.resource" };
    definitions.push_back(std::move(external));

    for (NodeDefinition& definition : definitions) {
        RefreshNodeDefinitionContentHash(definition);
    }
    return definitions;
}

} // namespace Stack::NodeMath
