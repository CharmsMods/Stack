#include "DngTiffReader.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Raw::Internal {
namespace {

constexpr std::size_t kMaxIfds = 64;
constexpr std::size_t kMaxNumberValues = 1024 * 1024;
constexpr std::uint64_t kMaxPayloadBytes = 64 * 1024 * 1024;

std::uint64_t UnsignedValue(const std::uint8_t* bytes, std::size_t size, bool littleEndian) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < size; ++i) {
        value = (value << 8) | bytes[littleEndian ? size - 1 - i : i];
    }
    return value;
}

} // namespace

DngTiffReader::DngTiffReader(const std::filesystem::path& path, std::function<bool()> shouldCancel)
    : m_File(path, std::ios::binary | std::ios::ate), m_ShouldCancel(std::move(shouldCancel)) {
    if (Cancelled()) return;
    if (!m_File) {
        Warn("DNG supplement parser could not open file.");
        return;
    }
    const auto end = m_File.tellg();
    if (end < 0) {
        Warn("DNG supplement parser could not determine file size.");
        return;
    }
    m_FileSize = static_cast<std::uint64_t>(end);
    std::array<std::uint8_t, 8> header {};
    if (Read(0, header.data(), header.size())) {
        m_LittleEndian = header[0] == 'I' && header[1] == 'I';
        const bool bigEndian = header[0] == 'M' && header[1] == 'M';
        m_Valid = (m_LittleEndian || bigEndian) && ReadU16(2) == 42;
        if (m_Valid) m_FirstIfd = ReadU32(4);
    }
    if (!m_Valid) Warn("DNG supplement parser only supports classic TIFF DNG files.");
}

bool DngTiffReader::Cancelled() {
    m_Cancelled = m_Cancelled || (m_ShouldCancel && m_ShouldCancel());
    return m_Cancelled;
}

void DngTiffReader::Warn(const char* warning) {
    if (m_Warning.empty() && !m_Cancelled) m_Warning = warning;
}

bool DngTiffReader::Contains(std::uint64_t offset, std::uint64_t size) const {
    return offset <= m_FileSize && size <= m_FileSize - offset;
}

bool DngTiffReader::Read(std::uint64_t offset, void* target, std::size_t size) {
    if (!Contains(offset, size)) {
        Warn("DNG supplement contains an out-of-range metadata offset.");
        return false;
    }
    auto* output = static_cast<std::uint8_t*>(target);
    while (size > 0) {
        if (Cancelled()) return false;
        if (offset < m_CacheOffset || offset - m_CacheOffset >= m_CacheSize) {
            m_CacheOffset = offset - offset % m_Cache.size();
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
                m_Cache.size(), m_FileSize - m_CacheOffset));
            m_File.clear();
            m_File.seekg(static_cast<std::streamoff>(m_CacheOffset));
            m_File.read(reinterpret_cast<char*>(m_Cache.data()), static_cast<std::streamsize>(count));
            m_CacheSize = static_cast<std::size_t>(m_File.gcount());
            m_BytesRead += m_CacheSize;
            if (m_CacheSize != count) {
                m_CacheSize = 0;
                Warn("DNG supplement metadata read failed.");
                return false;
            }
        }
        const auto cacheIndex = static_cast<std::size_t>(offset - m_CacheOffset);
        const std::size_t count = std::min(size, m_CacheSize - cacheIndex);
        std::memcpy(output, m_Cache.data() + cacheIndex, count);
        offset += count;
        output += count;
        size -= count;
    }
    return true;
}

std::uint16_t DngTiffReader::ReadU16(std::uint64_t offset) {
    std::array<std::uint8_t, 2> bytes {};
    return Read(offset, bytes.data(), bytes.size())
        ? static_cast<std::uint16_t>(UnsignedValue(bytes.data(), bytes.size(), m_LittleEndian)) : 0;
}

std::uint32_t DngTiffReader::ReadU32(std::uint64_t offset) {
    std::array<std::uint8_t, 4> bytes {};
    return Read(offset, bytes.data(), bytes.size())
        ? static_cast<std::uint32_t>(UnsignedValue(bytes.data(), bytes.size(), m_LittleEndian)) : 0;
}

std::vector<DngTiffReader::Entry> DngTiffReader::ReadEntries(std::uint32_t ifdOffset) {
    std::vector<Entry> entries;
    if (!m_Valid || ifdOffset == 0 || Cancelled()) return entries;
    const std::uint16_t count = ReadU16(ifdOffset);
    std::uint64_t offset = static_cast<std::uint64_t>(ifdOffset) + 2;
    if (!Contains(offset, static_cast<std::uint64_t>(count) * 12 + 4)) {
        Warn("DNG supplement contains a truncated TIFF directory.");
        return entries;
    }
    entries.reserve(count);
    for (std::size_t i = 0; i < count; ++i, offset += 12) {
        if (Cancelled()) return {};
        entries.push_back({ ReadU16(offset), ReadU16(offset + 2),
            ReadU32(offset + 4), ReadU32(offset + 8), offset });
    }
    return entries;
}

