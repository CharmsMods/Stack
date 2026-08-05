#include "NodeMath/FirstClassValue.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {

int gChecks = 0;

void Require(bool condition, const std::string& message) {
    ++gChecks;
    if (!condition) {
        std::cerr << "Phase 3 check failed: " << message << '\n';
        std::exit(1);
    }
}

Stack::NodeMath::FirstClassValue Known(
    Stack::NodeMath::LogicalValueType type,
    Stack::NodeMath::ValueStorageClass storage,
    Stack::NodeMath::FirstClassPayload payload,
    Stack::NodeMath::UnitDescriptor units = {}) {
    Stack::NodeMath::FirstClassValue value;
    value.logicalType = type;
    value.storage = storage;
    value.availability = Stack::NodeMath::ValueAvailability::Known;
    value.units = std::move(units);
    value.payload = std::move(payload);
    return value;
}

void TestRoundTrips() {
    using namespace Stack::NodeMath;
    const FirstClassValue values[] = {
        Known(LogicalValueType::Boolean, ValueStorageClass::Uniform, true),
        Known(LogicalValueType::Integer, ValueStorageClass::Uniform, std::int64_t{ 7 }),
        MakeUniformScalar(1.25, { UnitKind::ExposureValue, {} }),
        Known(LogicalValueType::Vector2, ValueStorageClass::Uniform, std::array<double, 2>{ 0.2, 0.8 }),
        Known(LogicalValueType::Vector3, ValueStorageClass::Uniform, std::array<double, 3>{ 1.0, 2.0, 3.0 }),
        Known(LogicalValueType::Vector4, ValueStorageClass::Uniform, std::array<double, 4>{ 1.0, 2.0, 3.0, 4.0 }),
        Known(LogicalValueType::Matrix3, ValueStorageClass::Uniform, std::array<double, 9>{ 1,0,0,0,1,0,0,0,1 }),
        Known(LogicalValueType::Matrix4, ValueStorageClass::Uniform, std::array<double, 16>{ 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1 }),
        Known(LogicalValueType::Coordinate2, ValueStorageClass::Uniform, std::array<double, 2>{ 42.0, 24.0 }, { UnitKind::Pixels, {} }),
        Known(LogicalValueType::Curve1D, ValueStorageClass::StructuredResource, CurveValue{ { {0.0, 0.0}, {0.5, 0.3}, {1.0, 1.0} }, "monotone-cubic", "clamp" }),
        Known(LogicalValueType::Histogram, ValueStorageClass::StructuredResource, HistogramValue{ -1.0, 2.0, { 1.0, 4.0, 2.0 }, "image:42/full" }),
        Known(LogicalValueType::Statistics, ValueStorageClass::StructuredResource, StatisticsValue{ { {"mean", 0.4}, {"maximum", 1.6} }, "image:42/full" }),
        Known(LogicalValueType::Metadata, ValueStorageClass::StructuredResource, ResourceValue{ "icc-profile", "profile:display-p3", std::string(64, 'a') }),
        Known(LogicalValueType::SpecializedHandle, ValueStorageClass::SpecializedHandle, ResourceValue{ "raw-stage", "raw:source-7", std::string(64, 'b') })
    };

    for (const FirstClassValue& source : values) {
        Require(ValidateFirstClassValue(source).empty(), "known representative value validates");
        FirstClassValue parsed;
        std::string error;
        Require(DeserializeFirstClassValue(SerializeFirstClassValue(source), parsed, &error), "representative value deserializes: " + error);
        Require(parsed == source, "representative value round-trips exactly");
        Require(FirstClassValueFingerprint(parsed) == FirstClassValueFingerprint(source), "round-trip preserves fingerprint");
    }
}

