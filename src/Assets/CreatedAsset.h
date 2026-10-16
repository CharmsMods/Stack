#pragma once
#include "ThirdParty/json.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Stack::Assets {
enum class Kind { DataRange, ScalarField, Texture, Mask };

// Immutable authored data. No GPU handles, editor selection or layer ownership.
struct FloatData {
    int width = 0, height = 0, channels = 1;
    std::string units;
    std::vector<float> values;
    bool Valid(bool coverage) const;
    std::size_t Fingerprint() const;
private:
    mutable std::once_flag m_Validation;
    mutable std::once_flag m_HashOnce;
    mutable std::size_t m_Fingerprint = 0;
    mutable bool m_Finite = false, m_Coverage = false;
};
struct Record {
    std::string id, name;
    Kind kind = Kind::Mask;
    std::shared_ptr<const FloatData> data;
    // Captures retain every sampled value as well as the derived coverage.
    std::shared_ptr<const FloatData> samples;
    nlohmann::json provenance = nlohmann::json::object();
};
using Library = std::vector<Record>;
bool SameLibrary(const Library& a, const Library& b);
bool ValidateLibrary(const Library& library, std::string& error);
nlohmann::json SerializeLibrary(const Library& library);
bool DeserializeLibrary(const nlohmann::json& value, Library& library, std::string& error);
} // namespace Stack::Assets
