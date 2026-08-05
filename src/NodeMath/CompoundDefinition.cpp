#include "NodeMath/CompoundDefinition.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <unordered_map>

namespace Stack::NodeMath {
namespace {

const char* DefinitionClassString(CompoundDefinitionClass value) {
    switch (value) {
        case CompoundDefinitionClass::TransparentGraph: return "transparent-graph";
        case CompoundDefinitionClass::GraphDefinedOptimizedEquivalent: return "graph-defined-optimized-equivalent";
        case CompoundDefinitionClass::OpaqueSpecialized: return "opaque-specialized";
    }
    return "transparent-graph";
}

CompoundDefinitionClass DefinitionClassFromString(const std::string& value) {
    if (value == "graph-defined-optimized-equivalent") {
        return CompoundDefinitionClass::GraphDefinedOptimizedEquivalent;
    }
    if (value == "opaque-specialized") return CompoundDefinitionClass::OpaqueSpecialized;
    return CompoundDefinitionClass::TransparentGraph;
}

const char* DefinitionSourceString(CompoundDefinitionSource value) {
    return value == CompoundDefinitionSource::ShippedTemplate ? "shipped-template" : "embedded";
}

CompoundDefinitionSource DefinitionSourceFromString(const std::string& value) {
    return value == "shipped-template"
        ? CompoundDefinitionSource::ShippedTemplate
        : CompoundDefinitionSource::Embedded;
}

const char* PortDirectionString(PortDirection value) {
    return value == PortDirection::Output ? "output" : "input";
}

const char* ParameterTypeString(ParameterType value) {
    switch (value) {
        case ParameterType::Boolean: return "boolean";
        case ParameterType::Integer: return "integer";
        case ParameterType::Scalar: return "scalar";
        case ParameterType::Vector2: return "vector2";
        case ParameterType::Vector3: return "vector3";
        case ParameterType::Vector4: return "vector4";
        case ParameterType::Text: return "text";
        case ParameterType::ResourceReference: return "resource-reference";
    }
    return "scalar";
}

ParameterType ParameterTypeFromString(const std::string& value) {
    if (value == "boolean") return ParameterType::Boolean;
    if (value == "integer") return ParameterType::Integer;
    if (value == "vector2") return ParameterType::Vector2;
    if (value == "vector3") return ParameterType::Vector3;
    if (value == "vector4") return ParameterType::Vector4;
    if (value == "text") return ParameterType::Text;
    if (value == "resource-reference") return ParameterType::ResourceReference;
    return ParameterType::Scalar;
}

nlohmann::json SerializeParameterValue(const ParameterValue& value) {
    return std::visit([](const auto& item) -> nlohmann::json { return item; }, value);
}

bool DeserializeParameterValue(
    const nlohmann::json& value,
    ParameterType type,
    ParameterValue& result) {
    try {
        switch (type) {
            case ParameterType::Boolean:
                if (!value.is_boolean()) return false;
                result = value.get<bool>();
                return true;
            case ParameterType::Integer:
                if (!value.is_number_integer()) return false;
                result = value.get<std::int64_t>();
                return true;
            case ParameterType::Scalar:
                if (!value.is_number()) return false;
                result = value.get<double>();
                return true;
            case ParameterType::Vector2:
            case ParameterType::Vector3:
            case ParameterType::Vector4: {
                const std::size_t count = type == ParameterType::Vector2 ? 2u :
                    (type == ParameterType::Vector3 ? 3u : 4u);
                if (!value.is_array() || value.size() != count) return false;
                if (count == 2u) result = std::array<double, 2>{ value[0].get<double>(), value[1].get<double>() };
                if (count == 3u) result = std::array<double, 3>{ value[0].get<double>(), value[1].get<double>(), value[2].get<double>() };
                if (count == 4u) result = std::array<double, 4>{ value[0].get<double>(), value[1].get<double>(), value[2].get<double>(), value[3].get<double>() };
                return true;
            }
            case ParameterType::Text:
            case ParameterType::ResourceReference:
                if (!value.is_string()) return false;
                result = value.get<std::string>();
                return true;
        }
    } catch (...) {
        return false;
    }
    return false;
}

nlohmann::json SerializeReference(const DefinitionReference& reference) {
    return {
        { "id", reference.id },
        { "version", ToString(reference.version) },
        { "contentHash", reference.contentHash }
    };
}

bool DeserializeReference(const nlohmann::json& value, DefinitionReference& reference) {
    if (!value.is_object()) return false;
    const std::optional<SemanticVersion> version =
        ParseSemanticVersion(value.value("version", std::string()));
    if (!version) return false;
    reference.id = value.value("id", std::string());
    reference.version = *version;
    reference.contentHash = value.value("contentHash", std::string());
    return IsValidDefinitionId(reference.id) && IsValidContentHash(reference.contentHash);
}

nlohmann::json SerializePort(const CompoundPortDefinition& port) {
    return {
        { "id", port.id },
        { "label", port.label },
        { "direction", PortDirectionString(port.direction) },
        { "logicalType", static_cast<int>(port.logicalType) },
        { "optional", port.optional },
        { "internalInstanceUuid", port.internalInstanceUuid },
        { "internalSocketId", port.internalSocketId }
    };
}

bool DeserializePort(const nlohmann::json& value, CompoundPortDefinition& port) {
    if (!value.is_object()) return false;
    const int logicalType = value.value("logicalType", static_cast<int>(LogicalValueType::Invalid));
    if (logicalType < static_cast<int>(LogicalValueType::Invalid) ||
        logicalType > static_cast<int>(LogicalValueType::Failure)) return false;
    port.id = value.value("id", std::string());
    port.label = value.value("label", std::string());
    port.direction = value.value("direction", std::string("input")) == "output"
        ? PortDirection::Output : PortDirection::Input;
    port.logicalType = static_cast<LogicalValueType>(logicalType);
    port.optional = value.value("optional", false);
    port.internalInstanceUuid = value.value("internalInstanceUuid", std::string());
    port.internalSocketId = value.value("internalSocketId", std::string());
    return true;
}

nlohmann::json SerializeParameter(const CompoundParameterDefinition& parameter) {
    return {
        { "id", parameter.id },
        { "label", parameter.label },
        { "type", ParameterTypeString(parameter.type) },
        { "default", SerializeParameterValue(parameter.defaultValue) },
        { "domain", {
            { "applicable", parameter.hardDomain.applicable },
            { "minimum", parameter.hardDomain.minimum },
            { "maximum", parameter.hardDomain.maximum },
            { "minimumInclusive", parameter.hardDomain.minimumInclusive },
            { "maximumInclusive", parameter.hardDomain.maximumInclusive }
        } },
        { "uiHint", parameter.uiHint },
        { "units", parameter.units },
        { "internalInstanceUuid", parameter.internalInstanceUuid },
        { "internalParameterId", parameter.internalParameterId }
    };
}

bool DeserializeParameter(const nlohmann::json& value, CompoundParameterDefinition& parameter) {
    if (!value.is_object()) return false;
    parameter.id = value.value("id", std::string());
    parameter.label = value.value("label", std::string());
    parameter.type = ParameterTypeFromString(value.value("type", std::string("scalar")));
    if (!DeserializeParameterValue(value.value("default", nlohmann::json()), parameter.type, parameter.defaultValue)) {
        return false;
    }
    const nlohmann::json domain = value.value("domain", nlohmann::json::object());
    parameter.hardDomain.applicable = domain.value("applicable", false);
    parameter.hardDomain.minimum = domain.value("minimum", 0.0);
    parameter.hardDomain.maximum = domain.value("maximum", 0.0);
    parameter.hardDomain.minimumInclusive = domain.value("minimumInclusive", true);
    parameter.hardDomain.maximumInclusive = domain.value("maximumInclusive", true);
    parameter.uiHint = value.value("uiHint", std::string("slider"));
    parameter.units = value.value("units", std::string("unitless"));
    parameter.internalInstanceUuid = value.value("internalInstanceUuid", std::string());
    parameter.internalParameterId = value.value("internalParameterId", std::string());
    return true;
}

bool VersionGreater(const SemanticVersion& left, const SemanticVersion& right) {
    if (left.major != right.major) return left.major > right.major;
    if (left.minor != right.minor) return left.minor > right.minor;
    return left.patch > right.patch;
}

} // namespace

std::string GenerateCanonicalUuid() {
    thread_local std::mt19937_64 engine([] {
        std::random_device random;
        const auto now = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        std::seed_seq seed {
            random(), random(), static_cast<unsigned int>(now),
            static_cast<unsigned int>(now >> 32u)
        };
        return std::mt19937_64(seed);
    }());
    std::array<unsigned char, 16> bytes {};
    for (std::size_t index = 0; index < bytes.size(); index += 8u) {
        const std::uint64_t value = engine();
        for (std::size_t offset = 0; offset < 8u; ++offset) {
            bytes[index + offset] = static_cast<unsigned char>((value >> (offset * 8u)) & 0xffu);
        }
    }
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0fu) | 0x40u);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3fu) | 0x80u);
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4u || index == 6u || index == 8u || index == 10u) stream << '-';
        stream << std::setw(2) << static_cast<unsigned int>(bytes[index]);
    }
    return stream.str();
}

