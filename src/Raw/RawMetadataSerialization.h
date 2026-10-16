#pragma once
#include "Raw/RawImageData.h"
#include "ThirdParty/json.hpp"

namespace Raw::Serialization {
// Stable metadata representation shared by project persistence and engine IPC.
nlohmann::json SerializeMetadata(const RawMetadata& metadata);
RawMetadata DeserializeMetadata(const nlohmann::json& value);
}
