#pragma once

#include "ThirdParty/json.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Stack::Restormer {

enum class Operation {
    Health,
    Denoise,
    Cancel,
    Shutdown
};

enum class Quality {
    InteractivePreview,
    Settled
};

struct ProtocolRequest {
    int protocolVersion = 1;
    std::string requestId;
    Operation operation = Operation::Health;
    std::uint64_t generation = 0;
    std::string modelKind;
    Quality quality = Quality::InteractivePreview;
    int width = 0;
    int height = 0;
    int channels = 3;
    std::size_t byteSize = 0;
    std::string inputMappingName;
    std::string outputMappingName;
};

struct ProtocolResponse {
    int protocolVersion = 1;
    std::string requestId;
    std::uint64_t generation = 0;
    std::string state;
    bool ok = false;
    std::string error;
    std::string provider;
    double inferenceMilliseconds = 0.0;
    int completedTiles = 0;
    int totalTiles = 0;
};

const char* OperationStableString(Operation operation);
const char* QualityStableString(Quality quality);
bool ParseProtocolRequest(
    const nlohmann::json& value,
    ProtocolRequest& request,
    std::string& error);
nlohmann::json SerializeProtocolRequest(const ProtocolRequest& request);
nlohmann::json SerializeProtocolResponse(const ProtocolResponse& response);
bool ParseProtocolResponse(
    const nlohmann::json& value,
    ProtocolResponse& response,
    std::string& error);

} // namespace Stack::Restormer