std::vector<std::uint32_t> DngTiffReader::ReadIfdTree() {
    std::vector<std::uint32_t> result;
    std::vector<std::uint32_t> pending;
    std::vector<std::uint32_t> discovered;
    auto enqueue = [&](std::uint32_t offset) {
        if (offset == 0 || std::find(discovered.begin(), discovered.end(), offset) != discovered.end()) return;
        if (discovered.size() == kMaxIfds) {
            Warn("DNG supplement exceeded the TIFF directory limit.");
            return;
        }
        discovered.push_back(offset);
        pending.push_back(offset);
    };
    enqueue(m_FirstIfd);
    while (!pending.empty() && !Cancelled()) {
        const std::uint32_t offset = pending.back();
        pending.pop_back();
        const auto entries = ReadEntries(offset);
        result.push_back(offset);
        // Empty directories can still link to the RAW directory.
        const std::uint64_t nextOffset = static_cast<std::uint64_t>(offset) + 2 +
            static_cast<std::uint64_t>(ReadU16(offset)) * 12;
        if (Contains(nextOffset, 4)) enqueue(ReadU32(nextOffset));
        for (const Entry& entry : entries) {
            if (entry.tag != 330) continue;
            if (entry.count > kMaxIfds) Warn("DNG supplement exceeded the TIFF directory limit.");
            for (double value : NumberValues(entry, kMaxIfds)) {
                if (value > 0 && value <= std::numeric_limits<std::uint32_t>::max()) {
                    enqueue(static_cast<std::uint32_t>(value));
                }
            }
        }
    }
    return result;
}

std::size_t DngTiffReader::TypeSize(std::uint16_t type) {
    switch (type) {
        case 1: case 2: case 6: case 7: return 1;
        case 3: case 8: return 2;
        case 4: case 9: case 11: case 13: return 4;
        case 5: case 10: case 12: return 8;
        default: return 0;
    }
}

bool DngTiffReader::Payload(const Entry& entry, std::uint64_t& offset, std::uint64_t& size) {
    size = static_cast<std::uint64_t>(TypeSize(entry.type)) * entry.count;
    offset = size <= 4 ? entry.entryOffset + 8 : entry.valueOffset;
    if (size == 0 || Cancelled()) return false;
    if (!Contains(offset, size)) {
        Warn("DNG supplement contains an out-of-range tag payload.");
        return false;
    }
    return true;
}

bool DngTiffReader::ChargePayload(std::uint64_t size) {
    // Bound cumulative retained metadata as well as individual allocations.
    if (size > kMaxPayloadBytes - m_PayloadBytes) {
        Warn("DNG supplement exceeded the 64 MiB metadata limit.");
        return false;
    }
    m_PayloadBytes += size;
    return true;
}

std::vector<std::uint8_t> DngTiffReader::RawBytes(const Entry& entry) {
    std::uint64_t offset = 0, size = 0;
    if (!Payload(entry, offset, size) || !ChargePayload(size)) return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    return Read(offset, bytes.data(), bytes.size()) ? std::move(bytes) : std::vector<std::uint8_t> {};
}

std::string DngTiffReader::StringValue(const Entry& entry) {
    if (entry.count > 4096) {
        Warn("DNG supplement camera model exceeds the string length limit.");
        return {};
    }
    auto bytes = RawBytes(entry);
    while (!bytes.empty() && bytes.back() == 0) bytes.pop_back();
    return std::string(bytes.begin(), bytes.end());
}

std::vector<double> DngTiffReader::NumberValues(const Entry& entry, std::size_t maxValues) {
    if (entry.type == 2 || TypeSize(entry.type) == 0 || maxValues == 0) return {};
    std::uint64_t offset = 0, size = 0;
    if (!Payload(entry, offset, size)) return {};
    const std::size_t count = std::min<std::size_t>(entry.count, maxValues);
    if (count > kMaxNumberValues) {
        Warn("DNG supplement exceeded the numeric tag length limit.");
        return {};
    }
    const std::size_t typeSize = TypeSize(entry.type);
    if (!ChargePayload(count * sizeof(double))) return {};
    std::vector<double> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::array<std::uint8_t, 8> bytes {};
        if (!Read(offset + i * typeSize, bytes.data(), typeSize)) return {};
        const auto bits = UnsignedValue(bytes.data(), typeSize, m_LittleEndian);
        double value = 0;
        switch (entry.type) {
            case 1: case 3: case 4: case 7: case 13: value = static_cast<double>(bits); break;
            case 6: value = static_cast<std::int8_t>(bits); break;
            case 8: value = static_cast<std::int16_t>(bits); break;
            case 9: value = static_cast<std::int32_t>(bits); break;
            case 5: case 10: {
                const auto num = static_cast<std::uint32_t>(UnsignedValue(bytes.data(), 4, m_LittleEndian));
                const auto den = static_cast<std::uint32_t>(UnsignedValue(bytes.data() + 4, 4, m_LittleEndian));
                if (den == 0) {
                    Warn("DNG supplement contains a zero rational denominator.");
                    return {};
                }
                value = entry.type == 5 ? static_cast<double>(num) / den
                    : static_cast<double>(static_cast<std::int32_t>(num)) / static_cast<std::int32_t>(den);
                break;
            }
            case 11: {
                const auto floatBits = static_cast<std::uint32_t>(bits);
                float decoded = 0;
                std::memcpy(&decoded, &floatBits, sizeof(decoded));
                value = decoded;
                break;
            }
            case 12: std::memcpy(&value, &bits, sizeof(value)); break;
        }
        if (!std::isfinite(value)) {
            Warn("DNG supplement contains a non-finite numeric value.");
            return {};
        }
        values.push_back(value);
    }
    return values;
}

} // namespace Raw::Internal