bool SameDefinitionReference(const DefinitionReference& left, const DefinitionReference& right) {
    return left.id == right.id && left.version == right.version &&
        left.contentHash == right.contentHash;
}

const char* CompoundDefinitionClassName(CompoundDefinitionClass value) {
    switch (value) {
        case CompoundDefinitionClass::TransparentGraph: return "Transparent graph";
        case CompoundDefinitionClass::GraphDefinedOptimizedEquivalent: return "Optimized equivalent";
        case CompoundDefinitionClass::OpaqueSpecialized: return "Opaque specialized";
    }
    return "Transparent graph";
}

const char* CompoundResolutionStatusName(CompoundResolutionStatus value) {
    switch (value) {
        case CompoundResolutionStatus::Exact: return "exact";
        case CompoundResolutionStatus::Missing: return "missing";
        case CompoundResolutionStatus::VersionMismatch: return "version-mismatch";
        case CompoundResolutionStatus::HashMismatch: return "hash-mismatch";
        case CompoundResolutionStatus::InvalidDefinition: return "invalid-definition";
        case CompoundResolutionStatus::RecursiveDependency: return "recursive-dependency";
    }
    return "missing";
}

std::string CanonicalCompoundDefinitionContent(const CompoundDefinition& definition) {
    nlohmann::json value = SerializeCompoundDefinition(definition);
    value["identity"]["contentHash"] = "";
    value["source"] = "semantic-content";
    return std::string("stack.compound-definition.canonical.v1\n") + value.dump();
}

