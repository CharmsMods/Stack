#pragma once
#include "Processor.h"
#include "CaptureMotion.h"
#include "ProcessingTimer.h"
#include "Raw/MultiFrameDenoise/Preparation.h"
#include "Raw/MultiFrameDenoise/NoiseModel.h"
#include "Raw/MultiFrameDenoise/GlobalRegistration.h"
#include "Raw/MultiFrameDenoise/LocalMotion.h"
#include "Raw/MultiFrameDenoise/Reliability.h"

namespace Raw::Bracketing {
struct PreparedSource {
    std::string id;
    std::size_t group=0;
    bool enabled=true;
    RawMetadata metadata;
    Mfd::PreparedRawFrame frame;
    Mfd::NoiseModel noise;
    double scale=1;
    double scaleVariance=0;
    double detailPreference=1;
    std::shared_ptr<CapturePreview> original;
    mutable std::weak_ptr<const ProcessingThumbnail> presentationThumbnail;
    unsigned proxyStride=2;
    Mfd::AffineModel alignment;
    CaptureAlignment alignmentDiagnostic;
    std::shared_ptr<Mfd::LocalMotionGrid> localMotion;
    std::shared_ptr<const MotionRefinementField> refinedMotion;
    std::shared_ptr<Mfd::ReliabilityStore> reliability;
    std::shared_ptr<AlignmentInspection> alignmentInspection;
};
struct PreparedDataset {
    ~PreparedDataset() {
        if(removeCacheOnRelease&&!directory.empty()) {std::error_code error;std::filesystem::remove_all(directory,error);}
    }
    bool removeCacheOnRelease=false;
    std::filesystem::path directory;
    std::vector<PreparedSource> sources;
    std::size_t origin=0;
    std::string motionIdentity;
    unsigned width=0,height=0;
    std::map<std::string,double> stageSeconds;
};
struct Observation {
    double value=0,variance=0,headroom=0,support=0;
    double fallback=0,exposure=0;
    bool finite=false,clipped=false,localRejected=false,fixedReference=false;
    // Fusion weights include registration/model uncertainty. Keep it separate
    // from sensor noise for temporal detail evidence and measurement sidecars.
    double measurementVariance=-1,uncertaintyVariance=0;
    // A group may contain accepted measurements while its selected fallback
    // capture is clipped. These are separate validity decisions.
    bool fallbackClipped=false;
};
struct Pixel {
    double value=0,variance=0,support=0;
    bool valid=false,clipped=false,fallback=false,localRejected=false;
    MeasurementFallbackReason fallbackReason=MeasurementFallbackReason::None;
    std::array<double,64> actual {};
    double measurementVariance=0,uncertaintyVariance=0;
};
class PreparedTileSampler;
// Shared by production fusion and native measurement diagnostics. Diagnostics
// must inspect the same registered measurements rather than another resampler.
Observation SampleAlignedCapture(const PreparedSource&,Mfd::CfaLayout&,PreparedTileSampler&,
    double,double,double,const Mfd::RawSignalGradient&,std::string&);
bool Prepare(const ProcessingRequest&,PreparedDataset&,std::vector<std::string>&,std::string&);
bool Align(const ProcessingRequest&,PreparedDataset&,std::vector<std::string>&,std::string&);
bool AlignLocal(const ProcessingRequest&,PreparedDataset&,std::vector<std::string>&,std::string&);
bool Calibrate(const ProcessingRequest&,PreparedDataset&,std::vector<std::string>&,std::string&);
bool RefineBurstNoise(const ProcessingRequest&,PreparedDataset&,std::vector<std::string>&,std::string&);
bool ReadGroups(const ProcessingRequest&,const PreparedDataset&,unsigned,unsigned,
    std::vector<std::vector<Observation>>&,Mfd::PreparedRawTile&,std::string&);
bool ReadTemporalGroups(const ProcessingRequest&,const PreparedDataset&,unsigned,unsigned,
    std::vector<std::vector<Observation>>&,Mfd::PreparedRawTile&,std::string&);
bool PrepareGroupTile(const ProcessingRequest&,const PreparedDataset&,unsigned,unsigned,std::string&);
Pixel Blend(const std::vector<Observation>&,const std::vector<double>&,const BracketingRecipe&,
    const std::array<double,64>* colorHeadroom=nullptr);
double Luminance(const RawMetadata&,const std::array<double,4>&);
} // namespace Raw::Bracketing
