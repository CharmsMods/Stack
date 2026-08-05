#include "NodeMath/ContractTypes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>

namespace Stack::NodeMath {

namespace {

bool IsLowerAlpha(char value) {
    return value >= 'a' && value <= 'z';
}

bool IsDigit(char value) {
    return value >= '0' && value <= '9';
}

bool IsLowerHex(char value) {
    return IsDigit(value) || (value >= 'a' && value <= 'f');
}

bool IsDefinitionSegment(const std::string& value, bool allowSlash) {
    if (value.empty() || !(IsLowerAlpha(value.front()) || IsDigit(value.front()))) {
        return false;
    }
    bool previousSlash = false;
    bool atSegmentStart = false;
    for (char character : value) {
        const bool slash = allowSlash && character == '/';
        const bool allowed = IsLowerAlpha(character) || IsDigit(character) ||
            character == '.' || character == '-' || slash;
        if (!allowed || (slash && previousSlash) ||
            (atSegmentStart && !(IsLowerAlpha(character) || IsDigit(character)))) {
            return false;
        }
        previousSlash = slash;
        atSegmentStart = slash;
    }
    return !previousSlash;
}

bool IsApplicableImageType(LogicalValueType type) {
    return type == LogicalValueType::Channel ||
        type == LogicalValueType::ScalarField ||
        type == LogicalValueType::Vector2Field ||
        type == LogicalValueType::Vector3Field ||
        type == LogicalValueType::Vector4Field ||
        type == LogicalValueType::ColorImage ||
        type == LogicalValueType::Mask ||
        type == LogicalValueType::DataImage ||
        type == LogicalValueType::ComplexSpectrum ||
        type == LogicalValueType::SpectrumMagnitude ||
        type == LogicalValueType::SpectrumPhase ||
        type == LogicalValueType::Raw;
}

void RequireState(
    std::vector<ContractIssue>& issues,
    const char* field,
    KnowledgeState actual,
    bool applicable) {
    if (applicable && actual == KnowledgeState::NotApplicable) {
        issues.push_back({ field, "applicable field cannot be NotApplicable" });
    } else if (!applicable && actual != KnowledgeState::NotApplicable) {
        issues.push_back({ field, "field must be NotApplicable for this logical type" });
    }
}

bool Contains(const Rect& outer, const Rect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
        inner.x + inner.width <= outer.x + outer.width &&
        inner.y + inner.height <= outer.y + outer.height;
}

std::optional<std::uint32_t> ParseVersionPart(const std::string& text) {
    if (text.empty() || (text.size() > 1 && text.front() == '0')) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (char character : text) {
        if (!IsDigit(character)) {
            return std::nullopt;
        }
        value = value * 10 + static_cast<std::uint64_t>(character - '0');
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
    }
    return static_cast<std::uint32_t>(value);
}

class Sha256 {
public:
    void Update(const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        m_TotalBytes += size;
        while (size > 0) {
            const std::size_t count = std::min(size, m_Block.size() - m_BlockBytes);
            std::copy(bytes, bytes + count, m_Block.begin() + static_cast<std::ptrdiff_t>(m_BlockBytes));
            m_BlockBytes += count;
            bytes += count;
            size -= count;
            if (m_BlockBytes == m_Block.size()) {
                Transform(m_Block.data());
                m_BlockBytes = 0;
            }
        }
    }

    std::string Finish() {
        const std::uint64_t bitCount = static_cast<std::uint64_t>(m_TotalBytes) * 8u;
        const std::uint8_t marker = 0x80;
        Update(&marker, 1);
        const std::uint8_t zero = 0;
        while (m_BlockBytes != 56) {
            Update(&zero, 1);
        }
        std::array<std::uint8_t, 8> length{};
        for (int index = 0; index < 8; ++index) {
            length[static_cast<std::size_t>(7 - index)] =
                static_cast<std::uint8_t>((bitCount >> (index * 8)) & 0xffu);
        }
        Update(length.data(), length.size());

        static constexpr char kHex[] = "0123456789abcdef";
        std::string digest;
        digest.reserve(64);
        for (std::uint32_t word : m_State) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                digest.push_back(kHex[(word >> shift) & 0x0fu]);
            }
        }
        return digest;
    }

private:
    static std::uint32_t RotateRight(std::uint32_t value, int bits) {
        return (value >> bits) | (value << (32 - bits));
    }

