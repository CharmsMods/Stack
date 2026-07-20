#include "NodeMath/ProjectSchema.h"

#include <set>

namespace Stack::NodeMath {

namespace {

const char* ToString(DefinitionSourceKind source) {
    switch (source) {
    case DefinitionSourceKind::BuiltIn: return "BuiltIn";
    case DefinitionSourceKind::Embedded: return "Embedded";
    case DefinitionSourceKind::External: return "External";
    }
    return "";
}

std::optional<DefinitionSourceKind> ParseSource(const std::string& value) {
    if (value == "BuiltIn") return DefinitionSourceKind::BuiltIn;
    if (value == "Embedded") return DefinitionSourceKind::Embedded;
    if (value == "External") return DefinitionSourceKind::External;
    return std::nullopt;
}

EnvelopeParseResult Malformed(const std::string& message) {
    return { EnvelopeParseStatus::Malformed, std::nullopt, message };
}

} // namespace

std::vector<ContractIssue> ValidateProjectEnvelope(const ProjectEnvelope& envelope) {
    std::vector<ContractIssue> issues;
    if (envelope.format != kNodeGraphFormat) {
        issues.push_back({ "format", "format must be stack.node-graph" });
    }
    if (envelope.generation != kNodeGraphGeneration ||
        envelope.schemaVersion != kNodeGraphSchemaVersion) {
        issues.push_back({ "generation", "unsupported generation/schema tuple" });
    }
    if (envelope.definitionResolution != kDefinitionResolutionPolicy) {
        issues.push_back({ "definitionResolution", "v1 resolution must be exact" });
    }
    if (envelope.missingDefinitionPolicy != kMissingDefinitionPolicy) {
        issues.push_back({ "missingDefinitionPolicy", "v1 missing definitions must remain unresolved" });
    }
    if (!IsValidCanonicalUuid(envelope.projectUuid)) {
        issues.push_back({ "projectUuid", "project UUID must use canonical lowercase form" });
    }

    std::set<std::string> exactReferences;
    for (const DefinitionManifestEntry& entry : envelope.definitionManifest) {
        if (!IsValidDefinitionId(entry.definition.id)) {
            issues.push_back({ "definitionManifest.id", "manifest definition ID is malformed" });
        }
        if (!IsValidContentHash(entry.definition.contentHash)) {
            issues.push_back({ "definitionManifest.contentHash", "manifest content hash is malformed" });
        }
        const std::string key = entry.definition.id + "@" +
            ToString(entry.definition.version) + "#" + entry.definition.contentHash;
        if (!exactReferences.insert(key).second) {
            issues.push_back({ "definitionManifest", "duplicate exact definition reference" });
        }
    }
    return issues;
}

nlohmann::json SerializeProjectEnvelope(const ProjectEnvelope& envelope) {
    nlohmann::json manifest = nlohmann::json::array();
    for (const DefinitionManifestEntry& entry : envelope.definitionManifest) {
        nlohmann::json item = {
            { "id", entry.definition.id },
            { "version", ToString(entry.definition.version) },
            { "contentHash", entry.definition.contentHash },
            { "source", ToString(entry.source) }
        };
        if (!entry.sourceLocator.empty()) {
            item["sourceLocator"] = entry.sourceLocator;
        }
        manifest.push_back(std::move(item));
    }
    return {
        { "format", envelope.format },
        { "generation", envelope.generation },
        { "schemaVersion", envelope.schemaVersion },
        { "definitionResolution", envelope.definitionResolution },
        { "missingDefinitionPolicy", envelope.missingDefinitionPolicy },
        { "projectUuid", envelope.projectUuid },
        { "definitionManifest", std::move(manifest) }
    };
}

EnvelopeParseResult ParseProjectEnvelope(const nlohmann::json& value) {
    if (!value.is_object()) {
        return Malformed("project envelope must be an object");
    }
    if (!value.contains("format") || !value.contains("generation")) {
        return {
            EnvelopeParseStatus::UnsupportedPreRewrite,
            std::nullopt,
            "project has no generation-1 node-graph envelope"
        };
    }
    if (!value["format"].is_string() || !value["generation"].is_number_unsigned()) {
        return Malformed("format and generation have invalid types");
    }
    if (value["format"].get<std::string>() != kNodeGraphFormat) {
        return {
            EnvelopeParseStatus::UnsupportedPreRewrite,
            std::nullopt,
            "project is not a generation-1 stack.node-graph document"
        };
    }
    const std::uint32_t generation = value["generation"].get<std::uint32_t>();
    if (generation != kNodeGraphGeneration) {
        return {
            EnvelopeParseStatus::UnsupportedGeneration,
            std::nullopt,
            "node-graph generation is not supported"
        };
    }
    const char* required[] = {
        "schemaVersion",
        "definitionResolution",
        "missingDefinitionPolicy",
        "projectUuid",
        "definitionManifest"
    };
    for (const char* field : required) {
        if (!value.contains(field)) {
            return Malformed(std::string("missing envelope field: ") + field);
        }
    }
    if (!value["schemaVersion"].is_number_unsigned() ||
        !value["definitionResolution"].is_string() ||
        !value["missingDefinitionPolicy"].is_string() ||
        !value["projectUuid"].is_string() ||
        !value["definitionManifest"].is_array()) {
        return Malformed("one or more envelope fields have invalid types");
    }
    if (value["schemaVersion"].get<std::uint32_t>() != kNodeGraphSchemaVersion) {
        return {
            EnvelopeParseStatus::UnsupportedGeneration,
            std::nullopt,
            "node-graph schema version is not supported"
        };
    }

    ProjectEnvelope envelope;
    envelope.format = value["format"].get<std::string>();
    envelope.generation = generation;
    envelope.schemaVersion = value["schemaVersion"].get<std::uint32_t>();
    envelope.definitionResolution = value["definitionResolution"].get<std::string>();
    envelope.missingDefinitionPolicy = value["missingDefinitionPolicy"].get<std::string>();
    envelope.projectUuid = value["projectUuid"].get<std::string>();

    for (const nlohmann::json& item : value["definitionManifest"]) {
        if (!item.is_object() || !item.contains("id") || !item.contains("version") ||
            !item.contains("contentHash") || !item.contains("source") ||
            !item["id"].is_string() || !item["version"].is_string() ||
            !item["contentHash"].is_string() || !item["source"].is_string()) {
            return Malformed("definition manifest entry is incomplete or incorrectly typed");
        }
        const std::optional<SemanticVersion> version =
            ParseSemanticVersion(item["version"].get<std::string>());
        const std::optional<DefinitionSourceKind> source =
            ParseSource(item["source"].get<std::string>());
        if (!version || !source) {
            return Malformed("definition manifest version or source kind is invalid");
        }
        DefinitionManifestEntry entry;
        entry.definition.id = item["id"].get<std::string>();
        entry.definition.version = *version;
        entry.definition.contentHash = item["contentHash"].get<std::string>();
        entry.source = *source;
        if (item.contains("sourceLocator")) {
            if (!item["sourceLocator"].is_string()) {
                return Malformed("definition source locator must be a string");
            }
            entry.sourceLocator = item["sourceLocator"].get<std::string>();
        }
        envelope.definitionManifest.push_back(std::move(entry));
    }

    const std::vector<ContractIssue> issues = ValidateProjectEnvelope(envelope);
    if (!issues.empty()) {
        return Malformed(issues.front().field + ": " + issues.front().message);
    }
    return { EnvelopeParseStatus::Ready, std::move(envelope), {} };
}

DefinitionResolutionResult ResolveExactDefinition(
    const DefinitionReference& requested,
    const std::vector<DefinitionReference>& catalog) {
    bool foundId = false;
    bool foundVersion = false;
    for (const DefinitionReference& candidate : catalog) {
        if (candidate.id != requested.id) {
            continue;
        }
        foundId = true;
        if (candidate.version != requested.version) {
            continue;
        }
        foundVersion = true;
        if (candidate.contentHash == requested.contentHash) {
            return { DefinitionResolutionStatus::Exact, candidate };
        }
    }
    if (!foundId) {
        return { DefinitionResolutionStatus::MissingId, std::nullopt };
    }
    if (!foundVersion) {
        return { DefinitionResolutionStatus::VersionMismatch, std::nullopt };
    }
    return { DefinitionResolutionStatus::HashMismatch, std::nullopt };
}

} // namespace Stack::NodeMath
