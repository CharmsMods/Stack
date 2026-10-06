#include "StackBinaryFormat.h"

#include "Raw/RawTechnicalEvidence.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);

namespace StackBinaryFormat {
namespace {

constexpr std::array<char, 4> kMagic = { 'M', 'S', 'T', 'K' };
constexpr std::uint16_t kFormatVersion = 2;
constexpr int kDeflateCompressionLevel = 1;
constexpr std::size_t kCompressionAttemptThreshold = 4 * 1024;
constexpr double kCompressionKeepRatio = 0.92;
// Preset graphs contain shallow JSON. Bound recursion before decoding file
// contents on ordinary Windows threads with a 1 MiB stack.
constexpr std::size_t kMaximumJsonDepth = 128;

constexpr std::array<char, 4> kMetaSection = { 'M', 'E', 'T', 'A' };
constexpr std::array<char, 4> kThumbnailSection = { 'T', 'H', 'M', 'B' };
constexpr std::array<char, 4> kSourceSection = { 'S', 'R', 'C', 'I' };
constexpr std::array<char, 4> kPipelineSection = { 'P', 'I', 'P', 'E' };
constexpr std::array<char, 4> kNodeBrowserSection = { 'N', 'B', 'R', 'W' };
constexpr std::array<char, 4> kRawWorkspaceSection = { 'R', 'A', 'W', 'K' };
constexpr std::array<char, 4> kProjectsSection = { 'P', 'R', 'O', 'J' };
constexpr std::array<char, 4> kAssetsSection = { 'A', 'S', 'S', 'T' };
constexpr std::array<char, 4> kPresetBoundarySection = { 'B', 'N', 'D', 'Y' };

enum class CompressionCodec : std::uint8_t {
    None = 0,
    Deflate = 1
};

enum class ValueType : std::uint8_t {
    Null = 0,
    BoolFalse = 1,
    BoolTrue = 2,
    Int64 = 3,
    UInt64 = 4,
    Double = 5,
    String = 6,
    Binary = 7,
    Array = 8,
    Object = 9
};

std::uint32_t ComputeCRC32(const unsigned char* data, std::size_t length, std::uint32_t previousCrc32 = 0) {
    std::uint32_t crc = ~previousCrc32;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : (crc >> 1);
        }
    }
    return ~crc;
}

struct SectionData {
    std::array<char, 4> id;
    std::vector<unsigned char> bytes;
    std::uint64_t uncompressedSize = 0;
    CompressionCodec compression = CompressionCodec::None;
};

struct SectionInfo {
    std::uint64_t offset = 0;
    std::uint64_t storedSize = 0;
    std::uint64_t uncompressedSize = 0;
    CompressionCodec compression = CompressionCodec::None;
};

class ByteWriter {
public:
    template <typename T>
    void WritePod(const T& value) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
        m_Bytes.insert(m_Bytes.end(), bytes, bytes + sizeof(T));
    }

    void WriteString(const std::string& value) {
        if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("ByteWriter: String size exceeds maximum allowed (4GB)");
        }
        const std::uint32_t size = static_cast<std::uint32_t>(value.size());
        WritePod(size);
        m_Bytes.insert(m_Bytes.end(), value.begin(), value.end());
    }

    void WriteBinary(const std::vector<unsigned char>& value) {
        if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("ByteWriter: Binary size exceeds maximum allowed (4GB)");
        }
        const std::uint32_t size = static_cast<std::uint32_t>(value.size());
        WritePod(size);
        m_Bytes.insert(m_Bytes.end(), value.begin(), value.end());
    }

    std::vector<unsigned char> TakeBytes() {
        return std::move(m_Bytes);
    }

private:
    std::vector<unsigned char> m_Bytes;
};

class ByteReader {
public:
    explicit ByteReader(const std::vector<unsigned char>& bytes)
        : m_Bytes(bytes) {}

    template <typename T>
    bool ReadPod(T& value) {
        if (!CanRead(sizeof(T))) return false;
        std::memcpy(&value, m_Bytes.data() + m_Offset, sizeof(T));
        m_Offset += sizeof(T);
        return true;
    }

    bool ReadString(std::string& value) {
        std::uint32_t size = 0;
        if (!ReadPod(size) || !CanRead(size)) return false;
        value.assign(reinterpret_cast<const char*>(m_Bytes.data() + m_Offset), size);
        m_Offset += size;
        return true;
    }

    bool ReadBinary(std::vector<unsigned char>& value) {
        std::uint32_t size = 0;
        if (!ReadPod(size) || !CanRead(size)) return false;
        value.assign(m_Bytes.begin() + static_cast<std::ptrdiff_t>(m_Offset), m_Bytes.begin() + static_cast<std::ptrdiff_t>(m_Offset + size));
        m_Offset += size;
        return true;
    }

    std::size_t Remaining() const { return m_Bytes.size() - m_Offset; }

private:
    bool CanRead(std::size_t size) const {
        return size <= Remaining();
    }

    const std::vector<unsigned char>& m_Bytes;
    std::size_t m_Offset = 0;
};

std::string SectionKey(const std::array<char, 4>& id) {
    return std::string(id.data(), id.size());
}

bool IsCompressionCandidate(FileKind kind, const std::array<char, 4>& id, std::size_t byteCount) {
    if (byteCount < kCompressionAttemptThreshold) {
        return false;
    }

    if (kind == FileKind::Project) {
        return id == kPipelineSection ||
               id == kThumbnailSection ||
               id == kSourceSection ||
               id == kNodeBrowserSection;
    }
    if (kind == FileKind::NodePreset) {
        return id == kPipelineSection ||
               id == kThumbnailSection ||
               id == kPresetBoundarySection;
    }
    return false;
}

