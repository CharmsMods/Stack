#include "Editor/MultiFrameResultCache.h"

#include "Editor/NodeGraph/Serialization/EditorNodeGraphRawSerialization.h"
#include "Raw/RawTechnicalEvidence.h"

#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <system_error>
#include <vector>

namespace Stack::EditorMultiFrameCache {
namespace {

constexpr std::array<char, 8> kMagic {
    'S', 'T', 'K', 'M', 'F', 'C', '1', '\0' };
constexpr std::uint64_t kMaximumHeaderBytes = 16u << 20u;

bool Fail(std::string* errorMessage, std::string message) {
    if (errorMessage) *errorMessage = std::move(message);
    return false;
}

std::filesystem::path ChecksumPath(const std::filesystem::path& path) {
    return std::filesystem::path(path.string() + ".sha256");
}

bool PublishTemporaryFile(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination,
    std::string* errorMessage) {
    std::error_code error;
    std::filesystem::remove(destination, error);
    error.clear();
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return Fail(errorMessage, "The MultiFrame result cache could not be published atomically.");
    }
    return true;
}

} // namespace

bool WriteBurstResult(
    const std::filesystem::path& path,
    const std::string& graphIdentity,
    std::uint64_t inputRevision,
    const Raw::RawImageData& raw,
    std::string* errorMessage) {
    if (path.empty() || graphIdentity.empty() ||
        !raw.normalizedMosaicBuffer || raw.normalizedMosaicBuffer->empty()) {
        return Fail(errorMessage, "The MultiFrame result cache input is incomplete.");
    }
    const int width = raw.metadata.rawWidth;
    const int height = raw.metadata.rawHeight;
    if (width <= 0 || height <= 0 ||
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) !=
            raw.normalizedMosaicBuffer->size()) {
        return Fail(errorMessage, "The MultiFrame result dimensions are invalid.");
    }
    for (float value : *raw.normalizedMosaicBuffer) {
        if (!std::isfinite(value)) {
            return Fail(errorMessage, "The MultiFrame result contains non-finite samples.");
        }
    }

    const nlohmann::json header = {
        { "contractId", "stack.multiframe-burst-result-cache" },
        { "formatVersion", 1u },
        { "graphIdentity", graphIdentity },
        { "inputRevision", inputRevision },
        { "normalizedMosaicContentHash", raw.normalizedMosaicContentHash },
        { "normalizedMosaicInputContract",
            Raw::NormalizedMosaicInputContractName(
                raw.normalizedMosaicInputContract) },
        { "pixelCount", raw.normalizedMosaicBuffer->size() },
        { "rawMetadata", EditorNodeGraph::SerializeRawMetadata(raw.metadata) }
    };
    const std::string headerText = header.dump();
    if (headerText.size() > kMaximumHeaderBytes) {
        return Fail(errorMessage, "The MultiFrame result cache header is too large.");
    }

    std::error_code filesystemError;
    std::filesystem::create_directories(path.parent_path(), filesystemError);
    if (filesystemError) {
        return Fail(errorMessage, "The MultiFrame result cache directory could not be created.");
    }
    const std::filesystem::path temporary = path.string() + ".tmp";
    const std::filesystem::path checksum = ChecksumPath(path);
    const std::filesystem::path temporaryChecksum = checksum.string() + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        const std::uint64_t headerSize = headerText.size();
        const std::uint64_t pixelCount = raw.normalizedMosaicBuffer->size();
        if (!stream ||
            !stream.write(kMagic.data(), kMagic.size()) ||
            !stream.write(
                reinterpret_cast<const char*>(&headerSize),
                sizeof(headerSize)) ||
            !stream.write(
                headerText.data(),
                static_cast<std::streamsize>(headerText.size())) ||
            !stream.write(
                reinterpret_cast<const char*>(&pixelCount),
                sizeof(pixelCount)) ||
            !stream.write(
                reinterpret_cast<const char*>(
                    raw.normalizedMosaicBuffer->data()),
                static_cast<std::streamsize>(
                    pixelCount * sizeof(float)))) {
            stream.close();
            std::filesystem::remove(temporary, filesystemError);
            return Fail(errorMessage, "The MultiFrame result cache could not be written.");
        }
    }
    const RawEvidence::SourceIdentity identity =
        RawEvidence::ComputeSourceIdentity(temporary);
    if (!identity.valid || identity.sha256.empty()) {
        std::filesystem::remove(temporary, filesystemError);
        return Fail(errorMessage, "The MultiFrame result cache could not be verified after writing.");
    }
    {
        std::ofstream stream(
            temporaryChecksum, std::ios::binary | std::ios::trunc);
        if (!stream || !stream.write(
                identity.sha256.data(),
                static_cast<std::streamsize>(identity.sha256.size()))) {
            stream.close();
            std::filesystem::remove(temporary, filesystemError);
            std::filesystem::remove(temporaryChecksum, filesystemError);
            return Fail(errorMessage, "The MultiFrame result checksum could not be written.");
        }
    }
    if (!PublishTemporaryFile(temporary, path, errorMessage) ||
        !PublishTemporaryFile(temporaryChecksum, checksum, errorMessage)) {
        return false;
    }
    if (errorMessage) errorMessage->clear();
    return true;
}

