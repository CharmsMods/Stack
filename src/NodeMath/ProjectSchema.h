#pragma once

#include "NodeMath/NodeDefinition.h"
#include "ThirdParty/json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Stack::NodeMath {

inline constexpr const char* kNodeGraphFormat = "stack.node-graph";
inline constexpr std::uint32_t kNodeGraphGeneration = 1;
inline constexpr std::uint32_t kNodeGraphSchemaVersion = 1;
inline constexpr const char* kDefinitionResolutionPolicy = "exact";
inline constexpr const char* kMissingDefinitionPolicy = "unresolved";

enum class DefinitionSourceKind {
    BuiltIn,
    Embedded,
    External
};

struct DefinitionManifestEntry {
    DefinitionReference definition;
    DefinitionSourceKind source = DefinitionSourceKind::BuiltIn;
    std::string sourceLocator;
};

struct ProjectEnvelope {
    std::string format = kNodeGraphFormat;
    std::uint32_t generation = kNodeGraphGeneration;
    std::uint32_t schemaVersion = kNodeGraphSchemaVersion;
    std::string definitionResolution = kDefinitionResolutionPolicy;
    std::string missingDefinitionPolicy = kMissingDefinitionPolicy;
    std::string projectUuid;
    std::vector<DefinitionManifestEntry> definitionManifest;
};

enum class EnvelopeParseStatus {
    Ready,
    UnsupportedPreRewrite,
    UnsupportedGeneration,
    Malformed
};

struct EnvelopeParseResult {
    EnvelopeParseStatus status = EnvelopeParseStatus::Malformed;
    std::optional<ProjectEnvelope> envelope;
    std::string message;
};

std::vector<ContractIssue> ValidateProjectEnvelope(const ProjectEnvelope& envelope);
nlohmann::json SerializeProjectEnvelope(const ProjectEnvelope& envelope);
EnvelopeParseResult ParseProjectEnvelope(const nlohmann::json& value);

enum class DefinitionResolutionStatus {
    Exact,
    MissingId,
    VersionMismatch,
    HashMismatch
};

struct DefinitionResolutionResult {
    DefinitionResolutionStatus status = DefinitionResolutionStatus::MissingId;
    std::optional<DefinitionReference> resolved;
};

DefinitionResolutionResult ResolveExactDefinition(
    const DefinitionReference& requested,
    const std::vector<DefinitionReference>& catalog);

} // namespace Stack::NodeMath
