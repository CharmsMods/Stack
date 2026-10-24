#include "Worker.h"
#include "GpuContext.h"
#include "Standalone.h"
#include "Protocol.h"
#include "WireData.h"
#include "Raw/Bracketing/ResultSerialization.h"
#include "Raw/Bracketing/InspectionImage.h"
#include "Raw/Bracketing/TemporalGpu.h"
#include "Raw/Bracketing/SuperResolution/Reconstruction.h"
#include "Raw/Bracketing/Panorama/Panorama.h"
#include "Raw/Bracketing/Depth/Depth.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include "Raw/RawTechnicalEvidence.h"
#include "Raw/LibRawRuntime.h"
#include "Renderer/GLLoader.h"
#include "App/AppVersion.h"
#include <GLFW/glfw3.h>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>

namespace Stack::BurstEngine {
using namespace Raw::Bracketing;
namespace {
Json Version(){return {{"product","StackBurst"},{"protocol",ProtocolVersion},{"version",AppVersion::kVersionString}};}
std::filesystem::path Artifact(const std::filesystem::path& root,const char* prefix,std::uint64_t id,const char* extension){return root/(std::string(prefix)+std::to_string(id)+extension);}
void LoadResult(const std::filesystem::path& path,std::size_t groups,BracketingResult& result,const std::filesystem::path& inspections){
    std::ifstream stream(path,std::ios::binary);Serialization::ReadResult(stream,std::filesystem::file_size(path),groups,result,inspections);
}
int RunWorker(HANDLE input,HANDLE output,HANDLE cancellation,const std::filesystem::path& directory){
    GpuContext gpu;std::mutex outputMutex;
    const auto send=[&](Json message){std::lock_guard<std::mutex> lock(outputMutex);WriteMessage(output,message);};
    auto hello=Version();hello["type"]="hello";hello["gpuAvailable"]=gpu.Available();send(hello);
    std::map<std::uint64_t,BracketingResult> retained;
    std::map<std::pair<std::uint64_t,std::string>,BracketingResult> importedResults;
    for(;;){
        Json command;try{command=ReadMessage(input);}catch(...){return 0;}
        const auto id=command.at("id").get<std::uint64_t>();
        try{
            if(command.at("protocol")!=ProtocolVersion)throw std::runtime_error("Unsupported burst protocol version.");
            const auto operation=command.at("operation").get<std::string>();const auto base=command.value("base",std::uint64_t{});
            if(operation=="release"){
                retained.erase(base);for(auto it=importedResults.begin();it!=importedResults.end();)if(it->first.first==base)it=importedResults.erase(it);else ++it;
                send({{"type","done"},{"id",id}});continue;
            }
            auto request=Wire::Request(command.at("request"));request.executeOpenGlTask=gpu.Executor();
            request.shouldCancel=[cancellation]{return WaitForSingleObject(cancellation,0)==WAIT_OBJECT_0;};
            const auto publish=[&](Json value){value["id"]=id;send(std::move(value));};
            if(command.at("request").value("progress",false))request.reportProgress=[&](double fraction,const std::string& detail){publish({{"type","progress"},{"fraction",fraction},{"detail",detail}});};
            if(command.at("request").value("presentation",false))request.reportPresentation=[&](const ProcessingProgress& p){publish({{"type","presentation"},{"progress",Wire::Progress(p)}});};
            const auto found=retained.find(base);const BracketingResult* previous=found==retained.end()?nullptr:&found->second;
            BracketingResult imported;
            const auto importKey=std::make_pair(base,command.value("resultIdentity",std::string()));
            if(command.value("importResult",false)){
                LoadResult(Artifact(directory,"input-",id,".bracket"),request.recipe.groups.size(),imported,directory/"imported-inspection");
                Wire::ApplyResultSummary(ReadDocument(Artifact(directory,"input-",id,".cbor")),imported);
                if(found!=retained.end())imported.analysis=found->second.analysis;
                if(imported.identity!=importKey.second)throw std::runtime_error("The imported burst result identity does not match the request.");
                importedResults[importKey]=std::move(imported);previous=&importedResults.at(importKey);
            }else if(command.contains("resultIdentity")){
                const auto loaded=importedResults.find(importKey);
                if(loaded==importedResults.end())throw std::runtime_error("The imported burst result is no longer retained.");
                previous=&loaded->second;
            }else if(base&&!previous)throw std::runtime_error("The requested prepared burst session is no longer available.");
            if(previous)request.analysis=previous->analysis;
            if(operation=="process"){
                // All prepared/cache files are owned by this private session.
                // Source paths in the explicit request remain read-only.
                request.cacheDirectory=directory/("prepared-"+std::to_string(id));request.removeCacheOnRelease=true;
                auto result=Raw::Bracketing::Process(request);
                const bool pixels=result.raw&&result.status==BracketingResult::Status::Completed;
                if(pixels&&!Serialization::WriteResultFile(Artifact(directory,"output-",id,".bracket"),result))throw std::runtime_error("Could not transfer the burst measurement.");
                auto summary=Wire::ResultSummary(result);summary["hasPixels"]=pixels;
                WriteDocument(Artifact(directory,"output-",id,".cbor"),summary);
                retained.emplace(id,std::move(result));publish({{"type","done"},{"retained",true}});
            }else if(operation=="reblend"){
                if(!request.analysis||!previous)throw std::runtime_error("Prepared burst analysis is unavailable for preview.");
                if(command.value("importOverview",false)&&found!=retained.end()&&!found->second.preview.width)
                    found->second.preview=Wire::Preview(ReadDocument(Artifact(directory,"preview-",id,".cbor")));
                const auto& source=previous->preview;
                if(command.at("width")!=source.width||command.at("height")!=source.height||command.at("originX")!=source.sensorOriginX||
                    command.at("originY")!=source.sensorOriginY||command.at("stepX")!=source.sensorStepX||command.at("stepY")!=source.sensorStepY)
                    throw std::runtime_error("Reblend requires the retained overview. Request native detail separately.");
                std::string error;
                if(request.shouldCancel())throw std::runtime_error("Canceled");
                auto preview=Raw::Bracketing::ReblendPreview(source,request.recipe,*request.analysis,request.executeOpenGlTask,&error,false,request.shouldCancel);
                if(!error.empty())throw std::runtime_error(error);
                // These measurements do not change with contribution edits.
                // They stay in the worker and the client's accepted result.
                preview.samples.clear();preview.originals.clear();preview.sourceRgb.clear();
                WriteDocument(Artifact(directory,"output-",id,".cbor"),Wire::Preview(preview));publish({{"type","done"}});
            }else if(operation=="native"||operation=="detail"){
                const unsigned x=command.at("x"),y=command.at("y"),size=command.at("size");
                if(!size||size>4096)throw std::runtime_error("Invalid burst detail size.");
                Raw::Bracketing::Preview preview;
                if(operation=="native")preview=Raw::Bracketing::RenderNativeDetail(request,x,y,size);
                else{
                    if(!previous)throw std::runtime_error("The completed burst result is unavailable.");
                    preview=previous->panorama?Panorama::NativeDetail(*previous,x,y,size):previous->reconstructionInspection?
                        SuperResolutionDetail(*previous,x,y,size,request.shouldCancel,request.memoryBudgetBytes):Depth::ReadResultDetail(request,*previous,x,y,size);
                }
                WriteDocument(Artifact(directory,"output-",id,".cbor"),Wire::Preview(preview));publish({{"type","done"}});
            }else if(operation=="inspect"){
                if(!previous)throw std::runtime_error("The completed burst result is unavailable.");
                const int kind=command.at("kind");if(kind<0||kind>int(InspectionImageKind::Group))throw std::runtime_error("Invalid burst inspection kind.");
                const unsigned maximum=command.at("maximum");if(!maximum||maximum>32768)throw std::runtime_error("Invalid burst inspection size.");
                auto image=Raw::Bracketing::RenderInspectionImage(request,*previous,static_cast<InspectionImageKind>(kind),command.at("frame"),command.at("group"),maximum);
                WriteDocument(Artifact(directory,"output-",id,".cbor"),image?Wire::Capture(*image):Json());publish({{"type","done"}});
            }else throw std::runtime_error("Unsupported burst engine operation.");
        }catch(const std::exception& e){send({{"type","error"},{"id",id},{"message",e.what()}});}
        catch(...){send({{"type","error"},{"id",id},{"message","The burst engine encountered an unknown processing error."}});}
    }
}
int ProcessFiles(int argc,wchar_t** argv){
    if(argc<5)throw std::runtime_error("Usage: StackBurst --process <output-directory> <RAW> <RAW> [RAW ...]");
    const std::filesystem::path directory=argv[2];
    if(std::filesystem::exists(directory))throw std::runtime_error("The output directory must be new.");
    std::filesystem::create_directories(directory);
    ProcessingRequest request;Group group;group.id="burst";group.name="Burst";
    for(int i=3;i<argc;++i){const std::filesystem::path path=argv[i];const auto identity=RawEvidence::ComputeSourceIdentity(path);
        if(!identity.valid)throw std::runtime_error("Could not identify a RAW input.");
        const auto id="frame-"+std::to_string(i-3);request.sources.push_back({id,identity.sha256,identity.byteSize,path,PathText(path.filename())});group.frames.push_back({id});}
    request.recipe.originFrameId=group.frames.front().id;request.recipe.groups.push_back(std::move(group));request.recipe.knots=EqualCurves(1);
    const auto budget=Raw::Mfd::ResolveMfdProcessingMemoryBudget(0,Raw::Mfd::QueryPhysicalMemorySnapshot());if(!budget.valid)throw std::runtime_error(budget.message);
    request.memoryBudgetBytes=budget.budgetBytes;request.automaticMemoryBudget=true;request.workerCount=ResolveBracketingWorkerCount(budget.budgetBytes);
    request.cacheDirectory=directory/"prepared";request.removeCacheOnRelease=true;
    GpuContext gpu;request.executeOpenGlTask=gpu.Executor();
    request.reportProgress=[](double fraction,const std::string& detail){std::cout<<Json({{"progress",fraction},{"detail",detail}}).dump()<<std::endl;};
    const auto result=Raw::Bracketing::Process(request);
    if(result.status!=BracketingResult::Status::Completed||!result.raw)throw std::runtime_error(result.message);
    if(!Serialization::WriteResultFile(directory/"result.bracket",result))throw std::runtime_error("Could not write the burst result.");
    std::cout<<Json({{"status","completed"},{"identity",result.identity},{"width",result.raw->metadata.visibleWidth},{"height",result.raw->metadata.visibleHeight},{"gpuAvailable",gpu.Available()}}).dump()<<std::endl;return 0;
}
}
int WorkerMain(int argc,wchar_t** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    try{
        if(IsStandaloneCommand(argc,argv))return StandaloneMain(argc,argv);
        if(argc==2&&std::wstring(argv[1])==L"--version"){std::cout<<Version().dump()<<std::endl;return 0;}
        if(argc==2&&std::wstring(argv[1])==L"--self-check"){
            GpuContext gpu;const auto& raw=Raw::GetLibRawRuntimeStatus();auto response=Version();
            ProcessingRequest combineRequest;combineRequest.executeOpenGlTask=gpu.Executor();
            TemporalGpu combining(combineRequest);std::string gpuError;
            const bool combiningReady=combining.Ready(gpuError);
            response["gpuCombiningAvailable"]=combiningReady;response["gpuRequired"]=true;
            response["gpuMessage"]=gpuError;
            response["rawAvailable"]=raw.runtimeAvailable;response["gpuAvailable"]=gpu.Available();response["message"]=raw.message;
            // Exercise the actual framed CBOR transport over an inherited-handle
            // compatible pipe without manufacturing image data.
            SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};HANDLE read=nullptr,write=nullptr;
            if(!CreatePipe(&read,&write,&security,0))throw std::runtime_error(WindowsError("Could not check burst transport"));
            Handle input(read),output(write);WriteMessage(output.Get(),Version());const auto roundtrip=ReadMessage(input.Get());
            response["protocolRoundTrip"]=roundtrip==Version();response["ok"]=raw.runtimeAvailable&&gpu.Available()&&combiningReady&&roundtrip==Version();
            std::cout<<response.dump()<<std::endl;return response["ok"].get<bool>()?0:1;
        }
        if(argc>=2&&std::wstring(argv[1])==L"--process")return ProcessFiles(argc,argv);
        if(argc==6&&std::wstring(argv[1])==L"--worker"){
            Handle input(reinterpret_cast<HANDLE>(std::stoull(argv[2]))),output(reinterpret_cast<HANDLE>(std::stoull(argv[3]))),cancel(reinterpret_cast<HANDLE>(std::stoull(argv[4])));
            return RunWorker(input.Get(),output.Get(),cancel.Get(),std::filesystem::path(argv[5]));
        }
        std::cerr<<"StackBurst: open without arguments to choose images; use --selection <manifest>, --files <RAW>..., --analyze <manifest>, --batch <manifest>, --version or --self-check."<<std::endl;return 2;
    }catch(const std::exception& e){std::cerr<<Json({{"product","StackBurst"},{"error",e.what()}}).dump()<<std::endl;return 1;}
}
}