bool CompressDeflateBytes(const std::vector<unsigned char>& input, std::vector<unsigned char>& output) {
    output.clear();
    if (input.empty()) {
        return true;
    }
    if (input.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    int compressedSize = 0;
    const std::unique_ptr<unsigned char, decltype(&std::free)> compressedBytes(stbi_zlib_compress(
        const_cast<unsigned char*>(input.data()),
        static_cast<int>(input.size()),
        &compressedSize,
        kDeflateCompressionLevel), &std::free);

    if (!compressedBytes || compressedSize <= 0) {
        return false;
    }

    output.assign(compressedBytes.get(), compressedBytes.get() + compressedSize);
    return true;
}

bool DecompressDeflateBytes(const std::vector<unsigned char>& input, std::size_t expectedSize, std::vector<unsigned char>& output) {
    output.clear();
    if (expectedSize == 0) {
        return input.empty();
    }
    if (input.empty()) {
        return false;
    }
    if (input.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        expectedSize > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    // The allocating decoder can expand far beyond the declared section size
    // before we get a chance to validate it. Decode into a bounded buffer.
    output.resize(expectedSize);
    const int outputSize = stbi_zlib_decode_buffer(
        reinterpret_cast<char*>(output.data()),
        static_cast<int>(output.size()),
        reinterpret_cast<const char*>(input.data()),
        static_cast<int>(input.size()));
    const bool validSize = outputSize >= 0 && static_cast<std::size_t>(outputSize) == expectedSize;
    if (!validSize) {
        output.clear();
    }
    return validSize;
}

SectionData MakeSectionData(FileKind kind, const std::array<char, 4>& id, const std::vector<unsigned char>& rawBytes) {
    SectionData section;
    section.id = id;
    section.bytes = rawBytes;
    section.uncompressedSize = static_cast<std::uint64_t>(rawBytes.size());
    section.compression = CompressionCodec::None;

    if (!IsCompressionCandidate(kind, id, rawBytes.size())) {
        return section;
    }

    std::vector<unsigned char> compressedBytes;
    if (!CompressDeflateBytes(rawBytes, compressedBytes)) {
        return section;
    }

    if (compressedBytes.size() <= static_cast<std::size_t>(static_cast<double>(rawBytes.size()) * kCompressionKeepRatio)) {
        section.bytes = std::move(compressedBytes);
        section.compression = CompressionCodec::Deflate;
    }

    return section;
}

json MakeBinaryJson(const std::vector<unsigned char>& bytes) {
    json::binary_t::container_type binaryBytes(bytes.begin(), bytes.end());
    return json::binary(std::move(binaryBytes));
}

bool EncodeJsonValue(ByteWriter& writer, const json& value, std::size_t depth = 0) {
    if (depth > kMaximumJsonDepth) return false;
    if (value.is_null()) {
        writer.WritePod(static_cast<std::uint8_t>(ValueType::Null));
        return true;
    }

    if (value.is_boolean()) {
        writer.WritePod(static_cast<std::uint8_t>(value.get<bool>() ? ValueType::BoolTrue : ValueType::BoolFalse));
        return true;
    }

    if (value.is_number_unsigned()) {
        writer.WritePod(static_cast<std::uint8_t>(ValueType::UInt64));
        const std::uint64_t integerValue = value.get<std::uint64_t>();
        writer.WritePod(integerValue);
        return true;
    }

    if (value.is_number_integer()) {
        writer.WritePod(static_cast<std::uint8_t>(ValueType::Int64));
        const std::int64_t integerValue = value.get<std::int64_t>();
        writer.WritePod(integerValue);
        return true;
    }

    if (value.is_number_float()) {
        writer.WritePod(static_cast<std::uint8_t>(ValueType::Double));
        const double floatValue = value.get<double>();
        writer.WritePod(floatValue);
        return true;
    }

    if (value.is_string()) {
        writer.WritePod(static_cast<std::uint8_t>(ValueType::String));
        writer.WriteString(value.get<std::string>());
        return true;
    }

    if (value.is_binary()) {
        writer.WritePod(static_cast<std::uint8_t>(ValueType::Binary));
        const auto& binaryValue = value.get_binary();
        std::vector<unsigned char> bytes(binaryValue.begin(), binaryValue.end());
        writer.WriteBinary(bytes);
        return true;
    }

    if (value.is_array()) {
        if (value.size() > std::numeric_limits<std::uint32_t>::max()) return false;
        writer.WritePod(static_cast<std::uint8_t>(ValueType::Array));
        const std::uint32_t count = static_cast<std::uint32_t>(value.size());
        writer.WritePod(count);
        for (const auto& item : value) {
            if (!EncodeJsonValue(writer, item, depth + 1)) return false;
        }
        return true;
    }

    if (value.is_object()) {
        if (value.size() > std::numeric_limits<std::uint32_t>::max()) return false;
        writer.WritePod(static_cast<std::uint8_t>(ValueType::Object));
        const std::uint32_t count = static_cast<std::uint32_t>(value.size());
        writer.WritePod(count);
        for (auto it = value.begin(); it != value.end(); ++it) {
            writer.WriteString(it.key());
            if (!EncodeJsonValue(writer, it.value(), depth + 1)) return false;
        }
        return true;
    }

    return false;
}

bool DecodeJsonValue(ByteReader& reader, json& value, std::size_t depth = 0) {
    if (depth > kMaximumJsonDepth) return false;
    std::uint8_t rawType = 0;
    if (!reader.ReadPod(rawType)) return false;

    const ValueType type = static_cast<ValueType>(rawType);
    switch (type) {
        case ValueType::Null:
            value = nullptr;
            return true;

        case ValueType::BoolFalse:
            value = false;
            return true;

        case ValueType::BoolTrue:
            value = true;
            return true;

        case ValueType::Int64: {
            std::int64_t integerValue = 0;
            if (!reader.ReadPod(integerValue)) return false;
            value = integerValue;
            return true;
        }

        case ValueType::UInt64: {
            std::uint64_t integerValue = 0;
            if (!reader.ReadPod(integerValue)) return false;
            value = integerValue;
            return true;
        }

        case ValueType::Double: {
            double floatValue = 0.0;
            if (!reader.ReadPod(floatValue)) return false;
            value = floatValue;
            return true;
        }

        case ValueType::String: {
            std::string stringValue;
            if (!reader.ReadString(stringValue)) return false;
            value = std::move(stringValue);
            return true;
        }

        case ValueType::Binary: {
            std::vector<unsigned char> bytes;
            if (!reader.ReadBinary(bytes)) return false;
            value = MakeBinaryJson(bytes);
            return true;
        }

        case ValueType::Array: {
            std::uint32_t count = 0;
            if (!reader.ReadPod(count) || count > reader.Remaining()) return false;
            value = json::array();
            for (std::uint32_t index = 0; index < count; ++index) {
                json item;
                if (!DecodeJsonValue(reader, item, depth + 1)) return false;
                value.push_back(std::move(item));
            }
            return true;
        }

        case ValueType::Object: {
            std::uint32_t count = 0;
            // Even an empty key and null value need five bytes per entry.
            if (!reader.ReadPod(count) || count > reader.Remaining() / 5u) return false;
            value = json::object();
            for (std::uint32_t index = 0; index < count; ++index) {
                std::string key;
                json item;
                if (!reader.ReadString(key) || value.contains(key) ||
                    !DecodeJsonValue(reader, item, depth + 1)) return false;
                value[key] = std::move(item);
            }
            return true;
        }
    }

    return false;
}

std::vector<unsigned char> SerializeJson(const json& value) {
    ByteWriter writer;
    if (!EncodeJsonValue(writer, value)) {
        throw std::runtime_error("JSON exceeds the binary format's nesting or collection size limit.");
    }
    return writer.TakeBytes();
}

bool DeserializeJson(const std::vector<unsigned char>& bytes, json& value) {
    ByteReader reader(bytes);
    return DecodeJsonValue(reader, value) && reader.Remaining() == 0;
}

bool ReplaceFileAtomically(
    const std::filesystem::path& temporaryPath,
    const std::filesystem::path& destinationPath) {
#if defined(_WIN32)
    return MoveFileExW(
               temporaryPath.c_str(),
               destinationPath.c_str(),
               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
#else
    std::error_code ec;
    std::filesystem::rename(temporaryPath, destinationPath, ec);
    return !ec;
#endif
}

bool WriteSectionedFile(const std::filesystem::path& path, FileKind kind, const std::vector<SectionData>& sections) {
    std::filesystem::path tempPath = path;
    tempPath += ".tmp";

    std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    const std::uint32_t sectionCount = static_cast<std::uint32_t>(sections.size());
    const std::uint64_t headerSize =
        static_cast<std::uint64_t>(kMagic.size()) +
        sizeof(kFormatVersion) +
        sizeof(kind) +
        sizeof(sectionCount) +
        sectionCount * (4 + sizeof(std::uint64_t) + sizeof(std::uint64_t) + sizeof(std::uint64_t) + sizeof(std::uint8_t));

    std::uint64_t dataOffset = headerSize;

    std::uint32_t currentCrc = 0;
    auto writeAndCrc = [&](const void* data, std::size_t size) {
        file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        currentCrc = ComputeCRC32(reinterpret_cast<const unsigned char*>(data), size, currentCrc);
    };

    writeAndCrc(kMagic.data(), kMagic.size());
    writeAndCrc(&kFormatVersion, sizeof(kFormatVersion));
    writeAndCrc(&kind, sizeof(kind));
    writeAndCrc(&sectionCount, sizeof(sectionCount));

    for (const auto& section : sections) {
        writeAndCrc(section.id.data(), section.id.size());
        const std::uint64_t sectionOffset = dataOffset;
        const std::uint64_t storedSize = static_cast<std::uint64_t>(section.bytes.size());
        const std::uint64_t uncompressedSize = section.uncompressedSize;
        const std::uint8_t compression = static_cast<std::uint8_t>(section.compression);
        writeAndCrc(&sectionOffset, sizeof(sectionOffset));
        writeAndCrc(&storedSize, sizeof(storedSize));
        writeAndCrc(&uncompressedSize, sizeof(uncompressedSize));
        writeAndCrc(&compression, sizeof(compression));
        dataOffset += storedSize;
    }

    for (const auto& section : sections) {
        if (!section.bytes.empty()) {
            writeAndCrc(section.bytes.data(), section.bytes.size());
        }
    }

    // Write CRC32 footer
    const std::array<char, 4> kCksmTag = { 'C', 'K', 'S', 'M' };
    file.write(kCksmTag.data(), kCksmTag.size());
    file.write(reinterpret_cast<const char*>(&currentCrc), sizeof(currentCrc));

    file.close();
    if (!file.good()) {
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        return false;
    }

    if (!ReplaceFileAtomically(tempPath, path)) {
        // The temporary file lives beside the destination, so replacement
        // should be one filesystem operation. Never fall back to copying over
        // a valid project in place: a crash during that copy would corrupt
        // both the previous save and the attempted replacement.
        std::error_code cleanupError;
        std::filesystem::remove(tempPath, cleanupError);
        return false;
    }

    return true;
}

bool ReadSectionTable(
    const std::filesystem::path& path,
    FileKind expectedKind,
    std::ifstream& file,
    std::unordered_map<std::string, SectionInfo>& sections,
    bool verifyChecksum = true) {

    file.open(path, std::ios::binary);
    if (!file.is_open()) return false;

    std::array<char, 4> magic = {};
    file.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!file.good() || magic != kMagic) {
        return false;
    }

    std::uint16_t version = 0;
    std::uint16_t rawKind = 0;
    std::uint32_t sectionCount = 0;

    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&rawKind), sizeof(rawKind));
    file.read(reinterpret_cast<char*>(&sectionCount), sizeof(sectionCount));

    if (!file.good() ||
        version != kFormatVersion ||
        rawKind != static_cast<std::uint16_t>(expectedKind)) {
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff signedFileSize = file.tellg();
    if (signedFileSize < 0) return false;
    const std::uint64_t fileSize = static_cast<std::uint64_t>(signedFileSize);
    std::uint64_t dataEnd = fileSize;
    
    if (fileSize >= 8) {
        file.seekg(static_cast<std::streamoff>(fileSize - 8), std::ios::beg);
        std::array<char, 4> footerTag = {};
        file.read(footerTag.data(), footerTag.size());
        if (footerTag[0] == 'C' && footerTag[1] == 'K' && footerTag[2] == 'S' && footerTag[3] == 'M') {
            dataEnd -= 8;
        }
        if (verifyChecksum && dataEnd != fileSize) {
            std::uint32_t expectedCrc = 0;
            file.read(reinterpret_cast<char*>(&expectedCrc), sizeof(expectedCrc));
            
            // Compute CRC32 of the file up to the footer
            file.seekg(0, std::ios::beg);
            std::uint32_t actualCrc = 0;
            constexpr std::size_t kBufferSize = 65536;
            std::vector<char> buffer(kBufferSize);
            std::uint64_t bytesToRead = fileSize - 8;
            
            while (bytesToRead > 0 && file.good()) {
                const std::size_t toRead = static_cast<std::size_t>(std::min(static_cast<std::uint64_t>(kBufferSize), bytesToRead));
                file.read(buffer.data(), static_cast<std::streamsize>(toRead));
                const std::size_t readCount = static_cast<std::size_t>(file.gcount());
                if (readCount == 0) break;
                actualCrc = ComputeCRC32(reinterpret_cast<const unsigned char*>(buffer.data()), readCount, actualCrc);
                bytesToRead -= readCount;
            }
            
            if (bytesToRead != 0 || !file.good() || actualCrc != expectedCrc) {
                return false; // Corrupted file
            }
        }
    }

    constexpr std::uint64_t fixedHeaderSize = 12u;
    constexpr std::uint64_t sectionEntrySize = 29u;
    if (dataEnd < fixedHeaderSize ||
        sectionCount > (dataEnd - fixedHeaderSize) / sectionEntrySize) return false;
    const std::uint64_t payloadStart = fixedHeaderSize + sectionCount * sectionEntrySize;
    file.seekg(static_cast<std::streamoff>(fixedHeaderSize), std::ios::beg);

    for (std::uint32_t index = 0; index < sectionCount; ++index) {
        std::array<char, 4> id = {};
        SectionInfo info;
        file.read(id.data(), static_cast<std::streamsize>(id.size()));
        file.read(reinterpret_cast<char*>(&info.offset), sizeof(info.offset));
        file.read(reinterpret_cast<char*>(&info.storedSize), sizeof(info.storedSize));
        if (!file.good()) return false;

        file.read(reinterpret_cast<char*>(&info.uncompressedSize), sizeof(info.uncompressedSize));
        std::uint8_t rawCompression = 0;
        file.read(reinterpret_cast<char*>(&rawCompression), sizeof(rawCompression));
        if (!file.good()) return false;
        info.compression = static_cast<CompressionCodec>(rawCompression);

        // Check the range by subtraction so a corrupt 64-bit length cannot
        // wrap around and pass validation before the allocation below.
        if (info.offset < payloadStart || info.offset > dataEnd ||
            info.storedSize > dataEnd - info.offset ||
            info.storedSize > std::numeric_limits<std::size_t>::max()) return false;
        if (info.compression == CompressionCodec::None) {
            if (info.uncompressedSize != info.storedSize) return false;
        } else if (info.compression == CompressionCodec::Deflate) {
            if (info.storedSize > std::numeric_limits<int>::max() ||
                info.uncompressedSize > std::numeric_limits<int>::max()) return false;
        } else {
            return false;
        }
        if (!sections.emplace(SectionKey(id), info).second) return false;
    }

    return true;
}

bool ReadSectionBytes(
    std::ifstream& file,
    const std::unordered_map<std::string, SectionInfo>& sections,
    const std::array<char, 4>& id,
    std::vector<unsigned char>& bytes) {

    const auto it = sections.find(SectionKey(id));
    if (it == sections.end()) {
        bytes.clear();
        return false;
    }

    const SectionInfo& info = it->second;
    std::vector<unsigned char> storedBytes(static_cast<std::size_t>(info.storedSize), 0);
    if (!storedBytes.empty()) {
        file.seekg(static_cast<std::streamoff>(it->second.offset), std::ios::beg);
        file.read(reinterpret_cast<char*>(storedBytes.data()), static_cast<std::streamsize>(storedBytes.size()));
        if (!file.good()) return false;
    }

    if (info.compression == CompressionCodec::None) {
        bytes = std::move(storedBytes);
        return true;
    }

    if (info.compression == CompressionCodec::Deflate) {
        return DecompressDeflateBytes(storedBytes, static_cast<std::size_t>(info.uncompressedSize), bytes);
    }

    bytes.clear();
    return false;
}

json ProjectMetadataToJson(const ProjectMetadata& metadata) {
    json value = json::object();
    value["projectKind"] = metadata.projectKind;
    value["projectName"] = metadata.projectName;
    value["timestamp"] = metadata.timestamp;
    value["sourceWidth"] = metadata.sourceWidth;
    value["sourceHeight"] = metadata.sourceHeight;
    return value;
}

bool ProjectMetadataFromJson(const json& value, ProjectMetadata& metadata) {
    if (!value.is_object()) return false;
    metadata.projectKind = value.value("projectKind", std::string(kEditorProjectKind));
    metadata.projectName = value.value("projectName", "Untitled Project");
    metadata.timestamp = value.value("timestamp", "Unknown");
    metadata.sourceWidth = value.value("sourceWidth", 0);
    metadata.sourceHeight = value.value("sourceHeight", 0);
    return true;
}

json NodePresetMetadataToJson(const NodePresetMetadata& metadata) {
    return {
        { "id", metadata.id },
        { "displayName", metadata.displayName },
        { "timestamp", metadata.timestamp },
        { "savedWithVersion", metadata.savedWithVersion },
        { "presetVersion", metadata.presetVersion },
        { "nodeCount", metadata.nodeCount },
        { "inputCount", metadata.inputCount },
        { "outputCount", metadata.outputCount }
    };
}

bool NodePresetMetadataFromJson(const json& value, NodePresetMetadata& metadata) {
    if (!value.is_object()) {
        return false;
    }
    metadata.id = value.value("id", std::string());
    metadata.displayName = value.value("displayName", "Untitled Preset");
    metadata.timestamp = value.value("timestamp", std::string());
    metadata.savedWithVersion = value.value("savedWithVersion", std::string());
    metadata.presetVersion = value.value("presetVersion", 1u);
    metadata.nodeCount = value.value("nodeCount", 0u);
    metadata.inputCount = value.value("inputCount", 0u);
    metadata.outputCount = value.value("outputCount", 0u);
    return !metadata.id.empty();
}

json NodePresetBoundarySocketsToJson(const std::vector<NodePresetBoundarySocket>& sockets) {
    json array = json::array();
    for (const NodePresetBoundarySocket& socket : sockets) {
        array.push_back({
            { "nodeTitle", socket.nodeTitle },
            { "socketLabel", socket.socketLabel },
            { "direction", socket.direction },
            { "type", socket.type }
        });
    }
    return array;
}

std::vector<NodePresetBoundarySocket> NodePresetBoundarySocketsFromJson(const json& value) {
    std::vector<NodePresetBoundarySocket> sockets;
    if (!value.is_array()) {
        return sockets;
    }
    for (const json& socketValue : value) {
        if (!socketValue.is_object()) {
            continue;
        }
        NodePresetBoundarySocket socket;
        socket.nodeTitle = socketValue.value("nodeTitle", std::string());
        socket.socketLabel = socketValue.value("socketLabel", std::string());
        socket.direction = socketValue.value("direction", std::string());
        socket.type = socketValue.value("type", std::string());
        sockets.push_back(std::move(socket));
    }
    return sockets;
}

json BundledProjectToJson(const BundledProjectDocument& project) {
    json value = json::object();
    value["fileName"] = project.fileName;
    value["projectKind"] = project.project.metadata.projectKind;
    value["projectName"] = project.project.metadata.projectName;
    value["timestamp"] = project.project.metadata.timestamp;
    value["sourceWidth"] = project.project.metadata.sourceWidth;
    value["sourceHeight"] = project.project.metadata.sourceHeight;
    value["thumbnailPng"] = MakeBinaryJson(project.project.thumbnailBytes);
    value["sourcePng"] = MakeBinaryJson(project.project.sourceImageBytes);
    value["pipeline"] = project.project.pipelineData.is_null() ? json::array() : project.project.pipelineData;
    value["rawWorkspace"] = project.project.rawWorkspaceData.is_null()
        ? json::object()
        : project.project.rawWorkspaceData;
    json nodeBrowserThumbs = json::array();
    for (const NodeBrowserThumbnailEntry& entry : project.project.nodeBrowserThumbnailEntries) {
        nodeBrowserThumbs.push_back({
            { "previewKey", entry.previewKey },
            { "previewSeedHash", entry.previewSeedHash },
            { "previewRecipeVersion", entry.previewRecipeVersion },
            { "pngBytes", MakeBinaryJson(entry.pngBytes) }
        });
    }
    value["nodeBrowserThumbnails"] = std::move(nodeBrowserThumbs);
    return value;
}

bool BundledProjectFromJson(const json& value, BundledProjectDocument& project) {
    if (!value.is_object()) return false;
    project.fileName = value.value("fileName", "");
    project.project.metadata.projectKind = value.value("projectKind", std::string(kEditorProjectKind));
    project.project.metadata.projectName = value.value("projectName", "Untitled Project");
    project.project.metadata.timestamp = value.value("timestamp", "Unknown");
    project.project.metadata.sourceWidth = value.value("sourceWidth", 0);
    project.project.metadata.sourceHeight = value.value("sourceHeight", 0);
    project.project.pipelineData = value.value("pipeline", json::array());
    project.project.rawWorkspaceData = value.value("rawWorkspace", json::object());
    project.project.nodeBrowserThumbnailEntries.clear();

    if (value.contains("thumbnailPng") && value["thumbnailPng"].is_binary()) {
        const auto& binaryValue = value["thumbnailPng"].get_binary();
        project.project.thumbnailBytes.assign(binaryValue.begin(), binaryValue.end());
    }

    if (value.contains("sourcePng") && value["sourcePng"].is_binary()) {
        const auto& binaryValue = value["sourcePng"].get_binary();
        project.project.sourceImageBytes.assign(binaryValue.begin(), binaryValue.end());
    }

    const json nodeBrowserThumbs = value.value("nodeBrowserThumbnails", json::array());
    if (nodeBrowserThumbs.is_array()) {
        for (const json& thumbValue : nodeBrowserThumbs) {
            if (!thumbValue.is_object()) {
                continue;
            }
            NodeBrowserThumbnailEntry entry;
            entry.previewKey = thumbValue.value("previewKey", "");
            entry.previewSeedHash = thumbValue.value("previewSeedHash", "");
            entry.previewRecipeVersion = thumbValue.value("previewRecipeVersion", 0u);
            if (thumbValue.contains("pngBytes") && thumbValue["pngBytes"].is_binary()) {
                const auto& binaryValue = thumbValue["pngBytes"].get_binary();
                entry.pngBytes.assign(binaryValue.begin(), binaryValue.end());
            }
            if (!entry.previewKey.empty()) {
                project.project.nodeBrowserThumbnailEntries.push_back(std::move(entry));
            }
        }
    }

    return !project.fileName.empty();
}

json AssetToJson(const AssetDocument& asset) {
    json value = json::object();
    value["fileName"] = asset.fileName;
    value["displayName"] = asset.displayName;
    value["timestamp"] = asset.timestamp;
    value["projectFileName"] = asset.projectFileName;
    value["projectName"] = asset.projectName;
    value["width"] = asset.width;
    value["height"] = asset.height;
    value["imagePng"] = MakeBinaryJson(asset.imageBytes);
    return value;
}

bool AssetFromJson(const json& value, AssetDocument& asset) {
    if (!value.is_object()) return false;
    asset.fileName = value.value("fileName", "");
    asset.displayName = value.value("displayName", "");
    asset.timestamp = value.value("timestamp", "Unknown");
    asset.projectFileName = value.value("projectFileName", "");
    asset.projectName = value.value("projectName", "");
    asset.width = value.value("width", 0);
    asset.height = value.value("height", 0);

    if (value.contains("imagePng") && value["imagePng"].is_binary()) {
        const auto& binaryValue = value["imagePng"].get_binary();
        asset.imageBytes.assign(binaryValue.begin(), binaryValue.end());
    }

    return !asset.fileName.empty();
}

std::vector<std::uint8_t> BinaryJsonBytes(const json& value) {
    if (!value.is_binary()) {
        return {};
    }
    const auto& binary = value.get_binary();
    return std::vector<std::uint8_t>(binary.begin(), binary.end());
}

bool StageManagedBytes(
    const Stack::Project::ProjectStoreHandle& store,
    const Stack::Project::ProjectStoreTransaction& transaction,
    Stack::Project::RawProjectSnapshot& snapshot,
    const std::vector<std::uint8_t>& bytes,
    std::string displayName,
    std::string projectAssetPath,
    std::string originalSourcePath,
    std::string originalFilename,
    const char* managedRole,
    std::string& outAssetId) {
    if (!store || !transaction || bytes.empty()) {
        return false;
    }
    const Stack::RawEvidence::SourceIdentity identity =
        Stack::RawEvidence::ComputeSourceIdentity(bytes);
    if (!identity.valid) {
        return false;
    }

    Stack::Project::EmbeddedAssetRecord record;
    record.assetId = Stack::Project::MakeAssetId(
        identity.sha256, identity.byteSize);
    record.sha256 = identity.sha256;
    record.byteLength = identity.byteSize;
    record.displayName = displayName.empty() ? originalFilename : displayName;
    record.projectAssetPath = std::move(projectAssetPath);
    record.originalSourcePath = std::move(originalSourcePath);
    record.originalFileFingerprint = identity.sha256;
    record.originalFilename = originalFilename.empty()
        ? std::filesystem::u8path(record.projectAssetPath).filename().u8string()
        : std::move(originalFilename);
    record.inputFamily = Stack::Project::MultiFrameInputFamily::Raster;
    record.captureMetadataSummary = json::object();
    record.managedRole = managedRole;

    outAssetId = record.assetId;
    if (Stack::Project::FindEmbeddedAsset(snapshot, record.assetId)) {
        return true;
    }

    const std::string byteString(
        reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::istringstream input(byteString, std::ios::in | std::ios::binary);
    if (!store->StageAssetStream(transaction, input, record)) {
        return false;
    }
    snapshot.embeddedAssets.push_back(std::move(record));
    return true;
}

bool ExternalizeGraphImageAssets(
    const Stack::Project::ProjectStoreHandle& store,
    const Stack::Project::ProjectStoreTransaction& transaction,
    Stack::Project::RawProjectSnapshot& snapshot) {
    if (!snapshot.pipelineData.is_object()) {
        return true;
    }
    auto graph = snapshot.pipelineData.find("nodeGraph");
    if (graph == snapshot.pipelineData.end() || !graph->is_object()) {
        return true;
    }
    auto nodes = graph->find("nodes");
    if (nodes == graph->end() || !nodes->is_array()) {
        return true;
    }

    std::unordered_set<std::string> referencedGraphAssets;
    for (json& node : *nodes) {
        if (!node.is_object() || node.value("kind", std::string()) != "Image") {
            continue;
        }
        const std::string existingId =
            node.value("managedAssetId", std::string());
        if (!existingId.empty()) {
            const Stack::Project::EmbeddedAssetRecord* existingAsset =
                Stack::Project::FindEmbeddedAsset(snapshot, existingId);
            if (existingAsset) {
                // Managed assets are immutable. The runtime PNG is present for
                // display/editing only; keeping its reference avoids a full
                // encode/hash pass on every ordinary project save.
                node["projectAssetPath"] = existingAsset->projectAssetPath;
                node.erase("pngBytes");
                referencedGraphAssets.insert(existingId);
                continue;
            }
        }
        const std::vector<std::uint8_t> bytes =
            BinaryJsonBytes(node.value("pngBytes", json()));
        if (bytes.empty()) {
            if (!existingId.empty()) {
                referencedGraphAssets.insert(existingId);
            }
            continue;
        }

        std::string stableNodeId = node.value("instanceUuid", std::string());
        if (stableNodeId.empty()) {
            stableNodeId = "node-" + std::to_string(node.value("id", 0));
        }
        const std::string sourcePath = node.value("sourcePath", std::string());
        const std::string originalFilename = sourcePath.empty()
            ? (stableNodeId + ".png")
            : std::filesystem::u8path(sourcePath).filename().u8string();
        const std::string displayName = node.value(
            "label", node.value("title", std::string("Image")));
        const std::string managedPath = (
            std::filesystem::path("assets") / "images" /
            (stableNodeId + ".png")).generic_string();
        std::string assetId;
        if (!StageManagedBytes(
                store,
                transaction,
                snapshot,
                bytes,
                displayName,
                managedPath,
                sourcePath,
                originalFilename,
                "graph-image",
                assetId)) {
            return false;
        }
        node["managedAssetId"] = assetId;
        node["projectAssetPath"] = managedPath;
        node.erase("pngBytes");
        referencedGraphAssets.insert(std::move(assetId));
    }

    snapshot.embeddedAssets.erase(
        std::remove_if(
            snapshot.embeddedAssets.begin(),
            snapshot.embeddedAssets.end(),
            [&](const Stack::Project::EmbeddedAssetRecord& asset) {
                return asset.managedRole == "graph-image" &&
                    referencedGraphAssets.find(asset.assetId) ==
                        referencedGraphAssets.end() &&
                    std::find(
                        snapshot.lifecycle.initialAssetIds.begin(),
                        snapshot.lifecycle.initialAssetIds.end(),
                        asset.assetId) == snapshot.lifecycle.initialAssetIds.end();
            }),
        snapshot.embeddedAssets.end());
    return true;
}

bool ReadManagedAssetBytes(
    const Stack::Project::ProjectStoreHandle& store,
    const Stack::Project::EmbeddedAssetRecord& asset,
    std::vector<std::uint8_t>& bytes) {
    if (!store || asset.byteLength >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return false;
    }
    Stack::Project::ProjectAssetStream stream =
        store->OpenAssetStream(asset.assetId);
    if (!stream) {
        return false;
    }
    bytes.resize(static_cast<std::size_t>(asset.byteLength));
    if (!bytes.empty()) {
        stream.stream->read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (stream.stream->gcount() !=
            static_cast<std::streamsize>(bytes.size())) {
            bytes.clear();
            return false;
        }
    }
    return true;
}

bool RehydrateGraphImageAssets(
    const Stack::Project::ProjectStoreOpenResult& opened,
    json& pipelineData) {
    if (!pipelineData.is_object()) {
        return true;
    }
    auto graph = pipelineData.find("nodeGraph");
    if (graph == pipelineData.end() || !graph->is_object()) {
        return true;
    }
    auto nodes = graph->find("nodes");
    if (nodes == graph->end() || !nodes->is_array()) {
        return true;
    }
    for (json& node : *nodes) {
        if (!node.is_object() || node.value("kind", std::string()) != "Image") {
            continue;
        }
        const std::string assetId =
            node.value("managedAssetId", std::string());
        if (assetId.empty()) {
            continue;
        }
        const Stack::Project::EmbeddedAssetRecord* asset =
            Stack::Project::FindEmbeddedAsset(opened.snapshot, assetId);
        std::vector<std::uint8_t> bytes;
        if (!asset || !ReadManagedAssetBytes(opened.store, *asset, bytes)) {
            return false;
        }
        node["pngBytes"] = json::binary(std::move(bytes));
    }
    return true;
}

void RewriteManagedRawSourcePaths(
    const Stack::Project::ProjectStoreOpenResult& opened,
    json& pipelineData,
    json& rawWorkspaceData) {
    if (!opened.store ||
        opened.store->StorageKind() !=
            Stack::Project::ProjectStorageKind::DirectoryBundle ||
        !rawWorkspaceData.is_object()) {
        return;
    }
    const std::string assetId =
        rawWorkspaceData.value("managedAssetId", std::string());
    const Stack::Project::EmbeddedAssetRecord* asset =
        Stack::Project::FindEmbeddedAsset(opened.snapshot, assetId);
    if (!asset || asset->projectAssetPath.empty()) {
        return;
    }
    const std::string managedPath = (
        opened.store->StoragePath() /
        std::filesystem::u8path(asset->projectAssetPath)).lexically_normal().u8string();
    if (rawWorkspaceData.contains("rawRecipe") &&
        rawWorkspaceData["rawRecipe"].is_object()) {
        rawWorkspaceData["rawRecipe"]["sourceRef"]["sourcePath"] =
            managedPath;
    }
    if (!pipelineData.is_object() ||
        !pipelineData.contains("nodeGraph") ||
        !pipelineData["nodeGraph"].is_object() ||
        !pipelineData["nodeGraph"].contains("nodes") ||
        !pipelineData["nodeGraph"]["nodes"].is_array()) {
        return;
    }
    for (json& node : pipelineData["nodeGraph"]["nodes"]) {
        if (!node.is_object()) {
            continue;
        }
        const std::string kind = node.value("kind", std::string());
        if (kind == "RawDevelopment" && node.contains("rawRecipe") &&
            node["rawRecipe"].is_object()) {
            node["rawRecipe"]["sourceRef"]["sourcePath"] = managedPath;
        } else if (kind == "RawSource") {
            node["sourcePath"] = managedPath;
            if (node.contains("rawMetadata") && node["rawMetadata"].is_object()) {
                node["rawMetadata"]["sourcePath"] = managedPath;
            }
        }
    }
}

} // namespace

bool ExternalizeManagedProjectAssets(
    const Stack::Project::ProjectStoreHandle& store,
    const Stack::Project::ProjectStoreTransaction& transaction,
    Stack::Project::RawProjectSnapshot& snapshot) {
    return ExternalizeGraphImageAssets(store, transaction, snapshot);
}

static bool WriteProjectFileCaptured(
    const std::filesystem::path& path,
    const ProjectDocument& document,
    bool requireNewStore,
    std::optional<std::uint64_t> expectedStorageRevision,
    ProjectWriteResult& result) {
    if (requireNewStore && document.projectStore) return false;
    if (document.rawProjectSnapshot) {
        if (expectedStorageRevision && *expectedStorageRevision != 0 && !document.projectStore) {
            result.commit.message = "The captured project store is no longer available.";
            return false;
        }
        Stack::Project::RawProjectSnapshot snapshot = *document.rawProjectSnapshot;
        snapshot.projectName = document.metadata.projectName;
        snapshot.projectKindHint = document.metadata.projectKind.empty()
            ? snapshot.projectKindHint
            : document.metadata.projectKind;
        snapshot.pipelineData = document.pipelineData.is_null()
            ? json::object()
            : document.pipelineData;
        snapshot.rawWorkspaceData = document.rawWorkspaceData.is_null()
            ? json::object()
            : document.rawWorkspaceData;
        snapshot.coverThumbnailBytes = document.thumbnailBytes;
        if (!document.adoptedFrom.empty()) {
            snapshot.adoptedFrom =
                document.adoptedFrom.lexically_normal().u8string();
        }

        if (document.projectStore) {
            const std::filesystem::path currentPath =
                document.projectStore->StoragePath().lexically_normal();
            std::filesystem::path destination = path.lexically_normal();
            if (destination.extension() == ".stack" &&
                destination.filename() != "project.stack") {
                destination = destination.parent_path() / destination.stem();
            } else {
                destination = Stack::Project::ResolveProjectStoreRoot(destination);
            }
            const Stack::Project::ProjectStoreTransaction transaction =
                document.projectStore->BeginTransaction(
                    expectedStorageRevision.value_or(snapshot.persistedStorageRevision));
            if (!transaction) return false;
            if (!ExternalizeManagedProjectAssets(
                    document.projectStore, transaction, snapshot)) {
                document.projectStore->Abort(transaction);
                return false;
            }
            const Stack::Project::ProjectStoreCommitResult commit =
                document.projectStore->Commit(transaction, snapshot);
            result.commit = commit;
            if (!commit) {
                document.projectStore->Abort(transaction);
                return false;
            }
            snapshot.persistedStorageRevision =
                commit.committedStorageRevision;
            if (currentPath != destination) {
                auto converted = Stack::Project::ConvertProjectStore(
                    document.projectStore,
                    snapshot,
                    destination,
                    Stack::Project::ProjectStorageKind::DirectoryBundle);
                if (!converted) {
                    result.commit = { Stack::Project::ProjectStoreCommitStatus::IoFailure,
                        0, std::move(converted.message) };
                    return false;
                }
                result.store = std::move(converted.store);
                result.snapshot = std::move(converted.snapshot);
                result.commit.committedStorageRevision = result.snapshot.persistedStorageRevision;
                return true;
            }
            result.store = document.projectStore;
            result.snapshot = std::move(snapshot);
            return true;
        }

        if (!snapshot.embeddedAssets.empty()) {
            // Original bytes are intentionally unavailable without a store;
            // never manufacture a v3 project whose manifest references them.
            return false;
        }
        std::filesystem::path destination = path.lexically_normal();
        if (destination.extension() == ".stack" &&
            destination.filename() != "project.stack") {
            destination = destination.parent_path() / destination.stem();
        }
        auto created = Stack::Project::CreateProjectStore(
            destination,
            Stack::Project::ProjectStorageKind::DirectoryBundle,
            snapshot);
        if (!created) {
            result.commit.message = std::move(created.message);
            return false;
        }
        result.store = std::move(created.store);
        result.snapshot = std::move(created.snapshot);
        result.commit = { Stack::Project::ProjectStoreCommitStatus::Committed,
            result.snapshot.persistedStorageRevision, {} };
        return true;
    }

    // Every current working document uses the same folder-backed store. A
    // "Name.stack" destination is interpreted as the working
    // folder "Name"; portable .stack files are created only by Pack Project.
    std::filesystem::path projectRoot = path.lexically_normal();
    std::string extension = projectRoot.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    std::string filename = projectRoot.filename().u8string();
    std::transform(filename.begin(), filename.end(), filename.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension == ".stack" && filename != "project.stack") {
        projectRoot = projectRoot.parent_path() / projectRoot.stem();
    } else {
        projectRoot = Stack::Project::ResolveProjectStoreRoot(projectRoot);
    }

    Stack::Project::ProjectStoreOpenResult opened;
    if (expectedStorageRevision && *expectedStorageRevision == 0) requireNewStore = true;
    if (!requireNewStore && Stack::Project::IsDirectoryProjectBundle(projectRoot)) {
        opened = Stack::Project::OpenProjectStore(projectRoot);
        if (!opened) return false;
        if (expectedStorageRevision &&
            (opened.snapshot.persistedStorageRevision != *expectedStorageRevision ||
             (!document.projectId.empty() && opened.snapshot.projectId != document.projectId))) {
            result.commit = { Stack::Project::ProjectStoreCommitStatus::Conflict,
                opened.snapshot.persistedStorageRevision,
                "The project changed outside this session. Reload or save a copy." };
            return false;
        }
    } else {
        if (expectedStorageRevision && *expectedStorageRevision != 0) {
            result.commit.message = "The captured project store is no longer available.";
            return false;
        }
        Stack::Project::RawProjectSnapshot bootstrap;
        bootstrap.projectId = document.projectId.empty()
            ? Stack::Project::GenerateStableUuid()
            : document.projectId;
        bootstrap.projectName = document.metadata.projectName.empty()
            ? projectRoot.filename().u8string()
            : document.metadata.projectName;
        bootstrap.projectKindHint = document.metadata.projectKind.empty()
            ? std::string(kEditorProjectKind)
            : document.metadata.projectKind;
        bootstrap.lifecycle.creationOrigin =
            Stack::Project::ProjectCreationOrigin::Manual;
        opened = Stack::Project::CreateProjectStore(
            projectRoot,
            Stack::Project::ProjectStorageKind::DirectoryBundle,
            bootstrap);
        if (!opened) {
            result.commit.message = std::move(opened.message);
            return false;
        }
    }

    Stack::Project::RawProjectSnapshot snapshot = opened.snapshot;
    snapshot.projectName = document.metadata.projectName.empty()
        ? snapshot.projectName
        : document.metadata.projectName;
    snapshot.projectKindHint = document.metadata.projectKind.empty()
        ? std::string(kEditorProjectKind)
        : document.metadata.projectKind;
    snapshot.pipelineData = document.pipelineData.is_null()
        ? json::object()
        : document.pipelineData;
    snapshot.rawWorkspaceData = document.rawWorkspaceData.is_null()
        ? json::object()
        : document.rawWorkspaceData;
    snapshot.coverThumbnailBytes = document.thumbnailBytes;
    snapshot.timestamp = document.metadata.timestamp;
    snapshot.sourceWidth = document.metadata.sourceWidth;
    snapshot.sourceHeight = document.metadata.sourceHeight;
    if (!document.adoptedFrom.empty()) {
        snapshot.adoptedFrom =
            document.adoptedFrom.lexically_normal().u8string();
    }

    json nodeBrowserThumbnails = json::array();
    for (const NodeBrowserThumbnailEntry& entry :
         document.nodeBrowserThumbnailEntries) {
        nodeBrowserThumbnails.push_back({
            { "previewKey", entry.previewKey },
            { "previewSeedHash", entry.previewSeedHash },
            { "previewRecipeVersion", entry.previewRecipeVersion },
            { "pngBytes", entry.pngBytes }
        });
    }
    snapshot.nodeBrowserThumbnails = std::move(nodeBrowserThumbnails);

    const Stack::Project::ProjectStoreTransaction transaction =
        opened.store->BeginTransaction(snapshot.persistedStorageRevision);
    if (!transaction) return false;

    if (!ExternalizeManagedProjectAssets(
            opened.store, transaction, snapshot)) {
        opened.store->Abort(transaction);
        return false;
    }

    if (!document.sourceImageBytes.empty()) {
        const std::vector<std::uint8_t> sourceBytes(
            document.sourceImageBytes.begin(), document.sourceImageBytes.end());
        const Stack::RawEvidence::SourceIdentity identity =
            Stack::RawEvidence::ComputeSourceIdentity(sourceBytes);
        if (!identity.valid) {
            opened.store->Abort(transaction);
            return false;
        }
        Stack::Project::EmbeddedAssetRecord sourceAsset;
        sourceAsset.assetId = Stack::Project::MakeAssetId(
            identity.sha256, identity.byteSize);
        sourceAsset.sha256 = identity.sha256;
        sourceAsset.byteLength = identity.byteSize;
        sourceAsset.displayName = "Project Source";
        sourceAsset.projectAssetPath = (
            std::filesystem::path("assets") / "source" /
            (sourceAsset.sha256 + ".png")).generic_string();
        sourceAsset.originalFileFingerprint = identity.sha256;
        sourceAsset.originalFilename = "source.png";
        sourceAsset.inputFamily = Stack::Project::MultiFrameInputFamily::Raster;
        sourceAsset.captureMetadataSummary = json::object();
        const bool alreadyManaged = Stack::Project::FindEmbeddedAsset(
            snapshot, sourceAsset.assetId) != nullptr;
        if (!alreadyManaged) {
            std::string sourceText(
                reinterpret_cast<const char*>(document.sourceImageBytes.data()),
                document.sourceImageBytes.size());
            std::istringstream sourceStream(
                sourceText,
                std::ios::in | std::ios::binary);
            if (!opened.store->StageAssetStream(
                    transaction, sourceStream, sourceAsset)) {
                opened.store->Abort(transaction);
                return false;
            }
            snapshot.embeddedAssets.push_back(std::move(sourceAsset));
        }
        snapshot.sourceAssetId =
            Stack::Project::MakeAssetId(identity.sha256, identity.byteSize);
    }

    const Stack::Project::ProjectStoreCommitResult committed =
        opened.store->Commit(transaction, snapshot);
    result.commit = committed;
    if (!committed) {
        opened.store->Abort(transaction);
        return false;
    }
    snapshot.persistedStorageRevision = committed.committedStorageRevision;
    result.store = std::move(opened.store);
    result.snapshot = std::move(snapshot);
    return true;
}

ProjectWriteResult WriteProjectFileWithResult(
    const std::filesystem::path& path,
    const ProjectDocument& document,
    bool requireNewStore,
    std::optional<std::uint64_t> expectedStorageRevision) {
    ProjectWriteResult result;
    if (!WriteProjectFileCaptured(path, document, requireNewStore, expectedStorageRevision, result) &&
        result.commit.message.empty()) {
        result.commit.message = "The project snapshot could not be published.";
    }
    return result;
}

bool WriteProjectFile(const std::filesystem::path& path,
    const ProjectDocument& document, bool requireNewStore) {
    return static_cast<bool>(WriteProjectFileWithResult(path, document, requireNewStore));
}

bool ReadProjectFile(const std::filesystem::path& path, ProjectDocument& document, const ProjectLoadOptions& options) {
    if (Stack::Project::IsDirectoryProjectBundle(path) ||
        Stack::Project::IsPortableV3Project(path)) {
        Stack::Project::ProjectStoreOpenResult opened =
            Stack::Project::OpenProjectStore(path);
        if (!opened) return false;
        document = {};
        document.projectId = opened.snapshot.projectId;
        if (!opened.snapshot.adoptedFrom.empty()) {
            document.adoptedFrom = std::filesystem::u8path(opened.snapshot.adoptedFrom);
        }
        document.metadata.projectKind = opened.snapshot.projectKindHint.empty()
            ? std::string(kEditorProjectKind)
            : opened.snapshot.projectKindHint;
        document.metadata.projectName = opened.snapshot.projectName;
        document.metadata.timestamp = opened.snapshot.timestamp;
        document.metadata.sourceWidth = opened.snapshot.sourceWidth;
        document.metadata.sourceHeight = opened.snapshot.sourceHeight;
        if (options.includeThumbnail) {
            document.thumbnailBytes = opened.snapshot.coverThumbnailBytes;
        }
        if (options.includePipelineData) {
            document.pipelineData = opened.snapshot.pipelineData;
            if (!RehydrateGraphImageAssets(opened, document.pipelineData)) {
                return false;
            }
        }
        if (options.includeSourceImage) {
            const std::string& sourceAssetId = opened.snapshot.sourceAssetId;
            auto sourceAsset = opened.snapshot.embeddedAssets.end();
            if (!sourceAssetId.empty()) {
                sourceAsset = std::find_if(
                    opened.snapshot.embeddedAssets.begin(),
                    opened.snapshot.embeddedAssets.end(),
                    [&](const Stack::Project::EmbeddedAssetRecord& asset) {
                        return asset.assetId == sourceAssetId;
                    });
            }
            if (sourceAsset != opened.snapshot.embeddedAssets.end() &&
                sourceAsset->byteLength <= static_cast<std::uint64_t>(
                    std::numeric_limits<std::size_t>::max())) {
                Stack::Project::ProjectAssetStream stream =
                    opened.store->OpenAssetStream(sourceAsset->assetId);
                if (stream) {
                    document.sourceImageBytes.resize(
                        static_cast<std::size_t>(sourceAsset->byteLength));
                    stream.stream->read(
                        reinterpret_cast<char*>(document.sourceImageBytes.data()),
                        static_cast<std::streamsize>(
                            document.sourceImageBytes.size()));
                    if (stream.stream->gcount() != static_cast<std::streamsize>(
                            document.sourceImageBytes.size())) {
                        document.sourceImageBytes.clear();
                    }
                }
            }
        }
        if (options.includeRawWorkspaceData) {
            document.rawWorkspaceData = opened.snapshot.rawWorkspaceData;
            if (!document.rawWorkspaceData.is_object()) {
                document.rawWorkspaceData = json::object();
            }
            if (document.metadata.projectKind == kRawProjectKind) {
                document.rawWorkspaceData["schema"] = "stack.rawWorkspace.project";
                document.rawWorkspaceData["schemaVersion"] =
                    Stack::Project::kRawWorkspaceProjectSchemaVersion;
                document.rawWorkspaceData["rawProjectModel"] =
                    Stack::Project::kRawProjectModelSourceSets;
                document.rawWorkspaceData["activeSourceSetId"] =
                    opened.snapshot.activeSourceSetId;
            }
        }
        RewriteManagedRawSourcePaths(
            opened,
            document.pipelineData,
            document.rawWorkspaceData);
        if (options.includeNodeBrowserThumbnails) {
            const json& entries = opened.snapshot.nodeBrowserThumbnails;
            if (entries.is_array()) {
                for (const json& value : entries) {
                    if (!value.is_object()) continue;
                    NodeBrowserThumbnailEntry entry;
                    entry.previewKey = value.value("previewKey", std::string());
                    entry.previewSeedHash = value.value(
                        "previewSeedHash", std::string());
                    entry.previewRecipeVersion = value.value(
                        "previewRecipeVersion", 0u);
                    const json bytes = value.value("pngBytes", json::array());
                    if (bytes.is_array()) {
                        for (const json& byte : bytes) {
                            if (byte.is_number_unsigned()) {
                                entry.pngBytes.push_back(static_cast<unsigned char>(
                                    byte.get<unsigned int>()));
                            }
                        }
                    }
                    if (!entry.previewKey.empty()) {
                        document.nodeBrowserThumbnailEntries.push_back(
                            std::move(entry));
                    }
                }
            }
        }
        document.projectStore = std::move(opened.store);
        document.rawProjectSnapshot =
            std::make_shared<Stack::Project::RawProjectSnapshot>(
                std::move(opened.snapshot));
        return true;
    }

    return false;
}

bool WriteNodePresetFile(const std::filesystem::path& path, const NodePresetDocument& document) try {
    std::vector<SectionData> sections;
    sections.push_back(MakeSectionData(FileKind::NodePreset, kMetaSection, SerializeJson(NodePresetMetadataToJson(document.metadata))));
    sections.push_back(MakeSectionData(FileKind::NodePreset, kThumbnailSection, document.thumbnailBytes));
    sections.push_back(MakeSectionData(FileKind::NodePreset, kPipelineSection, SerializeJson(document.graphPayload.is_null() ? json::object() : document.graphPayload)));
    sections.push_back(MakeSectionData(FileKind::NodePreset, kPresetBoundarySection, SerializeJson(NodePresetBoundarySocketsToJson(document.boundarySockets))));
    return WriteSectionedFile(path, FileKind::NodePreset, sections);
} catch (const std::exception&) {
    return false;
}

bool ReadNodePresetFile(const std::filesystem::path& path, NodePresetDocument& output, const NodePresetLoadOptions& options) try {
    NodePresetDocument document;
    std::ifstream file;
    std::unordered_map<std::string, SectionInfo> sections;
    if (!ReadSectionTable(path, FileKind::NodePreset, file, sections, options.verifyChecksum)) {
        return false;
    }

    std::vector<unsigned char> metaBytes;
    if (!ReadSectionBytes(file, sections, kMetaSection, metaBytes)) {
        return false;
    }

    json metaJson;
    if (!DeserializeJson(metaBytes, metaJson) || !NodePresetMetadataFromJson(metaJson, document.metadata)) {
        return false;
    }

    if (options.includeThumbnail) {
        if (sections.count(SectionKey(kThumbnailSection)) &&
            !ReadSectionBytes(file, sections, kThumbnailSection, document.thumbnailBytes)) {
            return false;
        }
    }

    if (options.includeGraphPayload) {
        std::vector<unsigned char> graphBytes;
        if (sections.count(SectionKey(kPipelineSection))) {
            if (!ReadSectionBytes(file, sections, kPipelineSection, graphBytes) ||
                !DeserializeJson(graphBytes, document.graphPayload)) {
                return false;
            }
        } else {
            document.graphPayload = json::object();
        }
    } else {
        document.graphPayload = json();
    }

    if (options.includeBoundarySockets) {
        std::vector<unsigned char> boundaryBytes;
        if (sections.count(SectionKey(kPresetBoundarySection))) {
            json boundaryJson;
            if (!ReadSectionBytes(file, sections, kPresetBoundarySection, boundaryBytes) ||
                !DeserializeJson(boundaryBytes, boundaryJson) || !boundaryJson.is_array()) {
                return false;
            }
            document.boundarySockets = NodePresetBoundarySocketsFromJson(boundaryJson);
        } else {
            document.boundarySockets.clear();
        }
    } else {
        document.boundarySockets.clear();
    }

    output = std::move(document);
    return true;
} catch (const std::exception&) {
    // Malformed metadata and allocation failures must not escape into library
    // scanning or the preset import UI.
    return false;
}

bool WriteLibraryBundle(const std::filesystem::path& path, const LibraryBundleDocument& document) try {
    json meta = json::object();
    meta["bundleName"] = document.bundleName;
    meta["timestamp"] = document.timestamp;

    json projects = json::array();
    for (const auto& project : document.projects) {
        projects.push_back(BundledProjectToJson(project));
    }

    json assets = json::array();
    for (const auto& asset : document.assets) {
        assets.push_back(AssetToJson(asset));
    }

    std::vector<SectionData> sections;
    sections.push_back(MakeSectionData(FileKind::LibraryBundle, kMetaSection, SerializeJson(meta)));
    sections.push_back(MakeSectionData(FileKind::LibraryBundle, kProjectsSection, SerializeJson(projects)));
    sections.push_back(MakeSectionData(FileKind::LibraryBundle, kAssetsSection, SerializeJson(assets)));
    return WriteSectionedFile(path, FileKind::LibraryBundle, sections);
} catch (const std::exception&) {
    return false;
}

bool ReadLibraryBundle(const std::filesystem::path& path, LibraryBundleDocument& output) try {
    LibraryBundleDocument document;
    std::ifstream file;
    std::unordered_map<std::string, SectionInfo> sections;
    if (!ReadSectionTable(path, FileKind::LibraryBundle, file, sections)) {
        return false;
    }

    std::vector<unsigned char> metaBytes;
    if (!ReadSectionBytes(file, sections, kMetaSection, metaBytes)) {
        return false;
    }

    json metaJson;
    if (!DeserializeJson(metaBytes, metaJson) || !metaJson.is_object()) {
        return false;
    }
    document.bundleName = metaJson.value("bundleName", "Modular Studio Library");
    document.timestamp = metaJson.value("timestamp", "Unknown");

    std::vector<unsigned char> projectBytes;
    if (sections.count(SectionKey(kProjectsSection))) {
        json projectsJson;
        if (!ReadSectionBytes(file, sections, kProjectsSection, projectBytes) ||
            !DeserializeJson(projectBytes, projectsJson) || !projectsJson.is_array()) {
            return false;
        }

        document.projects.clear();
        for (const auto& projectJson : projectsJson) {
            BundledProjectDocument project;
            if (!BundledProjectFromJson(projectJson, project)) {
                return false;
            }
            document.projects.push_back(std::move(project));
        }
    }

    std::vector<unsigned char> assetBytes;
    if (sections.count(SectionKey(kAssetsSection))) {
        json assetsJson;
        if (!ReadSectionBytes(file, sections, kAssetsSection, assetBytes) ||
            !DeserializeJson(assetBytes, assetsJson) || !assetsJson.is_array()) {
            return false;
        }

        document.assets.clear();
        for (const auto& assetJson : assetsJson) {
            AssetDocument asset;
            if (!AssetFromJson(assetJson, asset)) {
                return false;
            }
            document.assets.push_back(std::move(asset));
        }
    }

    output = std::move(document);
    return true;
} catch (const std::exception&) {
    return false;
}

bool AreProjectsIdentical(const ProjectDocument& a, const ProjectDocument& b) {
    if (a.metadata.projectKind != b.metadata.projectKind) return false;
    if (a.metadata.projectName != b.metadata.projectName) return false;
    if (a.metadata.sourceWidth != b.metadata.sourceWidth) return false;
    if (a.metadata.sourceHeight != b.metadata.sourceHeight) return false;
    if (a.sourceImageBytes != b.sourceImageBytes) return false;
    if (a.pipelineData != b.pipelineData) return false;
    if (a.rawWorkspaceData != b.rawWorkspaceData) return false;
    if (static_cast<bool>(a.rawProjectSnapshot) !=
        static_cast<bool>(b.rawProjectSnapshot)) return false;
    if (a.rawProjectSnapshot && b.rawProjectSnapshot &&
        Stack::Project::SerializeRawProjectSnapshot(*a.rawProjectSnapshot) !=
            Stack::Project::SerializeRawProjectSnapshot(*b.rawProjectSnapshot)) return false;
    return true;
}

} // namespace StackBinaryFormat
