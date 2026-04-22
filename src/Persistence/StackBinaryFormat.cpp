#include "StackBinaryFormat.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <unordered_map>

namespace StackBinaryFormat {
namespace {

constexpr std::array<char, 4> kMagic = { 'M', 'S', 'T', 'K' };
constexpr std::uint16_t kFormatVersion = 1;

constexpr std::array<char, 4> kMetaSection = { 'M', 'E', 'T', 'A' };
constexpr std::array<char, 4> kThumbnailSection = { 'T', 'H', 'M', 'B' };
constexpr std::array<char, 4> kSourceSection = { 'S', 'R', 'C', 'I' };
constexpr std::array<char, 4> kPipelineSection = { 'P', 'I', 'P', 'E' };
constexpr std::array<char, 4> kProjectsSection = { 'P', 'R', 'O', 'J' };
constexpr std::array<char, 4> kAssetsSection = { 'A', 'S', 'S', 'T' };

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

struct SectionData {
    std::array<char, 4> id;
    std::vector<unsigned char> bytes;
};

struct SectionInfo {
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

class ByteWriter {
public:
    template <typename T>
    void WritePod(const T& value) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
        m_Bytes.insert(m_Bytes.end(), bytes, bytes + sizeof(T));
    }

    void WriteString(const std::string& value) {
        const std::uint32_t size = static_cast<std::uint32_t>(value.size());
        WritePod(size);
        m_Bytes.insert(m_Bytes.end(), value.begin(), value.end());
    }

    void WriteBinary(const std::vector<unsigned char>& value) {
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

private:
    bool CanRead(std::size_t size) const {
        return (m_Offset + size) <= m_Bytes.size();
    }

    const std::vector<unsigned char>& m_Bytes;
    std::size_t m_Offset = 0;
};

std::string SectionKey(const std::array<char, 4>& id) {
    return std::string(id.data(), id.size());
}

json MakeBinaryJson(const std::vector<unsigned char>& bytes) {
    json::binary_t::container_type binaryBytes(bytes.begin(), bytes.end());
    return json::binary(std::move(binaryBytes));
}

bool EncodeJsonValue(ByteWriter& writer, const json& value) {
    if (value.is_null()) {
        writer.WritePod(static_cast<std::uint8_t>(ValueType::Null));
        return true;
    }

    if (value.is_boolean()) {
        writer.WritePod(static_cast<std::uint8_t>(value.get<bool>() ? ValueType::BoolTrue : ValueType::BoolFalse));
        return true;
    }

    if (value.is_number_integer()) {
        writer.WritePod(static_cast<std::uint8_t>(ValueType::Int64));
        const std::int64_t integerValue = value.get<std::int64_t>();
        writer.WritePod(integerValue);
        return true;
    }

    if (value.is_number_unsigned()) {
        writer.WritePod(static_cast<std::uint8_t>(ValueType::UInt64));
        const std::uint64_t integerValue = value.get<std::uint64_t>();
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
        writer.WritePod(static_cast<std::uint8_t>(ValueType::Array));
        const std::uint32_t count = static_cast<std::uint32_t>(value.size());
        writer.WritePod(count);
        for (const auto& item : value) {
            if (!EncodeJsonValue(writer, item)) return false;
        }
        return true;
    }

    if (value.is_object()) {
        writer.WritePod(static_cast<std::uint8_t>(ValueType::Object));
        const std::uint32_t count = static_cast<std::uint32_t>(value.size());
        writer.WritePod(count);
        for (auto it = value.begin(); it != value.end(); ++it) {
            writer.WriteString(it.key());
            if (!EncodeJsonValue(writer, it.value())) return false;
        }
        return true;
    }

    return false;
}

bool DecodeJsonValue(ByteReader& reader, json& value) {
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
            if (!reader.ReadPod(count)) return false;
            value = json::array();
            for (std::uint32_t index = 0; index < count; ++index) {
                json item;
                if (!DecodeJsonValue(reader, item)) return false;
                value.push_back(std::move(item));
            }
            return true;
        }

