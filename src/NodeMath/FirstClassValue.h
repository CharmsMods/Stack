#pragma once

#include "NodeMath/ContractTypes.h"
#include "ThirdParty/json.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace Stack::NodeMath {

inline constexpr std::uint32_t kFirstClassValueSchemaVersion = 1;

enum class ValueStorageClass { Uniform, PerPixelField, StructuredResource, SpecializedHandle };
enum class ValueAvailability { Known, Unknown, Missing, Failure };

struct CurvePoint { double x = 0.0; double y = 0.0; };
struct CurveValue {
    std::vector<CurvePoint> points;
    std::string interpolation = "linear";
    std::string extrapolation = "clamp";
};
struct HistogramValue {
    double domainMinimum = 0.0;
    double domainMaximum = 1.0;
    std::vector<double> bins;
    std::string populationIdentity;
};
struct StatisticsValue {
    std::vector<std::pair<std::string, double>> entries;
    std::string populationIdentity;
};
struct ResourceValue {
    std::string resourceType;
    std::string identity;
    std::string contentHash;
};

using FirstClassPayload = std::variant<
    std::monostate, bool, std::int64_t, double,
    std::array<double, 2>, std::array<double, 3>, std::array<double, 4>,
    std::array<double, 9>, std::array<double, 16>, CurveValue,
    HistogramValue, StatisticsValue, ResourceValue>;

struct FirstClassValue {
    std::uint32_t schemaVersion = kFirstClassValueSchemaVersion;
    LogicalValueType logicalType = LogicalValueType::Invalid;
    ValueStorageClass storage = ValueStorageClass::Uniform;
    ValueAvailability availability = ValueAvailability::Unknown;
    UnitDescriptor units;
    FirstClassPayload payload;
    std::string message;
};

enum class VectorReduction { Sum, Mean, Minimum, Maximum };

bool operator==(const CurvePoint& left, const CurvePoint& right);
bool operator==(const CurveValue& left, const CurveValue& right);
bool operator==(const HistogramValue& left, const HistogramValue& right);
bool operator==(const StatisticsValue& left, const StatisticsValue& right);
bool operator==(const ResourceValue& left, const ResourceValue& right);
bool operator==(const FirstClassValue& left, const FirstClassValue& right);

FirstClassValue MakeUnknownValue(LogicalValueType type, ValueStorageClass storage, UnitDescriptor units = {});
FirstClassValue MakeMissingValue(LogicalValueType type, ValueStorageClass storage, std::string message);
FirstClassValue MakeFailureValue(LogicalValueType type, ValueStorageClass storage, std::string message);
FirstClassValue MakeUniformScalar(double value, UnitDescriptor units = {});

std::vector<ContractIssue> ValidateFirstClassValue(const FirstClassValue& value);
bool AreUnitsCompatible(const UnitDescriptor& source, const UnitDescriptor& destination);
bool CanExplicitlyBroadcast(const FirstClassValue& source, LogicalValueType destinationType, ValueStorageClass destinationStorage);
FirstClassValue BroadcastUniformScalar(const FirstClassValue& source, LogicalValueType destinationType, ValueStorageClass destinationStorage);
FirstClassValue ExtractUniformComponent(const FirstClassValue& source, std::size_t component);
FirstClassValue ReduceUniformVector(const FirstClassValue& source, VectorReduction reduction);

nlohmann::json SerializeFirstClassValue(const FirstClassValue& value);
bool DeserializeFirstClassValue(const nlohmann::json& json, FirstClassValue& value, std::string* error = nullptr);
std::string FirstClassValueFingerprint(const FirstClassValue& value);

} // namespace Stack::NodeMath