std::string ComputeCompoundDefinitionContentHash(const CompoundDefinition& definition) {
    return Sha256ContentIdentity(CanonicalCompoundDefinitionContent(definition));
}

void RefreshCompoundDefinitionContentHash(CompoundDefinition& definition) {
    definition.identity.contentHash = ComputeCompoundDefinitionContentHash(definition);
}

std::vector<ContractIssue> ValidateCompoundDefinition(const CompoundDefinition& definition) {
    std::vector<ContractIssue> issues;
    if (definition.schemaVersion != kCompoundDefinitionSchemaVersion) {
        issues.push_back({ "schemaVersion", "unsupported compound definition schema" });
    }
    if (!IsValidCanonicalUuid(definition.definitionUuid)) {
        issues.push_back({ "definitionUuid", "compound definition UUID is not canonical" });
    }
    if (!IsValidDefinitionId(definition.identity.id)) {
        issues.push_back({ "identity.id", "compound definition ID is invalid" });
    }
    if (!IsValidContentHash(definition.identity.contentHash) ||
        definition.identity.contentHash != ComputeCompoundDefinitionContentHash(definition)) {
        issues.push_back({ "identity.contentHash", "compound definition content hash is missing or stale" });
    }
    if (definition.label.empty()) issues.push_back({ "label", "compound label is required" });

    std::set<std::string> portIds;
    for (const CompoundPortDefinition& port : definition.ports) {
        if (!IsValidScopedId(port.id) || !portIds.insert(port.id).second) {
            issues.push_back({ "ports.id", "compound port IDs must be unique stable tokens" });
        }
        if (port.logicalType == LogicalValueType::Invalid) {
            issues.push_back({ "ports.logicalType", "compound port type is invalid" });
        }
        if (!IsValidCanonicalUuid(port.internalInstanceUuid) || port.internalSocketId.empty()) {
            issues.push_back({ "ports.binding", "compound port binding is incomplete" });
        }
    }

    std::set<std::string> parameterIds;
    for (const CompoundParameterDefinition& parameter : definition.parameters) {
        if (!IsValidScopedId(parameter.id) || !parameterIds.insert(parameter.id).second) {
            issues.push_back({ "parameters.id", "compound parameter IDs must be unique stable tokens" });
        }
        if (!IsValidCanonicalUuid(parameter.internalInstanceUuid) || parameter.internalParameterId.empty()) {
            issues.push_back({ "parameters.binding", "promoted parameter binding is incomplete" });
        }
        if (parameter.hardDomain.applicable &&
            parameter.hardDomain.minimum > parameter.hardDomain.maximum) {
            issues.push_back({ "parameters.domain", "promoted parameter domain is reversed" });
        }
    }

    std::set<std::string> dependencyKeys;
    for (const DefinitionReference& dependency : definition.dependencies) {
        const std::string key = dependency.id + "@" + ToString(dependency.version) + "#" + dependency.contentHash;
        if (!IsValidDefinitionId(dependency.id) || !IsValidContentHash(dependency.contentHash) ||
            !dependencyKeys.insert(key).second) {
            issues.push_back({ "dependencies", "compound dependency is invalid or duplicated" });
        }
        if (SameDefinitionReference(dependency, definition.identity)) {
            issues.push_back({ "dependencies", "compound definition cannot depend on itself" });
        }
    }

    if (definition.definitionClass == CompoundDefinitionClass::OpaqueSpecialized) {
        if (definition.unpackable) issues.push_back({ "unpackable", "opaque specialized definitions cannot unpack" });
    } else {
        if (!definition.canonicalGraph.is_object() || !definition.canonicalGraph.contains("nodeGraph")) {
            issues.push_back({ "canonicalGraph", "graph-defined compound requires a canonical graph" });
        }
        if (!definition.unpackable) issues.push_back({ "unpackable", "graph-defined compound must support Unpack" });
    }
    if (definition.definitionClass == CompoundDefinitionClass::GraphDefinedOptimizedEquivalent) {
        if (definition.optimizedImplementationId.empty()) {
            issues.push_back({ "optimizedImplementationId", "optimized-equivalent compound requires an implementation ID" });
        }
        if (definition.equivalenceEvidenceIds.empty()) {
            issues.push_back({ "equivalenceEvidenceIds", "optimized-equivalent compound requires equivalence evidence" });
        }
        if (definition.absoluteTolerance < 0.0 || definition.relativeTolerance < 0.0) {
            issues.push_back({ "tolerance", "optimized-equivalent tolerance cannot be negative" });
        }
    }
    return issues;
}