        case ValueType::Object: {
            std::uint32_t count = 0;
            if (!reader.ReadPod(count)) return false;
            value = json::object();
            for (std::uint32_t index = 0; index < count; ++index) {
                std::string key;
                json item;
                if (!reader.ReadString(key) || !DecodeJsonValue(reader, item)) return false;
                value[key] = std::move(item);
            }
            return true;
        }
    }

    return false;
}

std::vector<unsigned char> SerializeJson(const json& value) {
    ByteWriter writer;
    EncodeJsonValue(writer, value);
    return writer.TakeBytes();
}

bool DeserializeJson(const std::vector<unsigned char>& bytes, json& value) {
    ByteReader reader(bytes);
    return DecodeJsonValue(reader, value);
}

bool WriteSectionedFile(const std::filesystem::path& path, FileKind kind, const std::vector<SectionData>& sections) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    const std::uint32_t sectionCount = static_cast<std::uint32_t>(sections.size());
    const std::uint64_t headerSize =
        static_cast<std::uint64_t>(kMagic.size()) +
        sizeof(kFormatVersion) +
        sizeof(kind) +
        sizeof(sectionCount) +
        sectionCount * (4 + sizeof(std::uint64_t) + sizeof(std::uint64_t));

    std::uint64_t dataOffset = headerSize;

    file.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    file.write(reinterpret_cast<const char*>(&kFormatVersion), sizeof(kFormatVersion));
    file.write(reinterpret_cast<const char*>(&kind), sizeof(kind));
    file.write(reinterpret_cast<const char*>(&sectionCount), sizeof(sectionCount));

    for (const auto& section : sections) {
        file.write(section.id.data(), static_cast<std::streamsize>(section.id.size()));
        const std::uint64_t sectionOffset = dataOffset;
        const std::uint64_t sectionSize = static_cast<std::uint64_t>(section.bytes.size());
        file.write(reinterpret_cast<const char*>(&sectionOffset), sizeof(sectionOffset));
        file.write(reinterpret_cast<const char*>(&sectionSize), sizeof(sectionSize));
        dataOffset += sectionSize;
    }

    for (const auto& section : sections) {
        if (!section.bytes.empty()) {
            file.write(reinterpret_cast<const char*>(section.bytes.data()), static_cast<std::streamsize>(section.bytes.size()));
        }
    }

    return file.good();
}

