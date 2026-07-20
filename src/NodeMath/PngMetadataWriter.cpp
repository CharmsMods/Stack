#include "NodeMath/PngMetadataWriter.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace Stack::NodeMath {
namespace {

constexpr std::array<unsigned char, 8> kPngSignature = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a
};

std::uint32_t ReadBigEndian32(const unsigned char* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
        (static_cast<std::uint32_t>(bytes[1]) << 16) |
        (static_cast<std::uint32_t>(bytes[2]) << 8) |
        static_cast<std::uint32_t>(bytes[3]);
}

void AppendBigEndian32(std::vector<unsigned char>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<unsigned char>((value >> 24) & 0xff));
    bytes.push_back(static_cast<unsigned char>((value >> 16) & 0xff));
    bytes.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
    bytes.push_back(static_cast<unsigned char>(value & 0xff));
}

std::uint32_t UpdateCrc(std::uint32_t crc, unsigned char value) {
    crc ^= value;
    for (int bit = 0; bit < 8; ++bit) {
        crc = (crc & 1u) ? (crc >> 1) ^ 0xedb88320u : (crc >> 1);
    }
    return crc;
}

void AppendChunk(
    std::vector<unsigned char>& output,
    const char type[5],
    const std::vector<unsigned char>& payload) {
    AppendBigEndian32(output, static_cast<std::uint32_t>(payload.size()));
    std::uint32_t crc = 0xffffffffu;
    for (int index = 0; index < 4; ++index) {
        const unsigned char value = static_cast<unsigned char>(type[index]);
        output.push_back(value);
        crc = UpdateCrc(crc, value);
    }
    for (unsigned char value : payload) {
        output.push_back(value);
        crc = UpdateCrc(crc, value);
    }
    AppendBigEndian32(output, crc ^ 0xffffffffu);
}

bool ValidatePngAndFindIhdrEnd(
    const std::vector<unsigned char>& png,
    std::size_t& insertionOffset) {
    insertionOffset = 0;
    if (png.size() < 33 || !std::equal(kPngSignature.begin(), kPngSignature.end(), png.begin())) {
        return false;
    }
    const std::uint32_t length = ReadBigEndian32(png.data() + 8);
    const std::string type(reinterpret_cast<const char*>(png.data() + 12), 4);
    if (type != "IHDR" || length != 13 || 8u + 12u + length > png.size()) return false;
    insertionOffset = 8u + 12u + length;
    return true;
}

} // namespace

std::vector<unsigned char> InsertPngColorMetadataChunks(
    const std::vector<unsigned char>& png,
    const PngColorMetadataChunks& chunks,
    std::vector<ContractIssue>& issues) {
    std::size_t insertionOffset = 0;
    if (!ValidatePngAndFindIhdrEnd(png, insertionOffset)) {
        issues.push_back({ "png", "cannot attach color metadata to a malformed PNG" });
        return {};
    }
    const int requestedKinds = (chunks.writeSrgb ? 1 : 0) +
        (chunks.writeDisplayP3Cicp ? 1 : 0) +
        (!chunks.retainedIccpPayload.empty() ? 1 : 0);
    if (requestedKinds > 1) {
        issues.push_back({ "png.color-metadata", "PNG output policy must select only one color metadata representation" });
        return {};
    }
    if (requestedKinds == 0) return png;

    std::vector<unsigned char> metadata;
    if (chunks.writeSrgb) {
        AppendChunk(metadata, "sRGB", { 0 });
    } else if (chunks.writeDisplayP3Cicp) {
        AppendChunk(metadata, "cICP", { 12, 13, 0, 1 });
    } else {
        AppendChunk(metadata, "iCCP", chunks.retainedIccpPayload);
    }

    std::vector<unsigned char> output;
    output.reserve(png.size() + metadata.size());
    output.insert(output.end(), png.begin(), png.begin() + static_cast<std::ptrdiff_t>(insertionOffset));
    output.insert(output.end(), metadata.begin(), metadata.end());
    output.insert(output.end(), png.begin() + static_cast<std::ptrdiff_t>(insertionOffset), png.end());
    return output;
}

} // namespace Stack::NodeMath