std::vector<ContractIssue> ValidateCompoundCatalog(const std::vector<CompoundDefinition>& definitions) {
    std::vector<ContractIssue> issues;
    std::map<std::string, const CompoundDefinition*> exactDefinitions;
    for (const CompoundDefinition& definition : definitions) {
        const std::string key = definition.identity.id + "@" + ToString(definition.identity.version) +
            "#" + definition.identity.contentHash;
        if (!exactDefinitions.emplace(key, &definition).second) {
            issues.push_back({ "catalog.duplicate", "duplicate exact compound definition" });
        }
        const std::vector<ContractIssue> definitionIssues = ValidateCompoundDefinition(definition);
        issues.insert(issues.end(), definitionIssues.begin(), definitionIssues.end());
    }

    std::unordered_map<std::string, std::vector<std::string>> edges;
    for (const CompoundDefinition& definition : definitions) {
        const std::string key = definition.identity.id + "@" + ToString(definition.identity.version) +
            "#" + definition.identity.contentHash;
        for (const DefinitionReference& dependency : definition.dependencies) {
            const std::string dependencyKey = dependency.id + "@" + ToString(dependency.version) +
                "#" + dependency.contentHash;
            if (!exactDefinitions.count(dependencyKey)) {
                issues.push_back({ "catalog.missingDependency", "compound dependency is not embedded exactly" });
            } else {
                edges[key].push_back(dependencyKey);
            }
        }
    }

    std::unordered_map<std::string, std::size_t> indegree;
    indegree.reserve(exactDefinitions.size());
    for (const auto& [key, definition] : exactDefinitions) {
        (void)definition;
        indegree.emplace(key, 0u);
    }
    for (const auto& [key, dependencies] : edges) {
        (void)key;
        for (const std::string& dependency : dependencies) {
            auto degree = indegree.find(dependency);
            if (degree != indegree.end()) {
                ++degree->second;
            }
        }
    }

    std::vector<std::string> ready;
    ready.reserve(indegree.size());
    for (const auto& [key, degree] : indegree) {
        if (degree == 0u) {
            ready.push_back(key);
        }
    }
    std::size_t processedDefinitionCount = 0;
    while (!ready.empty()) {
        std::string key = std::move(ready.back());
        ready.pop_back();
        ++processedDefinitionCount;
        for (const std::string& dependency : edges[key]) {
            auto degree = indegree.find(dependency);
            if (degree != indegree.end() &&
                degree->second > 0u &&
                --degree->second == 0u) {
                ready.push_back(dependency);
            }
        }
    }
    if (processedDefinitionCount != indegree.size()) {
        issues.push_back(
            {
                "catalog.recursion",
                "recursive compound dependency is not allowed"
            });
    }
    return issues;
}

