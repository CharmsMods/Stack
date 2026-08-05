#pragma once

#include "Editor/NodeGraph/SocketPresentation.h"
#include "NodeMath/FirstClassValue.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// Presentation-only wire readouts. These strings deliberately derive from
// immutable graph state; they are neither semantic data nor project state.
namespace EditorNodeGraph::WireReadout {

enum class Attention {
    None,
    Warning,
    Error
};

struct DetailFact {
    std::string label;
    std::string value;
};

struct Input {
    SocketDefinition sourceSocket;
    bool hasDescriptor = false;
    Stack::NodeMath::ValueDescriptor descriptor;
    std::optional<Stack::NodeMath::FirstClassValue> value;
    std::vector<Stack::NodeMath::Diagnostic> sourceDiagnostics;
};

struct Readout {
    std::string primary;
    std::string secondary;
    Attention attention = Attention::None;
    std::string accessibleText;
    std::vector<DetailFact> detailFacts;
};

inline std::string OutputIdentity(int nodeId, const std::string& socketId) {
    return "output-" + std::to_string(nodeId) + "-" + socketId;
}

// Semantic diagnostics are node-output scoped today. A node-wide output
// diagnostic is eligible for a wire only when the source has exactly one
// output; diagnostics associated with a destination never enter this result.
inline std::vector<Stack::NodeMath::Diagnostic> FilterSourceOutputDiagnostics(
    const std::vector<Stack::NodeMath::Diagnostic>& diagnostics,
    const std::string& sourceNodeIdentity,
    bool sourceOutputIsUnambiguous) {
    std::vector<Stack::NodeMath::Diagnostic> result;
    if (!sourceOutputIsUnambiguous) return result;
    for (const Stack::NodeMath::Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.affectedIdentity == sourceNodeIdentity) {
            result.push_back(diagnostic);
        }
    }
    return result;
}

inline constexpr const char* kSeparator = " \xC2\xB7 ";

inline std::string JoinFacts(const std::vector<std::string>& facts) {
    std::string result;
    for (const std::string& fact : facts) {
        if (fact.empty()) continue;
        if (!result.empty()) result += kSeparator;
        result += fact;
    }
    return result;
}

inline std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

inline std::string TitlecaseRole(std::string role) {
    if (role.empty()) return {};
    const std::string normalized = Lowercase(role);
    if (normalized == "value" || normalized == "neutral" || normalized == "unknown") {
        return {};
    }
    if (normalized == "r" || normalized == "red") return "Red";
    if (normalized == "g" || normalized == "green") return "Green";
    if (normalized == "b" || normalized == "blue") return "Blue";
    if (normalized == "a" || normalized == "alpha") return "Alpha";
    if (normalized == "mask") return "Mask";
    if (normalized == "luminance") return "Luminance";
    if (normalized == "ev" || normalized == "exposure") return "EV";
    role.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(role.front())));
    return role;
}

inline std::string SocketRole(const SocketDefinition& socket) {
    const std::string role = Lowercase(socket.semanticRoleKey);
    if (role == "alpha-channel") return "Alpha";
    if (role == "red-channel") return "Red";
    if (role == "green-channel") return "Green";
    if (role == "blue-channel") return "Blue";
    if (role == "mask") return "Mask";
    if (role == "exposure") return "EV";
    return {};
}

inline std::string DeclaredChannelRole(const Input& input) {
    if (input.hasDescriptor &&
        input.descriptor.channels.state == Stack::NodeMath::KnowledgeState::Known &&
        input.descriptor.channels.value.roles.size() == 1) {
        if (const std::string role = TitlecaseRole(input.descriptor.channels.value.roles.front());
            !role.empty()) {
            return role;
        }
    }
    return SocketRole(input.sourceSocket);
}

inline std::string FormatNumber(double value) {
    if (!std::isfinite(value)) return {};
    if (std::abs(value) < 0.00005) value = 0.0;
    std::ostringstream stream;
    stream << std::setprecision(4) << std::defaultfloat << value;
    return stream.str();
}

inline std::string CompactValues(const std::vector<double>& values) {
    if (values.empty() || values.size() > 4) return {};
    std::string result;
    for (const double value : values) {
        const std::string number = FormatNumber(value);
        if (number.empty()) return {};
        if (!result.empty()) result += ", ";
        result += number;
    }
    return result.size() <= 32 ? result : std::string{};
}

