#include "NodeMath/SourceColorMetadata.h"

#include "NodeMath/DescriptorSerialization.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <map>

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

SpatialDescriptor MakeSpatial(int width, int height) {
    SpatialDescriptor spatial;
    spatial.kind = SpatialExtentKind::Finite;
    spatial.fullWindow = { 0, 0, width, height };
    spatial.dataWindow = spatial.fullWindow;
    spatial.pixelAspect = 1.0;
    return spatial;
}

SamplingDescriptor MakeSampling() {
    SamplingDescriptor sampling;
    sampling.coordinates = CoordinateConvention::PixelCenters;
    sampling.filter = ReconstructionFilter::Linear;
    sampling.border = BorderPolicy::Transparent;
    return sampling;
}

AlphaMode SourceAlphaMode(int channels) {
    return channels == 2 || channels == 4 ? AlphaMode::Straight : AlphaMode::Opaque;
}

TransferDescriptor SrgbTransfer() {
    TransferDescriptor result;
    result.kind = TransferKind::Srgb;
    return result;
}

void SetKnownSourceRange(ValueDescriptor& descriptor) {
    descriptor.range = SemanticField<NumericRange>::Known({
        0.0, 1.0, false, false, NonFinitePolicy::Forbidden
    });
}

std::string PayloadIdentity(const char* domain, const std::vector<unsigned char>& payload) {
    const std::string bytes(reinterpret_cast<const char*>(payload.data()), payload.size());
    return Sha256ContentIdentity(std::string(domain) + "\n" + bytes);
}

struct PngSignals {
    std::vector<unsigned char> cicp;
    std::vector<unsigned char> iccp;
    std::string iccpName;
    std::vector<unsigned char> srgb;
    std::vector<unsigned char> gama;
    std::vector<unsigned char> chrm;
    bool malformed = false;
};

PngSignals InspectPng(const std::vector<unsigned char>& bytes) {
    PngSignals result;
    if (bytes.size() < kPngSignature.size() ||
        !std::equal(kPngSignature.begin(), kPngSignature.end(), bytes.begin())) {
        return result;
    }
    std::size_t cursor = kPngSignature.size();
    while (cursor + 12 <= bytes.size()) {
        const std::uint32_t length = ReadBigEndian32(bytes.data() + cursor);
        if (length > bytes.size() - cursor - 12) {
            result.malformed = true;
            break;
        }
        const std::string type(reinterpret_cast<const char*>(bytes.data() + cursor + 4), 4);
        const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(cursor + 8);
        const auto end = begin + static_cast<std::ptrdiff_t>(length);
        if (type == "cICP") result.cicp.assign(begin, end);
        else if (type == "iCCP") {
            result.iccp.assign(begin, end);
            const auto separator = std::find(result.iccp.begin(), result.iccp.end(), 0);
            if (separator != result.iccp.end()) {
                result.iccpName.assign(result.iccp.begin(), separator);
            }
        } else if (type == "sRGB") result.srgb.assign(begin, end);
        else if (type == "gAMA") result.gama.assign(begin, end);
        else if (type == "cHRM") result.chrm.assign(begin, end);
        cursor += static_cast<std::size_t>(length) + 12;
        if (type == "IEND") break;
    }
    return result;
}

std::vector<unsigned char> InspectJpegIcc(
    const std::vector<unsigned char>& bytes,
    bool& malformed) {
    malformed = false;
    if (bytes.size() < 4 || bytes[0] != 0xff || bytes[1] != 0xd8) return {};
    std::map<unsigned char, std::vector<unsigned char>> segments;
    unsigned char expectedCount = 0;
    std::size_t cursor = 2;
    const std::array<unsigned char, 12> signature = {
        'I','C','C','_','P','R','O','F','I','L','E',0
    };
    while (cursor + 4 <= bytes.size()) {
        while (cursor < bytes.size() && bytes[cursor] == 0xff) ++cursor;
        if (cursor >= bytes.size()) break;
        const unsigned char marker = bytes[cursor++];
        if (marker == 0xd9 || marker == 0xda) break;
        if (marker >= 0xd0 && marker <= 0xd7) continue;
        if (cursor + 2 > bytes.size()) { malformed = true; break; }
        const std::uint16_t length = static_cast<std::uint16_t>(bytes[cursor] << 8 | bytes[cursor + 1]);
        if (length < 2 || length > bytes.size() - cursor) { malformed = true; break; }
        const std::size_t payloadStart = cursor + 2;
        const std::size_t payloadSize = length - 2;
        if (marker == 0xe2 && payloadSize >= 14 &&
            std::equal(signature.begin(), signature.end(), bytes.begin() + static_cast<std::ptrdiff_t>(payloadStart))) {
            const unsigned char sequence = bytes[payloadStart + 12];
            const unsigned char count = bytes[payloadStart + 13];
            if (sequence == 0 || count == 0 || (expectedCount != 0 && expectedCount != count)) {
                malformed = true;
            } else {
                expectedCount = count;
                segments[sequence] = std::vector<unsigned char>(
                    bytes.begin() + static_cast<std::ptrdiff_t>(payloadStart + 14),
                    bytes.begin() + static_cast<std::ptrdiff_t>(payloadStart + payloadSize));
            }
        }
        cursor += length;
    }
    if (segments.empty()) return {};
    if (segments.size() != expectedCount) {
        malformed = true;
        return {};
    }
    std::vector<unsigned char> profile;
    for (unsigned int index = 1; index <= expectedCount; ++index) {
        const auto found = segments.find(static_cast<unsigned char>(index));
        if (found == segments.end()) { malformed = true; return {}; }
        profile.insert(profile.end(), found->second.begin(), found->second.end());
    }
    return profile;
}

