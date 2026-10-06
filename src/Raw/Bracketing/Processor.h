#pragma once
#include "Recipe.h"
#include "Progress.h"
#include "Raw/RawImageData.h"
#include "Raw/OpenGlTask.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <array>
#include <map>

namespace Raw::Bracketing {
namespace Panorama { struct Prepared; struct Layout; }
class ProcessingEvidenceStream;
struct Source {
    std::string frameId, sha256;
    std::uint64_t bytes = 0;
    std::filesystem::path path;
    // User-facing filename, independent of the content-addressed storage path.
    std::string displayName;
};
struct CapturePreview {
    unsigned width=0,height=0;
    double exposureScale=1;
    std::vector<float> rgb;
    std::vector<float> coverage;
    std::vector<std::uint8_t> clipped;
};
struct CaptureAlignment {
    enum class Model { Identity, Translation, GlobalAffine };
    Model model = Model::Identity;
    bool accepted = true;
    double translationX = 0;
    double translationY = 0;
    double rotationDegrees = 0;
    double overlapFraction = 1;
    double peakToSidelobeRatio = 0;
    double acceptedCoverage = 1;
    double uncertainCoverage = 0;
    double rejectedCoverage = 0;
    bool localApplied = false;
    std::string backend = "cpu-reference";
    std::string message;
};
struct AlignmentInspection {
    unsigned width=0,height=0;
    unsigned rawWidth=0,rawHeight=0;
    std::vector<float> confidence;
    std::vector<std::uint16_t> rejectionBits;
};
enum class MeasurementFallbackReason : std::uint8_t {
    None = 0,
    AutomaticValidInput = 1,
    FixedReference = 2,
    ShortestExposure = 3,
    NoFiniteMeasurement = 4
};
struct PreparedDataset;
struct BracketingAnalysis {
    std::shared_ptr<const Panorama::Prepared> panorama;
    unsigned version=1;
    std::string identity;
    std::shared_ptr<const PreparedDataset> prepared;
    std::vector<float> guideEv;
    std::vector<Knot> suggestion;
    std::array<float,256> histogram {};
    std::vector<std::string> diagnostics;
    std::vector<double> relativeEv;
    std::map<std::string,double> calibratedEv;
    std::map<std::string,std::shared_ptr<const CapturePreview>> originals;
    std::map<std::string,std::string> noiseConfidence;
    std::map<std::string,CaptureAlignment> alignments;
    std::map<std::string,std::shared_ptr<const AlignmentInspection>> alignmentInspection;
    std::uint64_t estimatedPeakResidentBytes=0;
    std::map<std::string,double> stageSeconds;
};
struct Preview {
    enum Diagnostic : std::uint8_t {
        Fallback=1,
        Unrecoverable=2,
        Invalid=4,
        AlignmentRejected=8,
        FixedReference=16,
        ShortestExposure=32
    };
    unsigned width=0,height=0;
    double sensorOriginX=0,sensorOriginY=0,sensorStepX=1,sensorStepY=1;
    // Pixel-major Bayer cell RGGB-equivalent RGB, then group.
    std::vector<float> resultRgb, sourceRgb, contributions, requested;
    std::vector<float> coverage;
    std::vector<float> guideEv;
    std::vector<std::uint8_t> diagnostics;
    struct Sample {
        float value=0,variance=0,headroom=0,support=0,fallback=0,exposure=0;
        bool finite=false,clipped=false,localRejected=false,fixedReference=false;
        float measurementVariance=-1,uncertaintyVariance=0;
        bool fallbackClipped=false;
    };
    std::vector<Sample> samples;
    std::map<std::string,std::shared_ptr<const CapturePreview>> originals;
    RawMetadata metadata;
};
struct BracketingResult {
    unsigned version=1;
    enum class Status { Completed, Canceled, Failed };
    Status status=Status::Failed;
    std::string identity, message;
    std::shared_ptr<const BracketingAnalysis> analysis;
    std::shared_ptr<RawImageData> raw;
    Preview preview;
    std::shared_ptr<const Panorama::Layout> panorama;
    struct ReconstructionInspection {
        std::filesystem::path directory;
        unsigned scale=1,tilePixels=256,sensorWidth=0,sensorHeight=0,groups=0;
        BracketingRecipe recipe;
        std::map<std::pair<unsigned,unsigned>,std::string> checksums;
        ~ReconstructionInspection();
    };
    std::shared_ptr<const ReconstructionInspection> reconstructionInspection;
    std::uint64_t fallbackSamples=0,unrecoverableSamples=0,invalidSamples=0;
};
struct ProcessingRequest {
    unsigned version=1;
    BracketingRecipe recipe;
    std::vector<Source> sources;
    std::filesystem::path cacheDirectory;
    std::uint64_t memoryBudgetBytes=1024ull*1024*1024;
    // Explicit budgets stay strict. Interactive/automatic callers allow the
    // reconstruction stage to recheck fresh RAM and system commit headroom.
    bool automaticMemoryBudget=false;
    // Zero means no graphics-device limit was supplied by a headless caller.
    unsigned maximumOutputDimension=0;
    // Rebuild source/group inspection data while retaining a saved final measurement.
    bool preparationOnly=false;
    std::uint32_t workerCount=1;
    bool preferGpuRegistration=true;
    Raw::OpenGlTaskExecutor executeOpenGlTask;
    // Only enable for a private, caller-created generation directory.
    bool removeCacheOnRelease=false;
    std::shared_ptr<const BracketingAnalysis> analysis;
    std::function<bool()> shouldCancel;
    std::function<void(double,const std::string&)> reportProgress;
    std::function<void(const ProcessingProgress&)> reportPresentation;
    std::shared_ptr<ProcessingEvidenceStream> evidenceStream;
    unsigned presentationPass=0;
    std::function<bool(const std::filesystem::path&,RawImageData&,const std::function<bool()>&)> decode;
};
inline void ReportPresentation(const ProcessingRequest& request,ProcessingStage stage,
    std::uint64_t completed=0,std::uint64_t total=0,const std::string& capture={},
    const std::string& detail={},unsigned pass=0) {
    if(!request.reportPresentation)return;
    ProcessingProgress event;event.stage=stage;event.completed=completed;event.total=total;
    event.captureId=capture;event.detail=detail;event.pass=pass+request.presentationPass;
    event.fraction=total?static_cast<double>(completed)/total:-1;
    request.reportPresentation(event);
}
BracketingResult Process(const ProcessingRequest&);
// A bounded interactive preview reuses the exact full-resolution evaluator's
// cached source cells; it never decodes or changes the neutral guide.
Preview ReblendPreview(const Preview&, const BracketingRecipe&, const BracketingAnalysis&);
Preview RenderNativeDetail(const ProcessingRequest&, unsigned centerX, unsigned centerY, unsigned size = 192);
std::string AnalysisIdentity(const ProcessingRequest&);
std::uint64_t EstimateBracketingPeakResidentBytes(
    unsigned width,unsigned height,std::size_t sourceCount,
    std::size_t groupCount,std::uint32_t workerCount,bool localAlignment);
std::uint32_t ResolveBracketingWorkerCount(
    std::uint64_t memoryBudgetBytes,std::uint32_t requestedLimit=0);
} // namespace Raw::Bracketing