inline std::string UnitsFor(const Stack::NodeMath::UnitDescriptor& units) {
    return SocketPresentation::UnitName(
        Stack::NodeMath::SemanticField<Stack::NodeMath::UnitDescriptor>::Known(units));
}

inline std::string RangeFor(
    const Stack::NodeMath::SemanticField<Stack::NodeMath::NumericRange>& range) {
    using namespace Stack::NodeMath;
    if (range.state == KnowledgeState::Unknown) return "Unknown range";
    if (range.state != KnowledgeState::Known) return {};
    const std::string minimum = FormatNumber(range.value.nominalMinimum);
    const std::string maximum = FormatNumber(range.value.nominalMaximum);
    if (minimum.empty() || maximum.empty()) return "Unknown range";
    std::string result = minimum + " to " + maximum;
    if (range.value.allowsBelowNominal && range.value.allowsAboveNominal) {
        result += " extended";
    } else if (range.value.allowsBelowNominal) {
        result += " extended below";
    } else if (range.value.allowsAboveNominal) {
        result += " extended above";
    }
    return result;
}

inline std::string ExtentFor(
    const Stack::NodeMath::SemanticField<Stack::NodeMath::SpatialDescriptor>& spatial) {
    using namespace Stack::NodeMath;
    if (spatial.state != KnowledgeState::Known ||
        spatial.value.kind != SpatialExtentKind::Finite ||
        spatial.value.dataWindow.width <= 0 || spatial.value.dataWindow.height <= 0) {
        return {};
    }
    return std::to_string(spatial.value.dataWindow.width) + " x " +
        std::to_string(spatial.value.dataWindow.height);
}

inline std::string ImageComponents(const Input& input) {
    using namespace Stack::NodeMath;
    if (input.hasDescriptor) {
        if (input.descriptor.presentImageComponents.state == KnowledgeState::Known) {
            std::vector<std::string> components;
            for (const ImageComponent component :
                    OrderedImageComponents(input.descriptor.presentImageComponents.value)) {
                components.push_back(ImageComponentToken(component));
            }
            if (!components.empty()) {
                std::string result;
                for (const std::string& component : components) {
                    if (!result.empty()) result += ", ";
                    result += component;
                }
                return result;
            }
        } else if (input.descriptor.presentImageComponents.state == KnowledgeState::Unknown) {
            return "Unknown components";
        }
        if (input.descriptor.channels.state == KnowledgeState::Known) {
            switch (input.descriptor.channels.value.layout) {
                case ChannelLayout::RGB: return "RGB";
                case ChannelLayout::RGBA: return "RGBA";
                default: break;
            }
        }
    }
    return "Unknown components";
}

inline std::string ColorIdentityFor(
    const Stack::NodeMath::SemanticField<Stack::NodeMath::ColorIdentity>& color) {
    using namespace Stack::NodeMath;
    if (color.state == KnowledgeState::Unknown) return "Unknown color";
    if (color.state != KnowledgeState::Known) return "N/A color";
    if (color.value.identity == "srgb-d65") return "sRGB";
    if (color.value.identity == "display-p3-d65") return "Display-P3";
    return color.value.identity.empty() ? "Unknown color" : color.value.identity;
}

inline std::string TransferAndReferenceFor(const Stack::NodeMath::ValueDescriptor& descriptor) {
    using namespace Stack::NodeMath;
    std::string transfer = "Unknown transfer";
    if (descriptor.transfer.state == KnowledgeState::NotApplicable) {
        transfer = "N/A transfer";
    } else if (descriptor.transfer.state == KnowledgeState::Known) {
        switch (descriptor.transfer.value.kind) {
            case TransferKind::Linear: transfer = "linear"; break;
            case TransferKind::Srgb: transfer = "encoded"; break;
            case TransferKind::Gamma: transfer = "gamma"; break;
            case TransferKind::Log: transfer = "log"; break;
            case TransferKind::Pq: transfer = "PQ"; break;
            case TransferKind::Hlg: transfer = "HLG"; break;
            case TransferKind::Custom: transfer = descriptor.transfer.value.key.empty()
                ? "custom transfer" : descriptor.transfer.value.key; break;
        }
    }
    if (descriptor.reference.state != KnowledgeState::Known) return transfer;
    const char* reference = "data";
    switch (descriptor.reference.value) {
        case ReferenceState::Scene: reference = "scene"; break;
        case ReferenceState::Display: reference = "display"; break;
        case ReferenceState::Output: reference = "output"; break;
        case ReferenceState::Data: reference = "data"; break;
    }
    return std::string(reference) + " " + transfer;
}