    void Transform(const std::uint8_t* block) {
        static constexpr std::array<std::uint32_t, 64> constants = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
            0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
            0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
            0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
            0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
            0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
        };
        std::array<std::uint32_t, 64> words{};
        for (int index = 0; index < 16; ++index) {
            const std::size_t offset = static_cast<std::size_t>(index) * 4;
            words[static_cast<std::size_t>(index)] =
                (static_cast<std::uint32_t>(block[offset]) << 24) |
                (static_cast<std::uint32_t>(block[offset + 1]) << 16) |
                (static_cast<std::uint32_t>(block[offset + 2]) << 8) |
                static_cast<std::uint32_t>(block[offset + 3]);
        }
        for (int index = 16; index < 64; ++index) {
            const std::uint32_t s0 = RotateRight(words[static_cast<std::size_t>(index - 15)], 7) ^
                RotateRight(words[static_cast<std::size_t>(index - 15)], 18) ^
                (words[static_cast<std::size_t>(index - 15)] >> 3);
            const std::uint32_t s1 = RotateRight(words[static_cast<std::size_t>(index - 2)], 17) ^
                RotateRight(words[static_cast<std::size_t>(index - 2)], 19) ^
                (words[static_cast<std::size_t>(index - 2)] >> 10);
            words[static_cast<std::size_t>(index)] =
                words[static_cast<std::size_t>(index - 16)] + s0 +
                words[static_cast<std::size_t>(index - 7)] + s1;
        }
        std::uint32_t a = m_State[0];
        std::uint32_t b = m_State[1];
        std::uint32_t c = m_State[2];
        std::uint32_t d = m_State[3];
        std::uint32_t e = m_State[4];
        std::uint32_t f = m_State[5];
        std::uint32_t g = m_State[6];
        std::uint32_t h = m_State[7];
        for (std::size_t index = 0; index < constants.size(); ++index) {
            const std::uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
            const std::uint32_t choice = (e & f) ^ ((~e) & g);
            const std::uint32_t t1 = h + s1 + choice + constants[index] + words[index];
            const std::uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        m_State[0] += a;
        m_State[1] += b;
        m_State[2] += c;
        m_State[3] += d;
        m_State[4] += e;
        m_State[5] += f;
        m_State[6] += g;
        m_State[7] += h;
    }

    std::array<std::uint32_t, 8> m_State = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    std::array<std::uint8_t, 64> m_Block{};
    std::size_t m_BlockBytes = 0;
    std::size_t m_TotalBytes = 0;
};

} // namespace

bool operator==(const ChannelDescriptor& left, const ChannelDescriptor& right) {
    return left.layout == right.layout && left.roles == right.roles;
}

bool operator==(const ImageComponentSet& left, const ImageComponentSet& right) {
    return left.bits == right.bits;
}

bool operator!=(const ImageComponentSet& left, const ImageComponentSet& right) {
    return !(left == right);
}

bool HasImageComponent(
    const ImageComponentSet& components,
    ImageComponent component) {
    return (components.bits & static_cast<std::uint8_t>(component)) != 0;
}

bool AddImageComponent(
    ImageComponentSet& components,
    ImageComponent component) {
    const std::uint8_t componentBit = static_cast<std::uint8_t>(component);
    const std::uint8_t previous = components.bits;
    components.bits = static_cast<std::uint8_t>(components.bits | componentBit);
    return previous != components.bits;
}

std::size_t ImageComponentCount(const ImageComponentSet& components) {
    std::size_t count = 0;
    std::uint8_t bits = components.bits;
    while (bits != 0) {
        count += bits & 1u;
        bits = static_cast<std::uint8_t>(bits >> 1u);
    }
    return count;
}

std::vector<ImageComponent> OrderedImageComponents(
    const ImageComponentSet& components) {
    std::vector<ImageComponent> ordered;
    ordered.reserve(ImageComponentCount(components));
    for (const ImageComponent component : {
             ImageComponent::Red,
             ImageComponent::Green,
             ImageComponent::Blue,
             ImageComponent::Alpha }) {
        if (HasImageComponent(components, component)) {
            ordered.push_back(component);
        }
    }
    return ordered;
}

std::string ImageComponentToken(ImageComponent component) {
    switch (component) {
    case ImageComponent::Red: return "R";
    case ImageComponent::Green: return "G";
    case ImageComponent::Blue: return "B";
    case ImageComponent::Alpha: return "A";
    }
    return {};
}

std::optional<ImageComponent> ParseImageComponentToken(
    const std::string& token) {
    if (token == "R") return ImageComponent::Red;
    if (token == "G") return ImageComponent::Green;
    if (token == "B") return ImageComponent::Blue;
    if (token == "A") return ImageComponent::Alpha;
    return std::nullopt;
}

ImageComponentSet MakeImageComponentSet(
    std::initializer_list<ImageComponent> components) {
    ImageComponentSet result;
    for (const ImageComponent component : components) {
        AddImageComponent(result, component);
    }
    return result;
}

ChannelDescriptor MakeImageChannelDescriptor(
    const ImageComponentSet& components) {
    ChannelDescriptor result;
    const ImageComponentSet rgb = MakeImageComponentSet({
        ImageComponent::Red,
        ImageComponent::Green,
        ImageComponent::Blue
    });
    const ImageComponentSet rgba = MakeImageComponentSet({
        ImageComponent::Red,
        ImageComponent::Green,
        ImageComponent::Blue,
        ImageComponent::Alpha
    });
    result.layout = components == rgb
        ? ChannelLayout::RGB
        : (components == rgba ? ChannelLayout::RGBA : ChannelLayout::NamedData);
    for (const ImageComponent component : OrderedImageComponents(components)) {
        switch (component) {
        case ImageComponent::Red: result.roles.push_back("red"); break;
        case ImageComponent::Green: result.roles.push_back("green"); break;
        case ImageComponent::Blue: result.roles.push_back("blue"); break;
        case ImageComponent::Alpha: result.roles.push_back("alpha"); break;
        }
    }
    return result;
}

