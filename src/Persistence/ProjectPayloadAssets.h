#pragma once
#include "ThirdParty/json.hpp"
#include <functional>
#include <vector>

namespace Stack::Project {
// Walk every container, including compound and layer graphs. Payload owners
// retain the interpretation of bytes; persistence only stores their identity.
using PayloadAssetWriter = std::function<bool(const std::vector<std::uint8_t>&, std::string&)>;
using PayloadAssetReader = std::function<bool(const std::string&, std::vector<std::uint8_t>&)>;
inline bool ExternalizePayloadBytes(nlohmann::json& value, const PayloadAssetWriter& write) {
    const bool wrapped = value.is_object() && value.size() == 1 && value.contains("$rawLayerBytes");
    if (value.is_binary() || wrapped) {
        std::vector<std::uint8_t> bytes;
        if (wrapped) {
            const auto& encoded = value.at("$rawLayerBytes");
            if (!encoded.is_array()) return false;
            bytes.reserve(encoded.size());
            for (const auto& byte : encoded) {
                if (!byte.is_number_integer() || byte.get<std::int64_t>() < 0 || byte.get<std::uint64_t>() > 255) return false;
                bytes.push_back(byte.get<std::uint8_t>());
            }
        } else {
            const auto& binary = value.get_binary();
            bytes.assign(binary.begin(), binary.end());
        }
        if (bytes.empty()) { value = {{"$stackEmptyBytes", true}}; return true; }
        std::string id;
        if (!write(bytes, id)) return false;
        value = {{"$stackAsset", id}};
    } else if (value.is_structured()) {
        for (auto& child : value) if (!ExternalizePayloadBytes(child, write)) return false;
    }
    return true;
}
inline bool RehydratePayloadBytes(nlohmann::json& value, const PayloadAssetReader& read) {
    if (value.is_object() && value.contains("$stackAsset")) {
        if (value.size() != 1 || !value.at("$stackAsset").is_string()) return false;
        std::vector<std::uint8_t> bytes;
        if (!read(value.at("$stackAsset").get<std::string>(), bytes)) return false;
        value = nlohmann::json::binary(std::move(bytes));
    } else if (value.is_object() && value.contains("$stackEmptyBytes")) {
        if (value.size() != 1 || value.at("$stackEmptyBytes") != true) return false;
        value = nlohmann::json::binary(std::vector<std::uint8_t>{});
    } else if (value.is_structured()) {
        for (auto& child : value) if (!RehydratePayloadBytes(child, read)) return false;
    }
    return true;
}
} // namespace Stack::Project