inline std::string AlphaFor(const Stack::NodeMath::ValueDescriptor& descriptor) {
    using namespace Stack::NodeMath;
    if (descriptor.alpha.state == KnowledgeState::Unknown) return "Unknown alpha";
    if (descriptor.alpha.state != KnowledgeState::Known) return "N/A alpha";
    switch (descriptor.alpha.value) {
        case AlphaMode::Absent: return "no alpha";
        case AlphaMode::Opaque: return "opaque";
        case AlphaMode::Straight: return "straight alpha";
        case AlphaMode::Premultiplied: return "premultiplied alpha";
    }
    return "Unknown alpha";
}

inline Stack::NodeMath::LogicalValueType LogicalTypeFor(const Input& input) {
    using Stack::NodeMath::LogicalValueType;
    if (input.value.has_value() &&
        input.value->logicalType != LogicalValueType::Invalid) {
        return input.value->logicalType;
    }
    if (input.hasDescriptor &&
        input.descriptor.logicalType != LogicalValueType::Invalid) {
        return input.descriptor.logicalType;
    }
    return input.sourceSocket.logicalType;
}

inline bool IsUniformValueType(Stack::NodeMath::LogicalValueType type) {
    using Type = Stack::NodeMath::LogicalValueType;
    switch (type) {
        case Type::Boolean:
        case Type::Integer:
        case Type::Scalar:
        case Type::Vector2:
        case Type::Vector3:
        case Type::Vector4:
        case Type::Matrix3:
        case Type::Matrix4:
        case Type::Coordinate2:
            return true;
        default:
            return false;
    }
}

inline std::string ValueShape(Stack::NodeMath::LogicalValueType type) {
    using Type = Stack::NodeMath::LogicalValueType;
    switch (type) {
        case Type::Boolean: return "Boolean";
        case Type::Integer: return "Integer";
        case Type::Scalar: return "Scalar";
        case Type::Vector2: return "Vector 2";
        case Type::Vector3: return "Vector 3";
        case Type::Vector4: return "Vector 4";
        case Type::Matrix3: return "Matrix 3 x 3";
        case Type::Matrix4: return "Matrix 4 x 4";
        case Type::Coordinate2: return "Coordinate 2D";
        default: return "Unknown";
    }
}

inline std::string FieldShape(Stack::NodeMath::LogicalValueType type) {
    using Type = Stack::NodeMath::LogicalValueType;
    switch (type) {
        case Type::ScalarField: return "Scalar";
        case Type::Vector2Field: return "Vector 2";
        case Type::Vector3Field: return "Vector 3";
        case Type::Vector4Field: return "Vector 4";
        default: return "Unknown";
    }
}

inline std::string CompactPayload(const Stack::NodeMath::FirstClassValue& value) {
    using namespace Stack::NodeMath;
    if (value.availability != ValueAvailability::Known) return {};
    if (const bool* boolean = std::get_if<bool>(&value.payload)) {
        return *boolean ? "True" : "False";
    }
    if (const std::int64_t* integer = std::get_if<std::int64_t>(&value.payload)) {
        return std::to_string(*integer);
    }
    if (const double* scalar = std::get_if<double>(&value.payload)) {
        return FormatNumber(*scalar);
    }
    if (const auto* vector2 = std::get_if<std::array<double, 2>>(&value.payload)) {
        return CompactValues(std::vector<double>(vector2->begin(), vector2->end()));
    }
    if (const auto* vector3 = std::get_if<std::array<double, 3>>(&value.payload)) {
        return CompactValues(std::vector<double>(vector3->begin(), vector3->end()));
    }
    if (const auto* vector4 = std::get_if<std::array<double, 4>>(&value.payload)) {
        return CompactValues(std::vector<double>(vector4->begin(), vector4->end()));
    }
    return {};
}

