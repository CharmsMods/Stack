#include "Editor/MultiFrameResultCache.h"
#include "Editor/NodeGraph/Serialization/EditorNodeGraphRawSerialization.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Raw/MultiFrameHdr/Processor.h"
#include "NodeMath/ContractTypes.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Fixtures {
    fs::path root;
    Fixtures() {
        root = fs::temp_directory_path() / ("stack-cache-reader-test-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Require(fs::create_directory(root), "Cannot reserve cache fixtures");
    }
    ~Fixtures() { std::error_code error; fs::remove_all(root, error); }
};

void WriteBurstFixture(const fs::path& path, const nlohmann::json& header,
    std::uint64_t declaredPixels = 1) {
    std::ofstream stream(path, std::ios::binary);
    const char magic[8] = {'S', 'T', 'K', 'M', 'F', 'C', '1', 0};
    const std::string text = header.dump();
    const std::uint64_t size = text.size();
    const float pixel = .2f;
    stream.write(magic, sizeof(magic));
    stream.write(reinterpret_cast<const char*>(&size), sizeof(size));
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.write(reinterpret_cast<const char*>(&declaredPixels), sizeof(declaredPixels));
    stream.write(reinterpret_cast<const char*>(&pixel), sizeof(pixel));
    stream.close();
    Require(stream.good(), "Cannot write cache fixture");
    const auto identity = Stack::RawEvidence::ComputeSourceIdentity(path);
    Require(identity.valid, "Cannot hash cache fixture");
    std::ofstream(path.string() + ".sha256") << identity.sha256;
}

void ReplaceHdrHeader(const fs::path& path, const char* field, const nlohmann::json& value) {
    std::ifstream input(path, std::ios::binary);
    char magic[8];
    std::uint64_t size = 0;
    input.read(magic, sizeof(magic));
    input.read(reinterpret_cast<char*>(&size), sizeof(size));
    std::string text(static_cast<std::size_t>(size), '\0');
    input.read(text.data(), static_cast<std::streamsize>(size));
    input.ignore(64);
    const std::string payload{std::istreambuf_iterator<char>(input), {}};
    auto header = nlohmann::json::parse(text);
    header[field] = value;
    text = header.dump();
    size = text.size();
    const auto hash = Stack::NodeMath::Sha256ContentIdentity(
        std::vector<std::string_view>{text, payload}).substr(7);
    input.close();
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(magic, sizeof(magic));
    output.write(reinterpret_cast<const char*>(&size), sizeof(size));
    output.write(text.data(), static_cast<std::streamsize>(size));
    output.write(hash.data(), static_cast<std::streamsize>(hash.size()));
    output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    Require(output.good(), "Cannot replace fixture header");
}
}

int main() try {
    Fixtures fixtures;
    const auto path = fixtures.root / "burst.cache";
    std::string error;
    std::shared_ptr<Raw::RawImageData> raw;
    WriteBurstFixture(path, {{"contractId", 42}});
    Require(!Stack::EditorMultiFrameCache::ReadBurstResult(path, "id", 1, raw, &error)
        && !raw && !error.empty(), "Wrong metadata type escaped the cache failure contract");

    Raw::RawImageData source;
    source.metadata.rawWidth = source.metadata.visibleWidth = 1;
    source.metadata.rawHeight = source.metadata.visibleHeight = 1;
    source.normalizedMosaicBuffer = std::make_shared<const std::vector<float>>(1, .2f);
    source.normalizedMosaicInputContract = Raw::NormalizedMosaicInputContract::MfdReferencePreGain;
    Require(Stack::EditorMultiFrameCache::WriteBurstResult(path, "id", 1, source, &error),
        "Cannot write valid Burst cache");
    Require(Stack::EditorMultiFrameCache::ReadBurstResult(path, "id", 1, raw, &error)
        && raw->normalizedMosaicBuffer->at(0) == .2f, "Valid Burst cache changed samples");

    auto metadata = EditorNodeGraph::SerializeRawMetadata(source.metadata);
    metadata["rawWidth"] = 2;
    nlohmann::json header = {{"contractId", "stack.multiframe-burst-result-cache"},
        {"formatVersion", 1}, {"graphIdentity", "id"}, {"inputRevision", 1},
        {"normalizedMosaicInputContract", "mfd-reference-pre-gain"},
        {"pixelCount", 2}, {"rawMetadata", metadata}};
    WriteBurstFixture(path, header, 2);
    Require(!Stack::EditorMultiFrameCache::ReadBurstResult(path, "id", 1, raw, &error)
        && !raw, "Truncated payload was accepted");
    header["rawMetadata"] = "not metadata";
    WriteBurstFixture(path, header, 2);
    Require(!Stack::EditorMultiFrameCache::ReadBurstResult(path, "id", 1, raw, &error)
        && !raw, "Malformed source metadata was accepted");

    Raw::Hdr::Result sourceHdr;
    sourceHdr.status = Raw::Hdr::ProcessingStatus::Published;
    sourceHdr.width = sourceHdr.height = 2;
    sourceHdr.virtualAnchorMosaic.assign(4, .2f);
    sourceHdr.varianceProxy.assign(4, .001f);
    sourceHdr.mergeConfidence.assign(4, .8f);
    sourceHdr.effectiveSampleCount.assign(4, 2.f);
    sourceHdr.recoveredHeadroomStops.assign(4, 0.f);
    sourceHdr.validityMask.assign(4, 1);
    sourceHdr.ownerFrame.assign(4, 0);
    sourceHdr.flags.assign(4, 0);
    sourceHdr.diagnostics.frames.resize(2);
    sourceHdr.diagnostics.finitePixelCount = 4;
    sourceHdr.diagnostics.cacheKey = std::string(64, 'a');
    Require(Raw::Hdr::WriteResultCache(fixtures.root, sourceHdr, &error), "Cannot write valid HDR cache");
    Raw::Hdr::Result restored;
    Require(Raw::Hdr::ReadResultCache(fixtures.root, sourceHdr.diagnostics.cacheKey, restored, &error), error.c_str());
    Require(restored.virtualAnchorMosaic == sourceHdr.virtualAnchorMosaic &&
        restored.varianceProxy == sourceHdr.varianceProxy &&
        restored.mergeConfidence == sourceHdr.mergeConfidence &&
        restored.effectiveSampleCount == sourceHdr.effectiveSampleCount &&
        restored.recoveredHeadroomStops == sourceHdr.recoveredHeadroomStops &&
        restored.validityMask == sourceHdr.validityMask &&
        restored.ownerFrame == sourceHdr.ownerFrame && restored.flags == sourceHdr.flags,
        "HDR cache roundtrip changed data");
    const auto hdrPath = fixtures.root / (sourceHdr.diagnostics.cacheKey + ".hdr-cache");
    ReplaceHdrHeader(hdrPath, "width", "not a number");
    Require(!Raw::Hdr::ReadResultCache(fixtures.root, sourceHdr.diagnostics.cacheKey, restored, &error)
        && !error.empty() && restored.virtualAnchorMosaic == sourceHdr.virtualAnchorMosaic,
        "Invalid HDR metadata escaped failure or replaced the previous result");
    std::cout << "Cache-reader metadata failure, payload length and exact roundtrip checks passed.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