bool operator==(const ColorIdentity& left, const ColorIdentity& right) {
    return left.identity == right.identity && left.profileHash == right.profileHash &&
        left.relation == right.relation;
}

bool operator==(const TransferDescriptor& left, const TransferDescriptor& right) {
    return left.kind == right.kind && left.parameter == right.parameter &&
        left.key == right.key;
}

bool operator==(const NumericRange& left, const NumericRange& right) {
    return left.nominalMinimum == right.nominalMinimum &&
        left.nominalMaximum == right.nominalMaximum &&
        left.allowsBelowNominal == right.allowsBelowNominal &&
        left.allowsAboveNominal == right.allowsAboveNominal &&
        left.nonFinite == right.nonFinite;
}

bool operator==(const Rect& left, const Rect& right) {
    return left.x == right.x && left.y == right.y &&
        left.width == right.width && left.height == right.height;
}

bool operator==(const SpatialDescriptor& left, const SpatialDescriptor& right) {
    return left.kind == right.kind && left.fullWindow == right.fullWindow &&
        left.dataWindow == right.dataWindow &&
        left.rasterOrigin == right.rasterOrigin &&
        left.pixelAspect == right.pixelAspect;
}

bool operator==(const SamplingDescriptor& left, const SamplingDescriptor& right) {
    return left.coordinates == right.coordinates && left.filter == right.filter &&
        left.border == right.border && left.customKey == right.customKey;
}

bool operator==(const UnitDescriptor& left, const UnitDescriptor& right) {
    return left.kind == right.kind && left.customKey == right.customKey;
}

bool operator==(const ProvenanceDescriptor& left, const ProvenanceDescriptor& right) {
    return left.kind == right.kind && left.sourceIdentity == right.sourceIdentity &&
        left.operationIdentity == right.operationIdentity;
}

bool operator==(const ValueDescriptor& left, const ValueDescriptor& right) {
    return left.schemaVersion == right.schemaVersion &&
        left.logicalType == right.logicalType && left.channels == right.channels &&
        left.presentImageComponents == right.presentImageComponents &&
        left.color == right.color && left.transfer == right.transfer &&
        left.reference == right.reference && left.alpha == right.alpha &&
        left.range == right.range && left.precision == right.precision &&
        left.spatial == right.spatial && left.sampling == right.sampling &&
        left.units == right.units && left.provenance == right.provenance;
}

bool operator!=(const ValueDescriptor& left, const ValueDescriptor& right) {
    return !(left == right);
}

bool StrictSemanticDescriptorMatch(
    const ValueDescriptor& left,
    const ValueDescriptor& right) {
    const auto strictFieldMatch = [](const auto& first, const auto& second) {
        if (first.state == KnowledgeState::Unknown ||
            second.state == KnowledgeState::Unknown ||
            first.state != second.state) {
            return false;
        }
        return first.state == KnowledgeState::NotApplicable ||
            first.value == second.value;
    };
    return left.schemaVersion == right.schemaVersion &&
        left.logicalType == right.logicalType &&
        strictFieldMatch(left.channels, right.channels) &&
        strictFieldMatch(
            left.presentImageComponents,
            right.presentImageComponents) &&
        strictFieldMatch(left.color, right.color) &&
        strictFieldMatch(left.transfer, right.transfer) &&
        strictFieldMatch(left.reference, right.reference) &&
        strictFieldMatch(left.alpha, right.alpha) &&
        strictFieldMatch(left.range, right.range) &&
        strictFieldMatch(left.precision, right.precision) &&
        strictFieldMatch(left.spatial, right.spatial) &&
        strictFieldMatch(left.sampling, right.sampling) &&
        strictFieldMatch(left.units, right.units) &&
        strictFieldMatch(left.provenance, right.provenance);
}

bool IsImageLike(LogicalValueType type) {
    return IsApplicableImageType(type);
}

bool IsNumericLike(LogicalValueType type) {
    switch (type) {
    case LogicalValueType::Integer:
    case LogicalValueType::Scalar:
    case LogicalValueType::Vector2:
    case LogicalValueType::Vector3:
    case LogicalValueType::Vector4:
    case LogicalValueType::Matrix3:
    case LogicalValueType::Matrix4:
    case LogicalValueType::Coordinate2:
    case LogicalValueType::Curve1D:
    case LogicalValueType::Lut:
    case LogicalValueType::Channel:
    case LogicalValueType::ColorImage:
    case LogicalValueType::Mask:
    case LogicalValueType::DataImage:
    case LogicalValueType::ComplexSpectrum:
    case LogicalValueType::FrequencyResponse:
    case LogicalValueType::SpectrumMagnitude:
    case LogicalValueType::SpectrumPhase:
    case LogicalValueType::Histogram:
    case LogicalValueType::Statistics:
    case LogicalValueType::ScalarField:
    case LogicalValueType::Vector2Field:
    case LogicalValueType::Vector3Field:
    case LogicalValueType::Vector4Field:
        return true;
    default:
        return false;
    }
}