nlohmann::json SerializeCompoundDefinition(const CompoundDefinition& definition) {
    nlohmann::json ports = nlohmann::json::array();
    for (const CompoundPortDefinition& port : definition.ports) ports.push_back(SerializePort(port));
    nlohmann::json parameters = nlohmann::json::array();
    for (const CompoundParameterDefinition& parameter : definition.parameters) {
        parameters.push_back(SerializeParameter(parameter));
    }
    nlohmann::json dependencies = nlohmann::json::array();
    for (const DefinitionReference& dependency : definition.dependencies) {
        dependencies.push_back(SerializeReference(dependency));
    }
    return {
        { "schemaVersion", definition.schemaVersion },
        { "definitionUuid", definition.definitionUuid },
        { "identity", SerializeReference(definition.identity) },
        { "label", definition.label },
        { "description", definition.description },
        { "class", DefinitionClassString(definition.definitionClass) },
        { "source", DefinitionSourceString(definition.source) },
        { "ports", std::move(ports) },
        { "parameters", std::move(parameters) },
        { "dependencies", std::move(dependencies) },
        { "canonicalGraph", definition.canonicalGraph },
        { "unpackable", definition.unpackable },
        { "optimizedImplementationId", definition.optimizedImplementationId },
        { "absoluteTolerance", definition.absoluteTolerance },
        { "relativeTolerance", definition.relativeTolerance },
        { "equivalenceEvidenceIds", definition.equivalenceEvidenceIds },
        { "lifecycleNotes", definition.lifecycleNotes }
    };
}

