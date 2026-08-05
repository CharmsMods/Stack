#include "NodeMath/FirstClassValue.h"

#include "NodeMath/DescriptorSerialization.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace Stack::NodeMath {
namespace {

const char* StorageName(ValueStorageClass storage) {
    switch (storage) {
    case ValueStorageClass::Uniform: return "uniform";
    case ValueStorageClass::PerPixelField: return "per-pixel-field";
    case ValueStorageClass::StructuredResource: return "structured-resource";
    case ValueStorageClass::SpecializedHandle: return "specialized-handle";
    }
    return "uniform";
}

bool ParseStorage(const std::string& text, ValueStorageClass& storage) {
    if (text == "uniform") storage = ValueStorageClass::Uniform;
    else if (text == "per-pixel-field") storage = ValueStorageClass::PerPixelField;
    else if (text == "structured-resource") storage = ValueStorageClass::StructuredResource;
    else if (text == "specialized-handle") storage = ValueStorageClass::SpecializedHandle;
    else return false;
    return true;
}

const char* AvailabilityName(ValueAvailability availability) {
    switch (availability) {
    case ValueAvailability::Known: return "known";
    case ValueAvailability::Unknown: return "unknown";
    case ValueAvailability::Missing: return "missing";
    case ValueAvailability::Failure: return "failure";
    }
    return "unknown";
}

bool ParseAvailability(const std::string& text, ValueAvailability& availability) {
    if (text == "known") availability = ValueAvailability::Known;
    else if (text == "unknown") availability = ValueAvailability::Unknown;
    else if (text == "missing") availability = ValueAvailability::Missing;
    else if (text == "failure") availability = ValueAvailability::Failure;
    else return false;
    return true;
}

const char* UnitName(UnitKind kind) {
    switch (kind) {
    case UnitKind::Unitless: return "unitless";
    case UnitKind::ExposureValue: return "ev";
    case UnitKind::Pixels: return "pixels";
    case UnitKind::NormalizedCoordinate: return "normalized-coordinate";
    case UnitKind::Degrees: return "degrees";
    case UnitKind::Percent: return "percent";
    case UnitKind::CodeValue: return "code-value";
    case UnitKind::Luminance: return "luminance";
    case UnitKind::Custom: return "custom";
    }
    return "unitless";
}

bool ParseUnit(const std::string& text, UnitKind& kind) {
    if (text == "unitless") kind = UnitKind::Unitless;
    else if (text == "ev") kind = UnitKind::ExposureValue;
    else if (text == "pixels") kind = UnitKind::Pixels;
    else if (text == "normalized-coordinate") kind = UnitKind::NormalizedCoordinate;
    else if (text == "degrees") kind = UnitKind::Degrees;
    else if (text == "percent") kind = UnitKind::Percent;
    else if (text == "code-value") kind = UnitKind::CodeValue;
    else if (text == "luminance") kind = UnitKind::Luminance;
    else if (text == "custom") kind = UnitKind::Custom;
    else return false;
    return true;
}

bool ParseLogicalType(const std::string& text, LogicalValueType& type) {
    static const std::pair<const char*, LogicalValueType> values[] = {
        { "boolean", LogicalValueType::Boolean }, { "integer", LogicalValueType::Integer },
        { "scalar", LogicalValueType::Scalar }, { "vector2", LogicalValueType::Vector2 },
        { "vector3", LogicalValueType::Vector3 }, { "vector4", LogicalValueType::Vector4 },
        { "matrix3", LogicalValueType::Matrix3 }, { "matrix4", LogicalValueType::Matrix4 },
        { "coordinate2", LogicalValueType::Coordinate2 }, { "curve1d", LogicalValueType::Curve1D },
        { "lut", LogicalValueType::Lut }, { "channel", LogicalValueType::Channel },
        { "scalar-field", LogicalValueType::ScalarField },
        { "vector2-field", LogicalValueType::Vector2Field }, { "vector3-field", LogicalValueType::Vector3Field },
        { "vector4-field", LogicalValueType::Vector4Field }, { "color-image", LogicalValueType::ColorImage },
        { "complex-spectrum", LogicalValueType::ComplexSpectrum },
        { "frequency-response", LogicalValueType::FrequencyResponse },
        { "spectrum-magnitude", LogicalValueType::SpectrumMagnitude },
        { "spectrum-phase", LogicalValueType::SpectrumPhase },
        { "histogram", LogicalValueType::Histogram }, { "statistics", LogicalValueType::Statistics },
        { "metadata", LogicalValueType::Metadata }, { "specialized-handle", LogicalValueType::SpecializedHandle }
    };
    for (const auto& value : values) {
        if (text == value.first) { type = value.second; return true; }
    }
    return false;
}

template <std::size_t N>
nlohmann::json ArrayJson(const std::array<double, N>& value) {
    nlohmann::json result = nlohmann::json::array();
    for (double component : value) result.push_back(component);
    return result;
}

template <std::size_t N>
bool ParseArray(const nlohmann::json& json, std::array<double, N>& value) {
    if (!json.is_array() || json.size() != N) return false;
    for (std::size_t i = 0; i < N; ++i) {
        if (!json[i].is_number()) return false;
        value[i] = json[i].get<double>();
    }
    return true;
}

template <std::size_t N>
bool AllFinite(const std::array<double, N>& values) {
    return std::all_of(
        values.begin(),
        values.end(),
        [](double value) { return std::isfinite(value); });
}

bool UniformNumericPayloadIsFinite(const FirstClassPayload& payload) {
    if (const double* scalar = std::get_if<double>(&payload)) {
        return std::isfinite(*scalar);
    }
    if (const auto* values =
            std::get_if<std::array<double, 2>>(&payload)) {
        return AllFinite(*values);
    }
    if (const auto* values =
            std::get_if<std::array<double, 3>>(&payload)) {
        return AllFinite(*values);
    }
    if (const auto* values =
            std::get_if<std::array<double, 4>>(&payload)) {
        return AllFinite(*values);
    }
    if (const auto* values =
            std::get_if<std::array<double, 9>>(&payload)) {
        return AllFinite(*values);
    }
    if (const auto* values =
            std::get_if<std::array<double, 16>>(&payload)) {
        return AllFinite(*values);
    }
    return true;
}

bool PayloadMatches(const FirstClassValue& value) {
    if (value.availability != ValueAvailability::Known) return std::holds_alternative<std::monostate>(value.payload);
    switch (value.logicalType) {
    case LogicalValueType::Boolean: return std::holds_alternative<bool>(value.payload);
    case LogicalValueType::Integer: return std::holds_alternative<std::int64_t>(value.payload);
    case LogicalValueType::Scalar: return std::holds_alternative<double>(value.payload);
    case LogicalValueType::Vector2:
    case LogicalValueType::Coordinate2: return std::holds_alternative<std::array<double, 2>>(value.payload);
    case LogicalValueType::Vector3: return std::holds_alternative<std::array<double, 3>>(value.payload);
    case LogicalValueType::Vector4: return std::holds_alternative<std::array<double, 4>>(value.payload);
    case LogicalValueType::Matrix3: return std::holds_alternative<std::array<double, 9>>(value.payload);
    case LogicalValueType::Matrix4: return std::holds_alternative<std::array<double, 16>>(value.payload);
    case LogicalValueType::Curve1D: return std::holds_alternative<CurveValue>(value.payload);
    case LogicalValueType::Histogram: return std::holds_alternative<HistogramValue>(value.payload);
    case LogicalValueType::Statistics: return std::holds_alternative<StatisticsValue>(value.payload);
    case LogicalValueType::Lut:
    case LogicalValueType::Metadata:
    case LogicalValueType::SpecializedHandle:
    case LogicalValueType::Channel:
    case LogicalValueType::ScalarField:
    case LogicalValueType::Vector2Field:
    case LogicalValueType::Vector3Field:
    case LogicalValueType::Vector4Field:
    case LogicalValueType::ColorImage:
    case LogicalValueType::ComplexSpectrum:
    case LogicalValueType::FrequencyResponse:
    case LogicalValueType::SpectrumMagnitude:
    case LogicalValueType::SpectrumPhase:
        return std::holds_alternative<ResourceValue>(value.payload);
    default: return false;
    }
}

std::vector<double> VectorComponents(const FirstClassValue& source) {
    if (const auto* v = std::get_if<std::array<double, 2>>(&source.payload)) return { (*v)[0], (*v)[1] };
    if (const auto* v = std::get_if<std::array<double, 3>>(&source.payload)) return { (*v)[0], (*v)[1], (*v)[2] };
    if (const auto* v = std::get_if<std::array<double, 4>>(&source.payload)) return { (*v)[0], (*v)[1], (*v)[2], (*v)[3] };
    return {};
}

bool IsFieldType(LogicalValueType type) {
    return type == LogicalValueType::Channel ||
        type == LogicalValueType::ScalarField || type == LogicalValueType::Vector2Field ||
        type == LogicalValueType::Vector3Field || type == LogicalValueType::Vector4Field ||
        type == LogicalValueType::ColorImage || type == LogicalValueType::ComplexSpectrum ||
        type == LogicalValueType::SpectrumMagnitude || type == LogicalValueType::SpectrumPhase;
}

} // namespace