ValueDescriptor MakeUnknownDescriptor(LogicalValueType type) {
    ValueDescriptor descriptor;
    descriptor.logicalType = type;

    const bool imageLike = IsApplicableImageType(type);
    const bool colorImage = type == LogicalValueType::ColorImage;
    const bool numeric = IsNumericLike(type) || type == LogicalValueType::Raw;
    const bool unitBearing = type == LogicalValueType::Scalar ||
        type == LogicalValueType::Vector2 || type == LogicalValueType::Vector3 ||
        type == LogicalValueType::Vector4 || type == LogicalValueType::Matrix3 ||
        type == LogicalValueType::Matrix4 || type == LogicalValueType::Coordinate2 ||
        type == LogicalValueType::Channel ||
        type == LogicalValueType::ScalarField || type == LogicalValueType::Vector2Field ||
        type == LogicalValueType::Vector3Field || type == LogicalValueType::Vector4Field ||
        type == LogicalValueType::Statistics;
    const bool executable = type != LogicalValueType::Invalid &&
        type != LogicalValueType::Failure;

    descriptor.channels = imageLike ? SemanticField<ChannelDescriptor>::Unknown()
        : SemanticField<ChannelDescriptor>::NotApplicable();
    descriptor.presentImageComponents = colorImage
        ? SemanticField<ImageComponentSet>::Unknown()
        : SemanticField<ImageComponentSet>::NotApplicable();
    descriptor.color = colorImage ? SemanticField<ColorIdentity>::Unknown()
        : SemanticField<ColorIdentity>::NotApplicable();
    descriptor.transfer = colorImage ? SemanticField<TransferDescriptor>::Unknown()
        : SemanticField<TransferDescriptor>::NotApplicable();
    descriptor.reference = colorImage ? SemanticField<ReferenceState>::Unknown()
        : SemanticField<ReferenceState>::NotApplicable();
    descriptor.alpha = colorImage ? SemanticField<AlphaMode>::Unknown()
        : SemanticField<AlphaMode>::NotApplicable();
    descriptor.range = numeric ? SemanticField<NumericRange>::Unknown()
        : SemanticField<NumericRange>::NotApplicable();
    descriptor.precision = numeric ? SemanticField<LogicalPrecision>::Unknown()
        : SemanticField<LogicalPrecision>::NotApplicable();
    descriptor.spatial = imageLike ? SemanticField<SpatialDescriptor>::Unknown()
        : SemanticField<SpatialDescriptor>::NotApplicable();
    descriptor.sampling = imageLike ? SemanticField<SamplingDescriptor>::Unknown()
        : SemanticField<SamplingDescriptor>::NotApplicable();
    descriptor.units = unitBearing ? SemanticField<UnitDescriptor>::Unknown()
        : SemanticField<UnitDescriptor>::NotApplicable();
    descriptor.provenance = executable ? SemanticField<ProvenanceDescriptor>::Unknown()
        : SemanticField<ProvenanceDescriptor>::NotApplicable();
    return descriptor;
}

ValueDescriptor MakeTaggedColorImageDescriptor(
    std::string colorIdentity,
    std::string profileHash,
    TransferDescriptor transfer,
    ReferenceState reference,
    AlphaMode alpha,
    SpatialDescriptor spatial,
    SamplingDescriptor sampling,
    LogicalPrecision precision,
    std::string sourceIdentity) {
    ValueDescriptor descriptor = MakeUnknownDescriptor(LogicalValueType::ColorImage);
    const ImageComponentSet components =
        alpha == AlphaMode::Absent || alpha == AlphaMode::Opaque
        ? MakeImageComponentSet({
            ImageComponent::Red,
            ImageComponent::Green,
            ImageComponent::Blue })
        : MakeImageComponentSet({
            ImageComponent::Red,
            ImageComponent::Green,
            ImageComponent::Blue,
            ImageComponent::Alpha });
    descriptor.channels = SemanticField<ChannelDescriptor>::Known(
        MakeImageChannelDescriptor(components));
    descriptor.presentImageComponents =
        SemanticField<ImageComponentSet>::Known(components);
    descriptor.color = SemanticField<ColorIdentity>::Known({
        std::move(colorIdentity), std::move(profileHash), ColorRelation::Standard });
    descriptor.transfer = SemanticField<TransferDescriptor>::Known(std::move(transfer));
    descriptor.reference = SemanticField<ReferenceState>::Known(reference);
    descriptor.alpha = SemanticField<AlphaMode>::Known(alpha);
    descriptor.range = SemanticField<NumericRange>::Unknown();
    descriptor.precision = SemanticField<LogicalPrecision>::Known(precision);
    descriptor.spatial = SemanticField<SpatialDescriptor>::Known(std::move(spatial));
    descriptor.sampling = SemanticField<SamplingDescriptor>::Known(std::move(sampling));
    descriptor.provenance = SemanticField<ProvenanceDescriptor>::Known({
        ProvenanceKind::Embedded, std::move(sourceIdentity), "source.decode" });
    return descriptor;
}