bool DeserializeCompoundDefinition(
    const nlohmann::json& value,
    CompoundDefinition& definition,
    std::string* error) {
    if (!value.is_object()) {
        if (error) *error = "Compound definition is not an object.";
        return false;
    }
    CompoundDefinition parsed;
    parsed.schemaVersion = value.value("schemaVersion", 0u);
    parsed.definitionUuid = value.value("definitionUuid", std::string());
    if (!DeserializeReference(value.value("identity", nlohmann::json()), parsed.identity)) {
        if (error) *error = "Compound definition identity is invalid.";
        return false;
    }
    parsed.label = value.value("label", std::string());
    parsed.description = value.value("description", std::string());
    parsed.definitionClass = DefinitionClassFromString(value.value("class", std::string()));
    parsed.source = DefinitionSourceFromString(value.value("source", std::string()));
    parsed.canonicalGraph = value.value("canonicalGraph", nlohmann::json::object());
    parsed.unpackable = value.value("unpackable", true);
    parsed.optimizedImplementationId = value.value("optimizedImplementationId", std::string());
    parsed.absoluteTolerance = value.value("absoluteTolerance", 0.0);
    parsed.relativeTolerance = value.value("relativeTolerance", 0.0);
    parsed.equivalenceEvidenceIds = value.value("equivalenceEvidenceIds", std::vector<std::string>{});
    parsed.lifecycleNotes = value.value("lifecycleNotes", std::string());
    for (const nlohmann::json& item : value.value("ports", nlohmann::json::array())) {
        CompoundPortDefinition port;
        if (!DeserializePort(item, port)) {
            if (error) *error = "Compound definition contains an invalid port.";
            return false;
        }
        parsed.ports.push_back(std::move(port));
    }
    for (const nlohmann::json& item : value.value("parameters", nlohmann::json::array())) {
        CompoundParameterDefinition parameter;
        if (!DeserializeParameter(item, parameter)) {
            if (error) *error = "Compound definition contains an invalid parameter.";
            return false;
        }
        parsed.parameters.push_back(std::move(parameter));
    }
    for (const nlohmann::json& item : value.value("dependencies", nlohmann::json::array())) {
        DefinitionReference dependency;
        if (!DeserializeReference(item, dependency)) {
            if (error) *error = "Compound definition contains an invalid dependency.";
            return false;
        }
        parsed.dependencies.push_back(std::move(dependency));
    }
    const std::vector<ContractIssue> issues = ValidateCompoundDefinition(parsed);
    if (!issues.empty()) {
        if (error) *error = issues.front().message;
        return false;
    }
    definition = std::move(parsed);
    return true;
}

nlohmann::json SerializeCompoundInstance(const CompoundInstance& instance) {
    nlohmann::json overrides = nlohmann::json::array();
    for (const ParameterOverride& overrideValue : instance.parameterOverrides) {
        overrides.push_back({
            { "parameterId", overrideValue.parameterId },
            { "value", SerializeParameterValue(overrideValue.value) }
        });
    }
    nlohmann::json interfaceSnapshot = nlohmann::json::array();
    for (const CompoundPortDefinition& port : instance.interfaceSnapshot) {
        interfaceSnapshot.push_back(SerializePort(port));
    }
    return {
        { "instanceUuid", instance.instanceUuid },
        { "definition", SerializeReference(instance.definition) },
        { "parameterOverrides", std::move(overrides) },
        { "interfaceSnapshot", std::move(interfaceSnapshot) }
    };
}

bool DeserializeCompoundInstance(
    const nlohmann::json& value,
    CompoundInstance& instance,
    std::string* error) {
    if (!value.is_object()) {
        if (error) *error = "Compound instance is not an object.";
        return false;
    }
    CompoundInstance parsed;
    parsed.instanceUuid = value.value("instanceUuid", std::string());
    if (!IsValidCanonicalUuid(parsed.instanceUuid) ||
        !DeserializeReference(value.value("definition", nlohmann::json()), parsed.definition)) {
        if (error) *error = "Compound instance identity is invalid.";
        return false;
    }
    for (const nlohmann::json& item : value.value("interfaceSnapshot", nlohmann::json::array())) {
        CompoundPortDefinition port;
        if (!DeserializePort(item, port)) {
            if (error) *error = "Compound instance interface snapshot is invalid.";
            return false;
        }
        parsed.interfaceSnapshot.push_back(std::move(port));
    }
    for (const nlohmann::json& item : value.value("parameterOverrides", nlohmann::json::array())) {
        if (!item.is_object()) continue;
        const std::string parameterId = item.value("parameterId", std::string());
        if (!IsValidScopedId(parameterId)) continue;
        const nlohmann::json rawValue = item.value("value", nlohmann::json());
        ParameterValue parameterValue = 0.0;
        bool parsedValue = false;
        for (ParameterType type : { ParameterType::Boolean, ParameterType::Integer, ParameterType::Scalar,
                ParameterType::Vector2, ParameterType::Vector3, ParameterType::Vector4,
                ParameterType::Text }) {
            if (DeserializeParameterValue(rawValue, type, parameterValue)) {
                parsedValue = true;
                break;
            }
        }
        if (parsedValue) parsed.parameterOverrides.push_back({ parameterId, std::move(parameterValue) });
    }
    instance = std::move(parsed);
    return true;
}

