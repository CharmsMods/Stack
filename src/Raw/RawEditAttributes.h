#pragma once

#include "Raw/RawDevelopmentRecipe.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Stack::RawRecipe {

inline constexpr std::uint32_t kRawEditAttributeBundleVersion = 1;
inline constexpr const char* kRawEditAttributeBundleSchema =
    "stack.rawEditAttributes";

struct RawEditAttributeDescriptor {
    const char* key = "";
    const char* groupKey = "";
    const char* groupLabel = "";
    const char* label = "";
    const char* description = "";
    bool selectedByDefault = true;
    bool spatiallySpecific = false;
};

struct RawEditAttributeBundle {
    std::uint32_t version = kRawEditAttributeBundleVersion;
    std::string sourceLabel;
    std::string sourceKind = "single-raw";
    std::string viewTransformPlacement = "internal";
    RawDevelopmentRecipe recipe;
};

struct RawEditAttributeApplyResult {
    bool success = false;
    bool changed = false;
    std::vector<std::string> appliedKeys;
    std::vector<std::string> warnings;
    std::string errorMessage;
};

struct RawEditAttributeTargetCompatibility {
    bool postCfaMerge = false;
    bool hdrMerge = false;
    bool graphViewTransform = false;
    bool canChangeViewTransformPlacement = false;
    bool canUpdateGraphViewTransform = false;
};

struct RawEditAttributeSelectionPlan {
    std::vector<std::string> applicableKeys;
    std::vector<std::string> skippedKeys;
    std::vector<std::string> warnings;
};

// Stable leaf descriptors used by persistence, validation, and every UI host.
// Group rows are presentation only; recipes are always applied through these
// leaf keys so a checked parent can never become a second mutation path.
const std::vector<RawEditAttributeDescriptor>& RawEditAttributeDescriptors();
const RawEditAttributeDescriptor* FindRawEditAttributeDescriptor(
    const std::string& key);
std::vector<std::string> DefaultRawEditAttributeSelection();
std::vector<std::string> AllRawEditAttributeKeys();
RawEditAttributeSelectionPlan PlanRawEditAttributeSelectionForTarget(
    const RawEditAttributeBundle& bundle,
    const std::vector<std::string>& selectedKeys,
    const RawEditAttributeTargetCompatibility& compatibility);

RawEditAttributeBundle CaptureRawEditAttributeBundle(
    const RawDevelopmentRecipe& recipe,
    std::string sourceLabel,
    std::string sourceKind = "single-raw",
    std::string viewTransformPlacement = "internal");
nlohmann::json SerializeRawEditAttributeBundle(
    const RawEditAttributeBundle& bundle);
bool DeserializeRawEditAttributeBundle(
    const nlohmann::json& value,
    RawEditAttributeBundle& bundle,
    std::string* errorMessage = nullptr);

RawEditAttributeApplyResult ApplyRawEditAttributeBundle(
    const RawEditAttributeBundle& bundle,
    const std::vector<std::string>& selectedKeys,
    RawDevelopmentRecipe& targetRecipe,
    std::string* targetViewTransformPlacement = nullptr);

} // namespace Stack::RawRecipe