ValueDescriptor MakeUntaggedColorImageDescriptor(
    AlphaMode alpha,
    SpatialDescriptor spatial,
    SamplingDescriptor sampling,
    LogicalPrecision precision,
    std::string sourceIdentity) {
    ValueDescriptor descriptor = MakeUnknownDescriptor(LogicalValueType::ColorImage);
    const ImageComponentSet components =
        alpha == AlphaMode::Absent || alpha == AlphaMode::Opaque
        ? MakeImageComponentSet({
            ImageComponent::Red,
            ImageComponent::Green,
            ImageComponent::Blue })
        : MakeImageComponentSet({
            ImageComponent::Red,
            ImageComponent::Green,
            ImageComponent::Blue,
            ImageComponent::Alpha });
    descriptor.channels = SemanticField<ChannelDescriptor>::Known(
        MakeImageChannelDescriptor(components));
    descriptor.presentImageComponents =
        SemanticField<ImageComponentSet>::Known(components);
    descriptor.color = SemanticField<ColorIdentity>::Unknown();
    descriptor.transfer = SemanticField<TransferDescriptor>::Unknown();
    descriptor.reference = SemanticField<ReferenceState>::Unknown();
    descriptor.alpha = SemanticField<AlphaMode>::Known(alpha);
    descriptor.precision = SemanticField<LogicalPrecision>::Known(precision);
    descriptor.spatial = SemanticField<SpatialDescriptor>::Known(std::move(spatial));
    descriptor.sampling = SemanticField<SamplingDescriptor>::Known(std::move(sampling));
    descriptor.provenance = SemanticField<ProvenanceDescriptor>::Known({
        ProvenanceKind::Untagged, std::move(sourceIdentity), "source.decode" });
    return descriptor;
}