const char* PayloadKindToken(EmbeddedColorPayloadKind kind) {
    switch (kind) {
    case EmbeddedColorPayloadKind::None: return "none";
    case EmbeddedColorPayloadKind::PngIccpChunk: return "png-iccp-chunk";
    case EmbeddedColorPayloadKind::JpegIccProfile: return "jpeg-icc-profile";
    case EmbeddedColorPayloadKind::PngColorSignal: return "png-color-signal";
    }
    return "none";
}

bool ParsePayloadKind(const std::string& token, EmbeddedColorPayloadKind& kind) {
    if (token == "none") kind = EmbeddedColorPayloadKind::None;
    else if (token == "png-iccp-chunk") kind = EmbeddedColorPayloadKind::PngIccpChunk;
    else if (token == "jpeg-icc-profile") kind = EmbeddedColorPayloadKind::JpegIccProfile;
    else if (token == "png-color-signal") kind = EmbeddedColorPayloadKind::PngColorSignal;
    else return false;
    return true;
}

} // namespace

SourceColorMetadata InspectSourceColorMetadata(
    const std::vector<unsigned char>& encodedFile,
    int width,
    int height,
    int originalChannels,
    LogicalPrecision precision,
    const std::string& sourceIdentity) {
    SourceColorMetadata result;
    const AlphaMode alpha = SourceAlphaMode(originalChannels);
    const SpatialDescriptor spatial = MakeSpatial(width, height);
    const SamplingDescriptor sampling = MakeSampling();
    const PngSignals png = InspectPng(encodedFile);
    bool jpegMalformed = false;
    const std::vector<unsigned char> jpegIcc = InspectJpegIcc(encodedFile, jpegMalformed);

    if (png.malformed || jpegMalformed) {
        result.issues.push_back({ "source.color-metadata", "encoded color metadata is malformed; color state remains Unknown" });
    }

    if (png.cicp.size() == 4 && png.cicp[2] == 0 && png.cicp[3] == 1 &&
        png.cicp[1] == 13 && (png.cicp[0] == 1 || png.cicp[0] == 12)) {
        const bool p3 = png.cicp[0] == 12;
        result.payloadKind = EmbeddedColorPayloadKind::PngColorSignal;
        result.retainedPayload = png.cicp;
        result.label = p3 ? "PNG cICP Display-P3" : "PNG cICP sRGB";
        result.dependencyIdentity = PayloadIdentity("png-cicp", result.retainedPayload);
        result.descriptor = MakeTaggedColorImageDescriptor(
            p3 ? "display-p3-d65" : "srgb-d65", result.dependencyIdentity,
            SrgbTransfer(), ReferenceState::Display, alpha, spatial, sampling,
            precision, sourceIdentity);
    } else if (!png.iccp.empty()) {
        result.payloadKind = EmbeddedColorPayloadKind::PngIccpChunk;
        result.retainedPayload = png.iccp;
        result.label = png.iccpName.empty() ? "PNG embedded ICC profile" : png.iccpName;
        result.dependencyIdentity = PayloadIdentity("png-iccp", result.retainedPayload);
        result.descriptor = MakeTaggedColorImageDescriptor(
            "icc:" + result.dependencyIdentity.substr(7), result.dependencyIdentity,
            { TransferKind::Custom, 0.0, "icc-embedded" }, ReferenceState::Display,
            alpha, spatial, sampling, precision, sourceIdentity);
        result.descriptor.transfer = SemanticField<TransferDescriptor>::Unknown();
        result.descriptor.reference = SemanticField<ReferenceState>::Unknown();
    } else if (!jpegIcc.empty()) {
        result.payloadKind = EmbeddedColorPayloadKind::JpegIccProfile;
        result.retainedPayload = jpegIcc;
        result.label = "JPEG embedded ICC profile";
        result.dependencyIdentity = PayloadIdentity("jpeg-icc", result.retainedPayload);
        result.descriptor = MakeTaggedColorImageDescriptor(
            "icc:" + result.dependencyIdentity.substr(7), result.dependencyIdentity,
            { TransferKind::Custom, 0.0, "icc-embedded" }, ReferenceState::Display,
            alpha, spatial, sampling, precision, sourceIdentity);
        result.descriptor.transfer = SemanticField<TransferDescriptor>::Unknown();
        result.descriptor.reference = SemanticField<ReferenceState>::Unknown();
    } else if (!png.srgb.empty()) {
        result.payloadKind = EmbeddedColorPayloadKind::PngColorSignal;
        result.retainedPayload = png.srgb;
        result.label = "PNG sRGB";
        result.dependencyIdentity = PayloadIdentity("png-srgb", result.retainedPayload);
        result.descriptor = MakeTaggedColorImageDescriptor(
            "srgb-d65", result.dependencyIdentity, SrgbTransfer(), ReferenceState::Display,
            alpha, spatial, sampling, precision, sourceIdentity);
    } else if (!png.gama.empty() && !png.chrm.empty()) {
        result.payloadKind = EmbeddedColorPayloadKind::PngColorSignal;
        result.retainedPayload = png.gama;
        result.retainedPayload.insert(result.retainedPayload.end(), png.chrm.begin(), png.chrm.end());
        result.label = "PNG gAMA + cHRM";
        result.dependencyIdentity = PayloadIdentity("png-gama-chrm", result.retainedPayload);
        result.descriptor = MakeTaggedColorImageDescriptor(
            "png-chromaticities:" + result.dependencyIdentity.substr(7), result.dependencyIdentity,
            { TransferKind::Custom, 0.0, "png-gama" }, ReferenceState::Display,
            alpha, spatial, sampling, precision, sourceIdentity);
    } else {
        result.label = "Unknown (untagged source)";
        result.descriptor = MakeUntaggedColorImageDescriptor(
            alpha, spatial, sampling, precision, sourceIdentity);
    }
    SetKnownSourceRange(result.descriptor);
    return result;
}