bool operator==(const CurvePoint& left, const CurvePoint& right) { return left.x == right.x && left.y == right.y; }
bool operator==(const CurveValue& left, const CurveValue& right) { return left.points == right.points && left.interpolation == right.interpolation && left.extrapolation == right.extrapolation; }
bool operator==(const HistogramValue& left, const HistogramValue& right) { return left.domainMinimum == right.domainMinimum && left.domainMaximum == right.domainMaximum && left.bins == right.bins && left.populationIdentity == right.populationIdentity; }
bool operator==(const StatisticsValue& left, const StatisticsValue& right) { return left.entries == right.entries && left.populationIdentity == right.populationIdentity; }
bool operator==(const ResourceValue& left, const ResourceValue& right) { return left.resourceType == right.resourceType && left.identity == right.identity && left.contentHash == right.contentHash; }
bool operator==(const FirstClassValue& left, const FirstClassValue& right) { return left.schemaVersion == right.schemaVersion && left.logicalType == right.logicalType && left.storage == right.storage && left.availability == right.availability && left.units == right.units && left.payload == right.payload && left.message == right.message; }

FirstClassValue MakeUnknownValue(LogicalValueType type, ValueStorageClass storage, UnitDescriptor units) {
    FirstClassValue value;
    value.logicalType = type;
    value.storage = storage;
    value.units = std::move(units);
    return value;
}