const CompoundDefinition* FindExactCompoundDefinition(
    const std::vector<CompoundDefinition>& definitions,
    const DefinitionReference& reference) {
    const auto it = std::find_if(definitions.begin(), definitions.end(), [&](const CompoundDefinition& definition) {
        return SameDefinitionReference(definition.identity, reference);
    });
    return it == definitions.end() ? nullptr : &(*it);
}

CompoundResolutionStatus ResolveCompoundInstance(
    CompoundInstance& instance,
    const std::vector<CompoundDefinition>& definitions) {
    const CompoundDefinition* exact = FindExactCompoundDefinition(definitions, instance.definition);
    if (exact) {
        const std::vector<ContractIssue> issues = ValidateCompoundDefinition(*exact);
        if (!issues.empty()) {
            instance.resolution = CompoundResolutionStatus::InvalidDefinition;
            instance.resolutionError = issues.front().message;
            return instance.resolution;
        }
        instance.interfaceSnapshot = exact->ports;
        instance.resolution = CompoundResolutionStatus::Exact;
        instance.resolutionError.clear();
        return instance.resolution;
    }
    bool hasId = false;
    bool hasVersion = false;
    for (const CompoundDefinition& definition : definitions) {
        if (definition.identity.id != instance.definition.id) continue;
        hasId = true;
        if (definition.identity.version == instance.definition.version) hasVersion = true;
    }
    instance.resolution = !hasId ? CompoundResolutionStatus::Missing :
        (!hasVersion ? CompoundResolutionStatus::VersionMismatch : CompoundResolutionStatus::HashMismatch);
    instance.resolutionError = std::string("Exact compound definition is ") +
        CompoundResolutionStatusName(instance.resolution) + ".";
    return instance.resolution;
}

std::vector<DefinitionReference> CollectCompoundDependencyClosure(
    const std::vector<CompoundDefinition>& definitions,
    const std::vector<DefinitionReference>& roots,
    std::string* error) {
    std::vector<DefinitionReference> result;
    const auto referenceKey = [](const DefinitionReference& reference) {
        return reference.id + "@" +
            ToString(reference.version) + "#" +
            reference.contentHash;
    };

    std::unordered_map<std::string, const CompoundDefinition*> definitionsByKey;
    definitionsByKey.reserve(definitions.size());
    for (const CompoundDefinition& definition : definitions) {
        definitionsByKey.emplace(
            referenceKey(definition.identity),
            &definition);
    }

    enum class ClosureVisitState {
        Visiting,
        Visited
    };
    struct ClosureFrame {
        DefinitionReference reference;
        const CompoundDefinition* definition = nullptr;
        std::size_t nextDependency = 0;
        bool initialized = false;
    };

    std::unordered_map<std::string, ClosureVisitState> states;
    states.reserve(definitions.size());
    for (const DefinitionReference& root : roots) {
        const std::string rootKey = referenceKey(root);
        if (states.count(rootKey) > 0) {
            continue;
        }

        std::vector<ClosureFrame> pending;
        pending.push_back(ClosureFrame{ root });
        while (!pending.empty()) {
            ClosureFrame& frame = pending.back();
            const std::string key = referenceKey(frame.reference);
            if (!frame.initialized) {
                const auto state = states.find(key);
                if (state != states.end()) {
                    pending.pop_back();
                    continue;
                }
                const auto definition = definitionsByKey.find(key);
                if (definition == definitionsByKey.end()) {
                    if (error) {
                        *error =
                            "Exact compound dependency is missing: " +
                            frame.reference.id;
                    }
                    return {};
                }
                frame.definition = definition->second;
                frame.initialized = true;
                states.emplace(key, ClosureVisitState::Visiting);
            }

            if (frame.nextDependency <
                frame.definition->dependencies.size()) {
                const DefinitionReference& dependency =
                    frame.definition
                        ->dependencies[frame.nextDependency];
                const std::string dependencyKey =
                    referenceKey(dependency);
                const auto dependencyState =
                    states.find(dependencyKey);
                if (dependencyState == states.end()) {
                    pending.push_back(
                        ClosureFrame{ dependency });
                    continue;
                }
                if (dependencyState->second ==
                    ClosureVisitState::Visiting) {
                    if (error) {
                        *error =
                            "Recursive compound dependency is not allowed.";
                    }
                    return {};
                }
                ++frame.nextDependency;
                continue;
            }

            states[key] = ClosureVisitState::Visited;
            result.push_back(frame.reference);
            pending.pop_back();
            if (!pending.empty()) {
                ++pending.back().nextDependency;
            }
        }
    }
    if (error) {
        error->clear();
    }
    return result;
}

