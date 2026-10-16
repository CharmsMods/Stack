#include "Assets/CreatedAsset.h"
#include "Utils/HashUtils.h"
#include <cstring>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace Stack::Assets {
namespace {
using Json = nlohmann::json;
constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
// Float32 little-endian bytes, stored as base64 so text manifests stay compact.
std::string Encode(const std::vector<float>& values) {
    std::string result;
    result.reserve((values.size() * 4 + 2) / 3 * 4);
    std::uint32_t bits = 0; int count = 0;
    for (float value : values) {
        std::uint32_t word = 0;
        static_assert(sizeof(float) == sizeof(word), "Asset storage requires float32.");
        std::memcpy(&word,&value,sizeof(word));
        for (int i = 0; i < 4; ++i) {
            bits = (bits << 8) | ((word >> (i * 8)) & 255u); count += 8;
            while (count >= 6) { count -= 6; result += Alphabet[(bits >> count) & 63u]; }
        }
    }
    if (count) result += Alphabet[(bits << (6 - count)) & 63u];
    while (result.size() % 4) result += '=';
    return result;
}
std::size_t Count(const FloatData& data) {
    if (data.width <= 0 || data.height <= 0 || data.channels < 1 || data.channels > 4)
        throw std::runtime_error("Invalid asset dimensions or channels.");
    const auto count = std::uint64_t(data.width) * data.height * data.channels;
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(float))
        throw std::runtime_error("Asset dimensions overflow storage.");
    return static_cast<std::size_t>(count);
}
Json SaveData(const std::shared_ptr<const FloatData>& data) {
    if (!data) return nullptr;
    return {{"width",data->width},{"height",data->height},{"channels",data->channels},
        {"units",data->units},{"float32le",Encode(data->values)}};
}
std::shared_ptr<const FloatData> ReadData(const Json& value) {
    if (value.is_null()) return {};
    auto data = std::make_shared<FloatData>();
    data->width = value.at("width"); data->height = value.at("height");
    data->channels = value.at("channels"); data->units = value.at("units");
    const auto count = Count(*data);
    const auto& encoded = value.at("float32le").get_ref<const std::string&>();
    const auto bytes = count * sizeof(float);
    if (bytes > (std::numeric_limits<std::size_t>::max() - 2) / 4 * 3 ||
        encoded.size() != (bytes + 2) / 3 * 4)
        throw std::runtime_error("Invalid asset byte count.");
    data->values.reserve(count);
    std::uint32_t bits = 0, word = 0; int bitCount = 0, byteCount = 0;
    std::size_t decoded = 0;
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        const char c = encoded[i];
        if (c == '=') {
            const auto padding = (3 - bytes % 3) % 3;
            if (i < encoded.size() - padding) throw std::runtime_error("Invalid asset padding.");
            continue;
        }
        if (i >= encoded.size() - (3 - bytes % 3) % 3)
            throw std::runtime_error("Missing asset padding.");
        const auto found = std::string_view(Alphabet).find(c);
        if (found == std::string_view::npos) throw std::runtime_error("Invalid asset encoding.");
        bits = (bits << 6) | std::uint32_t(found); bitCount += 6;
        if (bitCount >= 8) {
            bitCount -= 8;
            word |= ((bits >> bitCount) & 255u) << (byteCount++ * 8); ++decoded;
            if (byteCount == 4) { float value = 0; std::memcpy(&value,&word,sizeof(word)); data->values.push_back(value); word = 0; byteCount = 0; }
        }
    }
    if (decoded != bytes || byteCount || (bitCount && (bits & ((1u << bitCount) - 1))))
        throw std::runtime_error("Truncated or invalid asset data.");
    return data;
}
} // namespace
bool FloatData::Valid(bool coverage) const {
    std::call_once(m_Validation,[this] {
        m_Finite = m_Coverage = true;
        for (float value : values) {
            m_Finite &= std::isfinite(value);
            m_Coverage &= std::isfinite(value) && value >= 0 && value <= 1;
        }
    });
    return coverage ? m_Coverage : m_Finite;
}
std::size_t FloatData::Fingerprint() const {
    std::call_once(m_HashOnce,[this] {
        if(!values.empty()) m_Fingerprint=StackHash::HashBytes(
            reinterpret_cast<const unsigned char*>(values.data()),values.size()*sizeof(float));
    });
    return m_Fingerprint;
}
bool SameLibrary(const Library& a, const Library& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i=0;i<a.size();++i)
        if (a[i].id != b[i].id || a[i].name != b[i].name || a[i].kind != b[i].kind ||
            a[i].data != b[i].data || a[i].samples != b[i].samples || a[i].provenance != b[i].provenance) return false;
    return true;
}
bool ValidateLibrary(const Library& library, std::string& error) {
    try {
        std::unordered_set<std::string> ids;
        for (const auto& asset : library) {
            if (asset.id.empty() || asset.name.empty() || !ids.insert(asset.id).second || !asset.data ||
                !asset.provenance.is_object() || static_cast<int>(asset.kind)<0 || static_cast<int>(asset.kind)>static_cast<int>(Kind::Mask))
                throw std::runtime_error("Invalid created asset identity or payload.");
            for (const auto& data : {asset.data, asset.samples}) if (data) {
                if (data->values.size() != Count(*data)) throw std::runtime_error("Invalid asset data size.");
                if (!data->Valid(data == asset.data && asset.kind == Kind::Mask))
                    throw std::runtime_error("Invalid asset value.");
            }
            if (asset.kind == Kind::Mask && asset.data->channels != 1)
                throw std::runtime_error("A mask asset must have one coverage channel.");
        }
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
Json SerializeLibrary(const Library& library) {
    Json records = Json::array();
    for (const auto& asset : library) records.push_back({{"id",asset.id},{"name",asset.name},
        {"kind",static_cast<int>(asset.kind)},{"data",SaveData(asset.data)},
        {"samples",SaveData(asset.samples)},{"provenance",asset.provenance}});
    return {{"version",1},{"records",std::move(records)}};
}
bool DeserializeLibrary(const Json& value, Library& library, std::string& error) {
    try {
        if (value.at("version") != 1 || !value.at("records").is_array())
            throw std::runtime_error("Unsupported created asset library.");
        Library candidate;
        for (const auto& item : value.at("records")) {
            const int kind = item.at("kind");
            if (kind < 0 || kind > static_cast<int>(Kind::Mask)) throw std::runtime_error("Unsupported asset kind.");
            candidate.push_back({item.at("id"),item.at("name"),static_cast<Kind>(kind),
                ReadData(item.at("data")),ReadData(item.at("samples")),item.at("provenance")});
        }
        if (!ValidateLibrary(candidate,error)) return false;
        library = std::move(candidate); return true;
    } catch (const std::exception& e) { error = std::string("Cannot read created assets: ") + e.what(); return false; }
}
} // namespace Stack::Assets
