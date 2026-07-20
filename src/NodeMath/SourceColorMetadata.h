#pragma once

#include "NodeMath/ContractTypes.h"
#include "ThirdParty/json.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Stack::NodeMath {

inline constexpr std::uint32_t kSourceColorMetadataSchemaVersion = 1;

enum class EmbeddedColorPayloadKind {
    None,
    PngIccpChunk,
    JpegIccProfile,
    PngColorSignal
};

// Descriptive source state only. Inspecting or attaching this record never
// changes decoded pixels.
struct SourceColorMetadata {
    std::uint32_t schemaVersion = kSourceColorMetadataSchemaVersion;
    EmbeddedColorPayloadKind payloadKind = EmbeddedColorPayloadKind::None;
    std::string label;
    std::string dependencyIdentity;
    std::vector<unsigned char> retainedPayload;
    ValueDescriptor descriptor;
    std::vector<ContractIssue> issues;
};

SourceColorMetadata InspectSourceColorMetadata(
    const std::vector<unsigned char>& encodedFile,
    int width,
    int height,
    int originalChannels,
    LogicalPrecision precision,
    const std::string& sourceIdentity);

nlohmann::json SerializeSourceColorMetadata(const SourceColorMetadata& metadata);
bool ParseSourceColorMetadata(
    const nlohmann::json& value,
    SourceColorMetadata& metadata,
    std::vector<ContractIssue>& issues);

std::string SourceColorDependencyIdentity(const SourceColorMetadata& metadata);

} // namespace Stack::NodeMath