bool ReadSectionTable(
    const std::filesystem::path& path,
    FileKind expectedKind,
    std::ifstream& file,
    std::unordered_map<std::string, SectionInfo>& sections) {

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

    if (!file.good() || version != kFormatVersion || rawKind != static_cast<std::uint16_t>(expectedKind)) {
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::uint64_t fileSize = static_cast<std::uint64_t>(file.tellg());
    file.seekg(static_cast<std::streamoff>(kMagic.size() + sizeof(version) + sizeof(rawKind) + sizeof(sectionCount)), std::ios::beg);

    for (std::uint32_t index = 0; index < sectionCount; ++index) {
        std::array<char, 4> id = {};
        SectionInfo info;
        file.read(id.data(), static_cast<std::streamsize>(id.size()));
        file.read(reinterpret_cast<char*>(&info.offset), sizeof(info.offset));
        file.read(reinterpret_cast<char*>(&info.size), sizeof(info.size));
        if (!file.good()) return false;
        if ((info.offset + info.size) > fileSize) return false;
        sections[SectionKey(id)] = info;
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

    bytes.assign(static_cast<std::size_t>(it->second.size), 0);
    if (!bytes.empty()) {
        file.seekg(static_cast<std::streamoff>(it->second.offset), std::ios::beg);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file.good()) return false;
    }

    return true;
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

    if (value.contains("thumbnailPng") && value["thumbnailPng"].is_binary()) {
        const auto& binaryValue = value["thumbnailPng"].get_binary();
        project.project.thumbnailBytes.assign(binaryValue.begin(), binaryValue.end());
    }

    if (value.contains("sourcePng") && value["sourcePng"].is_binary()) {
        const auto& binaryValue = value["sourcePng"].get_binary();
        project.project.sourceImageBytes.assign(binaryValue.begin(), binaryValue.end());
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

} // namespace

bool WriteProjectFile(const std::filesystem::path& path, const ProjectDocument& document) {
    std::vector<SectionData> sections;
    sections.push_back({ kMetaSection, SerializeJson(ProjectMetadataToJson(document.metadata)) });
    sections.push_back({ kThumbnailSection, document.thumbnailBytes });
    sections.push_back({ kSourceSection, document.sourceImageBytes });
    sections.push_back({ kPipelineSection, SerializeJson(document.pipelineData.is_null() ? json::array() : document.pipelineData) });
    return WriteSectionedFile(path, FileKind::Project, sections);
}

bool ReadProjectFile(const std::filesystem::path& path, ProjectDocument& document, const ProjectLoadOptions& options) {
    std::ifstream file;
    std::unordered_map<std::string, SectionInfo> sections;
    if (!ReadSectionTable(path, FileKind::Project, file, sections)) {
        return false;
    }

    std::vector<unsigned char> metaBytes;
    if (!ReadSectionBytes(file, sections, kMetaSection, metaBytes)) {
        return false;
    }

    json metaJson;
    if (!DeserializeJson(metaBytes, metaJson) || !ProjectMetadataFromJson(metaJson, document.metadata)) {
        return false;
    }

    if (options.includeThumbnail) {
        ReadSectionBytes(file, sections, kThumbnailSection, document.thumbnailBytes);
    } else {
        document.thumbnailBytes.clear();
    }

    if (options.includeSourceImage) {
        ReadSectionBytes(file, sections, kSourceSection, document.sourceImageBytes);
    } else {
        document.sourceImageBytes.clear();
    }

    if (options.includePipelineData) {
        std::vector<unsigned char> pipelineBytes;
        if (!ReadSectionBytes(file, sections, kPipelineSection, pipelineBytes)) {
            document.pipelineData = json::array();
        } else if (!DeserializeJson(pipelineBytes, document.pipelineData)) {
            return false;
        }
    } else {
        document.pipelineData = json();
    }

    return true;
}

bool WriteLibraryBundle(const std::filesystem::path& path, const LibraryBundleDocument& document) {
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
    sections.push_back({ kMetaSection, SerializeJson(meta) });
    sections.push_back({ kProjectsSection, SerializeJson(projects) });
    sections.push_back({ kAssetsSection, SerializeJson(assets) });
    return WriteSectionedFile(path, FileKind::LibraryBundle, sections);
}

bool ReadLibraryBundle(const std::filesystem::path& path, LibraryBundleDocument& document) {
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
    if (ReadSectionBytes(file, sections, kProjectsSection, projectBytes)) {
        json projectsJson;
        if (!DeserializeJson(projectBytes, projectsJson) || !projectsJson.is_array()) {
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
    if (ReadSectionBytes(file, sections, kAssetsSection, assetBytes)) {
        json assetsJson;
        if (!DeserializeJson(assetBytes, assetsJson) || !assetsJson.is_array()) {
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

    return true;
}

bool AreProjectsIdentical(const ProjectDocument& a, const ProjectDocument& b) {
    if (a.metadata.projectKind != b.metadata.projectKind) return false;
    if (a.metadata.projectName != b.metadata.projectName) return false;
    if (a.metadata.sourceWidth != b.metadata.sourceWidth) return false;
    if (a.metadata.sourceHeight != b.metadata.sourceHeight) return false;
    if (a.sourceImageBytes != b.sourceImageBytes) return false;
    if (a.pipelineData != b.pipelineData) return false;
    return true;
}

} // namespace StackBinaryFormat
