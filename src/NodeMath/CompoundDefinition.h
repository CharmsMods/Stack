#pragma once

#include "NodeMath/NodeDefinition.h"
#include "ThirdParty/json.hpp"

#include <optional>
#include <string>
#include <vector>

namespace Stack::NodeMath {

inline constexpr std::uint32_t kCompoundDefinitionSchemaVersion = 1;

enum class CompoundDefinitionClass {
    TransparentGraph,
    GraphDefinedOptimizedEquivalent,
    OpaqueSpecialized
};

enum class CompoundDefinitionSource {
    Embedded,
    ShippedTemplate
};

struct CompoundPortDefinition {
    std::string id;
    std::string label;
    PortDirection direction = PortDirection::Input;
    LogicalValueType logicalType = LogicalValueType::Invalid;
    bool optional = false;
    std::string internalInstanceUuid;
    std::string internalSocketId;
};

struct CompoundParameterDefinition {
    std::string id;
    std::string label;
    ParameterType type = ParameterType::Scalar;
    ParameterValue defaultValue = 0.0;
    NumericParameterDomain hardDomain;
    std::string uiHint = "slider";
    std::string units = "unitless";
    std::string internalInstanceUuid;
    std::string internalParameterId;
};

struct CompoundDefinition {
    std::uint32_t schemaVersion = kCompoundDefinitionSchemaVersion;
    std::string definitionUuid;
    DefinitionReference identity;
    std::string label;
    std::string description;
    CompoundDefinitionClass definitionClass = CompoundDefinitionClass::TransparentGraph;
    CompoundDefinitionSource source = CompoundDefinitionSource::Embedded;
    std::vector<CompoundPortDefinition> ports;
    std::vector<CompoundParameterDefinition> parameters;
    std::vector<DefinitionReference> dependencies;
    nlohmann::json canonicalGraph = nlohmann::json::object();
    bool unpackable = true;
    std::string optimizedImplementationId;
    double absoluteTolerance = 0.0;
    double relativeTolerance = 0.0;
    std::vector<std::string> equivalenceEvidenceIds;
    std::string lifecycleNotes;
};

enum class CompoundResolutionStatus {
    Exact,
    Missing,
    VersionMismatch,
    HashMismatch,
    InvalidDefinition,
    RecursiveDependency
};

struct CompoundInstance {
    std::string instanceUuid;
    DefinitionReference definition;
    std::vector<ParameterOverride> parameterOverrides;
    std::vector<CompoundPortDefinition> interfaceSnapshot;
    CompoundResolutionStatus resolution = CompoundResolutionStatus::Missing;
    std::string resolutionError;
};

std::string GenerateCanonicalUuid();
bool SameDefinitionReference(const DefinitionReference& left, const DefinitionReference& right);

std::string CanonicalCompoundDefinitionContent(const CompoundDefinition& definition);
std::string ComputeCompoundDefinitionContentHash(const CompoundDefinition& definition);
void RefreshCompoundDefinitionContentHash(CompoundDefinition& definition);

std::vector<ContractIssue> ValidateCompoundDefinition(const CompoundDefinition& definition);
std::vector<ContractIssue> ValidateCompoundCatalog(const std::vector<CompoundDefinition>& definitions);

nlohmann::json SerializeCompoundDefinition(const CompoundDefinition& definition);
bool DeserializeCompoundDefinition(
    const nlohmann::json& value,
    CompoundDefinition& definition,
    std::string* error = nullptr);
nlohmann::json SerializeCompoundInstance(const CompoundInstance& instance);
bool DeserializeCompoundInstance(
    const nlohmann::json& value,
    CompoundInstance& instance,
    std::string* error = nullptr);

const CompoundDefinition* FindExactCompoundDefinition(
    const std::vector<CompoundDefinition>& definitions,
    const DefinitionReference& reference);
CompoundResolutionStatus ResolveCompoundInstance(
    CompoundInstance& instance,
    const std::vector<CompoundDefinition>& definitions);

std::vector<DefinitionReference> CollectCompoundDependencyClosure(
    const std::vector<CompoundDefinition>& definitions,
    const std::vector<DefinitionReference>& roots,
    std::string* error = nullptr);

bool CreateUniqueCompoundDefinition(
    const CompoundDefinition& source,
    const std::string& label,
    CompoundDefinition& uniqueDefinition,
    std::string* error = nullptr);
bool CreateEditedCompoundDefinitionVersion(
    const CompoundDefinition& source,
    SemanticVersion newVersion,
    const nlohmann::json& editedCanonicalGraph,
    CompoundDefinition& editedDefinition,
    std::string* error = nullptr);

const ParameterValue* FindCompoundParameterOverride(
    const CompoundInstance& instance,
    const std::string& parameterId);
ParameterValue ResolveCompoundParameterValue(
    const CompoundInstance& instance,
    const CompoundParameterDefinition& parameter);
void SetCompoundParameterOverride(
    CompoundInstance& instance,
    const std::string& parameterId,
    ParameterValue value);

const char* CompoundDefinitionClassName(CompoundDefinitionClass value);
const char* CompoundResolutionStatusName(CompoundResolutionStatus value);

} // namespace Stack::NodeMath
