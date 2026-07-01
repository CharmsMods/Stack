#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "ThirdParty/json.hpp"

#include <string>
#include <vector>

namespace Stack::RawWorkspace {

inline constexpr int kManagedRawSectionSchemaVersion = 1;
inline constexpr const char* kManagedRawValidatorVersion = "managed-raw-v1";
inline constexpr const char* kCustomGraphReadOnlyReason =
    "This RAW chain has been customized in the graph. RAW tab editing is read-only for this image until the chain is repaired or re-adopted.";

struct ManagedRawSection {
    int schemaVersion = kManagedRawSectionSchemaVersion;
    std::string sectionId;
    std::string projectLocalId;
    std::string sourceRelativePathKey;
    std::string sourceFingerprint;
    int groupId = -1;
    int rawSourceNodeId = -1;
    int rawDecodeNodeId = -1;
    int toneCurveNodeId = -1;
    int viewTransformNodeId = -1;
    std::vector<int> orderedNodeIds;
    std::vector<int> lockedFoundationNodeIds;
    std::string sectionOutputSocketId = EditorNodeGraph::kImageOutputSocketId;
    std::string validatorVersion = kManagedRawValidatorVersion;
};

struct ManagedRawValidationResult {
    bool valid = false;
    bool repairable = false;
    std::string message;
    Stack::RawRecipe::RawDevelopmentRecipe recipe;
};

struct ManagedRawRepairResult {
    bool repaired = false;
    bool changed = false;
    std::string message;
    ManagedRawValidationResult validation;
};

struct ManagedRawGraphMutationWarning {
    bool requiresConfirmation = false;
    std::string summary;
    std::string detail;
};

bool IsRecipeRepresentableAsManagedGraph(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    std::string* outReason = nullptr);

ManagedRawSection BuildManagedRawSection(
    std::string sectionId,
    std::string projectLocalId,
    std::string sourceRelativePathKey,
    std::string sourceFingerprint,
    int groupId,
    int rawSourceNodeId,
    int rawDecodeNodeId,
    int toneCurveNodeId,
    int viewTransformNodeId);

nlohmann::json SerializeManagedRawSection(const ManagedRawSection& section);
ManagedRawSection DeserializeManagedRawSection(const nlohmann::json& value);

ManagedRawValidationResult ValidateManagedRawSection(
    const EditorNodeGraph::Graph& graph,
    const ManagedRawSection& section,
    const Stack::RawRecipe::RawDevelopmentRecipe& baseRecipe);

ManagedRawRepairResult RepairManagedRawSectionGraph(
    EditorNodeGraph::Graph& graph,
    const ManagedRawSection& section,
    const Stack::RawRecipe::RawDevelopmentRecipe& baseRecipe);

ManagedRawGraphMutationWarning BuildManagedRawGraphConnectionWarning(
    const ManagedRawSection& section,
    int fromNodeId,
    const std::string& fromSocketId,
    int toNodeId,
    const std::string& toSocketId);

ManagedRawGraphMutationWarning BuildManagedRawGraphLinkRemovalWarning(
    const ManagedRawSection& section,
    int fromNodeId,
    const std::string& fromSocketId,
    int toNodeId,
    const std::string& toSocketId);

ManagedRawGraphMutationWarning BuildManagedRawGraphNodeRemovalWarning(
    const ManagedRawSection& section,
    int nodeId);

bool TryBuildManagedRawSectionFromGraph(
    const EditorNodeGraph::Graph& graph,
    const Stack::RawRecipe::RawDevelopmentRecipe& baseRecipe,
    ManagedRawSection& outSection,
    Stack::RawRecipe::RawDevelopmentRecipe& outRecipe,
    std::string* outReason = nullptr);

Stack::RawRecipe::RawDevelopmentRecipe BuildRecipeFromManagedRawGraph(
    const EditorNodeGraph::Graph& graph,
    const ManagedRawSection& section,
    const Stack::RawRecipe::RawDevelopmentRecipe& baseRecipe);

} // namespace Stack::RawWorkspace