bool ReadBurstResult(
    const std::filesystem::path& path,
    const std::string& expectedGraphIdentity,
    std::uint64_t expectedInputRevision,
    std::shared_ptr<Raw::RawImageData>& raw,
    std::string* errorMessage) {
    raw.reset();
    std::ifstream checksumStream(ChecksumPath(path), std::ios::binary);
    std::string expectedChecksum;
    if (checksumStream) {
        checksumStream >> expectedChecksum;
    }
    const RawEvidence::SourceIdentity identity =
        RawEvidence::ComputeSourceIdentity(path);
    if (!identity.valid || expectedChecksum.size() != 64u ||
        identity.sha256 != expectedChecksum) {
        return Fail(errorMessage, "The MultiFrame result cache failed content verification.");
    }

    std::ifstream stream(path, std::ios::binary);
    std::array<char, 8> magic {};
    std::uint64_t headerSize = 0;
    stream.read(magic.data(), magic.size());
    stream.read(reinterpret_cast<char*>(&headerSize), sizeof(headerSize));
    if (!stream || magic != kMagic || headerSize > kMaximumHeaderBytes) {
        return Fail(errorMessage, "The MultiFrame result cache header is corrupt.");
    }
    std::string headerText(static_cast<std::size_t>(headerSize), '\0');
    stream.read(
        headerText.data(), static_cast<std::streamsize>(headerText.size()));
    nlohmann::json header = nlohmann::json::parse(
        headerText, nullptr, false);
    if (!stream || header.is_discarded() || !header.is_object() ||
        header.value("contractId", std::string()) !=
            "stack.multiframe-burst-result-cache" ||
        header.value("formatVersion", 0u) != 1u ||
        header.value("graphIdentity", std::string()) !=
            expectedGraphIdentity ||
        header.value("inputRevision", std::uint64_t { 0 }) !=
            expectedInputRevision ||
        header.value("normalizedMosaicInputContract", std::string()) !=
            "mfd-reference-pre-gain") {
        return Fail(errorMessage, "The MultiFrame result cache does not match the saved graph.");
    }

    std::uint64_t pixelCount = 0;
    stream.read(reinterpret_cast<char*>(&pixelCount), sizeof(pixelCount));
    const nlohmann::json metadataJson = header.value(
        "rawMetadata", nlohmann::json::object());
    Raw::RawMetadata metadata =
        EditorNodeGraph::DeserializeRawMetadata(metadataJson);
    const std::uint64_t expectedPixelCount =
        metadata.rawWidth > 0 && metadata.rawHeight > 0
        ? static_cast<std::uint64_t>(metadata.rawWidth) *
            static_cast<std::uint64_t>(metadata.rawHeight)
        : 0u;
    if (!stream || pixelCount == 0u || pixelCount != expectedPixelCount ||
        pixelCount != header.value("pixelCount", std::uint64_t { 0 }) ||
        pixelCount > static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max() / sizeof(float))) {
        return Fail(errorMessage, "The MultiFrame result cache dimensions are corrupt.");
    }

    auto pixels = std::make_shared<std::vector<float>>();
    try {
        pixels->resize(static_cast<std::size_t>(pixelCount));
    } catch (const std::bad_alloc&) {
        return Fail(errorMessage, "The MultiFrame result cache is too large to restore.");
    }
    stream.read(
        reinterpret_cast<char*>(pixels->data()),
        static_cast<std::streamsize>(pixelCount * sizeof(float)));
    if (!stream || stream.peek() != std::ifstream::traits_type::eof()) {
        return Fail(errorMessage, "The MultiFrame result cache payload is corrupt.");
    }
    for (float value : *pixels) {
        if (!std::isfinite(value)) {
            return Fail(errorMessage, "The MultiFrame result cache contains non-finite samples.");
        }
    }

    auto decoded = std::make_shared<Raw::RawImageData>();
    decoded->metadata = std::move(metadata);
    decoded->normalizedMosaicBuffer = std::move(pixels);
    decoded->normalizedMosaicInputContract =
        Raw::NormalizedMosaicInputContract::MfdReferencePreGain;
    decoded->normalizedMosaicContentHash = header.value(
        "normalizedMosaicContentHash", std::uint64_t { 0 });
    raw = std::move(decoded);
    if (errorMessage) errorMessage->clear();
    return true;
}

} // namespace Stack::EditorMultiFrameCache
