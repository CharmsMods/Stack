#include "Standalone.h"
#include "LaunchInput.h"
#include "Window.h"
#include "Batch.h"
#include "Protocol.h"
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>

namespace Stack::BurstEngine {
namespace {
std::atomic<bool> interrupted{false};
BOOL WINAPI Interrupt(DWORD event){if(event==CTRL_C_EVENT||event==CTRL_BREAK_EVENT){interrupted=true;return TRUE;}return FALSE;}
struct ComScope {
    HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ~ComScope(){if(SUCCEEDED(result))CoUninitialize();}
};
bool Headless(int argc,wchar_t** argv){return argc>1&&(std::wstring(argv[1])==L"--analyze"||std::wstring(argv[1])==L"--batch");}
BatchOptions ReadOptions(const std::filesystem::path& path){
    std::ifstream stream(path,std::ios::binary);const auto json=Json::parse(stream);BatchOptions result;
    if(json.contains("outputFormats")){
        result.formats.clear();for(const auto& format:json.at("outputFormats")){
            if(format=="dng")result.formats.push_back(Output::Format::Dng);
            else if(format=="tiff")result.formats.push_back(Output::Format::Tiff16);
            else if(format=="jpeg")result.formats.push_back(Output::Format::Jpeg);
            else if(format=="png")result.formats.push_back(Output::Format::Png);
            else throw std::runtime_error("Supported output formats are dng, tiff, jpeg and png.");
        }
    }
    const auto destination=json.value("destination",std::string("folder"));
    const auto combining=json.value("captureCombining",std::string("auto"));
    if(combining!="auto"&&combining!="gpu")
        throw std::runtime_error("captureCombining must be auto or gpu. GPU processing is required; CPU mode is not supported.");
    result.preferGpuCombining=true;
    result.aiAlignment=json.value("aiAlignment",false);
    if(destination=="beside")result.location=OutputLocation::BesideOriginals;
    else if(destination!="folder")throw std::runtime_error("Destination must be beside or folder.");
    if(result.formats.empty())throw std::runtime_error("Choose at least one output format.");return result;
}
Json Summary(const SelectionResult& plan){
    Json value={{"type","selection"},{"canceled",plan.canceled},{"error",plan.error},{"sets",Json::array()},{"skipped",Json::array()}};
    for(const auto& set:plan.bursts){Json files=Json::array();for(const auto& source:set.sources)files.push_back(PathText(source.path));
        value["sets"].push_back({{"name",set.name},{"identity",set.identity},{"parent",PathText(set.parentDirectory)},{"files",files}});}
    for(const auto& skip:plan.skipped)value["skipped"].push_back({{"path",PathText(skip.path)},{"message",skip.message}});
    return value;
}
}
bool IsStandaloneCommand(int argc,wchar_t** argv){
    if(argc==1)return true;const std::wstring command=argv[1];
    return command==L"--selection"||command==L"--files"||command==L"--analyze"||command==L"--batch";
}
int StandaloneMain(int argc,wchar_t** argv){
    const bool headless=Headless(argc,argv);
    try{
        ComScope com;std::vector<std::filesystem::path> paths;
        if(headless){
            if(argc!=3)throw std::runtime_error("Usage: StackBurst --analyze|--batch <selection.json>");
            // Options are consumed before ReadSelectionManifest retires a
            // shell-owned manifest. User-authored manifests remain untouched.
            const auto options=ReadOptions(argv[2]);paths=ReadSelectionManifest(argv[2]);
            interrupted=false;SetConsoleCtrlHandler(Interrupt,TRUE);
            struct Handler{~Handler(){SetConsoleCtrlHandler(Interrupt,FALSE);}}handler;
            SelectionRequest request;request.paths=std::move(paths);request.shouldCancel=[]{return interrupted.load();};
            request.reportProgress=[](std::size_t done,std::size_t total,const std::string& detail){
                std::cout<<Json({{"type","selection-progress"},{"completed",done},{"total",total},{"detail",detail}}).dump()<<std::endl;};
            const auto plan=AnalyzeSelection(request);std::cout<<Summary(plan).dump()<<std::endl;
            if(plan.canceled)return 3;if(!plan.error.empty()||plan.bursts.empty())return 1;
            if(std::wstring(argv[1])==L"--analyze")return 0;
            std::mutex progressMutex;std::chrono::steady_clock::time_point lastNotice{};
            const auto batch=RunBatch(plan,options,[]{return interrupted.load();},[&](const BatchProgress& p){
                std::lock_guard<std::mutex> lock(progressMutex);const auto now=std::chrono::steady_clock::now();
                if(now-lastNotice<std::chrono::milliseconds(50)&&p.fraction<1)return;lastNotice=now;
                std::cout<<Json({{"type","progress"},{"set",p.set},{"sets",p.total},{"fraction",p.fraction},{"detail",p.detail}}).dump()<<std::endl;});
            for(const auto& m:batch.measurements){
                Json sources=Json::array();for(const auto& source:m.sources)sources.push_back({{"frameId",source.frameId},{"sha256",source.sha256},{"bytes",source.bytes},{"path",PathText(source.path)}});
                std::cout<<Json({{"type","measurement"},{"set",m.set},{"recipe",Raw::Bracketing::Serialize(m.recipe)},
                    {"sources",sources},{"resultIdentity",m.resultIdentity},{"analysisIdentity",m.analysisIdentity},
                    {"width",m.width},{"height",m.height},{"memoryBudgetBytes",m.memoryBudgetBytes},{"requestedWorkers",m.requestedWorkers},
                    {"processingSeconds",m.processingSeconds},{"outputSeconds",m.outputSeconds},{"stageSeconds",m.stageSeconds},{"diagnostics",m.diagnostics}}).dump()<<std::endl;
            }
            Json outputs=Json::array(),failures=Json::array();for(const auto& path:batch.outputs)outputs.push_back(PathText(path));
            for(const auto& error:batch.failures)failures.push_back({{"set",error.set},{"message",error.message}});
            std::cout<<Json({{"type","finished"},{"canceled",batch.canceled},{"completedSets",batch.completedSets},{"outputs",outputs},{"failures",failures}}).dump()<<std::endl;
            return batch.canceled?3:batch.failures.empty()?0:1;
        }
        if(argc==1)paths=PickSelection();
        else if(std::wstring(argv[1])==L"--selection"){
            if(argc!=3)throw std::runtime_error("Expected one Explorer selection manifest.");paths=ReadSelectionManifest(argv[2]);
        }else for(int i=2;i<argc;++i)paths.push_back(std::filesystem::absolute(argv[i]));
        return paths.empty()?0:RunWindow(std::move(paths));
    }catch(const std::exception& error){
        if(headless)std::cerr<<Json({{"error",error.what()}}).dump()<<std::endl;
        else MessageBoxW(nullptr,TextPath(error.what()).c_str(),L"Stack Burst",MB_OK|MB_ICONERROR);
        return 1;
    }
}
}
