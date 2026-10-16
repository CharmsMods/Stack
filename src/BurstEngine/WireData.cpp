#include "WireData.h"
#include "Raw/RawMetadataSerialization.h"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace Stack::BurstEngine::Wire {
using namespace Raw::Bracketing;
namespace {
template<class T> Json Pack(const std::vector<T>& values){
    static_assert(std::is_arithmetic_v<T>);
    std::vector<std::uint8_t> bytes(values.size()*sizeof(T));
    if(!bytes.empty())std::memcpy(bytes.data(),values.data(),bytes.size());
    return Json::binary(std::move(bytes));
}
template<class T> std::vector<T> Unpack(const Json& value){
    const auto& bytes=value.get_binary();
    if(bytes.size()%sizeof(T))throw std::runtime_error("Invalid burst array byte count.");
    std::vector<T> values(bytes.size()/sizeof(T));
    if(!bytes.empty())std::memcpy(values.data(),bytes.data(),bytes.size());return values;
}
Json Depth(const Raw::ReconstructionDepth& d){
    return {{"width",d.width},{"height",d.height},{"sensorWidth",d.sensorWidth},{"sensorHeight",d.sensorHeight},
        {"acceptedPairs",d.acceptedPairs},{"measuredFraction",d.measuredFraction},{"message",d.message},
        {"inverseDepth",Pack(d.inverseDepth)},{"confidence",Pack(d.confidence)}};
}
std::shared_ptr<const Raw::ReconstructionDepth> Depth(const Json& j){
    auto d=std::make_shared<Raw::ReconstructionDepth>();
    d->width=j.at("width");d->height=j.at("height");d->sensorWidth=j.at("sensorWidth");d->sensorHeight=j.at("sensorHeight");
    d->acceptedPairs=j.at("acceptedPairs");d->measuredFraction=j.at("measuredFraction");d->message=j.at("message");
    d->inverseDepth=Unpack<float>(j.at("inverseDepth"));d->confidence=Unpack<float>(j.at("confidence"));return d;
}
Json Evidence(const ProcessingEvidence& e){
    Json j={{"equation",e.equation},{"caption",e.caption},{"curveLabel",e.curveLabel},{"xUnits",e.xUnits},{"yUnits",e.yUnits},
        {"affine",e.affine},{"hasAffine",e.hasAffine},{"provisional",e.provisional},{"focusX",e.focusX},{"focusY",e.focusY}};
    j["rasters"]=Json::array();for(const auto& r:e.rasters)j["rasters"].push_back({{"label",r.label},{"units",r.units},
        {"kind",int(r.kind)},{"width",r.width},{"height",r.height},{"rawWidth",r.rawWidth},{"rawHeight",r.rawHeight},
        {"minimum",r.minimum},{"maximum",r.maximum},{"rgba",Pack(r.rgba)}});
    j["vectors"]=Json::array();for(const auto& v:e.vectors)j["vectors"].push_back({v.x,v.y,v.dx,v.dy,v.confidence,v.rejected});
    j["curve"]=Json::array();for(const auto& p:e.curve)j["curve"].push_back({p.x,p.y});
    j["observations"]=Json::array();for(const auto& p:e.observations)j["observations"].push_back({p.x,p.y});
    j["metrics"]=Json::array();for(const auto& m:e.metrics)j["metrics"].push_back({m.label,m.units,m.value});
    j["kernels"]=Json::array();for(const auto& k:e.kernels)j["kernels"].push_back({k.x,k.y,k.xx,k.xy,k.yy});return j;
}
std::shared_ptr<const ProcessingEvidence> Evidence(const Json& j){
    auto e=std::make_shared<ProcessingEvidence>();
    e->equation=j.at("equation");e->caption=j.at("caption");e->curveLabel=j.at("curveLabel");e->xUnits=j.at("xUnits");e->yUnits=j.at("yUnits");
    e->affine=j.at("affine").get<std::array<float,6>>();e->hasAffine=j.at("hasAffine");e->provisional=j.at("provisional");e->focusX=j.at("focusX");e->focusY=j.at("focusY");
    for(const auto& r:j.at("rasters")){EvidenceRaster v;v.label=r.at("label");v.units=r.at("units");v.kind=static_cast<EvidenceKind>(r.at("kind").get<int>());
        v.width=r.at("width");v.height=r.at("height");v.rawWidth=r.at("rawWidth");v.rawHeight=r.at("rawHeight");v.minimum=r.at("minimum");v.maximum=r.at("maximum");
        v.rgba=Unpack<std::uint8_t>(r.at("rgba"));if(v.rgba.size()!=std::uint64_t(v.width)*v.height*4)throw std::runtime_error("Invalid burst evidence dimensions.");e->rasters.push_back(std::move(v));}
    for(const auto& v:j.at("vectors"))e->vectors.push_back({v.at(0),v.at(1),v.at(2),v.at(3),v.at(4),v.at(5)});
    for(const auto& v:j.at("curve"))e->curve.push_back({v.at(0),v.at(1)});
    for(const auto& v:j.at("observations"))e->observations.push_back({v.at(0),v.at(1)});
    for(const auto& v:j.at("metrics"))e->metrics.push_back({v.at(0),v.at(1),v.at(2)});
    for(const auto& v:j.at("kernels"))e->kernels.push_back({v.at(0),v.at(1),v.at(2),v.at(3),v.at(4)});return e;
}
}
Json Request(const ProcessingRequest& r){
    if(r.decode)throw std::runtime_error("Custom decoder callbacks cannot cross the burst process boundary. Use explicit RAW source files or engine-side validation.");
    Json j={{"version",r.version},{"recipe",Serialize(r.recipe)},{"cacheDirectory",PathText(r.cacheDirectory)},
        {"memoryBudgetBytes",r.memoryBudgetBytes},{"automaticMemoryBudget",r.automaticMemoryBudget},
        {"maximumOutputDimension",r.maximumOutputDimension},{"preparationOnly",r.preparationOnly},{"workerCount",r.workerCount},
        {"preferGpuRegistration",r.preferGpuRegistration},{"preferGpuCombining",r.preferGpuCombining},{"presentationPass",r.presentationPass},
        {"progress",bool(r.reportProgress)},{"presentation",bool(r.reportPresentation)}};
    j["sources"]=Json::array();for(const auto& s:r.sources)j["sources"].push_back({{"frameId",s.frameId},{"sha256",s.sha256},
        {"bytes",s.bytes},{"path",PathText(s.path)},{"displayName",s.displayName}});return j;
}
ProcessingRequest Request(const Json& j){
    ProcessingRequest r;r.version=j.at("version");std::string error;
    if(!Deserialize(j.at("recipe"),r.recipe,error))throw std::runtime_error(error);
    r.cacheDirectory=TextPath(j.at("cacheDirectory"));r.memoryBudgetBytes=j.at("memoryBudgetBytes");
    r.automaticMemoryBudget=j.at("automaticMemoryBudget");r.maximumOutputDimension=j.at("maximumOutputDimension");
    r.preparationOnly=j.at("preparationOnly");r.workerCount=j.at("workerCount");r.preferGpuRegistration=j.at("preferGpuRegistration");r.preferGpuCombining=j.value("preferGpuCombining",true);r.presentationPass=j.at("presentationPass");
    if(j.at("sources").size()>100000)throw std::runtime_error("Too many burst source files.");
    for(const auto& s:j.at("sources"))r.sources.push_back({s.at("frameId"),s.at("sha256"),s.at("bytes"),TextPath(s.at("path")),s.at("displayName")});return r;
}
Json Capture(const CapturePreview& c){return {{"width",c.width},{"height",c.height},{"exposureScale",c.exposureScale},
    {"rgb",Pack(c.rgb)},{"coverage",Pack(c.coverage)},{"clipped",Pack(c.clipped)}};}
