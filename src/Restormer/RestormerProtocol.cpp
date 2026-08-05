#include "Restormer/RestormerProtocol.h"

#include <algorithm>
#include <array>
#include <limits>

namespace Stack::Restormer {
namespace {

bool ParseOperation(const std::string& value, Operation& operation) {
    if (value == "health") {
        operation = Operation::Health;
    } else if (value == "denoise") {
        operation = Operation::Denoise;
    } else if (value == "cancel") {
        operation = Operation::Cancel;
    } else if (value == "shutdown") {
        operation = Operation::Shutdown;
    } else {
        return false;
    }
    return true;
}

bool ParseQuality(const std::string& value, Quality& quality) {
    if (value == "interactive-preview") {
        quality = Quality::InteractivePreview;
    } else if (value == "settled") {
        quality = Quality::Settled;
    } else {
        return false;
    }
    return true;
}

bool ValidMappingName(const std::string& value) {
    constexpr const char* prefix = "Local\\StackRestormer-";
    return value.size() > 24 &&
        value.size() <= 240 &&
        value.rfind(prefix, 0) == 0 &&
        value.find_first_not_of(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.\\") ==
            std::string::npos;
}

template <std::size_t N>
bool HasOnlyKeys(
    const nlohmann::json& value,
    const std::array<const char*, N>& allowed) {
    for (const auto& [key, ignored] : value.items()) {
        (void)ignored;
        if (std::find(allowed.begin(), allowed.end(), key) ==
            allowed.end()) {
            return false;
        }
    }
    return true;
}

} // namespace

const char* OperationStableString(Operation operation) {
    switch (operation) {
        case Operation::Health: return "health";
        case Operation::Denoise: return "denoise";
        case Operation::Cancel: return "cancel";
        case Operation::Shutdown: return "shutdown";
    }
    return "health";
}

const char* QualityStableString(Quality quality) {
    switch (quality) {
        case Quality::InteractivePreview: return "interactive-preview";
        case Quality::Settled: return "settled";
    }
    return "interactive-preview";
}

bool ParseProtocolRequest(
    const nlohmann::json& value,
    ProtocolRequest& request,
    std::string& error) {
    if (!value.is_object()) {
        error = "Control request must be a JSON object.";
        return false;
    }
    static constexpr std::array<const char*, 12> allowedKeys {
        "protocolVersion",
        "requestId",
        "operation",
        "generation",
        "modelKind",
        "quality",
        "width",
        "height",
        "channels",
        "byteSize",
        "inputMappingName",
        "outputMappingName"
    };
    if (!HasOnlyKeys(value, allowedKeys)) {
        error = "Control request contains an unsupported field.";
        return false;
    }
    try {
    request.protocolVersion = value.value("protocolVersion", 0);
    request.requestId = value.value("requestId", std::string());
    request.generation = value.value("generation", std::uint64_t { 0 });
    request.modelKind = value.value("modelKind", std::string());
    request.width = value.value("width", 0);
    request.height = value.value("height", 0);
    request.channels = value.value("channels", 0);
    request.byteSize = value.value("byteSize", std::size_t { 0 });
    request.inputMappingName =
        value.value("inputMappingName", std::string());
    request.outputMappingName =
        value.value("outputMappingName", std::string());
    if (request.protocolVersion != 1 || request.requestId.empty() ||
        request.requestId.size() > 128) {
        error = "Control request identity or protocol is invalid.";
        return false;
    }
    if (!ParseOperation(value.value("operation", std::string()), request.operation)) {
        error = "Control request operation is unknown.";
        return false;
    }
    if (request.operation != Operation::Denoise) {
        return true;
    }
    if (!ParseQuality(value.value("quality", std::string()), request.quality) ||
        (request.modelKind != "real-photo" &&
         request.modelKind != "gaussian-blind") ||
        request.width <= 0 || request.height <= 0 ||
        request.width > 32768 || request.height > 32768 ||
        request.channels != 3) {
        error = "Denoise dimensions, model, or quality are invalid.";
        return false;
    }
    const std::uint64_t expected =
        static_cast<std::uint64_t>(request.width) *
        static_cast<std::uint64_t>(request.height) *
        3ULL *
        sizeof(float);
    if (expected > static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()) ||
        request.byteSize != static_cast<std::size_t>(expected) ||
        !ValidMappingName(request.inputMappingName) ||
        !ValidMappingName(request.outputMappingName)) {
        error = "Denoise shared-memory contract is invalid.";
        return false;
    }
    return true;
    } catch (const nlohmann::json::exception&) {
        error = "Control request fields use invalid JSON types.";
        return false;
    }
}

nlohmann::json SerializeProtocolRequest(const ProtocolRequest& request) {
    return {
        { "protocolVersion", request.protocolVersion },
        { "requestId", request.requestId },
        { "operation", OperationStableString(request.operation) },
        { "generation", request.generation },
        { "modelKind", request.modelKind },
        { "quality", QualityStableString(request.quality) },
        { "width", request.width },
        { "height", request.height },
        { "channels", request.channels },
        { "byteSize", request.byteSize },
        { "inputMappingName", request.inputMappingName },
        { "outputMappingName", request.outputMappingName }
    };
}

nlohmann::json SerializeProtocolResponse(const ProtocolResponse& response) {
    return {
        { "protocolVersion", response.protocolVersion },
        { "requestId", response.requestId },
        { "generation", response.generation },
        { "state", response.state },
        { "ok", response.ok },
        { "error", response.error },
        { "provider", response.provider },
        { "inferenceMilliseconds", response.inferenceMilliseconds },
        { "completedTiles", response.completedTiles },
        { "totalTiles", response.totalTiles }
    };
}

bool ParseProtocolResponse(
    const nlohmann::json& value,
    ProtocolResponse& response,
    std::string& error) {
    if (!value.is_object()) {
        error = "Control response must be a JSON object.";
        return false;
    }
    static constexpr std::array<const char*, 10> allowedKeys {
        "protocolVersion",
        "requestId",
        "generation",
        "state",
        "ok",
        "error",
        "provider",
        "inferenceMilliseconds",
        "completedTiles",
        "totalTiles"
    };
    if (!HasOnlyKeys(value, allowedKeys)) {
        error = "Control response contains an unsupported field.";
        return false;
    }
    try {
    response.protocolVersion = value.value("protocolVersion", 0);
    response.requestId = value.value("requestId", std::string());
    response.generation = value.value("generation", std::uint64_t { 0 });
    response.state = value.value("state", std::string());
    response.ok = value.value("ok", false);
    response.error = value.value("error", std::string());
    response.provider = value.value("provider", std::string());
    response.inferenceMilliseconds =
        value.value("inferenceMilliseconds", 0.0);
    response.completedTiles = value.value("completedTiles", 0);
    response.totalTiles = value.value("totalTiles", 0);
    if (response.protocolVersion != 1 || response.requestId.empty() ||
        response.state.empty()) {
        error = "Control response identity or protocol is invalid.";
        return false;
    }
    return true;
    } catch (const nlohmann::json::exception&) {
        error = "Control response fields use invalid JSON types.";
        return false;
    }
}

} // namespace Stack::Restormer
