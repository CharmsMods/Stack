#include "NodeMath/DescriptorSerialization.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <map>
#include <utility>

namespace Stack::NodeMath {
namespace {

template <typename Enum>
const char* EnumToken(Enum value, const std::map<Enum, const char*>& tokens) {
    const auto found = tokens.find(value);
    return found == tokens.end() ? "invalid" : found->second;
}

template <typename Enum>
bool ParseEnumToken(
    const nlohmann::json& value,
    const std::map<Enum, const char*>& tokens,
    Enum& output) {
    if (!value.is_string()) {
        return false;
    }
    const std::string token = value.get<std::string>();
    for (const auto& [candidate, text] : tokens) {
        if (token == text) {
            output = candidate;
            return true;
        }
    }
    return false;
}

const std::map<LogicalValueType, const char*> kLogicalTypes = {
    { LogicalValueType::Invalid, "invalid" },
    { LogicalValueType::Boolean, "boolean" },
    { LogicalValueType::Integer, "integer" },
    { LogicalValueType::Scalar, "scalar" },
    { LogicalValueType::Vector2, "vector2" },
    { LogicalValueType::Vector3, "vector3" },
    { LogicalValueType::Vector4, "vector4" },
    { LogicalValueType::Matrix3, "matrix3" },
    { LogicalValueType::Matrix4, "matrix4" },
    { LogicalValueType::Coordinate2, "coordinate2" },
    { LogicalValueType::Curve1D, "curve1d" },
    { LogicalValueType::Lut, "lut" },
    { LogicalValueType::Channel, "channel" },
    { LogicalValueType::ScalarField, "scalar-field" },
    { LogicalValueType::Vector2Field, "vector2-field" },
    { LogicalValueType::Vector3Field, "vector3-field" },
    { LogicalValueType::Vector4Field, "vector4-field" },
    { LogicalValueType::ColorImage, "color-image" },
    { LogicalValueType::Mask, "mask" },
    { LogicalValueType::DataImage, "data-image" },
    { LogicalValueType::ComplexSpectrum, "complex-spectrum" },
    { LogicalValueType::FrequencyResponse, "frequency-response" },
    { LogicalValueType::SpectrumMagnitude, "spectrum-magnitude" },
    { LogicalValueType::SpectrumPhase, "spectrum-phase" },
    { LogicalValueType::Histogram, "histogram" },
    { LogicalValueType::Statistics, "statistics" },
    { LogicalValueType::Metadata, "metadata" },
    { LogicalValueType::SpecializedHandle, "specialized-handle" },
    { LogicalValueType::Raw, "raw" },
    { LogicalValueType::Analysis, "analysis" },
    { LogicalValueType::Failure, "failure" }
};

const std::map<KnowledgeState, const char*> kKnowledgeStates = {
    { KnowledgeState::NotApplicable, "not-applicable" },
    { KnowledgeState::Unknown, "unknown" },
    { KnowledgeState::Known, "known" }
};

const std::map<ChannelLayout, const char*> kChannelLayouts = {
    { ChannelLayout::Gray, "gray" }, { ChannelLayout::RGB, "rgb" },
    { ChannelLayout::RGBA, "rgba" }, { ChannelLayout::XY, "xy" },
    { ChannelLayout::ComplexPair, "complex-pair" },
    { ChannelLayout::NamedData, "named-data" }
};

const std::map<ColorRelation, const char*> kColorRelations = {
    { ColorRelation::Standard, "standard" }, { ColorRelation::Derived, "derived" }
};

const std::map<TransferKind, const char*> kTransferKinds = {
    { TransferKind::Linear, "linear" }, { TransferKind::Srgb, "srgb" },
    { TransferKind::Gamma, "gamma" }, { TransferKind::Log, "log" },
    { TransferKind::Pq, "pq" }, { TransferKind::Hlg, "hlg" },
    { TransferKind::Custom, "custom" }
};

const std::map<ReferenceState, const char*> kReferenceStates = {
    { ReferenceState::Scene, "scene" }, { ReferenceState::Display, "display" },
    { ReferenceState::Output, "output" }, { ReferenceState::Data, "data" }
};

const std::map<AlphaMode, const char*> kAlphaModes = {
    { AlphaMode::Absent, "absent" }, { AlphaMode::Opaque, "opaque" },
    { AlphaMode::Straight, "straight" },
    { AlphaMode::Premultiplied, "premultiplied" }
};

const std::map<NonFinitePolicy, const char*> kNonFinitePolicies = {
    { NonFinitePolicy::Unknown, "unknown" },
    { NonFinitePolicy::Forbidden, "forbidden" },
    { NonFinitePolicy::Preserve, "preserve" }
};

const std::map<LogicalPrecision, const char*> kPrecisions = {
    { LogicalPrecision::UInt8, "uint8" }, { LogicalPrecision::UInt16, "uint16" },
    { LogicalPrecision::Float16, "float16" }, { LogicalPrecision::Float32, "float32" },
    { LogicalPrecision::Float64, "float64" }
};

const std::map<SpatialExtentKind, const char*> kSpatialKinds = {
    { SpatialExtentKind::Empty, "empty" }, { SpatialExtentKind::Finite, "finite" }
};

const std::map<RasterOrigin, const char*> kRasterOrigins = {
    { RasterOrigin::BottomLeft, "bottom-left" },
    { RasterOrigin::TopLeft, "top-left" }
};

const std::map<CoordinateConvention, const char*> kCoordinates = {
    { CoordinateConvention::PixelCenters, "pixel-centers" },
    { CoordinateConvention::PixelCorners, "pixel-corners" },
    { CoordinateConvention::Normalized, "normalized" }
};

const std::map<ReconstructionFilter, const char*> kFilters = {
    { ReconstructionFilter::Nearest, "nearest" },
    { ReconstructionFilter::Linear, "linear" },
    { ReconstructionFilter::Cubic, "cubic" },
    { ReconstructionFilter::Custom, "custom" }
};

const std::map<BorderPolicy, const char*> kBorders = {
    { BorderPolicy::Transparent, "transparent" }, { BorderPolicy::Clamp, "clamp" },
    { BorderPolicy::Repeat, "repeat" }, { BorderPolicy::Mirror, "mirror" },
    { BorderPolicy::Constant, "constant" }, { BorderPolicy::Custom, "custom" }
};

const std::map<UnitKind, const char*> kUnits = {
    { UnitKind::Unitless, "unitless" }, { UnitKind::ExposureValue, "ev" },
    { UnitKind::Pixels, "pixels" },
    { UnitKind::NormalizedCoordinate, "normalized-coordinate" },
    { UnitKind::Degrees, "degrees" }, { UnitKind::Percent, "percent" },
    { UnitKind::CodeValue, "code-value" }, { UnitKind::Luminance, "luminance" },
    { UnitKind::Custom, "custom" }
};

const std::map<ProvenanceKind, const char*> kProvenanceKinds = {
    { ProvenanceKind::Embedded, "embedded" }, { ProvenanceKind::Untagged, "untagged" },
    { ProvenanceKind::Assigned, "assigned" }, { ProvenanceKind::Converted, "converted" },
    { ProvenanceKind::Generated, "generated" }, { ProvenanceKind::Derived, "derived" },
    { ProvenanceKind::RawDeveloped, "raw-developed" },
    { ProvenanceKind::External, "external" }
};

template <typename T, typename Encode>
nlohmann::json EncodeField(const SemanticField<T>& field, Encode encode) {
    nlohmann::json result = { { "state", EnumToken(field.state, kKnowledgeStates) } };
    if (field.state == KnowledgeState::Known) {
        result["value"] = encode(field.value);
    }
    return result;
}

template <typename T, typename Decode>
bool DecodeField(
    const nlohmann::json& parent,
    const char* name,
    SemanticField<T>& output,
    Decode decode,
    std::vector<ContractIssue>& issues) {
    if (!parent.contains(name) || !parent[name].is_object()) {
        issues.push_back({ name, "descriptor field is missing or not an object" });
        return false;
    }
    const nlohmann::json& value = parent[name];
    KnowledgeState state = KnowledgeState::NotApplicable;
    if (!value.contains("state") || !ParseEnumToken(value["state"], kKnowledgeStates, state)) {
        issues.push_back({ name, "descriptor field has an invalid knowledge state" });
        return false;
    }
    if (state == KnowledgeState::NotApplicable) {
        output = SemanticField<T>::NotApplicable();
        return true;
    }
    if (state == KnowledgeState::Unknown) {
        output = SemanticField<T>::Unknown();
        return true;
    }
    if (!value.contains("value")) {
        issues.push_back({ name, "known descriptor field is missing its value" });
        return false;
    }
    T decoded{};
    if (!decode(value["value"], decoded)) {
        issues.push_back({ name, "known descriptor field value is malformed" });
        return false;
    }
    output = SemanticField<T>::Known(std::move(decoded));
    return true;
}

nlohmann::json EncodeRect(const Rect& rect) {
    return { { "x", rect.x }, { "y", rect.y }, { "width", rect.width }, { "height", rect.height } };
}

bool DecodeRect(const nlohmann::json& value, Rect& rect) {
    if (!value.is_object()) return false;
    try {
        rect.x = value.at("x").get<std::int64_t>();
        rect.y = value.at("y").get<std::int64_t>();
        rect.width = value.at("width").get<std::int64_t>();
        rect.height = value.at("height").get<std::int64_t>();
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

nlohmann::json SerializeValueDescriptor(const ValueDescriptor& descriptor) {
    nlohmann::json result;
    result["schemaVersion"] = descriptor.schemaVersion;
    result["logicalType"] = EnumToken(descriptor.logicalType, kLogicalTypes);
    result["channels"] = EncodeField(descriptor.channels, [](const ChannelDescriptor& value) {
        return nlohmann::json{
            { "layout", EnumToken(value.layout, kChannelLayouts) }, { "roles", value.roles }
        };
    });
    result["presentImageComponents"] = EncodeField(
        descriptor.presentImageComponents,
        [](const ImageComponentSet& value) {
            nlohmann::json components = nlohmann::json::array();
            for (const ImageComponent component : OrderedImageComponents(value)) {
                components.push_back(ImageComponentToken(component));
            }
            return components;
        });
    result["color"] = EncodeField(descriptor.color, [](const ColorIdentity& value) {
        return nlohmann::json{
            { "identity", value.identity }, { "profileHash", value.profileHash },
            { "relation", EnumToken(value.relation, kColorRelations) }
        };
    });
    result["transfer"] = EncodeField(descriptor.transfer, [](const TransferDescriptor& value) {
        return nlohmann::json{
            { "kind", EnumToken(value.kind, kTransferKinds) },
            { "parameter", value.parameter }, { "key", value.key }
        };
    });
    result["reference"] = EncodeField(descriptor.reference, [](ReferenceState value) {
        return nlohmann::json(EnumToken(value, kReferenceStates));
    });
    result["alpha"] = EncodeField(descriptor.alpha, [](AlphaMode value) {
        return nlohmann::json(EnumToken(value, kAlphaModes));
    });
    result["range"] = EncodeField(descriptor.range, [](const NumericRange& value) {
        return nlohmann::json{
            { "nominalMinimum", value.nominalMinimum },
            { "nominalMaximum", value.nominalMaximum },
            { "allowsBelowNominal", value.allowsBelowNominal },
            { "allowsAboveNominal", value.allowsAboveNominal },
            { "nonFinite", EnumToken(value.nonFinite, kNonFinitePolicies) }
        };
    });
    result["precision"] = EncodeField(descriptor.precision, [](LogicalPrecision value) {
        return nlohmann::json(EnumToken(value, kPrecisions));
    });
    result["spatial"] = EncodeField(descriptor.spatial, [](const SpatialDescriptor& value) {
        return nlohmann::json{
            { "kind", EnumToken(value.kind, kSpatialKinds) },
            { "fullWindow", EncodeRect(value.fullWindow) },
            { "dataWindow", EncodeRect(value.dataWindow) },
            { "rasterOrigin", EnumToken(value.rasterOrigin, kRasterOrigins) },
            { "pixelAspect", value.pixelAspect }
        };
    });
    result["sampling"] = EncodeField(descriptor.sampling, [](const SamplingDescriptor& value) {
        return nlohmann::json{
            { "coordinates", EnumToken(value.coordinates, kCoordinates) },
            { "filter", EnumToken(value.filter, kFilters) },
            { "border", EnumToken(value.border, kBorders) },
            { "customKey", value.customKey }
        };
    });
    result["units"] = EncodeField(descriptor.units, [](const UnitDescriptor& value) {
        return nlohmann::json{
            { "kind", EnumToken(value.kind, kUnits) }, { "customKey", value.customKey }
        };
    });
    result["provenance"] = EncodeField(descriptor.provenance, [](const ProvenanceDescriptor& value) {
        return nlohmann::json{
            { "kind", EnumToken(value.kind, kProvenanceKinds) },
            { "sourceIdentity", value.sourceIdentity },
            { "operationIdentity", value.operationIdentity }
        };
    });
    return result;
}

DescriptorParseResult ParseValueDescriptor(const nlohmann::json& value) {
    DescriptorParseResult result;
    if (!value.is_object()) {
        result.issues.push_back({ "descriptor", "descriptor JSON must be an object" });
        return result;
    }

    ValueDescriptor descriptor;
    std::uint32_t serializedSchemaVersion = 0;
    try {
        serializedSchemaVersion =
            value.at("schemaVersion").get<std::uint32_t>();
        descriptor.schemaVersion = serializedSchemaVersion;
        if (descriptor.schemaVersion == 1 ||
            descriptor.schemaVersion == 2) {
            descriptor.schemaVersion = kSemanticDescriptorSchemaVersion;
        }
    } catch (...) {
        result.issues.push_back({ "schemaVersion", "descriptor schema version is missing or invalid" });
    }
    if (!value.contains("logicalType") ||
        !ParseEnumToken(value["logicalType"], kLogicalTypes, descriptor.logicalType)) {
        result.issues.push_back({ "logicalType", "descriptor logical type is missing or invalid" });
    }

    DecodeField(value, "channels", descriptor.channels, [](const nlohmann::json& item, ChannelDescriptor& output) {
        if (!item.is_object() || !item.contains("layout") ||
            !ParseEnumToken(item["layout"], kChannelLayouts, output.layout) ||
            !item.contains("roles") || !item["roles"].is_array()) {
            return false;
        }
        try {
            output.roles = item["roles"].get<std::vector<std::string>>();
            return true;
        } catch (...) {
            return false;
        }
    }, result.issues);
    if (serializedSchemaVersion >= 3) {
        DecodeField(
            value,
            "presentImageComponents",
            descriptor.presentImageComponents,
            [](const nlohmann::json& item, ImageComponentSet& output) {
                if (!item.is_array()) {
                    return false;
                }
                for (const nlohmann::json& tokenValue : item) {
                    if (!tokenValue.is_string()) {
                        return false;
                    }
                    const std::optional<ImageComponent> component =
                        ParseImageComponentToken(tokenValue.get<std::string>());
                    if (!component || !AddImageComponent(output, *component)) {
                        return false;
                    }
                }
                return output.bits != 0;
            },
            result.issues);
    } else if (descriptor.logicalType != LogicalValueType::ColorImage) {
        descriptor.presentImageComponents =
            SemanticField<ImageComponentSet>::NotApplicable();
    } else if (descriptor.channels.state != KnowledgeState::Known) {
        descriptor.presentImageComponents =
            SemanticField<ImageComponentSet>::Unknown();
    } else {
        ImageComponentSet migrated;
        const ChannelDescriptor& channels = descriptor.channels.value;
        if (channels.layout == ChannelLayout::RGB) {
            migrated = MakeImageComponentSet({
                ImageComponent::Red,
                ImageComponent::Green,
                ImageComponent::Blue
            });
        } else if (channels.layout == ChannelLayout::RGBA) {
            migrated = MakeImageComponentSet({
                ImageComponent::Red,
                ImageComponent::Green,
                ImageComponent::Blue,
                ImageComponent::Alpha
            });
        } else {
            bool recognized = !channels.roles.empty();
            for (std::string role : channels.roles) {
                std::transform(
                    role.begin(),
                    role.end(),
                    role.begin(),
                    [](unsigned char character) {
                        return static_cast<char>(std::tolower(character));
                    });
                std::optional<ImageComponent> component;
                if (role == "r" || role == "red") {
                    component = ImageComponent::Red;
                } else if (role == "g" || role == "green") {
                    component = ImageComponent::Green;
                } else if (role == "b" || role == "blue") {
                    component = ImageComponent::Blue;
                } else if (role == "a" || role == "alpha") {
                    component = ImageComponent::Alpha;
                }
                if (!component || !AddImageComponent(migrated, *component)) {
                    recognized = false;
                    break;
                }
            }
            if (!recognized) {
                migrated.bits = 0;
            }
        }
        descriptor.presentImageComponents = migrated.bits != 0
            ? SemanticField<ImageComponentSet>::Known(migrated)
            : SemanticField<ImageComponentSet>::Unknown();
    }
    DecodeField(value, "color", descriptor.color, [](const nlohmann::json& item, ColorIdentity& output) {
        if (!item.is_object() || !item.contains("identity") || !item["identity"].is_string() ||
            !item.contains("profileHash") || !item["profileHash"].is_string() ||
            !item.contains("relation") || !ParseEnumToken(item["relation"], kColorRelations, output.relation)) return false;
        output.identity = item["identity"].get<std::string>();
        output.profileHash = item["profileHash"].get<std::string>();
        return true;
    }, result.issues);
    DecodeField(value, "transfer", descriptor.transfer, [](const nlohmann::json& item, TransferDescriptor& output) {
        if (!item.is_object() || !item.contains("kind") ||
            !ParseEnumToken(item["kind"], kTransferKinds, output.kind)) return false;
        try {
            output.parameter = item.at("parameter").get<double>();
            output.key = item.at("key").get<std::string>();
            return true;
        } catch (...) { return false; }
    }, result.issues);
    DecodeField(value, "reference", descriptor.reference, [](const nlohmann::json& item, ReferenceState& output) {
        return ParseEnumToken(item, kReferenceStates, output);
    }, result.issues);
    DecodeField(value, "alpha", descriptor.alpha, [](const nlohmann::json& item, AlphaMode& output) {
        return ParseEnumToken(item, kAlphaModes, output);
    }, result.issues);
    DecodeField(value, "range", descriptor.range, [](const nlohmann::json& item, NumericRange& output) {
        if (!item.is_object() || !item.contains("nonFinite") ||
            !ParseEnumToken(item["nonFinite"], kNonFinitePolicies, output.nonFinite)) return false;
        try {
            output.nominalMinimum = item.at("nominalMinimum").get<double>();
            output.nominalMaximum = item.at("nominalMaximum").get<double>();
            output.allowsBelowNominal = item.at("allowsBelowNominal").get<bool>();
            output.allowsAboveNominal = item.at("allowsAboveNominal").get<bool>();
            return true;
        } catch (...) { return false; }
    }, result.issues);
    DecodeField(value, "precision", descriptor.precision, [](const nlohmann::json& item, LogicalPrecision& output) {
        return ParseEnumToken(item, kPrecisions, output);
    }, result.issues);
    DecodeField(value, "spatial", descriptor.spatial, [](const nlohmann::json& item, SpatialDescriptor& output) {
        if (!item.is_object() || !item.contains("kind") ||
            !ParseEnumToken(item["kind"], kSpatialKinds, output.kind)) return false;
        try {
            output.pixelAspect = item.at("pixelAspect").get<double>();
        } catch (...) { return false; }
        if (item.contains("rasterOrigin") &&
            !ParseEnumToken(item["rasterOrigin"], kRasterOrigins, output.rasterOrigin)) {
            return false;
        }
        return item.contains("fullWindow") && DecodeRect(item["fullWindow"], output.fullWindow) &&
            item.contains("dataWindow") && DecodeRect(item["dataWindow"], output.dataWindow);
    }, result.issues);
    DecodeField(value, "sampling", descriptor.sampling, [](const nlohmann::json& item, SamplingDescriptor& output) {
        if (!item.is_object() || !item.contains("coordinates") || !item.contains("filter") ||
            !item.contains("border") || !item.contains("customKey") || !item["customKey"].is_string()) return false;
        output.customKey = item["customKey"].get<std::string>();
        return ParseEnumToken(item["coordinates"], kCoordinates, output.coordinates) &&
            ParseEnumToken(item["filter"], kFilters, output.filter) &&
            ParseEnumToken(item["border"], kBorders, output.border);
    }, result.issues);
    DecodeField(value, "units", descriptor.units, [](const nlohmann::json& item, UnitDescriptor& output) {
        if (!item.is_object() || !item.contains("kind") || !item.contains("customKey") ||
            !item["customKey"].is_string()) return false;
        output.customKey = item["customKey"].get<std::string>();
        return ParseEnumToken(item["kind"], kUnits, output.kind);
    }, result.issues);
    DecodeField(value, "provenance", descriptor.provenance, [](const nlohmann::json& item, ProvenanceDescriptor& output) {
        if (!item.is_object() || !item.contains("kind") || !item.contains("sourceIdentity") ||
            !item.contains("operationIdentity") || !item["sourceIdentity"].is_string() ||
            !item["operationIdentity"].is_string() ||
            !ParseEnumToken(item["kind"], kProvenanceKinds, output.kind)) return false;
        output.sourceIdentity = item["sourceIdentity"].get<std::string>();
        output.operationIdentity = item["operationIdentity"].get<std::string>();
        return true;
    }, result.issues);

    const std::vector<ContractIssue> validation = ValidateDescriptor(descriptor);
    result.issues.insert(result.issues.end(), validation.begin(), validation.end());
    if (result.issues.empty()) {
        result.descriptor = std::move(descriptor);
    }
    return result;
}

std::string CanonicalDescriptorContent(const ValueDescriptor& descriptor) {
    return std::string("stack.semantic-descriptor.canonical.v3\n") +
        SerializeValueDescriptor(descriptor).dump();
}

std::string DescriptorContentIdentity(const ValueDescriptor& descriptor) {
    return Sha256ContentIdentity(CanonicalDescriptorContent(descriptor));
}

} // namespace Stack::NodeMath