bool CreateUniqueCompoundDefinition(
    const CompoundDefinition& source,
    const std::string& label,
    CompoundDefinition& uniqueDefinition,
    std::string* error) {
    if (source.definitionClass == CompoundDefinitionClass::OpaqueSpecialized) {
        if (error) *error = "Opaque specialized definitions cannot be made into editable graph copies.";
        return false;
    }
    uniqueDefinition = source;
    uniqueDefinition.definitionUuid = GenerateCanonicalUuid();
    uniqueDefinition.identity.id = "project:compound/" + uniqueDefinition.definitionUuid;
    uniqueDefinition.identity.version = { 1, 0, 0 };
    uniqueDefinition.label = label.empty() ? source.label + " Copy" : label;
    uniqueDefinition.source = CompoundDefinitionSource::Embedded;
    uniqueDefinition.lifecycleNotes = "Created by Make Unique from " + source.identity.id + "@" +
        ToString(source.identity.version) + ".";
    RefreshCompoundDefinitionContentHash(uniqueDefinition);
    const std::vector<ContractIssue> issues = ValidateCompoundDefinition(uniqueDefinition);
    if (!issues.empty()) {
        if (error) *error = issues.front().message;
        return false;
    }
    return true;
}

bool CreateEditedCompoundDefinitionVersion(
    const CompoundDefinition& source,
    SemanticVersion newVersion,
    const nlohmann::json& editedCanonicalGraph,
    CompoundDefinition& editedDefinition,
    std::string* error) {
    if (source.definitionClass == CompoundDefinitionClass::OpaqueSpecialized ||
        !editedCanonicalGraph.is_object() || !editedCanonicalGraph.contains("nodeGraph")) {
        if (error) *error = "Only graph-defined compounds can create an edited graph version.";
        return false;
    }
    if (!VersionGreater(newVersion, source.identity.version)) {
        if (error) *error = "Edited compound version must be greater than the source version.";
        return false;
    }
    editedDefinition = source;
    editedDefinition.identity.version = newVersion;
    editedDefinition.canonicalGraph = editedCanonicalGraph;
    editedDefinition.source = CompoundDefinitionSource::Embedded;
    editedDefinition.lifecycleNotes = "Explicit edited version from " + ToString(source.identity.version) + ".";
    RefreshCompoundDefinitionContentHash(editedDefinition);
    const std::vector<ContractIssue> issues = ValidateCompoundDefinition(editedDefinition);
    if (!issues.empty()) {
        if (error) *error = issues.front().message;
        return false;
    }
    return true;
}

const ParameterValue* FindCompoundParameterOverride(
    const CompoundInstance& instance,
    const std::string& parameterId) {
    const auto it = std::find_if(
        instance.parameterOverrides.begin(), instance.parameterOverrides.end(),
        [&](const ParameterOverride& item) { return item.parameterId == parameterId; });
    return it == instance.parameterOverrides.end() ? nullptr : &it->value;
}

ParameterValue ResolveCompoundParameterValue(
    const CompoundInstance& instance,
    const CompoundParameterDefinition& parameter) {
    if (const ParameterValue* value = FindCompoundParameterOverride(instance, parameter.id)) return *value;
    return parameter.defaultValue;
}

void SetCompoundParameterOverride(
    CompoundInstance& instance,
    const std::string& parameterId,
    ParameterValue value) {
    auto it = std::find_if(
        instance.parameterOverrides.begin(), instance.parameterOverrides.end(),
        [&](const ParameterOverride& item) { return item.parameterId == parameterId; });
    if (it == instance.parameterOverrides.end()) {
        instance.parameterOverrides.push_back({ parameterId, std::move(value) });
    } else {
        it->value = std::move(value);
    }
}

} // namespace Stack::NodeMath