nlohmann::json SerializeSourceColorMetadata(const SourceColorMetadata& metadata) {
    return {
        { "schemaVersion", metadata.schemaVersion },
        { "payloadKind", PayloadKindToken(metadata.payloadKind) },
        { "label", metadata.label },
        { "dependencyIdentity", metadata.dependencyIdentity },
        { "retainedPayload", nlohmann::json::binary(metadata.retainedPayload) },
        { "descriptor", SerializeValueDescriptor(metadata.descriptor) }
    };
}

bool ParseSourceColorMetadata(
    const nlohmann::json& value,
    SourceColorMetadata& metadata,
    std::vector<ContractIssue>& issues) {
    if (!value.is_object()) {
        issues.push_back({ "sourceColorMetadata", "source color metadata must be an object" });
        return false;
    }
    try {
        metadata.schemaVersion = value.at("schemaVersion").get<std::uint32_t>();
        if (metadata.schemaVersion != kSourceColorMetadataSchemaVersion) {
            issues.push_back({ "sourceColorMetadata.schemaVersion", "unsupported source color metadata schema" });
        }
        if (!ParsePayloadKind(value.at("payloadKind").get<std::string>(), metadata.payloadKind)) {
            issues.push_back({ "sourceColorMetadata.payloadKind", "unknown embedded color payload kind" });
        }
        metadata.label = value.at("label").get<std::string>();
        metadata.dependencyIdentity = value.at("dependencyIdentity").get<std::string>();
        if (value.contains("retainedPayload") && value["retainedPayload"].is_binary()) {
            const auto& bytes = value["retainedPayload"].get_binary();
            metadata.retainedPayload.assign(bytes.begin(), bytes.end());
        }
        const DescriptorParseResult parsed = ParseValueDescriptor(value.at("descriptor"));
        issues.insert(issues.end(), parsed.issues.begin(), parsed.issues.end());
        if (parsed.descriptor) metadata.descriptor = *parsed.descriptor;
    } catch (...) {
        issues.push_back({ "sourceColorMetadata", "source color metadata is malformed" });
    }
    if (metadata.payloadKind != EmbeddedColorPayloadKind::None &&
        (metadata.retainedPayload.empty() || !IsValidContentHash(metadata.dependencyIdentity))) {
        issues.push_back({ "sourceColorMetadata.dependencyIdentity", "embedded payload requires retained bytes and a stable dependency identity" });
    }
    metadata.issues = issues;
    return issues.empty();
}

std::string SourceColorDependencyIdentity(const SourceColorMetadata& metadata) {
    return metadata.dependencyIdentity.empty()
        ? DescriptorContentIdentity(metadata.descriptor)
        : metadata.dependencyIdentity;
}

} // namespace Stack::NodeMath
