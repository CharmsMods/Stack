#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Stack::NodeMath {

inline constexpr std::uint32_t kSemanticDescriptorSchemaVersion = 2;

enum class LogicalValueType {
    Invalid,
    Boolean,
    Integer,
    Scalar,
    Vector2,
    Vector3,
    Vector4,
    Matrix3,
    Matrix4,
    Coordinate2,
    Curve1D,
    Lut,
    ScalarField,
    Vector2Field,
    Vector3Field,
    Vector4Field,
    ColorImage,
    Mask,
    DataImage,
    ComplexSpectrum,
    Histogram,
    Statistics,
    Metadata,
    SpecializedHandle,
    Raw,
    Analysis,
    Failure
};

enum class KnowledgeState {
    NotApplicable,
    Unknown,
    Known
};

template <typename T>
struct SemanticField {
    KnowledgeState state = KnowledgeState::NotApplicable;
    T value{};

    static SemanticField NotApplicable() { return {}; }

    static SemanticField Unknown() {
        SemanticField field;
        field.state = KnowledgeState::Unknown;
        return field;
    }

    static SemanticField Known(T knownValue) {
        SemanticField field;
        field.state = KnowledgeState::Known;
        field.value = std::move(knownValue);
        return field;
    }
};

template <typename T>
bool operator==(const SemanticField<T>& left, const SemanticField<T>& right) {
    return left.state == right.state &&
        (left.state != KnowledgeState::Known || left.value == right.value);
}

template <typename T>
bool operator!=(const SemanticField<T>& left, const SemanticField<T>& right) {
    return !(left == right);
}

enum class ChannelLayout {
    Gray,
    RGB,
    RGBA,
    XY,
    ComplexPair,
    NamedData
};

struct ChannelDescriptor {
    ChannelLayout layout = ChannelLayout::NamedData;
    std::vector<std::string> roles;
};

bool operator==(const ChannelDescriptor& left, const ChannelDescriptor& right);

enum class ColorRelation {
    Standard,
    Derived
};

struct ColorIdentity {
    std::string identity;
    std::string profileHash;
    ColorRelation relation = ColorRelation::Standard;
};

bool operator==(const ColorIdentity& left, const ColorIdentity& right);

enum class TransferKind {
    Linear,
    Srgb,
    Gamma,
    Log,
    Pq,
    Hlg,
    Custom
};

struct TransferDescriptor {
    TransferKind kind = TransferKind::Linear;
    double parameter = 0.0;
    std::string key;
};

bool operator==(const TransferDescriptor& left, const TransferDescriptor& right);

enum class ReferenceState {
    Scene,
    Display,
    Output,
    Data
};

enum class AlphaMode {
    Absent,
    Opaque,
    Straight,
    Premultiplied
};

enum class NonFinitePolicy {
    Unknown,
    Forbidden,
    Preserve
};

struct NumericRange {
    double nominalMinimum = 0.0;
    double nominalMaximum = 1.0;
    bool allowsBelowNominal = false;
    bool allowsAboveNominal = false;
    NonFinitePolicy nonFinite = NonFinitePolicy::Unknown;
};

bool operator==(const NumericRange& left, const NumericRange& right);

enum class LogicalPrecision {
    UInt8,
    UInt16,
    Float16,
    Float32,
    Float64
};

struct Rect {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t width = 0;
    std::int64_t height = 0;
};

bool operator==(const Rect& left, const Rect& right);

enum class SpatialExtentKind {
    Empty,
    Finite
};

enum class RasterOrigin {
    BottomLeft,
    TopLeft
};

struct SpatialDescriptor {
    SpatialExtentKind kind = SpatialExtentKind::Empty;
    Rect fullWindow;
    Rect dataWindow;
    RasterOrigin rasterOrigin = RasterOrigin::BottomLeft;
    double pixelAspect = 1.0;
};

bool operator==(const SpatialDescriptor& left, const SpatialDescriptor& right);

enum class CoordinateConvention {
    PixelCenters,
    PixelCorners,
    Normalized
};

enum class ReconstructionFilter {
    Nearest,
    Linear,
    Cubic,
    Custom
};

enum class BorderPolicy {
    Transparent,
    Clamp,
    Repeat,
    Mirror,
    Constant,
    Custom
};

struct SamplingDescriptor {
    CoordinateConvention coordinates = CoordinateConvention::PixelCenters;
    ReconstructionFilter filter = ReconstructionFilter::Linear;
    BorderPolicy border = BorderPolicy::Transparent;
    std::string customKey;
};

