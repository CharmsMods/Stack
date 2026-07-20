#pragma once

#include "Raw/RawTechnicalEvidence.h"
#include "ThirdParty/json.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace Stack::RenderedFeatures {

inline constexpr int kRenderedFeatureSchemaVersion = 1;
inline constexpr const char* kRenderedFeatureVersion = "rendered-features-v1";

enum class FeatureDisposition {
    Accepted,
    PrototypeOnly,
    Rejected,
    NeedsHumanStudy
};

struct LinearRgbImage {
    int width = 0;
    int height = 0;
    std::vector<float> pixels;

    bool Valid() const;
};

struct ScalarMask {
    int width = 0;
    int height = 0;
    std::vector<float> values;

    bool Valid() const;
};

struct FeatureContext {
    std::string sourceIdentity;
    std::string recipeIdentity;
    std::string stage;
    std::string colorSpace;
    std::string colorTransformIdentity;
    std::string transferFunction = "linear";
    std::array<double, 9> workingToXyz {
        1.0, 0.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 0.0, 1.0
    };
    double referenceGrey = 0.18;
    std::string rawEvidenceIdentity;
    std::string cropIdentity = "full-image";
    bool orientationNormalized = true;
    int sourceWidth = 0;
    int sourceHeight = 0;
    double globalLiftEv = 0.0;
    double maximumLocalLiftEv = 0.0;
};

struct DisplayModel {
    bool absoluteLuminanceKnown = false;
    double peakLuminanceCdM2 = 0.0;
    double blackLuminanceCdM2 = 0.0;
    double ambientReflectionCdM2 = 0.0;
    std::string primaries = "sRGB/Rec.709";
    std::string transferFunction = "sRGB";
};

struct FeatureValue {
    std::string id;
    std::string version = kRenderedFeatureVersion;
    FeatureDisposition disposition = FeatureDisposition::PrototypeOnly;
    bool valid = false;
    double value = 0.0;
    std::string units;
    std::string stage;
    std::string colorSpace;
    std::string colorTransformIdentity;
    std::string transferFunction;
    double referenceGrey = 0.18;
    std::string rawEvidenceIdentity;
    std::string reference;
    int width = 0;
    int height = 0;
    std::string cropIdentity;
    double validFraction = 0.0;
    double uncertainty01 = 1.0;
    double runtimeMs = 0.0;
    int minimumWidth = 1;
    int minimumHeight = 1;
    std::string reason;
};

struct FeatureRecord {
    int schemaVersion = kRenderedFeatureSchemaVersion;
    std::string featureVersion = kRenderedFeatureVersion;
    bool valid = false;
    std::string sourceIdentity;
    std::string recipeIdentity;
    std::string stage;
    std::string colorSpace;
    std::string colorTransformIdentity;
    std::string transferFunction;
    double referenceGrey = 0.18;
    std::string rawEvidenceIdentity;
    std::string cropIdentity;
    int width = 0;
    int height = 0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    bool orientationNormalized = false;
    std::string recordIdentity;
    std::vector<FeatureValue> values;
    std::vector<std::string> warnings;
    double runtimeMs = 0.0;
    std::string statusMessage;
};

struct EdgeProfileMetrics {
    bool valid = false;
    double referenceContrast = 0.0;
    double reversalEnergy = 0.0;
    double overshoot = 0.0;
    double undershoot = 0.0;
    double adjacentBandEnergy = 0.0;
    double edgeShiftPixels = 0.0;
    double newExtremaCount = 0.0;
    std::string reason;
};

struct FeatureAgreement {
    bool valid = false;
    int comparableFeatureCount = 0;
    double meanRelativeDelta = 0.0;
    double maxRelativeDelta = 0.0;
    std::vector<std::string> missingOrMismatched;
};

LinearRgbImage ResizeLinearRgb(const LinearRgbImage& source, int targetWidth, int targetHeight);
LinearRgbImage OrientLinearRgb(const LinearRgbImage& source, int exifOrientation);

FeatureRecord AnalyzeSceneLinear(
    const LinearRgbImage& image,
    const FeatureContext& context,
    const RawEvidence::RawTechnicalEvidenceRecord* rawEvidence = nullptr);

FeatureRecord AnalyzeRegionMask(
    const LinearRgbImage& sceneImage,
    const ScalarMask& mask,
    const FeatureContext& context);

FeatureRecord AnalyzeDisplayMapped(
    const LinearRgbImage& linearDisplayImage,
    const FeatureContext& context,
    const DisplayModel& displayModel = {});

FeatureRecord CompareRenderedImages(
    const LinearRgbImage& reference,
    const LinearRgbImage& candidate,
    const FeatureContext& context);

FeatureAgreement CompareFeatureRecords(
    const FeatureRecord& reference,
    const FeatureRecord& proxy);

bool FeatureRecordMatches(
    const FeatureRecord& record,
    const std::string& sourceIdentity,
    const std::string& recipeIdentity,
    const std::string& stage);

const FeatureValue* FindFeature(const FeatureRecord& record, const std::string& id);

EdgeProfileMetrics EvaluateEdgeProfile(
    const std::vector<double>& reference,
    const std::vector<double>& candidate,
    int centerIndex,
    int plateauRadius = 3);

std::vector<float> BilateralBasePrototype(
    const std::vector<float>& signal,
    int width,
    int height,
    int radius,
    double sigmaSpatial,
    double sigmaRange);

std::vector<float> GuidedBasePrototype(
    const std::vector<float>& guide,
    int width,
    int height,
    int radius,
    double epsilon);

std::vector<float> LocalLaplacianPrototype(
    const std::vector<float>& signal,
    int width,
    int height,
    int levels,
    double detailExponent,
    double edgeScale);

const char* FeatureDispositionName(FeatureDisposition disposition);
nlohmann::json SerializeFeatureRecord(const FeatureRecord& record);
nlohmann::json SerializeFeatureAgreement(const FeatureAgreement& agreement);

} // namespace Stack::RenderedFeatures