FirstClassValue MakeMissingValue(LogicalValueType type, ValueStorageClass storage, std::string message) {
    FirstClassValue value = MakeUnknownValue(type, storage);
    value.availability = ValueAvailability::Missing;
    value.message = std::move(message);
    return value;
}

FirstClassValue MakeFailureValue(LogicalValueType type, ValueStorageClass storage, std::string message) {
    FirstClassValue value = MakeUnknownValue(type, storage);
    value.availability = ValueAvailability::Failure;
    value.message = std::move(message);
    return value;
}

FirstClassValue MakeUniformScalar(double number, UnitDescriptor units) {
    FirstClassValue value = MakeUnknownValue(LogicalValueType::Scalar, ValueStorageClass::Uniform, std::move(units));
    value.availability = ValueAvailability::Known;
    value.payload = number;
    return value;
}

std::vector<ContractIssue> ValidateFirstClassValue(const FirstClassValue& value) {
    std::vector<ContractIssue> issues;
    if (value.schemaVersion != kFirstClassValueSchemaVersion) issues.push_back({ "schemaVersion", "unsupported first-class value schema" });
    if (value.logicalType == LogicalValueType::Invalid || value.logicalType == LogicalValueType::Failure) issues.push_back({ "logicalType", "a concrete logical type is required" });
    const bool payloadMatches = PayloadMatches(value);
    if (!payloadMatches) issues.push_back({ "payload", "payload does not match logical type or availability" });
    if ((value.availability == ValueAvailability::Missing || value.availability == ValueAvailability::Failure) && value.message.empty()) issues.push_back({ "message", "missing and failed values require a message" });
    if (value.storage == ValueStorageClass::Uniform && IsFieldType(value.logicalType)) issues.push_back({ "storage", "per-pixel field types cannot use uniform storage" });
    if (value.availability == ValueAvailability::Known &&
        payloadMatches &&
        value.storage == ValueStorageClass::Uniform &&
        !UniformNumericPayloadIsFinite(value.payload)) {
        issues.push_back({ "payload", "uniform numeric values must be finite" });
    }
    if (value.logicalType == LogicalValueType::Curve1D &&
        value.availability == ValueAvailability::Known) {
        if (const CurveValue* curve = std::get_if<CurveValue>(&value.payload)) {
            if (curve->points.size() < 2) issues.push_back({ "payload.points", "a known curve requires at least two points" });
            for (std::size_t i = 0; i < curve->points.size(); ++i) {
                if (!std::isfinite(curve->points[i].x) || !std::isfinite(curve->points[i].y)) issues.push_back({ "payload.points", "curve points must be finite" });
                if (i > 0 && curve->points[i].x <= curve->points[i - 1].x) issues.push_back({ "payload.points", "curve x values must be strictly increasing" });
            }
        }
    }
    if (value.logicalType == LogicalValueType::Histogram &&
        value.availability == ValueAvailability::Known) {
        if (const HistogramValue* histogram =
                std::get_if<HistogramValue>(&value.payload)) {
            const bool finite =
                std::isfinite(histogram->domainMinimum) &&
                std::isfinite(histogram->domainMaximum) &&
                std::all_of(
                    histogram->bins.begin(),
                    histogram->bins.end(),
                    [](double bin) { return std::isfinite(bin); });
            if (!finite) {
                issues.push_back({ "payload", "histogram domain and bins must be finite" });
            }
            if (!(histogram->domainMaximum > histogram->domainMinimum) ||
                histogram->bins.empty()) {
                issues.push_back({ "payload", "a histogram requires an ordered domain and bins" });
            }
        }
    }
    if (value.logicalType == LogicalValueType::Statistics &&
        value.availability == ValueAvailability::Known) {
        if (const StatisticsValue* statistics =
                std::get_if<StatisticsValue>(&value.payload);
            statistics &&
            !std::all_of(
                statistics->entries.begin(),
                statistics->entries.end(),
                [](const auto& entry) {
                    return std::isfinite(entry.second);
                })) {
            issues.push_back({ "payload", "statistics values must be finite" });
        }
    }
    return issues;
}

