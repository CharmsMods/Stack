#include "NodeMath/CompoundDefinition.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace Stack::NodeMath;

int gChecks = 0;

bool Check(bool condition, const std::string& message) {
    ++gChecks;
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

CompoundDefinition BuildDefinition(
    const std::string& id,
    const std::string& uuid,
    CompoundDefinitionClass definitionClass = CompoundDefinitionClass::TransparentGraph) {
    CompoundDefinition definition;
    definition.definitionUuid = uuid;
    definition.identity.id = id;
    definition.identity.version = { 1, 0, 0 };
    definition.label = "Phase 5 Test Compound";
    definition.description = "Small exact graph-defined contract used by Phase 5 tests.";
    definition.definitionClass = definitionClass;
    definition.ports = {
        { "image-in", "Image", PortDirection::Input, LogicalValueType::ColorImage, false,
          "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "imageIn" },
        { "image-out", "Image", PortDirection::Output, LogicalValueType::ColorImage, false,
          "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", "imageOut" }
    };
    definition.parameters = {
        { "amount", "Amount", ParameterType::Scalar, 1.0,
          { true, -16.0, 16.0 }, "drag", "unitless",
          "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", "dataMathSettings.constantB" }
    };
    definition.canonicalGraph = {
        { "schemaVersion", 6 },
        { "nodeGraph", {
            { "allowNoOutput", true },
            { "nodes", nlohmann::json::array({
                { { "id", 1 }, { "instanceUuid", "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa" } },
                { { "id", 2 }, { "instanceUuid", "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb" } }
            }) },
            { "links", nlohmann::json::array() }
        } }
    };
    definition.unpackable = definitionClass != CompoundDefinitionClass::OpaqueSpecialized;
    if (definitionClass == CompoundDefinitionClass::GraphDefinedOptimizedEquivalent) {
        definition.optimizedImplementationId = "phase5-test-optimized";
        definition.absoluteTolerance = 1.0e-6;
        definition.relativeTolerance = 1.0e-6;
        definition.equivalenceEvidenceIds = { "NMR-P5-OPT-EQUIVALENCE" };
    } else if (definitionClass == CompoundDefinitionClass::OpaqueSpecialized) {
        definition.canonicalGraph = nlohmann::json::object();
    }
    RefreshCompoundDefinitionContentHash(definition);
    return definition;
}

bool HasIssue(const std::vector<ContractIssue>& issues, const std::string& field) {
    return std::any_of(issues.begin(), issues.end(), [&](const ContractIssue& issue) {
        return issue.field == field;
    });
}

bool RunTests() {
    bool ok = true;
    const CompoundDefinition base = BuildDefinition(
        "stack:test/phase5-base", "11111111-1111-4111-8111-111111111111");

    ok &= Check(ValidateCompoundDefinition(base).empty(),
        "transparent graph definition satisfies the Phase 5 contract");
    ok &= Check(IsValidContentHash(base.identity.contentHash),
        "compound semantic content has an exact SHA-256 identity");

    const nlohmann::json serialized = SerializeCompoundDefinition(base);
    CompoundDefinition loaded;
    std::string error;
    ok &= Check(DeserializeCompoundDefinition(serialized, loaded, &error),
        "compound definition survives serialization");
    ok &= Check(SameDefinitionReference(base.identity, loaded.identity) &&
        loaded.definitionUuid == base.definitionUuid && loaded.ports.size() == base.ports.size() &&
        loaded.ports.front().id == base.ports.front().id &&
        loaded.ports.back().internalInstanceUuid == base.ports.back().internalInstanceUuid,
        "definition, stable UUID, and stable interface survive round-trip");

    CompoundInstance instance;
    instance.instanceUuid = "22222222-2222-4222-8222-222222222222";
    instance.definition = base.identity;
    instance.interfaceSnapshot = base.ports;
    SetCompoundParameterOverride(instance, "amount", 2.5);
    std::vector<CompoundDefinition> catalog { base };
    ok &= Check(ResolveCompoundInstance(instance, catalog) == CompoundResolutionStatus::Exact,
        "an instance resolves only its exact ID, version, and content hash");
    ok &= Check(std::get<double>(*FindCompoundParameterOverride(instance, "amount")) == 2.5,
        "promoted parameter override is stored by stable parameter ID");

    CompoundInstance instanceLoaded;
    ok &= Check(DeserializeCompoundInstance(SerializeCompoundInstance(instance), instanceLoaded, &error),
        "compound instance survives serialization");
    ok &= Check(instanceLoaded.interfaceSnapshot.size() == 2 &&
        instanceLoaded.interfaceSnapshot.front().id == "image-in",
        "interface snapshot survives independently of definition resolution");

    CompoundInstance missing = instance;
    ok &= Check(ResolveCompoundInstance(missing, {}) == CompoundResolutionStatus::Missing,
        "missing embedded definition remains unresolved");
    ok &= Check(missing.interfaceSnapshot.size() == instance.interfaceSnapshot.size() &&
        missing.interfaceSnapshot.front().id == instance.interfaceSnapshot.front().id,
        "missing definition does not erase preserved ports or connections");

    CompoundInstance newer = instance;
    newer.definition.version = { 2, 0, 0 };
    ok &= Check(ResolveCompoundInstance(newer, catalog) == CompoundResolutionStatus::VersionMismatch,
        "forward/newer version is not silently substituted");
    CompoundInstance tampered = instance;
    tampered.definition.contentHash.assign(64, '0');
    ok &= Check(ResolveCompoundInstance(tampered, catalog) == CompoundResolutionStatus::HashMismatch,
        "same ID and version with different content remains unresolved");

    CompoundDefinition dependency = BuildDefinition(
        "stack:test/phase5-dependency", "33333333-3333-4333-8333-333333333333");
    CompoundDefinition parent = BuildDefinition(
        "stack:test/phase5-parent", "44444444-4444-4444-8444-444444444444");
    parent.dependencies.push_back(dependency.identity);
    RefreshCompoundDefinitionContentHash(parent);
    catalog = { dependency, parent };
    const std::vector<DefinitionReference> closure =
        CollectCompoundDependencyClosure(catalog, { parent.identity }, &error);
    ok &= Check(closure.size() == 2 &&
        SameDefinitionReference(closure.front(), dependency.identity) &&
        SameDefinitionReference(closure.back(), parent.identity),
        "copied/project closure embeds dependencies before the exact root definition");

    CompoundDefinition unique;
    ok &= Check(CreateUniqueCompoundDefinition(parent, "Unique Parent", unique, &error),
        "Make Unique creates a valid independent definition");
    ok &= Check(unique.definitionUuid != parent.definitionUuid &&
        unique.identity.id != parent.identity.id &&
        unique.identity.contentHash != parent.identity.contentHash &&
        unique.canonicalGraph == parent.canonicalGraph,
        "Make Unique preserves math but gives ownership a new stable identity");

    CompoundDefinition edited;
    nlohmann::json editedGraph = parent.canonicalGraph;
    editedGraph["nodeGraph"]["editMarker"] = "deliberate-change";
    ok &= Check(CreateEditedCompoundDefinitionVersion(
            parent, { 1, 1, 0 }, editedGraph, edited, &error),
        "definition editing creates a deliberate new exact version");
    ok &= Check(edited.identity.id == parent.identity.id &&
        edited.identity.version == SemanticVersion{ 1, 1, 0 } &&
        edited.identity.contentHash != parent.identity.contentHash,
        "definition edit retains family ID but changes version and semantic hash");

    CompoundDefinition optimized = BuildDefinition(
        "stack:test/phase5-optimized", "55555555-5555-4555-8555-555555555555",
        CompoundDefinitionClass::GraphDefinedOptimizedEquivalent);
    ok &= Check(ValidateCompoundDefinition(optimized).empty(),
        "optimized-equivalent definition requires canonical graph, tolerance, and evidence");
    optimized.equivalenceEvidenceIds.clear();
    RefreshCompoundDefinitionContentHash(optimized);
    ok &= Check(HasIssue(ValidateCompoundDefinition(optimized), "equivalenceEvidenceIds"),
        "optimized shortcut without equivalence evidence is rejected");

    CompoundDefinition opaque = BuildDefinition(
        "stack:test/phase5-opaque", "66666666-6666-4666-8666-666666666666",
        CompoundDefinitionClass::OpaqueSpecialized);
    ok &= Check(ValidateCompoundDefinition(opaque).empty() && !opaque.unpackable,
        "opaque specialized definition is valid only when it cannot unpack");

    CompoundDefinition recursiveA = base;
    CompoundDefinition recursiveB = dependency;
    recursiveA.dependencies = { recursiveB.identity };
    RefreshCompoundDefinitionContentHash(recursiveA);
    recursiveB.dependencies = { recursiveA.identity };
    RefreshCompoundDefinitionContentHash(recursiveB);
    // Refresh A once more so its dependency reference points at B's final hash.
    recursiveA.dependencies = { recursiveB.identity };
    RefreshCompoundDefinitionContentHash(recursiveA);
    // The exact-hash cycle cannot be made self-consistent; it is rejected as a
    // missing exact dependency before it can ever become executable recursion.
    const auto recursiveIssues = ValidateCompoundCatalog({ recursiveA, recursiveB });
    ok &= Check(HasIssue(recursiveIssues, "catalog.missingDependency") ||
        HasIssue(recursiveIssues, "catalog.recursion"),
        "recursive or hash-inconsistent dependency catalogs are rejected");

    constexpr int kDeepDependencyCount = 2048;
    std::vector<CompoundDefinition> deepCatalog;
    deepCatalog.reserve(kDeepDependencyCount);
    DefinitionReference downstreamReference;
    for (int index = kDeepDependencyCount - 1; index >= 0; --index) {
        char uuid[37];
        std::snprintf(
            uuid,
            sizeof(uuid),
            "%08x-0000-4000-8000-%012llx",
            0x10000000 + index,
            static_cast<unsigned long long>(index + 1));
        CompoundDefinition definition = BuildDefinition(
            "stack:test/deep-compound-" + std::to_string(index),
            uuid);
        if (!downstreamReference.id.empty()) {
            definition.dependencies.push_back(downstreamReference);
            RefreshCompoundDefinitionContentHash(definition);
        }
        downstreamReference = definition.identity;
        deepCatalog.push_back(std::move(definition));
    }
    ok &= Check(
        ValidateCompoundCatalog(deepCatalog).empty(),
        "deep acyclic compound catalogs validate without recursive traversal");
    const std::vector<DefinitionReference> deepClosure =
        CollectCompoundDependencyClosure(
            deepCatalog,
            { downstreamReference },
            &error);
    ok &= Check(
        deepClosure.size() ==
            static_cast<std::size_t>(kDeepDependencyCount) &&
            SameDefinitionReference(
                deepClosure.back(),
                downstreamReference),
        "deep compound dependency closure uses an explicit postorder stack");

    return ok;
}

} // namespace

int main() {
    const bool ok = RunTests();
    if (ok) {
        std::cout << "Phase 5 compound contract tests passed (" << gChecks << " checks).\n";
        return 0;
    }
    std::cerr << "Phase 5 compound contract tests failed after " << gChecks << " checks.\n";
    return 1;
}
