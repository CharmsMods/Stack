#include "Client.h"
#include "Protocol.h"
#include "WireData.h"
#include "Raw/Bracketing/ResultSerialization.h"
#include "App/AppPaths.h"
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

namespace Stack::BurstEngine {
using namespace Raw::Bracketing;
namespace {
bool Canceled(const std::function<bool()>& callback)noexcept{try{return callback&&callback();}catch(...){return true;}}
std::filesystem::path Artifact(const std::filesystem::path& root,const char* prefix,std::uint64_t id,const char* extension){return root/(std::string(prefix)+std::to_string(id)+extension);}
std::wstring Quote(const std::wstring& argument){
    std::wstring result=L"\"";unsigned slashes=0;
    for(wchar_t character:argument){if(character==L'\\'){++slashes;continue;}
        if(character==L'\"'){result.append(slashes*2+1,L'\\');result+=character;}else{result.append(slashes,L'\\');result+=character;}slashes=0;}
    result.append(slashes*2,L'\\');result+=L'\"';return result;
}
std::filesystem::path Executable(){
    std::wstring path(32768,L'\0');const auto size=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(!size||size==path.size())throw std::runtime_error(WindowsError("Could not locate the burst engine"));path.resize(size);
    return std::filesystem::path(path).parent_path()/"StackBurst.exe";
}
// A cancellation event is independent of the request pipe, so cancellation
// remains visible while the worker is executing or publishing progress.
class CancellationWatch {
public:
    CancellationWatch(HANDLE cancel,HANDLE process,std::function<bool()> callback,std::chrono::milliseconds deadline={}):
        stop_(CreateEventW(nullptr,TRUE,FALSE,nullptr)){
        if(!stop_)throw std::runtime_error(WindowsError("Could not create burst cancellation monitor"));
        thread_=std::thread([this,cancel,process,callback=std::move(callback),deadline]{
            const auto started=std::chrono::steady_clock::now();auto canceledAt=started;bool sent=false;
            while(WaitForSingleObject(stop_.Get(),40)==WAIT_TIMEOUT){
                const auto now=std::chrono::steady_clock::now();
                if(!sent&&(Canceled(callback)||(deadline.count()>0&&now-started>=deadline))){SetEvent(cancel);sent=true;canceledAt=now;}
                // A crashed driver or noncooperative child must not hold a
                // canceled project/shutdown forever. Only this private process
                // is terminated; other projects own separate leases/processes.
                if(sent&&now-canceledAt>=std::chrono::seconds(8)){TerminateProcess(process,ERROR_CANCELLED);break;}
            }
        });
    }
    ~CancellationWatch(){SetEvent(stop_.Get());if(thread_.joinable())thread_.join();}
private:Handle stop_;std::thread thread_;
};
class Session;
struct Lease final:ProcessingSessionLease {
    std::shared_ptr<Session> session;std::uint64_t key=0;std::string resultIdentity;
    std::atomic<bool> hasOverview{false};
    ~Lease()override;
};
class Session final {
public:
    Session(){
        directory_=AppPaths::GetCacheDirectory()/"BurstSessions"/UniqueId();std::filesystem::create_directories(directory_);
        try{Launch();}catch(...){job_.Reset();if(process_)WaitForSingleObject(process_.Get(),1000);std::error_code error;std::filesystem::remove_all(directory_,error);throw;}
    }
    ~Session(){
        // Closing the job kills this session's worker immediately. The process
        // wait and directory cleanup run away from the releasing/UI thread.
        input_.Reset();job_.Reset();output_.Reset();cancel_.Reset();
        const auto process=process_.Release();auto directory=std::move(directory_);
        try{std::thread([process,directory=std::move(directory)]{
            Handle child(process);if(child&&WaitForSingleObject(child.Get(),5000)==WAIT_TIMEOUT){TerminateProcess(child.Get(),ERROR_CANCELLED);WaitForSingleObject(child.Get(),1000);}
            std::error_code error;std::filesystem::remove_all(directory,error);
        }).detach();}catch(...){if(process)CloseHandle(process);}
    }
    const std::filesystem::path& Directory()const{return directory_;}
    bool Alive()const{return process_&&WaitForSingleObject(process_.Get(),0)==WAIT_TIMEOUT;}
    std::uint64_t Command(const char* operation,const ProcessingRequest& request,std::uint64_t base,Json extra={},
        const BracketingResult* import=nullptr,const Raw::Bracketing::Preview* seedPreview=nullptr){
        std::unique_lock<std::timed_mutex> lock(mutex_,std::defer_lock);
        while(!lock.try_lock_for(std::chrono::milliseconds(40)))if(Canceled(request.shouldCancel))throw std::runtime_error("Canceled");
        if(Canceled(request.shouldCancel))throw std::runtime_error("Canceled");
        if(WaitForSingleObject(process_.Get(),0)==WAIT_OBJECT_0)throw std::runtime_error("The burst engine session exited. Process the captures again.");
        const auto id=++sequence_;ResetEvent(cancel_.Get());
        Json command=extra.is_object()?std::move(extra):Json::object();command["protocol"]=ProtocolVersion;command["id"]=id;
        command["operation"]=operation;command["base"]=base;command["request"]=Wire::Request(request);
        const auto importKey=std::make_pair(base,import?import->identity:std::string());
        if(import)command["resultIdentity"]=import->identity;
        if(import&&!imports_.count(importKey)){
            if(!import->raw)throw std::runtime_error("The burst result contains no pixels to inspect.");
            if(!Serialization::WriteResultFile(Artifact(directory_,"input-",id,".bracket"),*import))throw std::runtime_error("Could not transfer saved burst pixels.");
            WriteDocument(Artifact(directory_,"input-",id,".cbor"),Wire::ResultSummary(*import));command["importResult"]=true;
        }
        if(seedPreview){WriteDocument(Artifact(directory_,"preview-",id,".cbor"),Wire::Preview(*seedPreview));command["importOverview"]=true;}
        CancellationWatch watcher(cancel_.Get(),process_.Get(),request.shouldCancel);
        bool terminalConsumed=false;
        try{
            WriteMessage(input_.Get(),command);
            for(;;){
                const auto message=ReadMessage(output_.Get());
                if(message.at("id").get<std::uint64_t>()!=id)throw std::runtime_error("The burst engine returned an unrelated operation.");
                const auto type=message.at("type").get<std::string>();
                if(type=="progress"){if(request.reportProgress)request.reportProgress(message.at("fraction"),message.at("detail"));}
                else if(type=="presentation"){if(request.reportPresentation)request.reportPresentation(Wire::Progress(message.at("progress")));}
                else if(type=="error"){terminalConsumed=true;throw std::runtime_error(message.at("message").get<std::string>());}
                else if(type=="done"){terminalConsumed=true;break;}
                else throw std::runtime_error("The burst engine returned an unsupported response.");
            }
        }catch(...){
            // Never reuse a pipe with unread messages after a failed callback or
            // invalid response. Otherwise another job could adopt its result.
            if(!terminalConsumed)TerminateProcess(process_.Get(),ERROR_PROCESS_ABORTED);RemoveInputs(id);throw;
        }
        if(import)imports_.insert(importKey);
        RemoveInputs(id);return id;
    }
    void Release(std::uint64_t key)noexcept{
        try{
            std::lock_guard<std::timed_mutex> lock(mutex_);
            if(WaitForSingleObject(process_.Get(),0)==WAIT_OBJECT_0)return;
            const auto id=++sequence_;
            CancellationWatch watcher(cancel_.Get(),process_.Get(),{},std::chrono::seconds(2));
            WriteMessage(input_.Get(),{{"protocol",ProtocolVersion},{"id",id},{"operation","release"},{"base",key}});
            const auto response=ReadMessage(output_.Get());
            if(response.value("id",std::uint64_t{})!=id||response.value("type",std::string())!="done")TerminateProcess(process_.Get(),ERROR_INVALID_DATA);
            for(auto it=imports_.begin();it!=imports_.end();)if(it->first==key)it=imports_.erase(it);else ++it;
        }catch(...){TerminateProcess(process_.Get(),ERROR_PROCESS_ABORTED);}
    }
    Json Output(std::uint64_t id)const{return ReadDocument(Artifact(directory_,"output-",id,".cbor"));}
    void RemoveOutput(std::uint64_t id)const noexcept{
        std::error_code error;std::filesystem::remove(Artifact(directory_,"output-",id,".cbor"),error);std::filesystem::remove(Artifact(directory_,"output-",id,".bracket"),error);
    }
private:
    void RemoveInputs(std::uint64_t id)const noexcept{
        std::error_code error;std::filesystem::remove(Artifact(directory_,"input-",id,".bracket"),error);std::filesystem::remove(Artifact(directory_,"input-",id,".cbor"),error);std::filesystem::remove(Artifact(directory_,"preview-",id,".cbor"),error);
    }
    void Launch(){
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};HANDLE requestRead=nullptr,requestWrite=nullptr,responseRead=nullptr,responseWrite=nullptr;
        if(!CreatePipe(&requestRead,&requestWrite,&security,0))throw std::runtime_error(WindowsError("Could not create burst request pipe"));
        Handle childInput(requestRead);input_.Reset(requestWrite);
        if(!CreatePipe(&responseRead,&responseWrite,&security,0))throw std::runtime_error(WindowsError("Could not create burst response pipe"));
        output_.Reset(responseRead);Handle childOutput(responseWrite);
        if(!SetHandleInformation(input_.Get(),HANDLE_FLAG_INHERIT,0)||!SetHandleInformation(output_.Get(),HANDLE_FLAG_INHERIT,0))throw std::runtime_error(WindowsError("Could not isolate burst pipe handles"));
        cancel_.Reset(CreateEventW(&security,TRUE,FALSE,nullptr));if(!cancel_)throw std::runtime_error(WindowsError("Could not create burst cancellation event"));
        job_.Reset(CreateJobObjectW(nullptr,nullptr));if(!job_)throw std::runtime_error(WindowsError("Could not create burst process lifetime"));
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(job_.Get(),JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error(WindowsError("Could not protect burst process lifetime"));
        SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<unsigned char> attributes(bytes);
        auto* list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        if(!InitializeProcThreadAttributeList(list,1,0,&bytes))throw std::runtime_error(WindowsError("Could not configure burst process handles"));
        struct AttributeCleanup{LPPROC_THREAD_ATTRIBUTE_LIST value;~AttributeCleanup(){DeleteProcThreadAttributeList(value);}} cleanup{list};
        HANDLE inherited[]={childInput.Get(),childOutput.Get(),cancel_.Get()};
        if(!UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof(inherited),nullptr,nullptr))throw std::runtime_error(WindowsError("Could not restrict burst handle inheritance"));
        const auto executable=Executable();std::wstring command=Quote(executable.wstring())+L" --worker "+
            std::to_wstring(reinterpret_cast<std::uintptr_t>(childInput.Get()))+L" "+std::to_wstring(reinterpret_cast<std::uintptr_t>(childOutput.Get()))+L" "+
            std::to_wstring(reinterpret_cast<std::uintptr_t>(cancel_.Get()))+L" "+Quote(directory_.wstring());
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=list;PROCESS_INFORMATION process{};
        if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,executable.parent_path().c_str(),&startup.StartupInfo,&process))
            throw std::runtime_error(WindowsError("Could not start StackBurst.exe; reinstall the matching Stack burst engine"));
        process_.Reset(process.hProcess);Handle initialThread(process.hThread);
        if(!AssignProcessToJobObject(job_.Get(),process_.Get())){TerminateProcess(process_.Get(),ERROR_PROCESS_ABORTED);throw std::runtime_error(WindowsError("Could not attach burst process lifetime"));}
        if(ResumeThread(initialThread.Get())==DWORD(-1))throw std::runtime_error(WindowsError("Could not start burst worker thread"));
        childInput.Reset();childOutput.Reset();
        CancellationWatch startupWatch(cancel_.Get(),process_.Get(),{},std::chrono::seconds(15));
        const auto hello=ReadMessage(output_.Get());
        if(hello.value("type",std::string())!="hello"||hello.value("product",std::string())!="StackBurst"||hello.value("protocol",0u)!=ProtocolVersion)
            throw std::runtime_error("StackBurst.exe has an incompatible protocol. Install a matching engine.");
    }
    std::filesystem::path directory_;Handle input_,output_,cancel_,job_,process_;std::timed_mutex mutex_;std::uint64_t sequence_=0;
    std::set<std::pair<std::uint64_t,std::string>> imports_;
};
Lease::~Lease(){
    if(!session)return;
    try{std::thread([owner=std::move(session),key=key]{owner->Release(key);}).detach();}catch(...){session.reset();}
}
std::shared_ptr<Lease> FindLease(const std::shared_ptr<const BracketingAnalysis>& analysis){return analysis?std::dynamic_pointer_cast<Lease>(analysis->sessionLease):nullptr;}
std::shared_ptr<Lease> FindLease(const ProcessingRequest& request,const BracketingResult* result=nullptr){
    auto lease=FindLease(request.analysis);if(!lease&&result)lease=FindLease(result->analysis);return lease;
}
struct OutputCleanup{std::shared_ptr<Session> session;std::uint64_t id;~OutputCleanup(){session->RemoveOutput(id);}};
Raw::Bracketing::Preview Detail(const char* operation,const ProcessingRequest& request,const BracketingResult* result,unsigned x,unsigned y,unsigned size){
    const auto lease=FindLease(request,result);const auto session=lease?lease->session:std::make_shared<Session>();
    const bool import=result&&(!lease||lease->resultIdentity!=result->identity);
    const auto id=session->Command(operation,request,lease?lease->key:0,{{"x",x},{"y",y},{"size",size}},import?result:nullptr);
    OutputCleanup cleanup{session,id};if(Canceled(request.shouldCancel))throw std::runtime_error("Canceled");return Wire::Preview(session->Output(id));
}
}
bool HasLiveSession(const std::shared_ptr<const BracketingAnalysis>& analysis){
    const auto lease=FindLease(analysis);return lease&&lease->session&&lease->session->Alive();
}
BracketingResult Process(const ProcessingRequest& request){
    BracketingResult result;
    try{
        if(Canceled(request.shouldCancel)){result.status=BracketingResult::Status::Canceled;result.message="Canceled";return result;}
        // Reject nontransferable inputs before launching a child.
        if(request.decode)throw std::runtime_error("Custom decoder callbacks cannot cross the burst process boundary. Use explicit RAW source files or engine-side validation.");
        auto previous=FindLease(request);if(previous&&!previous->session->Alive())previous.reset();
        auto session=previous?previous->session:std::make_shared<Session>();
        const auto id=session->Command("process",request,previous?previous->key:0);OutputCleanup cleanup{session,id};
        const auto summary=session->Output(id);
        if(summary.value("hasPixels",false)){
            const auto path=Artifact(session->Directory(),"output-",id,".bracket");std::ifstream stream(path,std::ios::binary);
            Serialization::ReadResult(stream,std::filesystem::file_size(path),request.recipe.groups.size(),result,AppPaths::GetCacheDirectory()/"BracketInspection");
        }
        Wire::ApplyResultSummary(summary,result);
        if(result.analysis){auto analysis=std::const_pointer_cast<BracketingAnalysis>(result.analysis);auto lease=std::make_shared<Lease>();lease->session=session;lease->key=id;lease->resultIdentity=result.identity;lease->hasOverview=result.preview.width>0;analysis->sessionLease=std::move(lease);}
        else session->Release(id);
        if(Canceled(request.shouldCancel)){result.status=BracketingResult::Status::Canceled;result.message="Canceled";}
        return result;
    }catch(const std::exception& e){result={};result.status=Canceled(request.shouldCancel)?BracketingResult::Status::Canceled:BracketingResult::Status::Failed;result.message=e.what();return result;}
}
Raw::Bracketing::Preview ReblendPreview(const Raw::Bracketing::Preview& source,const BracketingRecipe& recipe,const BracketingAnalysis& analysis,
    std::string* error,const std::function<bool()>& shouldCancel){
    try{
        const auto lease=std::dynamic_pointer_cast<Lease>(analysis.sessionLease);if(!lease)throw std::runtime_error("The burst preview has no live processing session. Prepare the captures again.");
        ProcessingRequest request;request.recipe=recipe;request.shouldCancel=shouldCancel;
        const auto id=lease->session->Command("reblend",request,lease->key,{{"width",source.width},{"height",source.height},
            {"originX",source.sensorOriginX},{"originY",source.sensorOriginY},{"stepX",source.sensorStepX},{"stepY",source.sensorStepY}},
            nullptr,lease->hasOverview.load()?nullptr:&source);
        lease->hasOverview=true;
        OutputCleanup cleanup{lease->session,id};
        if(Canceled(shouldCancel))throw std::runtime_error("Canceled");
        auto preview=Wire::Preview(lease->session->Output(id));
        preview.sourceRgb=source.sourceRgb;preview.samples=source.samples;preview.originals=source.originals;return preview;
    }catch(const std::exception& e){if(error)*error=e.what();return {};}
}
Raw::Bracketing::Preview RenderNativeDetail(const ProcessingRequest& request,unsigned x,unsigned y,unsigned size){return Detail("native",request,nullptr,x,y,size);}
Raw::Bracketing::Preview RenderResultDetail(const ProcessingRequest& request,const BracketingResult& result,unsigned x,unsigned y,unsigned size){return Detail("detail",request,&result,x,y,size);}
std::shared_ptr<const CapturePreview> RenderInspectionImage(const ProcessingRequest& request,const BracketingResult& result,InspectionImageKind kind,
    const std::string& frame,unsigned group,unsigned maximumDimension){
    const auto lease=FindLease(request,&result);const auto session=lease?lease->session:std::make_shared<Session>();
    const bool import=!lease||lease->resultIdentity!=result.identity;
    const auto id=session->Command("inspect",request,lease?lease->key:0,{{"kind",int(kind)},{"frame",frame},{"group",group},{"maximum",maximumDimension}},import?&result:nullptr);
    OutputCleanup cleanup{session,id};if(Canceled(request.shouldCancel))throw std::runtime_error("Canceled");const auto image=session->Output(id);return image.is_null()?nullptr:Wire::Capture(image);
}
}