bool AreUnitsCompatible(const UnitDescriptor& source, const UnitDescriptor& destination) {
    return source.kind == destination.kind && (source.kind != UnitKind::Custom || source.customKey == destination.customKey);
}

bool CanExplicitlyBroadcast(const FirstClassValue& source, LogicalValueType destinationType, ValueStorageClass destinationStorage) {
    if (source.availability != ValueAvailability::Known ||
        source.storage != ValueStorageClass::Uniform ||
        !ValidateFirstClassValue(source).empty()) return false;
    if (source.logicalType == destinationType && source.storage == destinationStorage) return true;
    if (source.logicalType != LogicalValueType::Scalar) return false;
    if (destinationStorage == ValueStorageClass::Uniform) return destinationType == LogicalValueType::Vector2 || destinationType == LogicalValueType::Vector3 || destinationType == LogicalValueType::Vector4;
    return destinationStorage == ValueStorageClass::PerPixelField && IsFieldType(destinationType);
}

FirstClassValue BroadcastUniformScalar(const FirstClassValue& source, LogicalValueType destinationType, ValueStorageClass destinationStorage) {
    if (!CanExplicitlyBroadcast(source, destinationType, destinationStorage)) return MakeFailureValue(destinationType, destinationStorage, "broadcast is not declared for these types");
    const double scalar = std::get<double>(source.payload);
    FirstClassValue result = source;
    result.logicalType = destinationType;
    result.storage = destinationStorage;
    if (destinationType == LogicalValueType::Vector2) result.payload = std::array<double, 2>{ scalar, scalar };
    else if (destinationType == LogicalValueType::Vector3) result.payload = std::array<double, 3>{ scalar, scalar, scalar };
    else if (destinationType == LogicalValueType::Vector4) result.payload = std::array<double, 4>{ scalar, scalar, scalar, scalar };
    else result.payload = ResourceValue{ "uniform-broadcast", FirstClassValueFingerprint(source), {} };
    return result;
}

FirstClassValue ExtractUniformComponent(const FirstClassValue& source, std::size_t component) {
    const std::vector<double> values = VectorComponents(source);
    if (source.storage != ValueStorageClass::Uniform ||
        source.availability != ValueAvailability::Known ||
        !ValidateFirstClassValue(source).empty() ||
        component >= values.size()) return MakeFailureValue(LogicalValueType::Scalar, ValueStorageClass::Uniform, "component extraction requires a finite in-range uniform vector component");
    return MakeUniformScalar(values[component], source.units);
}