std::vector<ContractIssue> ValidateDescriptor(const ValueDescriptor& descriptor) {
    std::vector<ContractIssue> issues;
    if (descriptor.schemaVersion != kSemanticDescriptorSchemaVersion) {
        issues.push_back({ "schemaVersion", "unsupported descriptor schema version" });
    }
    if (descriptor.logicalType == LogicalValueType::Invalid) {
        issues.push_back({ "logicalType", "Invalid is not a serializable logical value" });
    }

    const bool imageLike = IsApplicableImageType(descriptor.logicalType);
    const bool colorImage = descriptor.logicalType == LogicalValueType::ColorImage;
    const bool numeric = IsNumericLike(descriptor.logicalType) ||
        descriptor.logicalType == LogicalValueType::Raw;
    const bool unitBearing = descriptor.logicalType == LogicalValueType::Scalar ||
        descriptor.logicalType == LogicalValueType::Vector2 ||
        descriptor.logicalType == LogicalValueType::Vector3 ||
        descriptor.logicalType == LogicalValueType::Vector4 ||
        descriptor.logicalType == LogicalValueType::Matrix3 ||
        descriptor.logicalType == LogicalValueType::Matrix4 ||
        descriptor.logicalType == LogicalValueType::Coordinate2 ||
        descriptor.logicalType == LogicalValueType::Channel ||
        descriptor.logicalType == LogicalValueType::ScalarField ||
        descriptor.logicalType == LogicalValueType::Vector2Field ||
        descriptor.logicalType == LogicalValueType::Vector3Field ||
        descriptor.logicalType == LogicalValueType::Vector4Field ||
        descriptor.logicalType == LogicalValueType::Statistics;
    const bool executable = descriptor.logicalType != LogicalValueType::Invalid &&
        descriptor.logicalType != LogicalValueType::Failure;

    RequireState(issues, "channels", descriptor.channels.state, imageLike);
    RequireState(
        issues,
        "presentImageComponents",
        descriptor.presentImageComponents.state,
        colorImage);
    RequireState(issues, "color", descriptor.color.state, colorImage);
    RequireState(issues, "transfer", descriptor.transfer.state, colorImage);
    RequireState(issues, "reference", descriptor.reference.state, colorImage);
    RequireState(issues, "alpha", descriptor.alpha.state, colorImage);
    RequireState(issues, "range", descriptor.range.state, numeric);
    RequireState(issues, "precision", descriptor.precision.state, numeric);
    RequireState(issues, "spatial", descriptor.spatial.state, imageLike);
    RequireState(issues, "sampling", descriptor.sampling.state, imageLike);
    RequireState(issues, "units", descriptor.units.state, unitBearing);
    RequireState(issues, "provenance", descriptor.provenance.state, executable);

    if (descriptor.color.state == KnowledgeState::Known &&
        descriptor.color.value.identity.empty()) {
        issues.push_back({ "color", "known color identity cannot be empty" });
    }
    if (descriptor.color.state == KnowledgeState::Known &&
        !descriptor.color.value.profileHash.empty() &&
        !IsValidContentHash(descriptor.color.value.profileHash)) {
        issues.push_back({ "color", "profile hash must be an exact SHA-256 identity when present" });
    }
    if (descriptor.channels.state == KnowledgeState::Known) {
        const ChannelDescriptor& channels = descriptor.channels.value;
        std::size_t expectedRoles = 0;
        switch (channels.layout) {
        case ChannelLayout::Gray: expectedRoles = 1; break;
        case ChannelLayout::RGB: expectedRoles = 3; break;
        case ChannelLayout::RGBA: expectedRoles = 4; break;
        case ChannelLayout::XY: expectedRoles = 2; break;
        case ChannelLayout::ComplexPair: expectedRoles = 2; break;
        case ChannelLayout::NamedData: expectedRoles = channels.roles.size(); break;
        }
        if (channels.roles.empty() || channels.roles.size() != expectedRoles ||
            std::any_of(channels.roles.begin(), channels.roles.end(),
                [](const std::string& role) { return role.empty(); })) {
            issues.push_back({ "channels", "known channel layout requires one explicit role per channel" });
        }
    }
    if (descriptor.presentImageComponents.state == KnowledgeState::Known) {
        constexpr std::uint8_t kAllComponentBits =
            static_cast<std::uint8_t>(ImageComponent::Red) |
            static_cast<std::uint8_t>(ImageComponent::Green) |
            static_cast<std::uint8_t>(ImageComponent::Blue) |
            static_cast<std::uint8_t>(ImageComponent::Alpha);
        const ImageComponentSet& components =
            descriptor.presentImageComponents.value;
        if (components.bits == 0 ||
            (components.bits & static_cast<std::uint8_t>(~kAllComponentBits)) != 0) {
            issues.push_back({
                "presentImageComponents",
                "known Image component presence must contain one or more of R, G, B, and A"
            });
        }
        if (colorImage &&
            descriptor.channels.state == KnowledgeState::Known &&
            !(descriptor.channels.value ==
                MakeImageChannelDescriptor(components))) {
            issues.push_back({
                "presentImageComponents",
                "known Image components must agree with the declared channel layout and roles"
            });
        }
        if (colorImage &&
            descriptor.alpha.state == KnowledgeState::Known) {
            const bool hasAlpha =
                HasImageComponent(components, ImageComponent::Alpha);
            const bool alphaClaimsNoStoredComponent =
                descriptor.alpha.value == AlphaMode::Absent ||
                descriptor.alpha.value == AlphaMode::Opaque;
            if (hasAlpha == alphaClaimsNoStoredComponent) {
                issues.push_back({
                    "presentImageComponents",
                    "stored alpha-component presence contradicts the declared alpha mode"
                });
            }
        }
    }
    if (descriptor.range.state == KnowledgeState::Known) {
        const NumericRange& range = descriptor.range.value;
        if (!std::isfinite(range.nominalMinimum) ||
            !std::isfinite(range.nominalMaximum) ||
            range.nominalMinimum > range.nominalMaximum) {
            issues.push_back({ "range", "known nominal range must be finite and ordered" });
        }
    }
    if (descriptor.spatial.state == KnowledgeState::Known) {
        const SpatialDescriptor& spatial = descriptor.spatial.value;
        if (!std::isfinite(spatial.pixelAspect) || spatial.pixelAspect <= 0.0) {
            issues.push_back({ "spatial", "pixel aspect must be finite and positive" });
        }
        if (spatial.kind == SpatialExtentKind::Finite) {
            if (spatial.fullWindow.width <= 0 || spatial.fullWindow.height <= 0 ||
                spatial.dataWindow.width < 0 || spatial.dataWindow.height < 0) {
                issues.push_back({ "spatial", "finite full window must be positive and data dimensions cannot be negative" });
            } else if (!Contains(spatial.fullWindow, spatial.dataWindow)) {
                issues.push_back({ "spatial", "data window must be contained by full window" });
            }
        }
        if (spatial.rasterOrigin != RasterOrigin::BottomLeft &&
            spatial.rasterOrigin != RasterOrigin::TopLeft) {
            issues.push_back({ "spatial", "raster origin must be BottomLeft or TopLeft" });
        }
    }
    if (descriptor.transfer.state == KnowledgeState::Known) {
        const TransferDescriptor& transfer = descriptor.transfer.value;
        if (transfer.kind == TransferKind::Gamma &&
            (!std::isfinite(transfer.parameter) || transfer.parameter <= 0.0)) {
            issues.push_back({ "transfer", "known gamma transfer requires a positive parameter" });
        }
        if (transfer.kind == TransferKind::Custom && transfer.key.empty()) {
            issues.push_back({ "transfer", "custom transfer requires a stable key" });
        }
    }
    if (descriptor.units.state == KnowledgeState::Known &&
        descriptor.units.value.kind == UnitKind::Custom &&
        descriptor.units.value.customKey.empty()) {
        issues.push_back({ "units", "custom unit requires a stable key" });
    }
    if (descriptor.sampling.state == KnowledgeState::Known &&
        (descriptor.sampling.value.filter == ReconstructionFilter::Custom ||
            descriptor.sampling.value.border == BorderPolicy::Custom) &&
        descriptor.sampling.value.customKey.empty()) {
        issues.push_back({ "sampling", "custom sampling behavior requires a stable key" });
    }
    if (descriptor.provenance.state == KnowledgeState::Known &&
        descriptor.provenance.value.sourceIdentity.empty() &&
        descriptor.provenance.value.operationIdentity.empty()) {
        issues.push_back({ "provenance", "known provenance requires a source or operation identity" });
    }
    return issues;
}

