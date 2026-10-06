#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace Raw::Internal {

// Classic TIFF metadata access. Image strips and unknown tag payloads stay on disk.
class DngTiffReader {
public:
    struct Entry {
        std::uint16_t tag = 0;
        std::uint16_t type = 0;
        std::uint32_t count = 0;
        std::uint32_t valueOffset = 0;
        std::uint64_t entryOffset = 0;
    };

    explicit DngTiffReader(const std::filesystem::path& path,
        std::function<bool()> shouldCancel = {});
    bool Valid() const { return m_Valid; }
    bool Cancelled();
    const std::string& Warning() const { return m_Warning; }
    std::uint32_t FirstIfd() const { return m_FirstIfd; }
    std::uint64_t BytesRead() const { return m_BytesRead; }
    std::vector<Entry> ReadEntries(std::uint32_t ifdOffset);
    std::vector<std::uint32_t> ReadIfdTree();
    std::vector<std::uint8_t> RawBytes(const Entry& entry);
    std::string StringValue(const Entry& entry);
    // Fixed-size consumers can request a prefix without allocating the whole tag.
    std::vector<double> NumberValues(const Entry& entry,
        std::size_t maxValues = std::numeric_limits<std::size_t>::max());

private:
    static std::size_t TypeSize(std::uint16_t type);
    bool Contains(std::uint64_t offset, std::uint64_t size) const;
    bool Read(std::uint64_t offset, void* target, std::size_t size);
    std::uint16_t ReadU16(std::uint64_t offset);
    std::uint32_t ReadU32(std::uint64_t offset);
    bool Payload(const Entry& entry, std::uint64_t& offset, std::uint64_t& size);
    bool ChargePayload(std::uint64_t size);
    void Warn(const char* warning);

    std::ifstream m_File;
    std::function<bool()> m_ShouldCancel;
    std::array<std::uint8_t, 64 * 1024> m_Cache {};
    std::uint64_t m_FileSize = 0;
    std::uint64_t m_CacheOffset = 0;
    std::size_t m_CacheSize = 0;
    std::uint64_t m_BytesRead = 0;
    std::uint64_t m_PayloadBytes = 0;
    std::string m_Warning;
    bool m_LittleEndian = true;
    bool m_Valid = false;
    bool m_Cancelled = false;
    std::uint32_t m_FirstIfd = 0;
};

} // namespace Raw::Internal