bool operator==(const SamplingDescriptor& left, const SamplingDescriptor& right);

enum class UnitKind {
    Unitless,
    ExposureValue,
    Pixels,
    NormalizedCoordinate,
    Degrees,
    Percent,
    CodeValue,
    Luminance,
    Custom
};

struct UnitDescriptor {
    UnitKind kind = UnitKind::Unitless;
    std::string customKey;
};

bool operator==(const UnitDescriptor& left, const UnitDescriptor& right);

enum class ProvenanceKind {
    Embedded,
    Untagged,
    Assigned,
    Converted,
    Generated,
    Derived,
    RawDeveloped,
    External
};

struct ProvenanceDescriptor {
    ProvenanceKind kind = ProvenanceKind::Generated;
    std::string sourceIdentity;
    std::string operationIdentity;
};

bool operator==(const ProvenanceDescriptor& left, const ProvenanceDescriptor& right);

struct ValueDescriptor {
    std::uint32_t schemaVersion = kSemanticDescriptorSchemaVersion;
    LogicalValueType logicalType = LogicalValueType::Invalid;
    SemanticField<ChannelDescriptor> channels;
    SemanticField<ColorIdentity> color;
    SemanticField<TransferDescriptor> transfer;
    SemanticField<ReferenceState> reference;
    SemanticField<AlphaMode> alpha;
    SemanticField<NumericRange> range;
    SemanticField<LogicalPrecision> precision;
    SemanticField<SpatialDescriptor> spatial;
    SemanticField<SamplingDescriptor> sampling;
    SemanticField<UnitDescriptor> units;
    SemanticField<ProvenanceDescriptor> provenance;
};

bool operator==(const ValueDescriptor& left, const ValueDescriptor& right);
bool operator!=(const ValueDescriptor& left, const ValueDescriptor& right);
bool StrictSemanticDescriptorMatch(
    const ValueDescriptor& left,
    const ValueDescriptor& right);

struct ContractIssue {
    std::string field;
    std::string message;
};

bool IsImageLike(LogicalValueType type);
bool IsNumericLike(LogicalValueType type);
ValueDescriptor MakeUnknownDescriptor(LogicalValueType type);
ValueDescriptor MakeTaggedColorImageDescriptor(
    std::string colorIdentity,
    std::string profileHash,
    TransferDescriptor transfer,
    ReferenceState reference,
    AlphaMode alpha,
    SpatialDescriptor spatial,
    SamplingDescriptor sampling,
    LogicalPrecision precision,
    std::string sourceIdentity);
ValueDescriptor MakeUntaggedColorImageDescriptor(
    AlphaMode alpha,
    SpatialDescriptor spatial,
    SamplingDescriptor sampling,
    LogicalPrecision precision,
    std::string sourceIdentity);
std::vector<ContractIssue> ValidateDescriptor(const ValueDescriptor& descriptor);

struct SemanticVersion {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
};

bool operator==(const SemanticVersion& left, const SemanticVersion& right);
bool operator!=(const SemanticVersion& left, const SemanticVersion& right);
std::optional<SemanticVersion> ParseSemanticVersion(const std::string& text);
std::string ToString(const SemanticVersion& version);

bool IsValidDefinitionId(const std::string& id);
bool IsValidScopedId(const std::string& id);
bool IsValidContentHash(const std::string& hash);
bool IsValidCanonicalUuid(const std::string& uuid);
std::string Sha256ContentIdentity(const std::string& content);

enum class DiagnosticStage {
    Connection,
    Semantic,
    Lowering,
    Runtime
};

enum class DiagnosticSeverity {
    HardError,
    Warning,
    Information,
    RuntimeFault
};

struct DiagnosticRule {
    std::string id;
    DiagnosticStage stage = DiagnosticStage::Semantic;
    DiagnosticSeverity defaultSeverity = DiagnosticSeverity::Warning;
    std::string purpose;
};

struct Diagnostic {
    std::string ruleId;
    DiagnosticStage stage = DiagnosticStage::Semantic;
    DiagnosticSeverity severity = DiagnosticSeverity::Warning;
    std::string authoredSourceIdentity;
    std::string affectedIdentity;
    std::string semanticFingerprint;
    std::string message;
    std::string suggestedRepair;
};

const std::vector<DiagnosticRule>& BuiltInDiagnosticRules();
std::vector<ContractIssue> ValidateDiagnosticRules(
    const std::vector<DiagnosticRule>& rules);
bool IsDiagnosticAcknowledgementAllowed(DiagnosticSeverity severity);

} // namespace Stack::NodeMath