void TestAvailabilityAndValidation() {
    using namespace Stack::NodeMath;
    FirstClassValue unknown = MakeUnknownValue(LogicalValueType::Scalar, ValueStorageClass::Uniform);
    Require(ValidateFirstClassValue(unknown).empty(), "unknown scalar is a valid explicit state");
    FirstClassValue missing = MakeMissingValue(LogicalValueType::Histogram, ValueStorageClass::StructuredResource, "analysis has not executed");
    Require(ValidateFirstClassValue(missing).empty(), "missing histogram is distinct and valid");
    FirstClassValue failure = MakeFailureValue(LogicalValueType::Statistics, ValueStorageClass::StructuredResource, "reduction failed");
    Require(ValidateFirstClassValue(failure).empty(), "failed statistics value is distinct and valid");
    Require(SerializeFirstClassValue(unknown).value("availability", "") == "unknown", "unknown serializes explicitly");
    Require(SerializeFirstClassValue(missing).value("availability", "") == "missing", "missing serializes explicitly");
    Require(SerializeFirstClassValue(failure).value("availability", "") == "failure", "failure serializes explicitly");

    CurveValue invalidCurve{ { {0.0, 0.0}, {0.0, 1.0} }, "linear", "clamp" };
    Require(!ValidateFirstClassValue(Known(LogicalValueType::Curve1D, ValueStorageClass::StructuredResource, invalidCurve)).empty(), "curve requires strictly ordered x coordinates");
    Require(!ValidateFirstClassValue(Known(LogicalValueType::ScalarField, ValueStorageClass::Uniform, ResourceValue{ "field", "x", {} })).empty(), "field cannot masquerade as a uniform value");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    Require(!ValidateFirstClassValue(MakeUniformScalar(nan)).empty(),
        "known uniform Scalar values must reject NaN");
    Require(!ValidateFirstClassValue(Known(
        LogicalValueType::Vector3,
        ValueStorageClass::Uniform,
        std::array<double, 3>{ 1.0, infinity, 3.0 })).empty(),
        "known uniform vector values must reject infinity");
    Require(!ValidateFirstClassValue(Known(
        LogicalValueType::Histogram,
        ValueStorageClass::StructuredResource,
        HistogramValue{ 0.0, 1.0, { 1.0, nan }, "invalid" })).empty(),
        "known histograms must reject non-finite bins");
    Require(!ValidateFirstClassValue(Known(
        LogicalValueType::Statistics,
        ValueStorageClass::StructuredResource,
        StatisticsValue{ { { "mean", infinity } }, "invalid" })).empty(),
        "known statistics must reject non-finite entries");
    const FirstClassValue nonFiniteVector = Known(
        LogicalValueType::Vector3,
        ValueStorageClass::Uniform,
        std::array<double, 3>{ 1.0, nan, 3.0 });
    Require(!CanExplicitlyBroadcast(
            MakeUniformScalar(infinity),
            LogicalValueType::Vector3,
            ValueStorageClass::Uniform),
        "explicit broadcast should reject a non-finite uniform source");
    Require(
        ExtractUniformComponent(nonFiniteVector, 0).availability ==
            ValueAvailability::Failure &&
        ReduceUniformVector(nonFiniteVector, VectorReduction::Mean).availability ==
            ValueAvailability::Failure,
        "component extraction and reduction should propagate invalid uniform values as typed failures");

    FirstClassValue mismatchedPayload = MakeUniformScalar(1.0);
    mismatchedPayload.logicalType = LogicalValueType::Curve1D;
    Require(!ValidateFirstClassValue(mismatchedPayload).empty(),
        "malformed type/payload pairs should fail validation without throwing");
}

void TestRules() {
    using namespace Stack::NodeMath;
    const FirstClassValue ev = MakeUniformScalar(2.0, { UnitKind::ExposureValue, {} });
    Require(AreUnitsCompatible(ev.units, { UnitKind::ExposureValue, {} }), "matching EV units are compatible");
    Require(!AreUnitsCompatible(ev.units, { UnitKind::Percent, {} }), "EV and percent units are incompatible");
    Require(CanExplicitlyBroadcast(ev, LogicalValueType::Vector3, ValueStorageClass::Uniform), "scalar-to-vector broadcast is explicit");
    Require(CanExplicitlyBroadcast(ev, LogicalValueType::ScalarField, ValueStorageClass::PerPixelField), "uniform-to-field broadcast is explicit");
    const FirstClassValue vector = BroadcastUniformScalar(ev, LogicalValueType::Vector3, ValueStorageClass::Uniform);
    Require(std::get<std::array<double, 3>>(vector.payload)[2] == 2.0, "broadcast fills every component");
    Require(std::get<double>(ExtractUniformComponent(vector, 1).payload) == 2.0, "component extraction returns a scalar");
    const FirstClassValue source = Known(LogicalValueType::Vector4, ValueStorageClass::Uniform, std::array<double, 4>{ -2.0, 2.0, 6.0, 10.0 });
    Require(std::get<double>(ReduceUniformVector(source, VectorReduction::Mean).payload) == 4.0, "uniform mean reduction is typed and exact");
    Require(ReduceUniformVector(MakeUnknownValue(LogicalValueType::Vector3, ValueStorageClass::Uniform), VectorReduction::Sum).availability == ValueAvailability::Failure, "unknown vector reduction becomes a typed failure");
}

} // namespace

int main() {
    TestRoundTrips();
    TestAvailabilityAndValidation();
    TestRules();
    std::cout << "Phase 3 first-class value checks passed: " << gChecks << '\n';
    return 0;
}