inline std::string DataSummary(const std::optional<Stack::NodeMath::FirstClassValue>& value) {
    using namespace Stack::NodeMath;
    if (value.has_value() && value->availability == ValueAvailability::Known) {
        if (const auto* curve = std::get_if<CurveValue>(&value->payload)) {
            return std::to_string(curve->points.size()) + " points";
        }
        if (const auto* histogram = std::get_if<HistogramValue>(&value->payload)) {
            return std::to_string(histogram->bins.size()) + " bins" + kSeparator +
                FormatNumber(histogram->domainMinimum) + " to " +
                FormatNumber(histogram->domainMaximum);
        }
        if (const auto* statistics = std::get_if<StatisticsValue>(&value->payload)) {
            return std::to_string(statistics->entries.size()) + " measures";
        }
        if (const auto* resource = std::get_if<ResourceValue>(&value->payload)) {
            return resource->resourceType.empty() ? "Structured data" : resource->resourceType;
        }
    }
    return "Structured data";
}

inline std::string CompactDiagnosticText(const Stack::NodeMath::Diagnostic& diagnostic) {
    std::string result = diagnostic.suggestedRepair.empty()
        ? diagnostic.message : diagnostic.suggestedRepair;
    const std::size_t sentenceEnd = result.find_first_of(".!?");
    if (sentenceEnd != std::string::npos) result.resize(sentenceEnd);
    constexpr std::size_t kMaximumLength = 64;
    if (result.size() > kMaximumLength) result = result.substr(0, kMaximumLength - 3) + "...";
    return result.empty() ? "Source state needs attention" : result;
}

inline std::pair<Attention, std::string> SourceAttention(const Input& input) {
    using namespace Stack::NodeMath;
    if (input.value.has_value() &&
        (input.value->availability == ValueAvailability::Failure ||
         input.value->availability == ValueAvailability::Missing)) {
        return { Attention::Error, input.value->message.empty()
            ? "Source value is unavailable" : input.value->message };
    }
    for (const Diagnostic& diagnostic : input.sourceDiagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::HardError) {
            return { Attention::Error, CompactDiagnosticText(diagnostic) };
        }
    }
    for (const Diagnostic& diagnostic : input.sourceDiagnostics) {
        if (diagnostic.severity == DiagnosticSeverity::Warning &&
            !diagnostic.suggestedRepair.empty()) {
            return { Attention::Warning, CompactDiagnosticText(diagnostic) };
        }
    }
    return { Attention::None, {} };
}

