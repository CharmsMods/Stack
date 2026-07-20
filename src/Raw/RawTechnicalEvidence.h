#pragma once

#include "Raw/RawImageData.h"
#include "ThirdParty/json.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Stack::RawEvidence {

inline constexpr int kRawTechnicalEvidenceSchemaVersion = 1;
inline constexpr const char* kRawTechnicalEvidenceVersion = "raw-technical-evidence-v1";
inline constexpr const char* kRawNormalizationVersion = "dng-linear-reference-v1";
inline constexpr const char* kRawDecoderIdentityVersion = "stack-libraw-dng-supplement-v2";

enum class EvidenceProvenance {
    Measured,
    Metadata,
    Derived,
    Fallback,
    Unavailable
};

struct EvidenceMeasurement {
    bool valid = false;
    double value = 0.0;
    std::string units;
    std::string stage = "normalized-raw-mosaic";
    EvidenceProvenance provenance = EvidenceProvenance::Unavailable;
    double uncertainty01 = 1.0;
    std::string reason;
};

struct SourceIdentity {
    bool valid = false;
    std::string sha256;
    std::uint64_t byteSize = 0;
    std::string reason;
};

struct DecodeIdentityOptions {
    std::string decoderBackend = "libraw";
    std::string decoderVersion = kRawDecoderIdentityVersion;
    std::string normalizationVersion = kRawNormalizationVersion;
    bool applyLinearizationTable = true;
    bool useActiveArea = true;
    bool useMaskedAreas = true;
    bool overrideBlackLevel = false;
    double blackLevelOverride = 0.0;
    bool overrideWhiteLevel = false;
    double whiteLevelOverride = 0.0;
    bool opcodeList1Applied = false;
    bool opcodeList2GainMapsApplied = false;
    bool profileGainTableMapApplied = false;
};

struct DecodeIdentity {
    bool valid = false;
    std::string sha256;
    std::string canonicalFields;
    std::string reason;
};

struct SensorGeometry {
    Raw::RawSensorRect activeArea;
    std::vector<Raw::RawSensorRect> maskedAreas;
    bool activeAreaFromMetadata = false;
    bool maskedAreasFromMetadata = false;
    int orientation = 0;
    std::string sensorCoordinates = "stored-raw-top-left";
    std::string photographicTransform;
    double activeValidFraction = 0.0;
};

struct PlaneEvidence {
    int plane = -1;
    std::string planeName;
    std::uint64_t sampleCount = 0;
    EvidenceMeasurement blackMaximum;
    EvidenceMeasurement whiteLevel;
    EvidenceMeasurement linearResponseLimit;
    EvidenceMeasurement p999;
    EvidenceMeasurement nearNonlinearFraction;
    EvidenceMeasurement nearWhiteFraction;
    EvidenceMeasurement clippedFraction;
    EvidenceMeasurement headroomEv;
    EvidenceMeasurement wbMultiplier;
    EvidenceMeasurement wbScaledHeadroomEv;
};

struct ClipPatternEvidence {
    bool valid = false;
    std::uint64_t superpixelCount = 0;
    EvidenceMeasurement noChannelClippedFraction;
    EvidenceMeasurement singleChannelClippedFraction;
    EvidenceMeasurement multiChannelClippedFraction;
    EvidenceMeasurement allChannelClippedFraction;
    std::string reason;
};

struct NoisePlaneEvidence {
    int plane = -1;
    std::string planeName;
    EvidenceMeasurement shotScale;
    EvidenceMeasurement readNoiseVariance;
    std::vector<EvidenceMeasurement> snrBySignal;
};

struct MetadataCoverage {
    bool hasLinearizationTable = false;
    bool hasSpatialBlackPattern = false;
    bool hasBlackDeltaH = false;
    bool hasBlackDeltaV = false;
    bool hasActiveArea = false;
    bool hasMaskedAreas = false;
    bool hasLinearResponseLimit = false;
    bool hasAsShotNeutral = false;
    bool hasBaselineExposure = false;
    bool hasNoiseProfile = false;
    bool hasOpcodeList1 = false;
    bool hasOpcodeList2 = false;
    bool hasOpcodeList3 = false;
    bool hasOpcodeList2GainMap = false;
    bool hasProfileGainTableMap = false;
    bool hasProfileGainTableMap2 = false;
    std::array<int, 3> opcodeCount { 0, 0, 0 };
    std::array<int, 3> appliedOpcodeCount { 0, 0, 0 };
    std::array<int, 3> unsupportedOpcodeCount { 0, 0, 0 };
    std::vector<std::string> omissions;
};

struct RawTechnicalEvidenceRecord {
    int schemaVersion = kRawTechnicalEvidenceSchemaVersion;
    std::string featureVersion = kRawTechnicalEvidenceVersion;
    bool valid = false;
    std::string stage = "normalized-raw-mosaic";
    std::string units = "normalized-linear-reference";
    std::string normalizationVersion = kRawNormalizationVersion;
    SourceIdentity sourceIdentity;
    DecodeIdentity decodeIdentity;
    std::string evidenceIdentitySha256;
    SensorGeometry geometry;
    std::string pixelLayout;
    std::string sampleFormat;
    std::string cfaPattern;
    std::array<std::string, 3> planeOrder { "R", "G", "B" };
    std::vector<PlaneEvidence> planes;
    ClipPatternEvidence clipping;
    std::vector<NoisePlaneEvidence> noise;
    EvidenceMeasurement maskedBlackMean;
    EvidenceMeasurement maskedBlackStd;
    EvidenceMeasurement maskedBlackDeltaFromMetadata;
    EvidenceMeasurement hotPixelFraction;
    EvidenceMeasurement deadPixelFraction;
    EvidenceMeasurement baselineExposureEv;
    EvidenceMeasurement cameraProfileConfidence;
    MetadataCoverage metadataCoverage;
    std::string limitingPlane;
    std::string statusMessage;
    std::vector<std::string> warnings;
    double runtimeMs = 0.0;
    std::uint64_t sampledRawValues = 0;
};

struct BuildOptions {
    DecodeIdentityOptions decode;
    std::size_t maxSamples = 1000000;
    double nearWhiteMargin = 0.005;
    double nearNonlinearMargin = 0.01;
    bool measureDefectivePixels = true;
    std::function<bool()> shouldCancel;
};

SourceIdentity ComputeSourceIdentity(const std::filesystem::path& path);
SourceIdentity ComputeSourceIdentity(const std::vector<std::uint8_t>& bytes);
DecodeIdentity BuildDecodeIdentity(
    const Raw::RawMetadata& metadata,
    const DecodeIdentityOptions& options);
RawTechnicalEvidenceRecord BuildRawTechnicalEvidence(
    const Raw::RawImageData& raw,
    const SourceIdentity& sourceIdentity,
    const BuildOptions& options = {});

class RawTechnicalEvidenceCache {
public:
    const RawTechnicalEvidenceRecord& GetOrBuild(
        const Raw::RawImageData& raw,
        const SourceIdentity& sourceIdentity,
        const BuildOptions& options = {});
    void Clear();
    std::size_t Size() const;

private:
    std::unordered_map<std::string, RawTechnicalEvidenceRecord> m_Records;
};

const char* EvidenceProvenanceName(EvidenceProvenance provenance);
nlohmann::json SerializeRawTechnicalEvidence(const RawTechnicalEvidenceRecord& record);
std::string SummarizeRawTechnicalEvidence(const RawTechnicalEvidenceRecord& record);

} // namespace Stack::RawEvidence