std::shared_ptr<const CapturePreview> Capture(const Json& j){
    auto c=std::make_shared<CapturePreview>();c->width=j.at("width");c->height=j.at("height");c->exposureScale=j.at("exposureScale");
    c->rgb=Unpack<float>(j.at("rgb"));c->coverage=Unpack<float>(j.at("coverage"));c->clipped=Unpack<std::uint8_t>(j.at("clipped"));
    const auto pixels=std::uint64_t(c->width)*c->height;
    if(c->rgb.size()!=pixels*3||(!c->clipped.empty()&&c->clipped.size()!=pixels)||(!c->coverage.empty()&&c->coverage.size()!=pixels))throw std::runtime_error("Invalid burst capture dimensions.");return c;
}
Json Preview(const Raw::Bracketing::Preview& p){
    Json j={{"width",p.width},{"height",p.height},{"sensorOriginX",p.sensorOriginX},{"sensorOriginY",p.sensorOriginY},
        {"sensorStepX",p.sensorStepX},{"sensorStepY",p.sensorStepY},{"metadata",Raw::Serialization::SerializeMetadata(p.metadata)},
        {"resultRgb",Pack(p.resultRgb)},{"sourceRgb",Pack(p.sourceRgb)},{"contributions",Pack(p.contributions)},
        {"requested",Pack(p.requested)},{"coverage",Pack(p.coverage)},{"guideEv",Pack(p.guideEv)},{"diagnostics",Pack(p.diagnostics)}};
    std::vector<float> values;std::vector<std::uint8_t> flags;values.reserve(p.samples.size()*8);flags.reserve(p.samples.size());
    for(const auto& s:p.samples){values.insert(values.end(),{s.value,s.variance,s.headroom,s.support,s.fallback,s.exposure,s.measurementVariance,s.uncertaintyVariance});
        flags.push_back(std::uint8_t((s.finite?1:0)|(s.clipped?2:0)|(s.localRejected?4:0)|(s.fixedReference?8:0)|(s.fallbackClipped?16:0)));}
    j["sampleValues"]=Pack(values);j["sampleFlags"]=Pack(flags);j["originals"]=Json::object();
    for(const auto& [id,capture]:p.originals)if(capture)j["originals"][id]=Capture(*capture);return j;
}
Raw::Bracketing::Preview Preview(const Json& j){
    Raw::Bracketing::Preview p;p.width=j.at("width");p.height=j.at("height");p.sensorOriginX=j.at("sensorOriginX");p.sensorOriginY=j.at("sensorOriginY");
    p.sensorStepX=j.at("sensorStepX");p.sensorStepY=j.at("sensorStepY");p.metadata=Raw::Serialization::DeserializeMetadata(j.at("metadata"));
    p.resultRgb=Unpack<float>(j.at("resultRgb"));p.sourceRgb=Unpack<float>(j.at("sourceRgb"));p.contributions=Unpack<float>(j.at("contributions"));
    p.requested=Unpack<float>(j.at("requested"));p.coverage=Unpack<float>(j.at("coverage"));p.guideEv=Unpack<float>(j.at("guideEv"));p.diagnostics=Unpack<std::uint8_t>(j.at("diagnostics"));
    const auto pixels=std::uint64_t(p.width)*p.height;
    if(pixels>std::numeric_limits<std::size_t>::max()/3||p.resultRgb.size()!=pixels*3||
        (!p.coverage.empty()&&p.coverage.size()!=pixels)||(!p.guideEv.empty()&&p.guideEv.size()!=pixels)||
        (!p.diagnostics.empty()&&p.diagnostics.size()!=pixels))throw std::runtime_error("Invalid burst preview dimensions.");
    const auto values=Unpack<float>(j.at("sampleValues"));const auto flags=Unpack<std::uint8_t>(j.at("sampleFlags"));
    if(values.size()!=flags.size()*8)throw std::runtime_error("Invalid burst preview samples.");p.samples.resize(flags.size());
    for(std::size_t i=0;i<flags.size();++i){const auto n=i*8;auto& s=p.samples[i];s.value=values[n];s.variance=values[n+1];s.headroom=values[n+2];s.support=values[n+3];s.fallback=values[n+4];s.exposure=values[n+5];s.measurementVariance=values[n+6];s.uncertaintyVariance=values[n+7];
        s.finite=(flags[i]&1)!=0;s.clipped=(flags[i]&2)!=0;s.localRejected=(flags[i]&4)!=0;s.fixedReference=(flags[i]&8)!=0;s.fallbackClipped=(flags[i]&16)!=0;}
    for(const auto& [id,capture]:j.at("originals").items())p.originals[id]=Capture(capture);return p;
}
Json Analysis(const BracketingAnalysis& a){
    Json j={{"version",a.version},{"identity",a.identity},{"guideEv",Pack(a.guideEv)},{"histogram",a.histogram},
        {"diagnostics",a.diagnostics},{"relativeEv",a.relativeEv},{"calibratedEv",a.calibratedEv},{"noiseConfidence",a.noiseConfidence},
        {"estimatedPeakResidentBytes",a.estimatedPeakResidentBytes},{"stageSeconds",a.stageSeconds}};
    j["suggestion"]=Json::array();for(const auto& k:a.suggestion)j["suggestion"].push_back({k.ev,k.incoming,k.outgoing,k.share,k.left,k.right});
    j["originals"]=Json::object();for(const auto& [id,c]:a.originals)if(c)j["originals"][id]=Capture(*c);
    j["alignments"]=Json::object();for(const auto& [id,c]:a.alignments)j["alignments"][id]={{"model",int(c.model)},{"accepted",c.accepted},
        {"translationX",c.translationX},{"translationY",c.translationY},{"rotationDegrees",c.rotationDegrees},{"overlapFraction",c.overlapFraction},
        {"peakToSidelobeRatio",c.peakToSidelobeRatio},{"acceptedCoverage",c.acceptedCoverage},{"uncertainCoverage",c.uncertainCoverage},
        {"rejectedCoverage",c.rejectedCoverage},{"localApplied",c.localApplied},{"backend",c.backend},{"message",c.message}};
    j["alignmentInspection"]=Json::object();for(const auto& [id,c]:a.alignmentInspection)if(c)j["alignmentInspection"][id]={{"width",c->width},{"height",c->height},
        {"rawWidth",c->rawWidth},{"rawHeight",c->rawHeight},{"confidence",Pack(c->confidence)},{"rejectionBits",Pack(c->rejectionBits)}};
    if(a.depth)j["depth"]=Depth(*a.depth);return j;
}
std::shared_ptr<BracketingAnalysis> Analysis(const Json& j){
    auto a=std::make_shared<BracketingAnalysis>();a->version=j.at("version");a->identity=j.at("identity");a->guideEv=Unpack<float>(j.at("guideEv"));
    a->histogram=j.at("histogram").get<std::array<float,256>>();a->diagnostics=j.at("diagnostics").get<std::vector<std::string>>();
    a->relativeEv=j.at("relativeEv").get<std::vector<double>>();a->calibratedEv=j.at("calibratedEv").get<std::map<std::string,double>>();
    a->noiseConfidence=j.at("noiseConfidence").get<std::map<std::string,std::string>>();a->estimatedPeakResidentBytes=j.at("estimatedPeakResidentBytes");a->stageSeconds=j.at("stageSeconds").get<std::map<std::string,double>>();
    for(const auto& k:j.at("suggestion"))a->suggestion.push_back({k.at(0),k.at(1),k.at(2),k.at(3).get<std::vector<double>>(),k.at(4).get<std::vector<double>>(),k.at(5).get<std::vector<double>>()});
    for(const auto& [id,c]:j.at("originals").items())a->originals[id]=Capture(c);
    for(const auto& [id,c]:j.at("alignments").items()){CaptureAlignment v;v.model=static_cast<CaptureAlignment::Model>(c.at("model").get<int>());v.accepted=c.at("accepted");
        v.translationX=c.at("translationX");v.translationY=c.at("translationY");v.rotationDegrees=c.at("rotationDegrees");v.overlapFraction=c.at("overlapFraction");v.peakToSidelobeRatio=c.at("peakToSidelobeRatio");
        v.acceptedCoverage=c.at("acceptedCoverage");v.uncertainCoverage=c.at("uncertainCoverage");v.rejectedCoverage=c.at("rejectedCoverage");v.localApplied=c.at("localApplied");v.backend=c.at("backend");v.message=c.at("message");a->alignments[id]=std::move(v);}
    for(const auto& [id,c]:j.at("alignmentInspection").items()){auto v=std::make_shared<AlignmentInspection>();v->width=c.at("width");v->height=c.at("height");v->rawWidth=c.at("rawWidth");v->rawHeight=c.at("rawHeight");v->confidence=Unpack<float>(c.at("confidence"));v->rejectionBits=Unpack<std::uint16_t>(c.at("rejectionBits"));
        if(v->confidence.size()!=std::uint64_t(v->width)*v->height||v->rejectionBits.size()!=v->confidence.size())throw std::runtime_error("Invalid burst alignment inspection dimensions.");a->alignmentInspection[id]=std::move(v);}
    if(j.contains("depth"))a->depth=Depth(j.at("depth"));return a;
}
Json Progress(const ProcessingProgress& p){
    Json j={{"stage",int(p.stage)},{"generation",p.generation},{"completed",p.completed},{"total",p.total},{"pass",p.pass},{"width",p.width},{"height",p.height},
        {"captureId",p.captureId},{"groupId",p.groupId},{"detail",p.detail},{"fraction",p.fraction},{"noiseVariance",p.noiseVariance},{"hasNoiseEstimate",p.hasNoiseEstimate},
        {"sequence",p.sequence},{"occurrence",p.occurrence},{"evidenceOnly",p.evidenceOnly}};
    if(p.thumbnail)j["thumbnail"]={{"frameId",p.thumbnail->frameId},{"width",p.thumbnail->width},{"height",p.thumbnail->height},{"group",p.thumbnail->group},{"rgba",Pack(p.thumbnail->rgba)}};
    if(p.evidence)j["evidence"]=Evidence(*p.evidence);return j;
}
ProcessingProgress Progress(const Json& j){
    ProcessingProgress p;const int stage=j.at("stage");if(stage<0||stage>int(ProcessingStage::PanoramaComposite))throw std::runtime_error("Invalid burst stage.");p.stage=static_cast<ProcessingStage>(stage);
    p.generation=j.at("generation");p.completed=j.at("completed");p.total=j.at("total");p.pass=j.at("pass");p.width=j.at("width");p.height=j.at("height");p.captureId=j.at("captureId");p.groupId=j.at("groupId");p.detail=j.at("detail");p.fraction=j.at("fraction");
    p.noiseVariance=j.at("noiseVariance").get<std::array<double,16>>();p.hasNoiseEstimate=j.at("hasNoiseEstimate");p.sequence=j.at("sequence");p.occurrence=j.at("occurrence");p.evidenceOnly=j.at("evidenceOnly");
    if(j.contains("thumbnail")){const auto& v=j.at("thumbnail");auto t=std::make_shared<ProcessingThumbnail>();t->frameId=v.at("frameId");t->width=v.at("width");t->height=v.at("height");t->group=v.at("group");t->rgba=Unpack<std::uint8_t>(v.at("rgba"));
        if(t->rgba.size()!=std::uint64_t(t->width)*t->height*4)throw std::runtime_error("Invalid burst thumbnail dimensions.");p.thumbnail=std::move(t);}
    if(j.contains("evidence"))p.evidence=Evidence(j.at("evidence"));return p;
}
Json ResultSummary(const BracketingResult& r){
    Json j={{"version",r.version},{"status",int(r.status)},{"identity",r.identity},{"message",r.message},
        {"fallbackSamples",r.fallbackSamples},{"unrecoverableSamples",r.unrecoverableSamples},{"invalidSamples",r.invalidSamples}};
    if(r.analysis)j["analysis"]=Analysis(*r.analysis);return j;
}
void ApplyResultSummary(const Json& j,BracketingResult& r){
    r.version=j.at("version");const int status=j.at("status");if(status<0||status>int(BracketingResult::Status::Failed))throw std::runtime_error("Invalid burst result status.");
    r.status=static_cast<BracketingResult::Status>(status);r.identity=j.at("identity");r.message=j.at("message");r.fallbackSamples=j.at("fallbackSamples");r.unrecoverableSamples=j.at("unrecoverableSamples");r.invalidSamples=j.at("invalidSamples");
    if(j.contains("analysis"))r.analysis=Analysis(j.at("analysis"));
}
}