bool operator==(const SemanticVersion& left, const SemanticVersion& right) {
    return left.major == right.major && left.minor == right.minor &&
        left.patch == right.patch;
}

bool operator!=(const SemanticVersion& left, const SemanticVersion& right) {
    return !(left == right);
}

std::optional<SemanticVersion> ParseSemanticVersion(const std::string& text) {
    const std::size_t first = text.find('.');
    const std::size_t second = first == std::string::npos
        ? std::string::npos : text.find('.', first + 1);
    if (first == std::string::npos || second == std::string::npos ||
        text.find('.', second + 1) != std::string::npos) {
        return std::nullopt;
    }
    const auto major = ParseVersionPart(text.substr(0, first));
    const auto minor = ParseVersionPart(text.substr(first + 1, second - first - 1));
    const auto patch = ParseVersionPart(text.substr(second + 1));
    if (!major || !minor || !patch) {
        return std::nullopt;
    }
    return SemanticVersion{ *major, *minor, *patch };
}

std::string ToString(const SemanticVersion& version) {
    return std::to_string(version.major) + "." +
        std::to_string(version.minor) + "." + std::to_string(version.patch);
}

bool IsValidDefinitionId(const std::string& id) {
    const std::size_t separator = id.find(':');
    return separator != std::string::npos && separator > 0 &&
        id.find(':', separator + 1) == std::string::npos &&
        IsDefinitionSegment(id.substr(0, separator), false) &&
        IsDefinitionSegment(id.substr(separator + 1), true);
}

bool IsValidScopedId(const std::string& id) {
    if (id.empty() || !IsLowerAlpha(id.front())) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char character) {
        return IsLowerAlpha(character) || IsDigit(character) ||
            character == '.' || character == '_' || character == '-';
    });
}

bool IsValidContentHash(const std::string& hash) {
    constexpr std::size_t kPrefixSize = 7;
    constexpr std::size_t kDigestSize = 64;
    if (hash.size() != kPrefixSize + kDigestSize ||
        hash.compare(0, kPrefixSize, "sha256:") != 0) {
        return false;
    }
    return std::all_of(hash.begin() + static_cast<std::ptrdiff_t>(kPrefixSize),
        hash.end(), IsLowerHex);
}

bool IsValidCanonicalUuid(const std::string& uuid) {
    if (uuid.size() != 36 || uuid[8] != '-' || uuid[13] != '-' ||
        uuid[18] != '-' || uuid[23] != '-') {
        return false;
    }
    for (std::size_t index = 0; index < uuid.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            continue;
        }
        if (!IsLowerHex(uuid[index])) {
            return false;
        }
    }
    return true;
}

std::string Sha256ContentIdentity(const std::string& content) {
    Sha256 hash;
    hash.Update(content.data(), content.size());
    return "sha256:" + hash.Finish();
}

