#pragma once

#include "NodeMath/ContractTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace Stack::NodeMath {

struct DefinitionReference {
    std::string id;
    SemanticVersion version;
    std::string contentHash;
};

bool operator==(const DefinitionReference& left, const DefinitionReference& right);
bool operator!=(const DefinitionReference& left, const DefinitionReference& right);

enum class CapabilityClass {
    Pointwise,
    SampleResample,
    Neighborhood,
    Reduction,
    MultipassIterative,
    SpecializedExternal
};

enum class Inspectability {
    TransparentGraph,
    GraphDefinedOptimizedEquivalent,
    OpaqueSpecialized
};

enum class PortDirection {
    Input,
    Output
};

enum class PortArity {
    Single,
    Variadic
};

enum class RequirementPolicy {
    Agnostic,
    Recommended,
    Strict
};

enum class BroadcastPolicy {
    None,
    UniformToImage,
    ComponentWise,
    DeclaredOnly
};

struct PortDefinition {
    std::string id;
    PortDirection direction = PortDirection::Input;
    LogicalValueType logicalType = LogicalValueType::Invalid;
    PortArity arity = PortArity::Single;
    std::size_t minimumConnections = 0;
    std::size_t maximumConnections = 1; // Zero means unbounded for variadic ports.
    bool optional = false;
    BroadcastPolicy broadcast = BroadcastPolicy::None;
    SemanticField<UnitDescriptor> units;
    RequirementPolicy semanticRequirement = RequirementPolicy::Agnostic;
    std::string requirementDescription;
};

enum class ParameterType {
    Boolean,
    Integer,
    Scalar,
    Vector2,
    Vector3,
    Vector4,
    Text,
    ResourceReference
};

using ParameterValue = std::variant<
    bool,
    std::int64_t,
    double,
    std::array<double, 2>,
    std::array<double, 3>,
    std::array<double, 4>,
    std::string>;

struct NumericParameterDomain {
    bool applicable = false;
    double minimum = 0.0;
    double maximum = 0.0;
    bool minimumInclusive = true;
    bool maximumInclusive = true;
};

struct ParameterDefinition {
    std::string id;
    ParameterType type = ParameterType::Scalar;
    SemanticField<UnitDescriptor> units;
    ParameterValue defaultValue = 0.0;
    NumericParameterDomain hardDomain;
    bool serialized = true;
    std::string uiHint;
    bool graphInputCapable = false;
};

enum class DescriptorPropagationKind {
    Source,
    Identity,
    GenericArithmetic,
    Exposure,
    Mask,
    Geometry,
    ColorTransform,
    Composite,
    Reduction,
    DirectOutput,
    RawDevelopment,
    FrequencyTransform,
    UnknownExternal
};

enum class RangeBehavior {
    Preserve,
    Derived,
    ExplicitOutput,
    Reduced,
    NotApplicable
};

enum class AlphaBehavior {
    Preserve,
    ExplicitFormula,
    Remove,
    NotApplicable
};

enum class SpatialBehavior {
    Preserve,
    ExplicitOutput,
    ReduceAway,
    NotApplicable
};

enum class MetadataBehavior {
    Preserve,
    Derive,
    ConsumeAndGenerate,
    ExplicitFieldPolicy
};

struct SemanticPolicies {
    RangeBehavior range = RangeBehavior::Preserve;
    AlphaBehavior alpha = AlphaBehavior::Preserve;
    SpatialBehavior spatial = SpatialBehavior::Preserve;
    MetadataBehavior metadata = MetadataBehavior::Preserve;
    NonFinitePolicy nonFinite = NonFinitePolicy::Unknown;
    std::string operationOrder;
};

enum class DescriptorField {
    Channels,
    Color,
    Transfer,
    Reference,
    Alpha,
    Range,
    Precision,
    Spatial,
    Sampling,
    Units,
    Provenance
};

enum class FieldDisposition {
    Preserve,
    Replace,
    Invalidate,
    Consume,
    Generate,
    Drop
};

struct FieldDispositionRule {
    DescriptorField field = DescriptorField::Channels;
    FieldDisposition disposition = FieldDisposition::Preserve;
};

enum class ImplementationKind {
    CanonicalCpu,
    GpuShader,
    Cpu,
    ExternalLibrary,
    Model
};

struct ImplementationContract {
    std::string id;
    ImplementationKind kind = ImplementationKind::CanonicalCpu;
    std::string definitionBinding;
    SemanticVersion version;
    std::string contentHash;
    std::string targetRequirement;
    double absoluteTolerance = 0.0;
    double relativeTolerance = 0.0;
    bool canonical = false;
    std::vector<std::string> evidenceIds;
};

struct NodeDefinition {
    DefinitionReference identity;
    std::string technicalName;
    std::string operationId;
    std::string semanticDescription;
    CapabilityClass capability = CapabilityClass::Pointwise;
    Inspectability inspectability = Inspectability::TransparentGraph;
    std::vector<PortDefinition> ports;
    std::vector<ParameterDefinition> parameters;
    DescriptorPropagationKind propagation = DescriptorPropagationKind::Identity;
    SemanticPolicies policies;
    std::vector<FieldDispositionRule> externalFieldPolicy;
    std::vector<std::string> diagnosticRuleIds;
    std::vector<ImplementationContract> implementations;
    std::vector<std::string> referenceTestIds;
    bool requiresUserVisualReview = false;
    std::string knownLimits;
    std::string lifecycleNotes;
};

struct ParameterOverride {
    std::string parameterId;
    ParameterValue value = 0.0;
};

struct NodeInstance {
    std::string instanceUuid;
    DefinitionReference definition;
    std::vector<ParameterOverride> parameterOverrides;
    std::string presentationState;
};

struct Connection {
    std::string sourceInstanceUuid;
    std::string sourcePortId;
    std::string destinationInstanceUuid;
    std::string destinationPortId;
};

std::vector<ContractIssue> ValidateNodeDefinition(const NodeDefinition& definition);
std::string CanonicalImplementationContent(
    const ImplementationContract& implementation);
std::string ComputeImplementationContentHash(
    const ImplementationContract& implementation);
void RefreshImplementationContentHash(ImplementationContract& implementation);
std::string CanonicalNodeDefinitionContent(const NodeDefinition& definition);
std::string ComputeNodeDefinitionContentHash(const NodeDefinition& definition);
void RefreshNodeDefinitionContentHash(NodeDefinition& definition);
std::vector<ContractIssue> ValidateNodeInstance(const NodeInstance& instance);
std::vector<ContractIssue> ValidateConnection(const Connection& connection);
std::vector<NodeDefinition> BuildRepresentativeDefinitions();

} // namespace Stack::NodeMath