FirstClassValue ReduceUniformVector(const FirstClassValue& source, VectorReduction reduction) {
    const std::vector<double> values = VectorComponents(source);
    if (source.storage != ValueStorageClass::Uniform ||
        source.availability != ValueAvailability::Known ||
        !ValidateFirstClassValue(source).empty() ||
        values.empty()) return MakeFailureValue(LogicalValueType::Scalar, ValueStorageClass::Uniform, "reduction requires a known finite uniform vector");
    double result = 0.0;
    if (reduction == VectorReduction::Minimum) result = *std::min_element(values.begin(), values.end());
    else if (reduction == VectorReduction::Maximum) result = *std::max_element(values.begin(), values.end());
    else {
        for (double value : values) result += value;
        if (reduction == VectorReduction::Mean) result /= static_cast<double>(values.size());
    }
    return MakeUniformScalar(result, source.units);
}

nlohmann::json SerializeFirstClassValue(const FirstClassValue& value) {
    nlohmann::json descriptorJson = SerializeValueDescriptor(MakeUnknownDescriptor(value.logicalType));
    nlohmann::json json = {
        { "schemaVersion", value.schemaVersion },
        { "logicalType", descriptorJson.value("logicalType", "invalid") },
        { "storage", StorageName(value.storage) },
        { "availability", AvailabilityName(value.availability) },
        { "units", { { "kind", UnitName(value.units.kind) }, { "customKey", value.units.customKey } } },
        { "message", value.message }
    };
    if (value.availability != ValueAvailability::Known) return json;
    std::visit([&](const auto& payload) {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, std::int64_t> || std::is_same_v<T, double>) json["payload"] = payload;
        else if constexpr (std::is_same_v<T, std::array<double, 2>> || std::is_same_v<T, std::array<double, 3>> || std::is_same_v<T, std::array<double, 4>> || std::is_same_v<T, std::array<double, 9>> || std::is_same_v<T, std::array<double, 16>>) json["payload"] = ArrayJson(payload);
        else if constexpr (std::is_same_v<T, CurveValue>) { nlohmann::json points = nlohmann::json::array(); for (const CurvePoint& point : payload.points) points.push_back({ point.x, point.y }); json["payload"] = { { "points", points }, { "interpolation", payload.interpolation }, { "extrapolation", payload.extrapolation } }; }
        else if constexpr (std::is_same_v<T, HistogramValue>) json["payload"] = { { "domainMinimum", payload.domainMinimum }, { "domainMaximum", payload.domainMaximum }, { "bins", payload.bins }, { "populationIdentity", payload.populationIdentity } };
        else if constexpr (std::is_same_v<T, StatisticsValue>) { nlohmann::json entries = nlohmann::json::array(); for (const auto& entry : payload.entries) entries.push_back({ { "name", entry.first }, { "value", entry.second } }); json["payload"] = { { "entries", entries }, { "populationIdentity", payload.populationIdentity } }; }
        else if constexpr (std::is_same_v<T, ResourceValue>) json["payload"] = { { "resourceType", payload.resourceType }, { "identity", payload.identity }, { "contentHash", payload.contentHash } };
    }, value.payload);
    return json;
}