const std::vector<DiagnosticRule>& BuiltInDiagnosticRules() {
    static const std::vector<DiagnosticRule> rules = {
        { "nmr.connection.missing-definition", DiagnosticStage::Connection, DiagnosticSeverity::HardError, "Exact definition is unavailable." },
        { "nmr.connection.missing-input", DiagnosticStage::Connection, DiagnosticSeverity::HardError, "Required input is disconnected." },
        { "nmr.connection.unknown-port", DiagnosticStage::Connection, DiagnosticSeverity::HardError, "Saved port identity is absent." },
        { "nmr.connection.type-mismatch", DiagnosticStage::Connection, DiagnosticSeverity::HardError, "Port logical types are incompatible." },
        { "nmr.connection.cycle", DiagnosticStage::Connection, DiagnosticSeverity::HardError, "Connection creates an unsupported cycle." },
        { "nmr.semantic.color-mismatch", DiagnosticStage::Semantic, DiagnosticSeverity::Warning, "Color identities differ during numeric work." },
        { "nmr.semantic.color-unexpected", DiagnosticStage::Semantic, DiagnosticSeverity::Warning, "An explicit color matrix received a different declared color identity." },
        { "nmr.semantic.source-color-unknown", DiagnosticStage::Semantic, DiagnosticSeverity::Information, "An untagged source remains visibly Unknown." },
        { "nmr.semantic.transfer-nonlinear", DiagnosticStage::Semantic, DiagnosticSeverity::Warning, "Operation is commonly intended for linear transfer." },
        { "nmr.semantic.transfer-unknown", DiagnosticStage::Semantic, DiagnosticSeverity::Warning, "Transfer is unknown for a transfer-sensitive operation." },
        { "nmr.semantic.transfer-unexpected", DiagnosticStage::Semantic, DiagnosticSeverity::Warning, "An explicit transfer operation received a different declared transfer." },
        { "nmr.semantic.alpha-mismatch", DiagnosticStage::Semantic, DiagnosticSeverity::HardError, "Alpha representation contradicts the declared formula." },
        { "nmr.semantic.alpha-formula-mismatch", DiagnosticStage::Semantic, DiagnosticSeverity::Warning, "An explicitly selected alpha formula received a different declared representation but remains numerically executable." },
        { "nmr.semantic.extent-policy-missing", DiagnosticStage::Semantic, DiagnosticSeverity::HardError, "Multi-input extent policy is undeclared." },
        { "nmr.semantic.image-component-missing", DiagnosticStage::Semantic, DiagnosticSeverity::HardError, "A requested Image component is semantically absent." },
        { "nmr.semantic.image-component-set-invalid", DiagnosticStage::Semantic, DiagnosticSeverity::HardError, "An Image component-presence set is malformed or empty." },
        { "nmr.semantic.image-combine-color-missing", DiagnosticStage::Semantic, DiagnosticSeverity::HardError, "Image Combine requires at least one color Channel." },
        { "nmr.semantic.image-combine-extent-unknown", DiagnosticStage::Semantic, DiagnosticSeverity::HardError, "Image Combine requires known finite Channel extents." },
        { "nmr.semantic.image-combine-extent-mismatch", DiagnosticStage::Semantic, DiagnosticSeverity::HardError, "Image Combine Channel extents do not match." },
        { "nmr.semantic.precision-risk", DiagnosticStage::Semantic, DiagnosticSeverity::Warning, "Precision may be inadequate for declared behavior." },
        { "nmr.semantic.metadata-missing", DiagnosticStage::Semantic, DiagnosticSeverity::HardError, "Execution-critical metadata is unknown." },
        { "nmr.semantic.external-policy-missing", DiagnosticStage::Semantic, DiagnosticSeverity::HardError, "External operation omits a field disposition." },
        { "nmr.output.type-mismatch", DiagnosticStage::Semantic, DiagnosticSeverity::HardError, "The direct output file type cannot represent this logical value." },
        { "nmr.output.channel-export-unsupported", DiagnosticStage::Semantic, DiagnosticSeverity::HardError, "PNG export requires an Image rather than a Channel inspection value." },
        { "nmr.output.png-premultiplied", DiagnosticStage::Semantic, DiagnosticSeverity::HardError, "PNG requires straight rather than premultiplied alpha." },
        { "nmr.output.color-unknown", DiagnosticStage::Semantic, DiagnosticSeverity::Warning, "Direct output has Unknown color state and is not silently tagged." },
        { "nmr.output.profile-unavailable", DiagnosticStage::Semantic, DiagnosticSeverity::Warning, "Declared output profile bytes are unavailable." },
        { "nmr.output.png-quantization", DiagnosticStage::Semantic, DiagnosticSeverity::Information, "Direct PNG output performs only explicit file quantization." },
        { "nmr.lowering.implementation-missing", DiagnosticStage::Lowering, DiagnosticSeverity::HardError, "No exact implementation satisfies the definition." },
        { "nmr.lowering.capability-missing", DiagnosticStage::Lowering, DiagnosticSeverity::HardError, "Target lacks a required capability." },
        { "nmr.runtime.shader", DiagnosticStage::Runtime, DiagnosticSeverity::RuntimeFault, "Shader execution failed." },
        { "nmr.runtime.resource", DiagnosticStage::Runtime, DiagnosticSeverity::RuntimeFault, "Required resource failed." },
        { "nmr.runtime.non-finite", DiagnosticStage::Runtime, DiagnosticSeverity::RuntimeFault, "Forbidden non-finite result was observed." },
        { "nmr.runtime.allocation", DiagnosticStage::Runtime, DiagnosticSeverity::RuntimeFault, "Resource allocation failed." }
    };
    return rules;
}

std::vector<ContractIssue> ValidateDiagnosticRules(
    const std::vector<DiagnosticRule>& rules) {
    std::vector<ContractIssue> issues;
    std::set<std::string> identities;
    for (const DiagnosticRule& rule : rules) {
        if (!IsValidScopedId(rule.id)) {
            issues.push_back({ "diagnostic.id", "malformed diagnostic rule ID: " + rule.id });
        }
        if (!identities.insert(rule.id).second) {
            issues.push_back({ "diagnostic.id", "duplicate diagnostic rule ID: " + rule.id });
        }
        if (rule.purpose.empty()) {
            issues.push_back({ "diagnostic.purpose", "diagnostic purpose cannot be empty" });
        }
        if (rule.stage == DiagnosticStage::Runtime &&
            rule.defaultSeverity != DiagnosticSeverity::RuntimeFault) {
            issues.push_back({ "diagnostic.severity", "runtime rules must be runtime faults" });
        }
    }
    return issues;
}

bool IsDiagnosticAcknowledgementAllowed(DiagnosticSeverity severity) {
    return severity == DiagnosticSeverity::Warning ||
        severity == DiagnosticSeverity::Information;
}

} // namespace Stack::NodeMath