inline Readout Build(const Input& input) {
    using namespace Stack::NodeMath;
    const LogicalValueType type = LogicalTypeFor(input);
    Readout result;
    switch (type) {
        case LogicalValueType::ColorImage:
            result.primary = "Image" + std::string(kSeparator) + ImageComponents(input);
            result.secondary = input.hasDescriptor
                ? JoinFacts({ ColorIdentityFor(input.descriptor.color),
                    TransferAndReferenceFor(input.descriptor), AlphaFor(input.descriptor) })
                : "Unknown image state";
            break;
        case LogicalValueType::Channel: {
            result.primary = "Channel";
            if (const std::string role = DeclaredChannelRole(input); !role.empty()) {
                result.primary += std::string(kSeparator) + role;
            }
            break;
        }
        case LogicalValueType::Mask:
            result.primary = "Channel" + std::string(kSeparator) + "Mask";
            break;
        case LogicalValueType::ScalarField:
        case LogicalValueType::Vector2Field:
        case LogicalValueType::Vector3Field:
        case LogicalValueType::Vector4Field:
            result.primary = "Field" + std::string(kSeparator) + FieldShape(type);
            break;
        case LogicalValueType::DataImage:
            result.primary = "Data image";
            break;
        case LogicalValueType::Curve1D:
            result.primary = "Curve";
            result.secondary = DataSummary(input.value);
            break;
        case LogicalValueType::Lut:
            result.primary = "Lookup table";
            result.secondary = DataSummary(input.value);
            break;
        case LogicalValueType::Histogram:
            result.primary = "Histogram";
            result.secondary = DataSummary(input.value);
            break;
        case LogicalValueType::Statistics:
            result.primary = "Statistics";
            result.secondary = DataSummary(input.value);
            break;
        case LogicalValueType::Metadata:
            result.primary = "Metadata";
            result.secondary = DataSummary(input.value);
            break;
        case LogicalValueType::ComplexSpectrum:
            result.primary = "Spectrum" + std::string(kSeparator) + "Complex";
            break;
        case LogicalValueType::FrequencyResponse:
            result.primary = "Frequency response";
            result.secondary = "Resolution-independent response";
            break;
        case LogicalValueType::SpectrumMagnitude:
            result.primary = "Spectrum" + std::string(kSeparator) + "Magnitude";
            break;
        case LogicalValueType::SpectrumPhase:
            result.primary = "Spectrum" + std::string(kSeparator) + "Phase";
            break;
        case LogicalValueType::Raw:
            result.primary = "RAW";
            result.secondary = "Sensor data";
            break;
        case LogicalValueType::Analysis:
            result.primary = "Analysis";
            result.secondary = "Specialized analysis";
            break;
        case LogicalValueType::SpecializedHandle:
            result.primary = "Specialized handle";
            result.secondary = "Specialized resource";
            break;
        case LogicalValueType::Failure:
            result.primary = "Failed value";
            result.secondary = "Source failed";
            break;
        case LogicalValueType::Invalid:
            result.primary = "Unknown value";
            result.secondary = "Unknown source state";
            break;
        default:
            if (IsUniformValueType(type)) {
                result.primary = "Value" + std::string(kSeparator) + ValueShape(type);
            } else {
                result.primary = SocketPresentation::LogicalTypeName(type);
            }
            break;
    }

    const bool fieldLike = type == LogicalValueType::Channel ||
        type == LogicalValueType::Mask ||
        type == LogicalValueType::ScalarField ||
        type == LogicalValueType::Vector2Field ||
        type == LogicalValueType::Vector3Field ||
        type == LogicalValueType::Vector4Field ||
        type == LogicalValueType::DataImage;
    if (fieldLike && result.secondary.empty()) {
        SemanticField<UnitDescriptor> units = input.sourceSocket.declaredUnits;
        SemanticField<NumericRange> range;
        SemanticField<SpatialDescriptor> spatial;
        if (input.hasDescriptor) {
            if (input.descriptor.units.state != KnowledgeState::NotApplicable) {
                units = input.descriptor.units;
            }
            range = input.descriptor.range;
            spatial = input.descriptor.spatial;
        }
        const std::string unitText = SocketPresentation::UnitName(units);
        const std::string rangeText = RangeFor(range);
        result.secondary = JoinFacts({ unitText, rangeText });
        if (result.secondary.empty()) {
            result.secondary = ExtentFor(spatial);
        }
        if (result.secondary.empty()) result.secondary = "Per-pixel field";
    }
    if (IsUniformValueType(type) && result.secondary.empty()) {
        if (input.value.has_value()) {
            const std::string payload = CompactPayload(*input.value);
            if (!payload.empty()) {
                result.secondary = JoinFacts({ payload, UnitsFor(input.value->units) });
            }
        }
        if (result.secondary.empty()) {
            const std::string units = input.value.has_value()
                ? UnitsFor(input.value->units)
                : input.hasDescriptor
                    ? SocketPresentation::UnitName(input.descriptor.units)
                    : SocketPresentation::UnitName(input.sourceSocket.declaredUnits);
            result.secondary = units.empty() ? "Uniform value" : units;
        }
    }
    if (result.secondary.empty()) {
        result.secondary = input.hasDescriptor
            ? ExtentFor(input.descriptor.spatial) : std::string{};
    }
    if (result.secondary.empty()) result.secondary = "Unknown state";

    const auto [attention, attentionText] = SourceAttention(input);
    result.attention = attention;
    if (attention != Attention::None) {
        result.secondary = (attention == Attention::Error ? "Error" : "Warning") +
            std::string(kSeparator) + attentionText;
    }
    result.accessibleText = result.primary + ". " + result.secondary + ".";
    result.detailFacts.push_back({ "Carried value", result.primary });
    result.detailFacts.push_back({ "Current state", result.secondary });
    if (input.hasDescriptor) {
        if (const std::string extent = ExtentFor(input.descriptor.spatial); !extent.empty()) {
            result.detailFacts.push_back({ "Extent", extent });
        }
        if (const std::string range = RangeFor(input.descriptor.range); !range.empty()) {
            result.detailFacts.push_back({ "Declared range", range });
        }
    }
    return result;
}

} // namespace EditorNodeGraph::WireReadout