bool DeserializeFirstClassValue(const nlohmann::json& json, FirstClassValue& value, std::string* error) {
    auto fail = [&](const std::string& message) { if (error) *error = message; return false; };
    if (!json.is_object()) return fail("value must be an object");
    FirstClassValue parsed;
    parsed.schemaVersion = json.value("schemaVersion", 0u);
    if (!ParseLogicalType(json.value("logicalType", std::string("invalid")), parsed.logicalType)) return fail("invalid logical type");
    if (!ParseStorage(json.value("storage", std::string()), parsed.storage)) return fail("invalid storage class");
    if (!ParseAvailability(json.value("availability", std::string()), parsed.availability)) return fail("invalid availability");
    const nlohmann::json units = json.value("units", nlohmann::json::object());
    if (!ParseUnit(units.value("kind", std::string("unitless")), parsed.units.kind)) return fail("invalid unit kind");
    parsed.units.customKey = units.value("customKey", std::string());
    parsed.message = json.value("message", std::string());
    if (parsed.availability == ValueAvailability::Known) {
        const nlohmann::json payload = json.value("payload", nlohmann::json());
        switch (parsed.logicalType) {
        case LogicalValueType::Boolean: if (!payload.is_boolean()) return fail("boolean payload required"); parsed.payload = payload.get<bool>(); break;
        case LogicalValueType::Integer: if (!payload.is_number_integer()) return fail("integer payload required"); parsed.payload = payload.get<std::int64_t>(); break;
        case LogicalValueType::Scalar: if (!payload.is_number()) return fail("scalar payload required"); parsed.payload = payload.get<double>(); break;
        case LogicalValueType::Vector2:
        case LogicalValueType::Coordinate2: { std::array<double, 2> data{}; if (!ParseArray(payload, data)) return fail("two-component payload required"); parsed.payload = data; break; }
        case LogicalValueType::Vector3: { std::array<double, 3> data{}; if (!ParseArray(payload, data)) return fail("three-component payload required"); parsed.payload = data; break; }
        case LogicalValueType::Vector4: { std::array<double, 4> data{}; if (!ParseArray(payload, data)) return fail("four-component payload required"); parsed.payload = data; break; }
        case LogicalValueType::Matrix3: { std::array<double, 9> data{}; if (!ParseArray(payload, data)) return fail("3x3 matrix payload required"); parsed.payload = data; break; }
        case LogicalValueType::Matrix4: { std::array<double, 16> data{}; if (!ParseArray(payload, data)) return fail("4x4 matrix payload required"); parsed.payload = data; break; }
        case LogicalValueType::Curve1D: { CurveValue curve; curve.interpolation = payload.value("interpolation", std::string("linear")); curve.extrapolation = payload.value("extrapolation", std::string("clamp")); for (const nlohmann::json& point : payload.value("points", nlohmann::json::array())) { if (!point.is_array() || point.size() != 2 || !point[0].is_number() || !point[1].is_number()) return fail("invalid curve point"); curve.points.push_back({ point[0].get<double>(), point[1].get<double>() }); } parsed.payload = std::move(curve); break; }
        case LogicalValueType::Histogram: { HistogramValue histogram; histogram.domainMinimum = payload.value("domainMinimum", 0.0); histogram.domainMaximum = payload.value("domainMaximum", 1.0); histogram.bins = payload.value("bins", std::vector<double>{}); histogram.populationIdentity = payload.value("populationIdentity", std::string()); parsed.payload = std::move(histogram); break; }
        case LogicalValueType::Statistics: { StatisticsValue statistics; statistics.populationIdentity = payload.value("populationIdentity", std::string()); for (const nlohmann::json& entry : payload.value("entries", nlohmann::json::array())) statistics.entries.emplace_back(entry.value("name", std::string()), entry.value("value", 0.0)); parsed.payload = std::move(statistics); break; }
        case LogicalValueType::Lut:
        case LogicalValueType::Metadata:
        case LogicalValueType::SpecializedHandle:
        case LogicalValueType::Channel:
        case LogicalValueType::ScalarField:
        case LogicalValueType::Vector2Field:
        case LogicalValueType::Vector3Field:
        case LogicalValueType::Vector4Field:
        case LogicalValueType::ColorImage:
        case LogicalValueType::ComplexSpectrum:
        case LogicalValueType::FrequencyResponse:
        case LogicalValueType::SpectrumMagnitude:
        case LogicalValueType::SpectrumPhase: { ResourceValue resource; resource.resourceType = payload.value("resourceType", std::string()); resource.identity = payload.value("identity", std::string()); resource.contentHash = payload.value("contentHash", std::string()); parsed.payload = std::move(resource); break; }
        default: return fail("known payload is unsupported for this logical type");
        }
    }
    const std::vector<ContractIssue> issues = ValidateFirstClassValue(parsed);
    if (!issues.empty()) return fail(issues.front().field + ": " + issues.front().message);
    value = std::move(parsed);
    return true;
}

std::string FirstClassValueFingerprint(const FirstClassValue& value) {
    return Sha256ContentIdentity(SerializeFirstClassValue(value).dump());
}

} // namespace Stack::NodeMath
